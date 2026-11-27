// SPDX-FileCopyrightText: 2026 Aspect Wave LLC
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-GSW-additional-permission
// SeaLabMOS IWB (in-water board) - the wired SeaLabMOS CTD on an Adafruit ESP32-S2 Feather
// Aspect Wave LLC - Sophie Scopazzi - firmware version: FW_VERSION below
//
// The wired build: an Ethernet FeatherWing, no RTC and no microSD. Power and data come down one network cable from the WTB (wet-talk board)
// in the VMR's box: 12 V on the spare pairs (passive PoE) -> MPM3610 buck -> the Feather's 3V pin.
//
// What it does, as in the VMR / WTB / IWB interface spec:
//  - samples the Bar30 (pressure) and TSYS01 (temperature) at 5 Hz, and the EZO-EC at about 1 Hz
//  - computes practical salinity (PSS-78) from EC x EC_GAIN and in-situ density (TEOS-10, true
//    density, Absolute Salinity taken as Reference Salinity), on the samples with a new EC reading
//  - streams one $SLM line per sample and one $SLH housekeeping line a second on TCP port 7700
//  - keeps the last 30 min of samples in PSRAM, replayed to the WTB after a link drop
//  - serves /status and /update (firmware) on HTTP port 80, over the cable and over its own Wi-Fi
//  - fires the Niskin bottles: an Adafruit 6318 solenoid driver (MCP23017, I2C 0x20) on the same I2C bus,
//    solenoids (Adafruit 412) on its channels 0-5, powered from a 10,000 uF 25 V capacitor that a charge
//    resistor tops up from the 12 V. The capacitor is measured on A5, so a bottle only fires when it is full.
//
// Lines (NMEA-style checksum: XOR of the characters between $ and *):
//  $SLM,seq,ms,utc_ms,p_abs_dbar,p_sea_dbar,t_c,ec_uscm,sp,rho_kgm3,flags*hh
//  $SLH,seq,ms,cpu_c,int_t_c,int_rh_pct,dewpt_c,v_in,fw*hh
//  $SLF,ms,board,cap_v,v_in,ready,ready_in_s,last_recharge_s*hh   (5 Hz: the bottle-firing capacitor)
//  in:  $CMD,TIME,utc_ms | $CMD,TARE | $CMD,TARE,p_dbar | $CMD,REPLAY,from_seq | $CMD,EZO,<text>
//       $CMD,CFG,<key>,<value> | $CMD,PING        -> each answered $ACK,<cmd>,<ok|err>,<text>
//       $CMD,FIRE,ch,pulse_ms -> $ACK,FIRE,ok,ch,p_sea_dbar,cap_before_v,cap_after_v | $ACK,FIRE,err,ch,reason
//
// Board: Adafruit Feather ESP32-S2 (arduino-esp32 3.3.x), Partition Scheme: Minimal SPIFFS
// Libraries: BlueRobotics MS5837, BlueRobotics TSYS01, Adafruit SHT4x, Adafruit NeoPixel

#define FW_VERSION "2.0.0"      // MAJOR.MINOR.PATCH (semver.org): see Versions in README.md. Up to 15 characters (the WTB passes it on in 16 bytes)
static_assert(sizeof(FW_VERSION) <= 16, "FW_VERSION: up to 15 characters (the WTB's iwbFw, LNK_FW in vmr_link.h)");
const char FW_TAG[] = "SLMVER " FW_VERSION;      // the firmware page refuses an image without this tag

// ###### CONFIG ######
#define SYSTEM_NAME     "IWB_1"       // this unit's name, in /status and the Wi-Fi name
bool serialDisplay    = true;         // USB serial prints

// ###### SENSORS ######
bool salinityBool     = true;         // Atlas Scientific EZO-EC (UART, Serial1)
bool brFastTempBool   = true;         // Blue Robotics TSYS01 fast temperature (I2C 0x77)
bool bar30Bool        = true;         // Blue Robotics Bar30 (MS5837-30BA, I2C 0x76)
bool tubeRHBool       = true;         // Adafruit SHT40 (4885) in the tube's air (I2C 0x44)

#define SAMPLE_HZ        5            // pressure and temperature rate
#define EZO_CONT_S       1            // EZO-EC continuous reading every n s (C,n); about 1 Hz is its limit
#define EZO_LED_ON       true         // EZO LED on (shows it is reading)
#define EZO_BOOT_MS      2000         // EZO power-up before its first command (1200 ms is not enough)
#define EZO_STALE_MS     3000         // no EZO reading this long -> the EZO-stale flag
#define TARE_BOOT_S      10           // in-air median taken at power-up as the first tare
#define TARE_CMD_S       2            // $CMD,TARE (the WTB's Tare button): median over this long
#define BUF_MIN          30           // PSRAM sample buffer, minutes
#define I2C_HZ           100000       // 100 kHz (long sensor leads)

// ###### PINS ######
#define ETH_CS_PIN       10           // Ethernet FeatherWing CS, as shipped
#define SALINITY_ENABLE_PIN 6         // EZO-EC power enable (pin 4 is SCL on this Feather)
#define VIN_PIN          9            // 12 V through a divider (ADC1, so it works with Wi-Fi on)
#define VIN_DIVIDER      6.6f         // (R1 + R2) / R2: R1 = 56k to 12 V, R2 = 10k to GND -> 14.6 V reads 2.2 V
#define CAP_PIN          A5           // the firing capacitor through the same 56k / 10k divider (A5 = GPIO8, ADC1)
#define CAP_DIVIDER      6.6f

// ###### NISKIN BOTTLES ######
#define SOL_ADDR         0x20         // Adafruit 6318 I2C solenoid driver (MCP23017), no address jumpers
#define SOL_CHANNELS     6            // bottles 1-6 on solenoid channels 0-5
#define CAP_READY_FRAC   0.95f        // ready when the capacitor is at 95 % of the 12 V in
#define CAP_MIN_VIN      9.0f         // and the 12 V in is above this
#define CHARGE_R_OHM     47.0f        // the charge resistor, 12 V -> capacitor (for the ready-in estimate)
#define CAP_UF           10000.0f     // the capacitor
#define PULSE_MIN_MS     20
#define PULSE_MAX_MS     500          // a FIRE asking for more is cut to this
#define CAP_DROP_MIN_V   0.4f         // a pulse that pulls the capacitor down less than this drew no current: no solenoid?
#define SALINITY_BAUD    9600         // EZO-EC default

// ###### NETWORK ######
#define IWB_IP           10, 77, 0, 2
#define WTB_IP           10, 77, 0, 1
#define NET_MASK         255, 255, 255, 0
#define DATA_PORT        7700
#define FORCE_10M        true         // both ends forced to 10 Mbps full duplex (the WTB does the same)
#define WIFI_PASS        "sealabmos"  // own Wi-Fi for firmware updates on deck (open in the source on purpose)
#define WIFI_ON_MIN      10           // Wi-Fi runs this long after power-up, then only while a phone stays on
// ############################################################################################################

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <ETH.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "MS5837.h"
#include "TSYS01.h"
#include "Adafruit_SHT4x.h"
#include <Adafruit_NeoPixel.h>
#include "teos10.h"

// ----------------------------------------------------------------- globals
MS5837 brSensor30;
TSYS01 brFastTempSensor;
Adafruit_SHT4x sht4;
Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
Preferences prefs;
WebServer web(80);
NetworkServer dataServer(DATA_PORT);
NetworkClient wtb;

bool br30_ready = false, tsys_ready = false, sht_ready = false;
bool ethUp = false, wifiOn = false;
float ecGain = 1.0f;                       // set from the VMR dashboard ($CMD,CFG,EC_GAIN,x), kept in flash
float tareDbar = NAN;                      // absolute pressure in air, dbar
float brPressureDbar = NAN, brFastTemp = NAN;
float tubeT = NAN, tubeRH = NAN;
float lastEc = NAN; uint32_t lastEcMs = 0; bool ecNew = false;
int64_t utcOffsetMs = 0; bool timeSet = false;
uint32_t seq = 0;

// Niskin firing
bool solReady = false;                     // the 6318 answered
float capV = NAN, vinV = NAN;              // smoothed
uint32_t fireEndMs = 0;                    // when the last pulse ended
float lastRechargeS = NAN;                 // pulse end -> ready again, measured
bool rechargeWait = false;

// flags
#define F_TIME    0x01
#define F_REPLAY  0x02
#define F_EZO     0x04
#define F_BAR30   0x08
#define F_TSYS    0x10
#define F_SHT     0x20

struct Sample { uint32_t seq, ms; float pAbs, t, ec, sp, rho; uint8_t flags; };
Sample* buf = nullptr; uint32_t bufN = 0, bufHead = 0, bufCount = 0;
int64_t replayNext = -1;                   // next sequence number to replay, or -1

// tare median: a short history of absolute pressure
#define TARE_HIST 64
float tareHist[TARE_HIST]; int tareN = 0;
int tareWantN = 0; bool tareActive = false;

// ----------------------------------------------------------------- helpers
void ack(const char* cmd, bool ok, const char* text);
uint8_t nmeaSum(const char* s) { uint8_t c = 0; for (; *s && *s != '*'; s++) c ^= (uint8_t)*s; return c; }
void sendLine(char* body) {                // body starts after '$'; adds *hh and CRLF
  char out[200];
  snprintf(out, sizeof(out), "$%s*%02X\r\n", body, nmeaSum(body));
  if (wtb && wtb.connected()) wtb.print(out);
  if (serialDisplay) Serial.print(out);
}
void fmtF(char* d, size_t n, float v, int dec) { if (isnan(v)) d[0] = 0; else snprintf(d, n, "%.*f", dec, v); }
static int cmpF(const void* a, const void* b) { float x = *(const float*)a, y = *(const float*)b; return x < y ? -1 : x > y; }
float median(float* v, int n) { if (n <= 0) return NAN; float t[TARE_HIST]; memcpy(t, v, n * sizeof(float)); qsort(t, n, sizeof(float), cmpF); return t[n / 2]; }
float dewPoint(float t, float rh) {         // Magnus (Sonntag 1990 constants)
  if (isnan(t) || isnan(rh) || rh <= 0) return NAN;
  const float b = 17.62f, c = 243.12f; float g = logf(rh / 100.0f) + b * t / (c + t);
  return c * g / (b - g);
}
// The tag a firmware image must carry, stored scrambled so this sketch's own image can't pass for one
bool tagMatch(const uint8_t* p, size_t n, const uint8_t* tagX, size_t tl, size_t& state) {
  for (size_t i = 0; i < n; i++) {
    uint8_t want = tagX[state] ^ 0x5A;
    if (p[i] == want) { if (++state == tl) return true; }
    else state = (p[i] == (uint8_t)(tagX[0] ^ 0x5A)) ? 1 : 0;
  }
  return false;
}
static const uint8_t SLM_TAG_X[] = {'S' ^ 0x5A, 'L' ^ 0x5A, 'M' ^ 0x5A, 'V' ^ 0x5A, 'E' ^ 0x5A, 'R' ^ 0x5A};

void redFlash() { pixel.setPixelColor(0, pixel.Color(255, 0, 0)); pixel.show(); delay(150); pixel.clear(); pixel.show(); delay(150); }

// ----------------------------------------------------------------- EZO-EC (Serial1)
char ezoLine[64]; uint8_t ezoLen = 0;
char ezoReply[64]; bool ezoReplyWait = false; bool ezoReplyGot = false;
void salinSetup() {
  Serial1.begin(SALINITY_BAUD);
  pinMode(SALINITY_ENABLE_PIN, OUTPUT);
  digitalWrite(SALINITY_ENABLE_PIN, HIGH);   // on for the whole session (the IWB itself is only powered during one)
  delay(EZO_BOOT_MS);
  Serial1.print(EZO_LED_ON ? "L,1\r" : "L,0\r"); delay(100);
  char c[12]; snprintf(c, sizeof(c), "C,%d\r", EZO_CONT_S); Serial1.print(c); delay(100);
  while (Serial1.available()) Serial1.read();
}
void salinPoll() {                         // non-blocking: whole lines only
  while (Serial1.available()) {
    char ch = Serial1.read();
    if (ch == '\r' || ch == '\n') {
      if (!ezoLen) continue;
      ezoLine[ezoLen] = 0; ezoLen = 0;
      if (isdigit((unsigned char)ezoLine[0])) {         // a reading: EC first (then TDS, S, SG if enabled)
        lastEc = atof(ezoLine); lastEcMs = millis(); ecNew = true;
      } else if (ezoReplyWait) {                        // *OK, *ER, or a command's answer
        strncpy(ezoReply, ezoLine, sizeof(ezoReply) - 1); ezoReply[sizeof(ezoReply) - 1] = 0;
        ezoReplyGot = true; ezoReplyWait = false;           // the first line that isn't a reading: ?K,1.0 / *OK / *ER
      }
    } else if (ezoLen < sizeof(ezoLine) - 1) ezoLine[ezoLen++] = ch;
  }
}

// ----------------------------------------------------------------- sensors
void sensorsSetup() {
  Wire.begin(); Wire.setClock(I2C_HZ); delay(50);
  if (bar30Bool) {
    for (int i = 0; i < 10 && !br30_ready; i++) { if (brSensor30.init()) br30_ready = true; else { if (serialDisplay) Serial.println("bar30 init failed! Check SDA/SCL."); redFlash(); } }
    if (br30_ready) { brSensor30.setModel(MS5837::MS5837_30BA); brSensor30.setFluidDensity(1029); }
  }
  if (brFastTempBool) { Wire.beginTransmission(0x77); tsys_ready = Wire.endTransmission() == 0 && brFastTempSensor.init(); }
  if (tubeRHBool) { sht_ready = sht4.begin(&Wire); if (sht_ready) { sht4.setPrecision(SHT4X_MED_PRECISION); sht4.setHeater(SHT4X_NO_HEATER); } }
  solInit();
  if (salinityBool) salinSetup();
  if (serialDisplay) Serial.printf("bar30 %s, TSYS01 %s, SHT40 %s\n", br30_ready ? "ok" : "MISSING", tsys_ready ? "ok" : "MISSING", sht_ready ? "ok" : "MISSING");
}
bool present(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }

// ----------------------------------------------------------------- Niskin: 6318 solenoid driver and the capacitor
// MCP23017 registers (IOCON.BANK = 0): the solenoids are on port A
#define MCP_IODIRA 0x00
#define MCP_OLATA  0x14
bool mcpWrite(uint8_t reg, uint8_t v) { Wire.beginTransmission(SOL_ADDR); Wire.write(reg); Wire.write(v); return Wire.endTransmission() == 0; }
int mcpRead(uint8_t reg) {
  Wire.beginTransmission(SOL_ADDR); Wire.write(reg); if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((uint8_t)SOL_ADDR, (uint8_t)1) != 1) return -1;
  return Wire.read();
}
void solInit() {                           // latch all off BEFORE making them outputs, so nothing fires at power-up
  solReady = present(SOL_ADDR) && mcpWrite(MCP_OLATA, 0x00) && mcpWrite(MCP_IODIRA, 0x00) && mcpRead(MCP_IODIRA) == 0x00;
  if (serialDisplay) Serial.printf("Solenoid driver (6318) %s\n", solReady ? "ok" : "MISSING");
}
float readDivided(uint8_t pin, float div) { uint32_t mv = 0; for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(pin); return mv / 8.0f * div / 1000.0f; }
void capMeasure() {
  float c = readDivided(CAP_PIN, CAP_DIVIDER), v = readDivided(VIN_PIN, VIN_DIVIDER);
  capV = isnan(capV) ? c : capV + 0.5f * (c - capV);
  vinV = isnan(vinV) ? v : vinV + 0.5f * (v - vinV);
}
bool capReady() { return solReady && vinV > CAP_MIN_VIN && capV >= CAP_READY_FRAC * vinV; }
float readyInS() {                         // RC estimate: t = RC ln((Vin - Vcap) / (Vin - 0.95 Vin))
  if (capReady()) return 0;
  if (!(vinV > CAP_MIN_VIN) || isnan(capV)) return NAN;
  float rc = CHARGE_R_OHM * CAP_UF * 1e-6f, gap = vinV - capV, end = (1.0f - CAP_READY_FRAC) * vinV;
  return gap <= end ? 0 : rc * logf(gap / end);
}
void sendFireStatus() {
  char b[120], c[12], v[12], r[12], l[12];
  fmtF(c, sizeof(c), capV, 2); fmtF(v, sizeof(v), vinV, 2); fmtF(r, sizeof(r), readyInS(), 2); fmtF(l, sizeof(l), lastRechargeS, 2);
  snprintf(b, sizeof(b), "SLF,%lu,%d,%s,%s,%d,%s,%s", (unsigned long)millis(), solReady ? 1 : 0, c, v, capReady() ? 1 : 0, r, l);
  sendLine(b);
}
// one pulse on one channel; it blocks for the pulse (at most PULSE_MAX_MS) so the off can't be missed
void doFire(int ch, int pulseMs) {
  char t[96];
  if (ch < 0 || ch >= SOL_CHANNELS) { snprintf(t, sizeof(t), "%d,bad channel", ch); ack("FIRE", false, t); return; }
  if (!solReady) solInit();
  if (!solReady) { snprintf(t, sizeof(t), "%d,no solenoid driver", ch); ack("FIRE", false, t); return; }
  capMeasure();
  if (!capReady()) { snprintf(t, sizeof(t), "%d,capacitor not charged (%.1f V of %.1f V)", ch, capV, vinV); ack("FIRE", false, t); return; }
  pulseMs = constrain(pulseMs, PULSE_MIN_MS, PULSE_MAX_MS);
  float before = capV;
  float pSea = (isnan(tareDbar) || isnan(brPressureDbar)) ? NAN : brPressureDbar - tareDbar;
  bool on = mcpWrite(MCP_OLATA, (uint8_t)(1 << ch));
  uint32_t t0 = millis();
  while (millis() - t0 < (uint32_t)pulseMs) delay(1);
  float after = readDivided(CAP_PIN, CAP_DIVIDER);           // under load, at the end of the pulse
  bool off = mcpWrite(MCP_OLATA, 0x00);
  if (!off) { delay(2); off = mcpWrite(MCP_OLATA, 0x00); }
  if (!off) { mcpWrite(MCP_IODIRA, 0xFF); }                   // last resort: make them inputs (the board's pull-downs turn it off)
  fireEndMs = millis(); rechargeWait = true; capV = after;
  if (!on) { snprintf(t, sizeof(t), "%d,I2C write failed", ch); solReady = false; ack("FIRE", false, t); return; }
  char ps[16]; fmtF(ps, sizeof(ps), pSea, 3);
  snprintf(t, sizeof(t), "%d,%s,%.2f,%.2f", ch, ps, before, after);
  ack("FIRE", true, t);
  if (serialDisplay) Serial.printf("Fired %d: %d ms, cap %.2f -> %.2f V%s\n", ch + 1, pulseMs, before, after, before - after < CAP_DROP_MIN_V ? " (no current drawn?)" : "");
}

// ----------------------------------------------------------------- network
void onNet(arduino_event_id_t e, arduino_event_info_t) {
  if (e == ARDUINO_EVENT_ETH_CONNECTED) {
    ethUp = true;
    if (FORCE_10M) { ETH.setAutoNegotiation(false); ETH.setLinkSpeed(10); ETH.setFullDuplex(true); }
  } else if (e == ARDUINO_EVENT_ETH_DISCONNECTED || e == ARDUINO_EVENT_ETH_STOP) ethUp = false;
}
void netSetup() {
  Network.onEvent(onNet);
  SPI.begin();
  ETH.setPollPeriod(5);                    // the FeatherWing's interrupt pin isn't wired: poll the W5500
  if (!ETH.begin(ETH_PHY_W5500, 1, ETH_CS_PIN, -1, -1, SPI)) { if (serialDisplay) Serial.println("Ethernet FeatherWing not found"); }
  ETH.config(IPAddress(IWB_IP), IPAddress(WTB_IP), IPAddress(NET_MASK));
  dataServer.begin(); dataServer.setNoDelay(true);
  char ssid[32]; snprintf(ssid, sizeof(ssid), "SeaLabMOS-%s", SYSTEM_NAME);
  WiFi.mode(WIFI_AP); wifiOn = WiFi.softAP(ssid, WIFI_PASS);
}

// ----------------------------------------------------------------- web: status and firmware
size_t upTagState = 0; bool upTagOk = false, upBad = false;
void handleStatus() {
  char j[700], ec[16], sp[16];
  fmtF(ec, sizeof(ec), lastEc, 1); fmtF(sp, sizeof(sp), ecGain, 4);
  snprintf(j, sizeof(j),
    "{\"name\":\"%s\",\"fw\":\"%s\",\"tag\":\"%s\",\"up_s\":%lu,\"eth\":%s,\"wtb\":%s,\"wifi\":%s,\"seq\":%lu,\"buffered\":%lu,"
    "\"time_set\":%s,\"tare_dbar\":%.4f,\"ec_gain\":%s,\"p_abs_dbar\":%.4f,\"t_c\":%.4f,\"ec_uscm\":\"%s\","
    "\"bar30\":%s,\"tsys01\":%s,\"sht40\":%s,\"ezo_age_ms\":%lu,\"solenoids\":%s,\"cap_v\":%.2f,\"cap_ready\":%s}",
    SYSTEM_NAME, FW_VERSION, FW_TAG, (unsigned long)(millis() / 1000), ethUp ? "true" : "false", (wtb && wtb.connected()) ? "true" : "false",
    wifiOn ? "true" : "false", (unsigned long)seq, (unsigned long)bufCount, timeSet ? "true" : "false",
    isnan(tareDbar) ? 0.0 : tareDbar, sp, isnan(brPressureDbar) ? 0.0 : brPressureDbar, isnan(brFastTemp) ? 0.0 : brFastTemp, ec,
    br30_ready ? "true" : "false", tsys_ready ? "true" : "false", sht_ready ? "true" : "false",
    (unsigned long)(lastEcMs ? millis() - lastEcMs : 0), solReady ? "true" : "false", isnan(capV) ? 0.0 : capV, capReady() ? "true" : "false");
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", j);
}
void handleUpdatePage() {
  web.send(200, "text/html", "<!doctype html><meta name=viewport content='width=device-width'><title>SeaLabMOS IWB</title>"
    "<body style='font-family:sans-serif;max-width:480px;margin:24px auto'><h3>SeaLabMOS IWB " FW_VERSION "</h3>"
    "<form method=POST action=/update enctype=multipart/form-data><input type=file name=fw> <button>Update</button></form>"
    "<p><a href=/status>status</a></p>");
}
void handleUpdateUpload() {
  HTTPUpload& u = web.upload();
  if (u.status == UPLOAD_FILE_START) {
    upTagState = 0; upTagOk = false; upBad = false;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) upBad = true;
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (upBad) return;
    if (u.totalSize == 0 && u.currentSize && u.buf[0] != 0xE9) { upBad = true; Update.abort(); return; }   // not an ESP32 image
    if (!upTagOk) upTagOk = tagMatch(u.buf, u.currentSize, SLM_TAG_X, sizeof(SLM_TAG_X), upTagState);
    if (Update.write(u.buf, u.currentSize) != u.currentSize) { upBad = true; Update.abort(); }
    esp_task_wdt_reset();
  } else if (u.status == UPLOAD_FILE_END) {
    if (!upBad && !upTagOk) { upBad = true; Update.abort(); }
    else if (!upBad && !Update.end(true)) upBad = true;
  } else if (u.status == UPLOAD_FILE_ABORTED) { Update.abort(); upBad = true; }
}
void handleUpdateDone() {
  if (upBad || Update.hasError()) { web.send(400, "text/plain", upTagOk ? "Update failed" : "Not a SeaLabMOS IWB image (no IWB firmware tag)"); return; }
  web.send(200, "text/plain", "OK, restarting");
  delay(500); ESP.restart();
}

// ----------------------------------------------------------------- commands from the WTB
char cmdBuf[128]; uint8_t cmdLen = 0;
void ack(const char* cmd, bool ok, const char* text) {
  char b[160]; snprintf(b, sizeof(b), "ACK,%s,%s,%s", cmd, ok ? "ok" : "err", text ? text : ""); sendLine(b);
}
void startTare(int seconds) { tareN = 0; tareWantN = max(1, min(TARE_HIST, seconds * SAMPLE_HZ)); tareActive = true; }
void doCommand(char* line) {
  // strip $ and *hh
  if (line[0] == '$') line++;
  char* star = strchr(line, '*'); if (star) *star = 0;
  if (strncmp(line, "CMD,", 4) != 0) return;
  char* a = line + 4;
  char* arg = strchr(a, ','); if (arg) *arg++ = 0;
  if (!strcmp(a, "TIME") && arg) {
    int64_t utc = strtoll(arg, nullptr, 10);
    utcOffsetMs = utc - (int64_t)millis(); timeSet = true; ack("TIME", true, "");
  } else if (!strcmp(a, "TARE")) {
    if (arg && *arg) { tareDbar = atof(arg); tareActive = false; ack("TARE", true, arg); }
    else { startTare(TARE_CMD_S); ack("TARE", true, "taking"); }
  } else if (!strcmp(a, "REPLAY") && arg) {
    replayNext = strtoll(arg, nullptr, 10); ack("REPLAY", true, "");
  } else if (!strcmp(a, "EZO") && arg) {
    ezoReplyGot = false; ezoReplyWait = true;
    Serial1.print(arg); Serial1.print('\r');
    uint32_t t0 = millis();
    while (!ezoReplyGot && millis() - t0 < 1500) { salinPoll(); delay(5); }
    ezoReplyWait = false;
    ack("EZO", ezoReplyGot, ezoReplyGot ? ezoReply : "no answer");
  } else if (!strcmp(a, "CFG") && arg) {
    char* val = strchr(arg, ','); if (val) *val++ = 0;
    if (!strcmp(arg, "EC_GAIN") && !val) { char t[24]; snprintf(t, sizeof(t), "EC_GAIN %.5f", ecGain); ack("CFG", true, t); }   // asked: the value in use
    else if (!strcmp(arg, "EC_GAIN") && val) {
      float g = atof(val);
      if (g > 0.2f && g < 5.0f) { ecGain = g; prefs.putFloat("ecgain", g); char t[16]; snprintf(t, sizeof(t), "%.5f", g); ack("CFG", true, t); }
      else ack("CFG", false, "EC_GAIN out of range (0.2-5)");
    } else ack("CFG", false, "unknown key");
  } else if (!strcmp(a, "FIRE") && arg) {
    char* pm = strchr(arg, ','); if (pm) *pm++ = 0;
    doFire(atoi(arg), pm ? atoi(pm) : 100);
  } else if (!strcmp(a, "PING")) ack("PING", true, FW_VERSION);
  else ack(a, false, "unknown command");
}
void pollCommands() {
  while (wtb && wtb.available()) {
    char c = wtb.read();
    if (c == '\n' || c == '\r') { if (cmdLen) { cmdBuf[cmdLen] = 0; cmdLen = 0; doCommand(cmdBuf); } }
    else if (cmdLen < sizeof(cmdBuf) - 1) cmdBuf[cmdLen++] = c;
  }
}

// ----------------------------------------------------------------- lines
void sendSample(const Sample& s, bool replay) {
  char b[180], utc[24], pa[16], ps[16], t[16], ec[16], sp[16], rho[16];
  if (timeSet) snprintf(utc, sizeof(utc), "%lld", (long long)((int64_t)s.ms + utcOffsetMs)); else utc[0] = 0;
  fmtF(pa, sizeof(pa), s.pAbs, 4);
  fmtF(ps, sizeof(ps), isnan(tareDbar) || isnan(s.pAbs) ? NAN : s.pAbs - tareDbar, 4);
  fmtF(t, sizeof(t), s.t, 4); fmtF(ec, sizeof(ec), s.ec, 1); fmtF(sp, sizeof(sp), s.sp, 4); fmtF(rho, sizeof(rho), s.rho, 4);
  snprintf(b, sizeof(b), "SLM,%lu,%lu,%s,%s,%s,%s,%s,%s,%s,%02X", (unsigned long)s.seq, (unsigned long)s.ms, utc, pa, ps, t, ec, sp, rho,
           (unsigned)(s.flags | (replay ? F_REPLAY : 0) | (timeSet ? F_TIME : 0)));
  sendLine(b);
}
void sendHousekeeping() {
  char b[160], it[12], rh[12], dp[12], vi[12];
  fmtF(it, sizeof(it), tubeT, 2); fmtF(rh, sizeof(rh), tubeRH, 1); fmtF(dp, sizeof(dp), dewPoint(tubeT, tubeRH), 2);
  float vin = !isnan(vinV) ? vinV : readDivided(VIN_PIN, VIN_DIVIDER);   // the smoothed one $SLF uses
  fmtF(vi, sizeof(vi), vin, 2);
  snprintf(b, sizeof(b), "SLH,%lu,%lu,%.1f,%s,%s,%s,%s,%s", (unsigned long)seq, (unsigned long)millis(), temperatureRead(), it, rh, dp, vi, FW_VERSION);
  sendLine(b);
}

// ----------------------------------------------------------------- one sample
void takeSample() {
  uint8_t flags = 0;
  if (br30_ready) {
    if (present(0x76)) { brSensor30.read(); float mbar = brSensor30.pressure(); brPressureDbar = (mbar > 300 && mbar < 40000) ? mbar / 100.0f : NAN; }
    else brPressureDbar = NAN;
  }
  if (isnan(brPressureDbar)) flags |= F_BAR30;
  if (tsys_ready) {
    if (present(0x77)) { brFastTempSensor.read(); float t = brFastTempSensor.temperature(); brFastTemp = (t > -5 && t < 45) ? t : NAN; }
    else brFastTemp = NAN;
  }
  if (isnan(brFastTemp)) flags |= F_TSYS;
  if (!sht_ready) flags |= F_SHT;
  salinPoll();
  bool fresh = ecNew; ecNew = false;
  if (!lastEcMs || millis() - lastEcMs > EZO_STALE_MS) flags |= F_EZO;

  // tare: median of in-air samples
  if (tareActive && !isnan(brPressureDbar)) {
    tareHist[tareN++] = brPressureDbar;
    if (tareN >= tareWantN) { tareDbar = median(tareHist, tareN); tareActive = false; if (serialDisplay) Serial.printf("Tare %.4f dbar\n", tareDbar); }
  }

  Sample s; s.seq = ++seq; s.ms = millis(); s.pAbs = brPressureDbar; s.t = brFastTemp; s.ec = NAN; s.sp = NAN; s.rho = NAN; s.flags = flags;
  if (fresh && !isnan(lastEc)) {
    s.ec = lastEc;
    float pSea = (isnan(tareDbar) || isnan(brPressureDbar)) ? 0.0f : max(0.0f, brPressureDbar - tareDbar);
    if (!isnan(brFastTemp)) {
      double c = (double)lastEc * ecGain / 1000.0;                // uS/cm -> mS/cm, with the cell's gain
      double sp = gsw_sp_from_c(c, brFastTemp, pSea);
      if (sp >= 0 && sp < 50) {
        double sa = gsw_sr_from_sp(sp), ct = gsw_ct_from_t(sa, brFastTemp, pSea);
        s.sp = (float)sp; s.rho = (float)gsw_rho(sa, ct, pSea);
      }
    }
  }
  if (buf) { buf[bufHead] = s; bufHead = (bufHead + 1) % bufN; if (bufCount < bufN) bufCount++; }
  sendSample(s, false);
}

// replay a few buffered samples each pass, so live samples keep flowing
void serviceReplay() {
  if (replayNext < 0 || !buf || !(wtb && wtb.connected())) return;
  if (bufCount == 0) { replayNext = -1; return; }
  uint32_t oldest = buf[(bufHead + bufN - bufCount) % bufN].seq;
  if (replayNext < (int64_t)oldest) replayNext = oldest;          // older than the buffer: gone
  for (int k = 0; k < 40 && replayNext <= (int64_t)seq; k++, replayNext++) {
    uint32_t idx = (bufHead + bufN - bufCount + (uint32_t)(replayNext - oldest)) % bufN;
    sendSample(buf[idx], true);
  }
  if (replayNext > (int64_t)seq) replayNext = -1;
}

// ----------------------------------------------------------------- setup / loop
void setup() {
  if (serialDisplay) { Serial.begin(115200); delay(1500); Serial.printf("\nSeaLabMOS IWB %s (%s) %s\n", FW_VERSION, SYSTEM_NAME, FW_TAG); }
  pinMode(NEOPIXEL_POWER, OUTPUT); digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
  pixel.begin(); pixel.setBrightness(40);
  for (int i = 0; i < 3; i++) { pixel.setPixelColor(0, pixel.Color(0, 255, 0)); pixel.show(); delay(80); pixel.clear(); pixel.show(); delay(80); }
#if defined(PIN_I2C_POWER)
  pinMode(PIN_I2C_POWER, INPUT); delay(1);
  int off = digitalRead(PIN_I2C_POWER);                             // the rail's off level differs between board revisions
  pinMode(PIN_I2C_POWER, OUTPUT); digitalWrite(PIN_I2C_POWER, !off); delay(100);
#endif
  analogReadResolution(12); analogSetPinAttenuation(VIN_PIN, ADC_11db); analogSetPinAttenuation(CAP_PIN, ADC_11db);
  prefs.begin("iwb", false); ecGain = prefs.getFloat("ecgain", 1.0f);
  bufN = (uint32_t)BUF_MIN * 60 * SAMPLE_HZ;
  buf = (Sample*)ps_malloc(sizeof(Sample) * bufN);
  if (!buf && serialDisplay) Serial.println("No PSRAM: no replay buffer");
  sensorsSetup();
  netSetup();
  web.on("/status", HTTP_GET, handleStatus);
  web.on("/", HTTP_GET, handleUpdatePage);
  web.on("/update", HTTP_GET, handleUpdatePage);
  web.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  web.begin();
  startTare(TARE_BOOT_S);                                          // first tare in air, at power-up
  esp_task_wdt_config_t wc = {.timeout_ms = 10000, .idle_core_mask = 0, .trigger_panic = true};
  esp_task_wdt_reconfigure(&wc); esp_task_wdt_add(NULL);
}

void loop() {
  static uint32_t nextSample = 0, nextHk = 0, nextLed = 0, nextFire = 0, nextSolCheck = 0;
  uint32_t now = millis();
  esp_task_wdt_reset();

  // the WTB's connection: the newest one wins
  NetworkClient c = dataServer.accept();
  if (c) { if (wtb) wtb.stop(); wtb = c; wtb.setNoDelay(true); if (serialDisplay) Serial.println("WTB connected"); }

  salinPoll();
  pollCommands();
  if ((int32_t)(now - nextSample) >= 0) { nextSample = now + 1000 / SAMPLE_HZ; takeSample(); }
  if ((int32_t)(now - nextHk) >= 0) {
    nextHk = now + 1000;
    if (sht_ready) { sensors_event_t h, t; if (present(0x44) && sht4.getEvent(&h, &t)) { tubeT = t.temperature; tubeRH = h.relative_humidity; } else { tubeT = tubeRH = NAN; } }
    sendHousekeeping();
  }
  if ((int32_t)(now - nextFire) >= 0) {                            // the capacitor, 5 Hz
    nextFire = now + 200;
    capMeasure();
    if (rechargeWait && capReady()) { rechargeWait = false; lastRechargeS = (millis() - fireEndMs) / 1000.0f; }
    sendFireStatus();
  }
  if ((int32_t)(now - nextSolCheck) >= 0) {                        // the 6318 unplugged or plugged in
    nextSolCheck = now + 2000;
    bool p = present(SOL_ADDR);
    if (p && !solReady) solInit(); else if (!p) solReady = false;
  }
  serviceReplay();
  web.handleClient();

  // Wi-Fi only for the first minutes on deck, or while a phone is on it
  if (wifiOn && now > (uint32_t)WIFI_ON_MIN * 60000UL && WiFi.softAPgetStationNum() == 0) { WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); wifiOn = false; }

  // NeoPixel: green blink = streaming to the WTB, orange = no WTB, red = a sensor fault
  if ((int32_t)(now - nextLed) >= 0) {
    nextLed = now + 1000;
    bool fault = (bar30Bool && !br30_ready) || (brFastTempBool && !tsys_ready);
    uint32_t col = fault ? pixel.Color(255, 0, 0) : (wtb && wtb.connected()) ? pixel.Color(0, 255, 0) : pixel.Color(255, 120, 0);
    pixel.setPixelColor(0, col); pixel.show(); delay(20); pixel.clear(); pixel.show();
  }
  delay(1);
}
