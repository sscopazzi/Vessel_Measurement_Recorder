// SPDX-FileCopyrightText: 2026 Aspect Wave LLC
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-GSW-additional-permission
// SeaLabMOS WTB (wet-talk board) - the VMR's optional link to the in-water board (IWB)
// Aspect Wave LLC - Sophie Scopazzi - firmware version: FW_VERSION below
//
// Adafruit ESP32-S2 Feather in the VMR's box, its own stack: Ethernet FeatherWing (CS 10) + Adafruit 4682
// microSD breakout (CS 6) + Tare button (A0), Start cast button (A1), Ethernet-down LED (A2).
// Powered with the IWB through the VMR's latching relay: 12 V -> its own MPM3610 -> 3V pin, and 12 V down the
// spare pairs (passive PoE) to the IWB.
//
// What it does (VMR / WTB / IWB interface spec):
//  - talks to the VMR over ESP-NOW on the VMR hotspot's channel (vmr_link.h): STATUS and LIVE once a second,
//    CAST_DONE and PROFILE after each cast, files and firmware on request
//  - reads the IWB's $SLM / $SLH lines over TCP (10.77.0.2:7700), keeps every sample on its card, asks for a
//    replay after a link drop, and passes on time, tare and commands
//  - runs the cast session: casts start and stop on depth (or by hand), and each down cast is loop-edited,
//    binned and averaged for density
//  - Niskin bottles: six buttons on an Adafruit 5545 (PCF8574, I2C 0x20), held 1.5 s to fire; their LED rings
//    and the green ready LED on an Adafruit 6318 (I2C 0x21, A0 jumper); fires through the IWB ($CMD,FIRE), from the
//    VMR's Fire Control card, or at planned depths on the way down; each firing goes in the cast's _bottles.csv,
//    the profile file's header and to the VMR (a mark)
//
// Board: Adafruit Feather ESP32-S2 (arduino-esp32 3.3.x), Partition Scheme: Minimal SPIFFS

#define FW_VERSION "2.0.0"      // MAJOR.MINOR.PATCH (semver.org): see Versions in README.md. Up to 15 characters (LNK_FW in vmr_link.h)
const char FW_TAG[] = "WTBVER " FW_VERSION;

// ###### CONFIG ######
#define SYSTEM_NAME    "WTB_1"
bool serialDisplay   = true;
#define LNK_ENCRYPT    true            // ESP-NOW frames encrypted (the VMR must match)

// ###### PINS ######
#define ETH_CS_PIN     10              // Ethernet FeatherWing, as shipped
#define SD_CS_PIN      6               // 4682 microSD breakout
#define TARE_PIN       A0              // button to GND: zero the pressure just before the package touches the water
#define START_PIN      A1              // button to GND: open a cast; press again to close it
#define ETHLED_PIN     A2              // LED (with its resistor) to GND: on while the Ethernet link or the IWB is down

// ###### NETWORK ######
#define WTB_IP         10, 77, 0, 1
#define IWB_IP         10, 77, 0, 2
#define NET_MASK       255, 255, 255, 0
#define DATA_PORT      7700
#define FORCE_10M      true            // as the IWB
#define IWB_SILENT_MS  2000            // no line this long = the link is down
#define TIME_EVERY_MS  60000UL         // $CMD,TIME to the IWB
#define BEACON_LOST_MS 5000UL          // no VMR beacon this long -> scan the channels again

// ###### CAST DEFAULTS (the VMR sends CONFIG on every arm) ######
#define DEF_START_M    0.15f
#define DEF_START_S    2.0f
#define DEF_END_M      0.10f
#define DEF_END_S      5.0f
#define DEF_BIN_M      0.25f
#define DEF_SOAK_S     30.0f
#define LOOP_EDIT_DBAR 0.10f           // keep a sample only if p >= running max - this
#define CAST_MAX_MIN   60              // samples held for processing (5 Hz): 60 min
#define STARTING_FAULT_MS 60000UL

// ###### NISKIN BOTTLES ######
#define NK_BTN_ADDR    0x20            // Adafruit 5545 PCF8574: buttons (Adafruit 560) from P0-P5 to GND
#define NK_LED_ADDR    0x21            // Adafruit 6318 MCP23017 with A0 bridged (0x20 is the 5545): rings on 0-5, ready LED on 6
#define NK_I2C_HZ      100000          // the PCF8574 is a 100 kHz part
#define NISKIN_PULSE_MS 100            // solenoid pulse (the IWB cuts anything over 500 ms)
#define NK_HOLD_MS     1500            // a button held this long fires its bottle
#define NK_ANSWER_MS   2000            // FIRE sent and no answer this long: "no answer" (it may have closed)
#define NK_LATE_M      1.0f            // a planned depth still not fired this far past it: missed (left armed)
#define NK_GAP_MIN_M   0.5f            // planned depths at least this far apart, and at least
#define NK_RECHARGE_DEF_S 1.0f         //   lowering speed x recharge time x 2 (this recharge time until the IWB has measured one)
#define NK_LOWER_DEF   0.20f           // lowering speed, m/s
#define NK_SLF_STALE_MS 1500UL         // no $SLF this long: the IWB's firing side is unknown
#define NK_CAP_DROP_MIN_V 0.4f         // a pulse that pulled the capacitor down less than this: no solenoid on that channel?
// ############################################################################################################

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <ETH.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "vmr_link.h"
static_assert(sizeof(FW_VERSION) <= LNK_FW, "FW_VERSION: up to 15 characters (LNK_FW, vmr_link.h)");
#include "teos10.h"

Preferences prefs;
NetworkClient iwb;

// ----------------------------------------------------------------- state
struct Btn { uint8_t pin; bool last; uint32_t ms; };
bool ethUp = false, sdOk = false;
uint8_t vmrMac[6]; bool paired = false; bool vmrKnown = false;
uint8_t chan = 1; bool chanLocked = false; uint32_t lastBeaconMs = 0, scanStepMs = 0;
int64_t utcOffsetMs = 0; bool timeKnown = false; uint8_t timeSrc = 0;
float vmrLat = NAN, vmrLon = NAN, draftDeep = NAN;
BConfig cfg = {DEF_START_M, DEF_START_S, DEF_END_M, DEF_END_S, DEF_BIN_M, DEF_SOAK_S, 0, 0};
uint8_t state = S_STARTING; bool manual = false; bool disarming = false;
uint32_t bootMs = 0, readySinceMs = 0;
uint16_t castNum = 0; bool castOpen = false;

// IWB
uint32_t lastLineMs = 0, lastSeq = 0, lastTimeSentMs = 0; bool iwbAlive = false, iwbEverSeen = false;
char iwbFw[LNK_FW] = "";
float tareDbar = NAN, iwbV = NAN, tubeT = NAN, tubeRH = NAN, dewpt = NAN, iwbCpu = NAN, ecGain = NAN;
float dewBase = NAN; bool dewWarned = false, lowVWarned = false;
float lastDepth = NAN;

// Niskin bottles (the code is further down)
struct NkRec  { uint8_t st, src; uint16_t cast; float planM, depthM, pSea, t, sp, rho, lat, lon, capB, capA; int64_t utc; };
struct NkSave { uint8_t ver, on; float lowerMps; NkRec r[NK_N]; };
NkSave nk;
bool nkBtnOk = false, nkLedOk = false, solOk = false, capReady = false;
float capV = NAN, capVin = NAN, capReadyIn = NAN, rechargeS = NAN;
uint32_t slfMs = 0;
int8_t nkPend = -1; uint32_t nkPendMs = 0, nkRetryMs = 0;
uint32_t lastFireId = 0;
uint8_t btnHeld = 0, btnDone = 0; uint32_t btnDownMs[NK_N], btnUpMs[NK_N], refuseUntil[NK_N];
bool nkDirty = false; int nkLedLast = -1;
float lastT = NAN, lastSp = NAN, lastRho = NAN; uint32_t lastTMs = 0, lastCMs = 0;


// files
File sessF, castF; char sessName[48] = "", castRawName[48] = "", castProfName[48] = "";
uint32_t lastFlushMs = 0;
bool hkPending = false;                     // housekeeping values go on the next sample row only

// LIVE second
int64_t liveSec = -1; double aP = 0, aD = 0, aT = 0, aE = 0, aS = 0, aR = 0; uint8_t nPT = 0, nTs = 0, nC = 0;

// cast samples for processing
struct CS { float p, depth, t, sp, rho; uint32_t ms; };
CS* cs = nullptr; uint32_t csN = 0, csCap = 0;
int64_t castStartUtc = 0; uint32_t castStartMs = 0;
uint32_t deepSinceMs = 0, shallowSinceMs = 0;

// ----------------------------------------------------------------- small helpers
int64_t nowUtc() { return timeKnown ? (int64_t)millis() + utcOffsetMs : 0; }
uint8_t nmeaSum(const char* s) { uint8_t c = 0; for (; *s && *s != '*'; s++) c ^= (uint8_t)*s; return c; }
void iwbSend(const char* body) {
  if (!(iwb && iwb.connected())) return;
  char out[220]; snprintf(out, sizeof(out), "$%s*%02X\r\n", body, nmeaSum(body)); iwb.print(out);
  if (serialDisplay) Serial.print("> "), Serial.print(out);
}
void utcStr(int64_t ms, char* d, size_t n) {
  if (ms <= 0) { d[0] = 0; return; }
  time_t s = (time_t)(ms / 1000); struct tm t; gmtime_r(&s, &t);
  snprintf(d, n, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, (int)(ms % 1000));
}
void stampName(char* d, size_t n, const char* dir, const char* tail) {
  int64_t u = nowUtc();
  if (u > 0) { time_t s = (time_t)(u / 1000); struct tm t; gmtime_r(&s, &t);
    snprintf(d, n, "/%s/%04d%02d%02d_%02d%02d%02dZ_%s", dir, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, tail); }
  else { uint32_t k = prefs.getUInt("noclk", 0) + 1; prefs.putUInt("noclk", k); snprintf(d, n, "/%s/NOCLOCK_%03lu_%s", dir, (unsigned long)k, tail); }
}
float depthFrom(float pSea) {                 // TEOS-10, at the VMR's latitude (45 deg until it is known)
  if (isnan(pSea)) return NAN;
  double lat = isnan(vmrLat) ? 45.0 : vmrLat;
  return (float)(-gsw_z_from_p(max(0.0f, pSea), lat, 0, 0));
}
static const uint8_t WTB_TAG_X[] = {'W' ^ 0x5A, 'T' ^ 0x5A, 'B' ^ 0x5A, 'V' ^ 0x5A, 'E' ^ 0x5A, 'R' ^ 0x5A};
static const uint8_t SLM_TAG_X[] = {'S' ^ 0x5A, 'L' ^ 0x5A, 'M' ^ 0x5A, 'V' ^ 0x5A, 'E' ^ 0x5A, 'R' ^ 0x5A};
bool tagMatch(const uint8_t* p, size_t n, const uint8_t* tagX, size_t tl, size_t& st) {
  for (size_t i = 0; i < n; i++) {
    if (p[i] == (uint8_t)(tagX[st] ^ 0x5A)) { if (++st == tl) return true; }
    else st = (p[i] == (uint8_t)(tagX[0] ^ 0x5A)) ? 1 : 0;
  }
  return false;
}

// ----------------------------------------------------------------- ESP-NOW
struct RxF { uint8_t mac[6]; uint8_t len; uint8_t d[LNK_MAX]; };
QueueHandle_t rxQ;
volatile int8_t txDone = 1;                  // 1 idle, 0 waiting, 2 ok, 3 failed
uint16_t txSeq = 0;

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len < (int)sizeof(LnkHdr) || len > LNK_MAX) return;
  RxF f; memcpy(f.mac, info->src_addr, 6); f.len = (uint8_t)len; memcpy(f.d, data, len);
  xQueueSend(rxQ, &f, 0);
}
void onSent(const esp_now_send_info_t*, esp_now_send_status_t st) { txDone = (st == ESP_NOW_SEND_SUCCESS) ? 2 : 3; }

void addVmrPeer(bool encrypt) {
  if (esp_now_is_peer_exist(vmrMac)) esp_now_del_peer(vmrMac);
  esp_now_peer_info_t p = {}; memcpy(p.peer_addr, vmrMac, 6); p.channel = 0; p.ifidx = WIFI_IF_STA;
  p.encrypt = encrypt; if (encrypt) memcpy(p.lmk, LNK_LMK, 16);
  esp_now_add_peer(&p);
}
// send one frame to the VMR and wait (briefly) for the radio's own acknowledgement
bool lnkSend(uint8_t type, const void* body, uint16_t blen, uint8_t flags = 0, uint16_t seq = 0) {
  if (!vmrKnown || !chanLocked) return false;                     // no VMR beacon: not on its channel, nothing would arrive
  uint8_t b[LNK_MAX]; LnkHdr* h = (LnkHdr*)b;
  h->magic = LNK_MAGIC; h->ver = LNK_VER; h->type = type; h->flags = flags; h->seq = seq ? seq : ++txSeq; h->len = blen;
  if (blen) memcpy(b + sizeof(LnkHdr), body, blen);
  for (int tries = 0; tries < 3; tries++) {
    txDone = 0;
    if (esp_now_send(vmrMac, b, sizeof(LnkHdr) + blen) != ESP_OK) { delay(2); continue; }
    uint32_t t0 = millis(); while (txDone == 0 && millis() - t0 < 60) delay(1);
    if (txDone == 2) return true;
  }
  return false;
}

// acked frames wait here until the VMR answers T_ACK (kept while the link is down)
struct Pend { bool used; uint8_t type; uint16_t seq, len; uint8_t d[LNK_MAX - sizeof(LnkHdr)]; uint32_t lastMs; };
#define PEND_N 40
Pend pend[PEND_N];
void queueAcked(uint8_t type, const void* body, uint16_t len) {
  int k = -1;
  for (int i = 0; i < PEND_N; i++) if (!pend[i].used) { k = i; break; }
  if (k < 0) { for (int i = 0; i < PEND_N; i++) if (pend[i].type == T_EVENT) { k = i; break; } }   // full: drop the oldest event
  if (k < 0) return;
  Pend& p = pend[k]; p.used = true; p.type = type; p.seq = ++txSeq; p.len = len; memcpy(p.d, body, len); p.lastMs = 0;
}
void servicePend() {
  uint32_t now = millis(); int sent = 0;
  if (!chanLocked || !paired) return;
  for (int i = 0; i < PEND_N && sent < 4; i++) {                  // a few a pass, oldest first
    Pend& p = pend[i];
    if (!p.used || (p.lastMs && now - p.lastMs < 400)) continue;
    p.lastMs = now ? now : 1; sent++;
    lnkSend(p.type, p.d, p.len, F_ACKED, p.seq);
  }
}
void event(uint8_t code, const char* text) {
  BEvent e = {}; e.code = code; strncpy(e.text, text, sizeof(e.text) - 1);
  queueAcked(T_EVENT, &e, sizeof(e));
  if (serialDisplay) Serial.printf("EVENT %u %s\n", code, text);
}

void lnkSetup() {
  WiFi.mode(WIFI_STA); WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE);
  rxQ = xQueueCreate(24, sizeof(RxF));
  esp_now_init();
  esp_now_set_pmk(LNK_PMK);
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);
  paired = prefs.getBytes("vmr", vmrMac, 6) == 6;
  if (paired) { vmrKnown = true; addVmrPeer(LNK_ENCRYPT); }
}
// channel scan: 300 ms on each channel until the paired VMR's beacon (or any VMR open for pairing)
void serviceScan() {
  uint32_t now = millis();
  if (chanLocked && now - lastBeaconMs > BEACON_LOST_MS) chanLocked = false;
  if (!chanLocked && now - scanStepMs > 300) {
    scanStepMs = now; chan = chan % 13 + 1;
    esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE);
  }
}

// ----------------------------------------------------------------- forward
void handleFrame(const RxF& f);
void endCast(const char* why);
void startCast(const char* why);
void sendStatus();

// ----------------------------------------------------------------- Ethernet and the IWB
void onNet(arduino_event_id_t e, arduino_event_info_t) {
  if (e == ARDUINO_EVENT_ETH_CONNECTED) { ethUp = true; if (FORCE_10M) { ETH.setAutoNegotiation(false); ETH.setLinkSpeed(10); ETH.setFullDuplex(true); } }
  else if (e == ARDUINO_EVENT_ETH_DISCONNECTED || e == ARDUINO_EVENT_ETH_STOP) ethUp = false;
}
void ethSetup() {
  Network.onEvent(onNet);
  ETH.setPollPeriod(5);
  if (!ETH.begin(ETH_PHY_W5500, 1, ETH_CS_PIN, -1, -1, SPI)) { if (serialDisplay) Serial.println("Ethernet FeatherWing not found"); }
  ETH.config(IPAddress(WTB_IP), IPAddress(0, 0, 0, 0), IPAddress(NET_MASK));
}
void iwbConnectService() {
  static uint32_t lastTry = 0;
  if (iwb && iwb.connected()) return;
  if (millis() - lastTry < 2000) return;
  lastTry = millis();
  if (!ethUp) return;
  if (iwb.connect(IPAddress(IWB_IP), DATA_PORT, 500)) {
    iwb.setNoDelay(true);
    if (serialDisplay) Serial.println("IWB connected");
    char b[48];
    if (timeKnown) { snprintf(b, sizeof(b), "CMD,TIME,%lld", (long long)nowUtc()); iwbSend(b); lastTimeSentMs = millis(); }
    if (lastSeq) { snprintf(b, sizeof(b), "CMD,REPLAY,%lu", (unsigned long)(lastSeq + 1)); iwbSend(b); }
  }
}

// one sample line from the IWB
void writeRow(File& f, const char* utc, uint32_t sq, float pa, float ps, float dp, float t, float ec, float sp, float rho, unsigned flags) {
  if (!f) return;
  char a[16], b[16], c[16], d[16], e[16], g[16], h[16];
  auto fmt = [](char* o, float v, int k) { if (isnan(v)) o[0] = 0; else snprintf(o, 16, "%.*f", k, v); };
  fmt(a, pa, 4); fmt(b, ps, 4); fmt(c, dp, 3); fmt(d, t, 4); fmt(e, ec, 1); fmt(g, sp, 4); fmt(h, rho, 4);
  f.printf("%s,%lu,%s,%s,%s,%s,%s,%s,%s,%02X,%s", utc, (unsigned long)sq, a, b, c, d, e, g, h, flags, castOpen ? String(castNum).c_str() : "");
  if (&f == &sessF && hkPending) {
    char t1[12], t2[12], t3[12], t4[12], t5[12], t6[12];
    fmt(t1, tareDbar, 4); fmt(t2, tubeT, 2); fmt(t3, tubeRH, 1); fmt(t4, dewpt, 2); fmt(t5, iwbV, 2); fmt(t6, iwbCpu, 1);
    f.printf(",%s,%s,%s,%s,%s,%s\n", t1, t2, t3, t4, t5, t6);
  } else f.print(",,,,,,\n");
}
static const char* SESS_HDR = "utc_time,seq,p_abs_dbar,p_sea_dbar,depth_m,t_c,ec_uscm,sp,rho_kgm3,flags,cast,tare_dbar,int_t_c,int_rh_pct,dewpt_c,v_in,cpu_c";

int split(char* s, char** f, int maxf) { int n = 0; f[n++] = s; while (*s && n < maxf) { if (*s == ',') { *s = 0; f[n++] = s + 1; } s++; } return n; }
float numOrNan(const char* s) { return (s && *s) ? atof(s) : NAN; }

void liveAdd(float ps, float dp, float t, float ec, float sp, float rho) {
  int64_t sec = timeKnown ? nowUtc() / 1000 : (int64_t)(millis() / 1000);
  if (sec != liveSec) {
    if (liveSec >= 0) {
      BLive L; L.utcSec = timeKnown ? liveSec * 1000 : 0;
      L.pSea = nPT ? aP / nPT : NAN; L.depth = nPT ? aD / nPT : NAN; L.t = nTs ? aT / nTs : NAN;
      L.ec = nC ? aE / nC : NAN; L.sp = nC ? aS / nC : NAN; L.rho = nC ? aR / nC : NAN; L.nPT = nPT; L.nC = nC;
      lnkSend(T_LIVE, &L, sizeof(L));
    }
    liveSec = sec; aP = aD = aT = aE = aS = aR = 0; nPT = nTs = nC = 0;
  }
  if (!isnan(ps)) { aP += ps; aD += dp; nPT++; }
  if (!isnan(t)) { aT += t; nTs++; }
  if (!isnan(sp) && !isnan(rho)) { aE += ec; aS += sp; aR += rho; nC++; }
}

void onSample(char** f, int n, bool replay) {
  // SLM,seq,ms,utc_ms,p_abs,p_sea,t,ec,sp,rho,flags
  if (n < 11) return;
  uint32_t sq = strtoul(f[1], nullptr, 10);
  if (!replay && sq && lastSeq && sq + 5 < lastSeq) {                // sequence went back: the IWB restarted
    event(E_IWB_RESET, "IWB restarted");
    char b[40]; if (!isnan(tareDbar)) { snprintf(b, sizeof(b), "CMD,TARE,%.4f", tareDbar); iwbSend(b); }
    if (timeKnown) { snprintf(b, sizeof(b), "CMD,TIME,%lld", (long long)nowUtc()); iwbSend(b); }
    lastSeq = 0;
  }
  if (!replay && sq > lastSeq + 1 && lastSeq) { char b[40]; snprintf(b, sizeof(b), "CMD,REPLAY,%lu", (unsigned long)(lastSeq + 1)); iwbSend(b); }
  if (sq > lastSeq) lastSeq = sq;
  int64_t utc = *f[3] ? strtoll(f[3], nullptr, 10) : nowUtc();
  float pa = numOrNan(f[4]), ps = numOrNan(f[5]), t = numOrNan(f[6]), ec = numOrNan(f[7]), sp = numOrNan(f[8]), rho = numOrNan(f[9]);
  unsigned flags = strtoul(f[10], nullptr, 16);
  if (!isnan(pa) && !isnan(ps)) tareDbar = pa - ps;               // the IWB's tare, kept to resend after a restart
  float dp = depthFrom(ps);
  char us[32]; utcStr(utc, us, sizeof(us));
  writeRow(sessF, us, sq, pa, ps, dp, t, ec, sp, rho, flags); hkPending = false;
  if (replay) return;                                              // replays go to the session file only
  lastDepth = dp;
  if (!isnan(t)) { lastT = t; lastTMs = millis(); }
  if (!isnan(sp) && !isnan(rho)) { lastSp = sp; lastRho = rho; lastCMs = millis(); }
  liveAdd(ps, dp, t, ec, sp, rho);
  if (castOpen) {
    writeRow(castF, us, sq, pa, ps, dp, t, ec, sp, rho, flags);
    if (cs && csN < csCap) cs[csN++] = {ps, dp, t, sp, rho, millis()};
  }
  // automatic start and stop on depth
  if (!manual && state != S_PROC && !isnan(dp)) {
    uint32_t now = millis();
    if (!castOpen) {
      if (dp > cfg.startDepthM) { if (!deepSinceMs) deepSinceMs = now; if (now - deepSinceMs >= cfg.startHoldS * 1000) startCast("depth"); }
      else deepSinceMs = 0;
    } else {
      if (dp < cfg.endDepthM) { if (!shallowSinceMs) shallowSinceMs = now; if (now - shallowSinceMs >= cfg.endHoldS * 1000) endCast("surfaced"); }
      else shallowSinceMs = 0;
    }
  }
}
void onHousekeeping(char** f, int n) {
  // SLH,seq,ms,cpu_c,int_t_c,int_rh_pct,dewpt_c,v_in,fw
  if (n < 9) return;
  iwbCpu = numOrNan(f[3]); tubeT = numOrNan(f[4]); tubeRH = numOrNan(f[5]); dewpt = numOrNan(f[6]); iwbV = numOrNan(f[7]);
  strlcpy(iwbFw, f[8], sizeof(iwbFw));
  hkPending = true;
  if (!isnan(dewpt)) {
    if (isnan(dewBase)) dewBase = dewpt;
    if (!dewWarned && dewpt - dewBase >= 3.0f) { dewWarned = true; event(E_DEWPOINT, "Tube dew point up 3 C: check for a leak"); }
  }
  if (!isnan(iwbV) && iwbV < 10.5f && !lowVWarned) { lowVWarned = true; event(E_IWB_LOWV, "IWB supply below 10.5 V"); }
}
// ----------------------------------------------------------------- Niskin bottles
// Buttons: Adafruit 5545 (PCF8574, 0x20), six Adafruit 560 buttons from P0-P5 to GND.
// LEDs: Adafruit 6318 (MCP23017, 0x21): channel n switches button n's ring (12 V, through its resistor), channel 6
// the green ready LED. The IWB fires (6318 at 0x20 on its own bus) and reports its capacitor in $SLF lines.
void nkClearRec(NkRec& r) { float p = r.planM; memset(&r, 0, sizeof(r)); r.planM = p; r.depthM = r.pSea = r.t = r.sp = r.rho = r.lat = r.lon = r.capB = r.capA = NAN; }
void nkSaveState() { prefs.putBytes("nk", &nk, sizeof(nk)); }
void nkLoad() {
  if (prefs.getBytes("nk", &nk, sizeof(nk)) != sizeof(nk) || nk.ver != 1) {
    memset(&nk, 0, sizeof(nk)); nk.ver = 1; nk.lowerMps = NK_LOWER_DEF;
    for (int i = 0; i < NK_N; i++) { nk.r[i].planM = NAN; nkClearRec(nk.r[i]); }
  }
  if (!(nk.lowerMps > 0.02f && nk.lowerMps < 3.0f)) nk.lowerMps = NK_LOWER_DEF;
}
float nkRecharge() { return (rechargeS > 0 && rechargeS < 60) ? rechargeS : NK_RECHARGE_DEF_S; }
float nkMinGap() { return max(NK_GAP_MIN_M, nk.lowerMps * nkRecharge() * 2.0f); }
// planned depths: 0.3-300 m, and no two closer than the capacitor can recharge between at the lowering speed
bool nkPlanOk(const float* plan, float lowerMps, char* why, size_t wn) {
  float gap = max(NK_GAP_MIN_M, lowerMps * nkRecharge() * 2.0f);
  for (int i = 0; i < NK_N; i++) {
    if (isnan(plan[i])) continue;
    if (plan[i] < 0.3f || plan[i] > 300) { snprintf(why, wn, "bottle %d: %.1f m is outside 0.3-300 m", i + 1, plan[i]); return false; }
    for (int k = i + 1; k < NK_N; k++)
      if (!isnan(plan[k]) && fabsf(plan[i] - plan[k]) < gap - 0.001f) { snprintf(why, wn, "bottles %d and %d closer than %.1f m", i + 1, k + 1, gap); return false; }
  }
  return true;
}

// I2C: the PCF8574 is read as one byte; the MCP23017 by register
bool i2cPresent(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }
bool mcpW(uint8_t a, uint8_t reg, uint8_t v) { Wire.beginTransmission(a); Wire.write(reg); Wire.write(v); return Wire.endTransmission() == 0; }
int pcfRead() { if (Wire.requestFrom((uint8_t)NK_BTN_ADDR, (uint8_t)1) != 1) return -1; return Wire.read(); }
void nkHwInit() {
  Wire.beginTransmission(NK_BTN_ADDR); Wire.write(0xFF); nkBtnOk = Wire.endTransmission() == 0;    // all pins high = inputs
  nkLedOk = i2cPresent(NK_LED_ADDR) && mcpW(NK_LED_ADDR, 0x14, 0x00) && mcpW(NK_LED_ADDR, 0x00, 0x00);   // OLATA off, then IODIRA outputs
  nkLedLast = -1;
  static int8_t said = -1; int8_t now = (nkBtnOk ? 1 : 0) | (nkLedOk ? 2 : 0);   // said once, then on a change
  if (serialDisplay && now != said) Serial.printf("Niskin buttons (5545) %s, LED driver (6318 at 0x%02X) %s\n", nkBtnOk ? "ok" : "MISSING", NK_LED_ADDR, nkLedOk ? "ok" : "MISSING");
  said = now;
}

const char* nkSrcName(uint8_t s) { return s == NK_SRC_DASH ? "dashboard" : s == NK_SRC_BUTTON ? "button" : s == NK_SRC_PLAN ? "planned depth" : ""; }
bool slfFresh() { return slfMs && millis() - slfMs < NK_SLF_STALE_MS; }
// why a bottle can't fire now (nullptr = it can)
const char* nkWhyNot(int i, char* b, size_t bn) {
  if (!nk.on) return "Niskin bottles are off";
  if (i < 0 || i >= NK_N) return "no such bottle";
  if (nk.r[i].st == NK_FIRED || nk.r[i].st == NK_NOANSWER) return "already fired";
  if (nk.r[i].st != NK_ARMED) return "not armed";
  if (!castOpen) return "no cast open";
  if (!iwbAlive) return "no IWB link";
  if (!slfFresh() || !solOk) return "no solenoid driver on the IWB";
  if (nkPend >= 0) return "another bottle is firing";
  if (!capReady) { if (capReadyIn > 0) { snprintf(b, bn, "capacitor charging (%.1f s)", capReadyIn); return b; } return "capacitor not charged"; }
  return nullptr;
}
void nkFire(int i, uint8_t src) {
  char b[48]; const char* why = nkWhyNot(i, b, sizeof(b));
  if (why) {
    if (i >= 0 && i < NK_N) refuseUntil[i] = millis() + 1000;
    char t[80]; snprintf(t, sizeof(t), "Bottle %d not fired: %s", i + 1, why); event(E_BOTTLE, t);
    nkDirty = true; return;
  }
  char c[40]; snprintf(c, sizeof(c), "CMD,FIRE,%d,%d", i, NISKIN_PULSE_MS); iwbSend(c);
  nkPend = i; nkPendMs = millis(); nk.r[i].src = src; capReady = false; nkDirty = true;
}
void nkBottleFile(int i) {                     // casts/..._castNNN_bottles.csv, next to the cast's raw file
  if (!sdOk || !castRawName[0]) return;
  char name[48]; strncpy(name, castRawName, sizeof(name)); char* r = strstr(name, "_raw.csv"); if (!r) return; strcpy(r, "_bottles.csv");
  bool fresh = !SD.exists(name);
  File f = SD.open(name, FILE_APPEND, true); if (!f) return;
  if (fresh) f.println("bottle,utc_time,source,p_sea_dbar,depth_m,t_c,sp,rho_kgm3,lat,lon,planned_m,cap_before_v,cap_after_v");
  const NkRec& R = nk.r[i]; char u[32]; utcStr(R.utc, u, sizeof(u));
  auto fm = [](float v, int k) { static char o[4][16]; static int n = 0; char* d = o[n++ & 3]; if (isnan(v)) d[0] = 0; else snprintf(d, 16, "%.*f", k, v); return (const char*)d; };
  f.printf("%d,%s,%s,%s,%s,", i + 1, u, nkSrcName(R.src), fm(R.pSea, 3), fm(R.depthM, 3));
  f.printf("%s,%s,%s,", fm(R.t, 4), fm(R.sp, 4), fm(R.rho, 4));
  f.printf("%s,%s,", fm(R.lat, 6), fm(R.lon, 6));
  f.printf("%s,%s,%s\n", fm(R.planM, 2), fm(R.capB, 2), fm(R.capA, 2));
  f.close();
}
// $ACK,FIRE,ok,ch,p_sea,cap_before,cap_after | $ACK,FIRE,err,ch,reason
void nkOnAck(char** f, int n) {
  if (n < 4) return;
  int i = atoi(f[3]); if (i < 0 || i >= NK_N) return;
  bool ok = !strcmp(f[2], "ok");
  if (i == nkPend) nkPend = -1;
  NkRec& R = nk.r[i];
  if (ok) {
    uint8_t src = R.src; nkClearRec(R); R.src = src;
    R.st = NK_FIRED; R.cast = castNum; R.utc = nowUtc();
    R.pSea = n > 4 ? numOrNan(f[4]) : NAN; R.depthM = isnan(R.pSea) ? lastDepth : depthFrom(R.pSea);
    uint32_t now = millis();
    R.t = (lastTMs && now - lastTMs < 3000) ? lastT : NAN;
    if (lastCMs && now - lastCMs < 3000) { R.sp = lastSp; R.rho = lastRho; }
    R.lat = vmrLat; R.lon = vmrLon;
    R.capB = n > 5 ? numOrNan(f[5]) : NAN; R.capA = n > 6 ? numOrNan(f[6]) : NAN;
    nkSaveState(); nkBottleFile(i);
    BBottle b = {}; b.bottle = i; b.src = R.src; b.castNum = R.cast; b.utcMs = R.utc; b.depthM = R.depthM; b.pSea = R.pSea;
    b.t = R.t; b.sp = R.sp; b.rho = R.rho; b.lat = R.lat; b.lon = R.lon; b.capBefore = R.capB; b.capAfter = R.capA;
    queueAcked(T_BOTTLE, &b, sizeof(b));
    if (!isnan(R.capB) && !isnan(R.capA) && R.capB - R.capA < NK_CAP_DROP_MIN_V) {
      char t[80]; snprintf(t, sizeof(t), "Bottle %d: capacitor hardly dropped, check its solenoid", i + 1); event(E_BOTTLE, t);
    }
    if (serialDisplay) Serial.printf("Bottle %d fired at %.2f m (%s)\n", i + 1, R.depthM, nkSrcName(R.src));
  } else {
    if (R.st == NK_NOANSWER) R.st = NK_ARMED;                 // a late "no": it didn't fire
    char t[100]; snprintf(t, sizeof(t), "Bottle %d didn't fire: %s", i + 1, n > 4 ? f[4] : "IWB error"); event(E_BOTTLE, t);
    nkRetryMs = millis() + 1000;
  }
  nkDirty = true;
}
// $SLF,ms,board,cap_v,v_in,ready,ready_in_s,last_recharge_s
void nkOnSlf(char** f, int n) {
  if (n < 8) return;
  slfMs = millis();
  solOk = atoi(f[2]) == 1; capV = numOrNan(f[3]); capVin = numOrNan(f[4]);
  bool r = atoi(f[5]) == 1 && nkPend < 0;
  if (r != capReady) nkDirty = true;
  capReady = r; capReadyIn = numOrNan(f[6]);
  float rs = numOrNan(f[7]); if (rs > 0) rechargeS = rs;
}
void nkFrameFire(const BFire* q) {
  if (q->id == lastFireId) return;                          // a resend
  lastFireId = q->id;
  int i = q->bottle; bool one = i >= 0 && i < NK_N;
  switch (q->action) {
    case NK_A_FIRE: nkFire(i, NK_SRC_DASH); return;
    case NK_A_ARM:  if (one && i != nkPend && nk.r[i].st != NK_ARMED) { nkClearRec(nk.r[i]); nk.r[i].st = NK_ARMED; } break;
    case NK_A_SAFE: if (one && i != nkPend) { nkClearRec(nk.r[i]); nk.r[i].st = NK_SAFE; } break;
    case NK_A_ARMALL: for (int k = 0; k < NK_N; k++) if (nk.r[k].st == NK_SAFE) nk.r[k].st = NK_ARMED; break;
    case NK_A_SAFEALL: for (int k = 0; k < NK_N; k++) if (nk.r[k].st == NK_ARMED && k != nkPend) nk.r[k].st = NK_SAFE; break;
    case NK_A_RESET: for (int k = 0; k < NK_N; k++) if (k != nkPend) { nkClearRec(nk.r[k]); nk.r[k].st = NK_SAFE; } break;
    default: return;
  }
  nkSaveState(); nkDirty = true;
}
void nkFrameCfg(const BNkCfg* q, uint16_t len) {
  if (len < sizeof(BNkCfg)) return;
  float lower = (q->set & NK_SET_SPEED) && q->lowerMps > 0.02f && q->lowerMps < 3.0f ? q->lowerMps : nk.lowerMps;
  if (q->set & NK_SET_PLAN) {
    char why[64];
    if (nkPlanOk(q->planM, lower, why, sizeof(why))) { for (int i = 0; i < NK_N; i++) nk.r[i].planM = q->planM[i]; }
    else { char t[100]; snprintf(t, sizeof(t), "Planned depths refused: %s", why); event(E_BOTTLE, t); }
  }
  if ((q->set & NK_SET_SPEED) && lower != nk.lowerMps) {
    float cur[NK_N]; for (int i = 0; i < NK_N; i++) cur[i] = nk.r[i].planM;
    char why[64];
    if (nkPlanOk(cur, lower, why, sizeof(why))) nk.lowerMps = lower;
    else { char t[100]; snprintf(t, sizeof(t), "Speed refused: %s", why); event(E_BOTTLE, t); }
  }
  if (q->set & NK_SET_ON) { nk.on = q->on ? 1 : 0; if (!nk.on) { btnHeld = btnDone = 0; } }
  nkSaveState(); nkDirty = true;
}
// buttons: held 1.5 s fires; the ring blinks while held
void nkButtons(uint32_t now) {
  static uint32_t next = 0;
  if ((int32_t)(now - next) < 0) return;
  next = now + 20;
  if (!nkBtnOk || !nk.on) { btnHeld = btnDone = 0; return; }
  int v = pcfRead(); if (v < 0) { nkBtnOk = false; return; }
  uint8_t down = ~v & 0x3F;
  for (int i = 0; i < NK_N; i++) {
    uint8_t m = 1 << i;
    if (down & m) {
      btnUpMs[i] = 0;
      if (!(btnHeld & m)) { btnHeld |= m; btnDownMs[i] = now; btnDone &= ~m; nkDirty = true; }
      else if (!(btnDone & m) && now - btnDownMs[i] >= NK_HOLD_MS) { btnDone |= m; nkFire(i, NK_SRC_BUTTON); }
    } else if (btnHeld & m) {                                   // let go (60 ms, so a bounce doesn't restart the hold)
      if (!btnUpMs[i]) btnUpMs[i] = now ? now : 1;
      else if (now - btnUpMs[i] >= 60) { btnHeld &= ~m; btnDone &= ~m; btnUpMs[i] = 0; nkDirty = true; }
    }
  }
}
void nkLeds(uint32_t now) {
  static uint32_t next = 0, lastWrite = 0;
  if ((int32_t)(now - next) < 0) return;
  next = now + 40;
  if (!nkLedOk) return;
  uint8_t o = 0;
  if (nk.on) {
    for (int i = 0; i < NK_N; i++) {
      uint8_t m = 1 << i; bool on = nk.r[i].st == NK_ARMED;
      if (on && (btnHeld & m) && !(btnDone & m)) on = (now / 125) & 1;   // held: blinks until it fires
      if (i == nkPend) on = true;
      if ((int32_t)(refuseUntil[i] - now) > 0) on = (now / 60) & 1;      // refused: a fast flicker for a second
      if (on) o |= m;
    }
    if (iwbAlive && slfFresh() && solOk && capReady && nkPend < 0) o |= 0x40;   // the green ready LED
  }
  if (o != nkLedLast || now - lastWrite > 1000) {
    if (!mcpW(NK_LED_ADDR, 0x14, o)) { nkLedOk = false; return; }
    nkLedLast = o; lastWrite = now;
  }
}
// planned depths: on the way down, the shallowest due bottle first
void nkAuto(uint32_t now) {
  if (!nk.on || !castOpen || !iwbAlive || isnan(lastDepth) || nkPend >= 0) return;
  int best = -1;
  for (int i = 0; i < NK_N; i++) {
    NkRec& R = nk.r[i];
    if (R.st != NK_ARMED || isnan(R.planM) || lastDepth < R.planM) continue;
    if (lastDepth > R.planM + NK_LATE_M) {
      char t[90]; snprintf(t, sizeof(t), "Bottle %d missed %.1f m (at %.1f m): still armed", i + 1, R.planM, lastDepth);
      event(E_BOTTLE, t); R.planM = NAN; nkSaveState(); nkDirty = true; continue;
    }
    if (best < 0 || R.planM < nk.r[best].planM) best = i;
  }
  if (best >= 0 && capReady && solOk && slfFresh() && (int32_t)(now - nkRetryMs) >= 0) nkFire(best, NK_SRC_PLAN);
}
void nkSendStatus() {
  BBottles b = {};
  b.on = nk.on; b.hw = (nkBtnOk ? NK_HW_BUTTONS : 0) | (nkLedOk ? NK_HW_LEDS : 0) | (slfFresh() && solOk ? NK_HW_SOL : 0) | (slfFresh() ? NK_HW_CAP : 0);
  b.ready = slfFresh() && solOk && capReady && nkPend < 0; b.held = btnHeld & ~btnDone; b.pend = nkPend < 0 ? 0xFF : nkPend; b.castOpen = castOpen;
  b.capV = slfFresh() ? capV : NAN; b.vin = slfFresh() ? capVin : NAN; b.readyInS = slfFresh() ? capReadyIn : NAN;
  b.rechargeS = rechargeS; b.minGapM = nkMinGap(); b.lowerMps = nk.lowerMps;
  for (int i = 0; i < NK_N; i++) {
    const NkRec& R = nk.r[i];
    b.st[i] = R.st; b.src[i] = R.src; b.planM[i] = R.planM; b.depthM[i] = R.depthM; b.t[i] = R.t; b.sp[i] = R.sp; b.rho[i] = R.rho; b.utcMs[i] = R.utc;
  }
  lnkSend(T_BOTTLES, &b, sizeof(b));
}
void nkService(uint32_t now) {
  static uint32_t nextHw = 0, nextSt = 0, lastSt = 0;
  if ((int32_t)(now - nextHw) >= 0) { nextHw = now + 3000; if (!nkBtnOk || !nkLedOk) nkHwInit(); }   // plugged in later, or came back
  nkButtons(now);
  if (nkPend >= 0 && now - nkPendMs > NK_ANSWER_MS) {
    nk.r[nkPend].st = NK_NOANSWER; nk.r[nkPend].cast = castNum; nk.r[nkPend].utc = nowUtc(); nk.r[nkPend].depthM = lastDepth;
    char t[80]; snprintf(t, sizeof(t), "Bottle %d: no answer from the IWB, it may have closed", nkPend + 1); event(E_BOTTLE, t);
    nkPend = -1; nkSaveState(); nkDirty = true;
  }
  nkAuto(now);
  nkLeds(now);
  if (nk.on && ((int32_t)(now - nextSt) >= 0 || (nkDirty && now - lastSt > 150))) { nextSt = now + 1000; lastSt = now; nkDirty = false; nkSendStatus(); }
}

char lineBuf[256]; uint16_t lineLen = 0;
char lastAck[200] = ""; bool ackFresh = false;
void readIwb() {
  while (iwb && iwb.available()) {
    char c = iwb.read();
    if (c == '\n' || c == '\r') {
      if (!lineLen) continue;
      lineBuf[lineLen] = 0; lineLen = 0;
      char* s = lineBuf; if (*s != '$') continue; s++;
      char* star = strchr(s, '*'); if (!star) continue;
      if (strtoul(star + 1, nullptr, 16) != nmeaSum(s)) continue;     // bad checksum: dropped
      *star = 0;
      lastLineMs = millis();
      if (!iwbAlive) { iwbAlive = true; event(E_LINK_UP, "IWB link up"); }
      iwbEverSeen = true;
      char* f[16]; int n = split(s, f, 16);
      if (!strcmp(f[0], "SLM")) { unsigned fl = n > 10 ? strtoul(f[10], nullptr, 16) : 0; onSample(f, n, fl & 0x02); }
      else if (!strcmp(f[0], "SLH")) onHousekeeping(f, n);
      else if (!strcmp(f[0], "SLF")) nkOnSlf(f, n);
      else if (!strcmp(f[0], "ACK") && n > 1 && !strcmp(f[1], "FIRE")) nkOnAck(f, n);   // bottles: handled here
      else if (!strcmp(f[0], "ACK")) {                              // pass the answer on to the VMR
        char t[200] = ""; for (int i = 1; i < n; i++) { if (i > 1) strncat(t, ",", sizeof(t) - strlen(t) - 1); strncat(t, f[i], sizeof(t) - strlen(t) - 1); }
        BText r = {}; strncpy(r.text, t, sizeof(r.text) - 1); lnkSend(T_IWBREPLY, &r, sizeof(r));
      }
    } else if (lineLen < sizeof(lineBuf) - 1) lineBuf[lineLen++] = c;
  }
  if (iwbAlive && millis() - lastLineMs > IWB_SILENT_MS) { iwbAlive = false; event(E_LINK_DOWN, "IWB link down"); if (iwb) iwb.stop(); }
}

// ----------------------------------------------------------------- casts
void startCast(const char* why) {
  if (castOpen || state == S_PROC) return;
  castNum = prefs.getUShort("cast", 0) + 1; prefs.putUShort("cast", castNum);
  char tail[32];
  snprintf(tail, sizeof(tail), "cast%03u_raw.csv", castNum); stampName(castRawName, sizeof(castRawName), "casts", tail);
  strncpy(castProfName, castRawName, sizeof(castProfName)); char* r = strstr(castProfName, "_raw.csv"); if (r) strcpy(r, "_prof.csv");
  if (sdOk) { castF = SD.open(castRawName, FILE_WRITE, true); if (castF) castF.println(SESS_HDR); }
  castOpen = true; state = S_CAST; csN = 0; castStartUtc = nowUtc(); castStartMs = millis(); deepSinceMs = shallowSinceMs = 0;
  if (serialDisplay) Serial.printf("Cast %u open (%s)\n", castNum, why);
  sendStatus();
}
struct Bin { double p, d, t, sp, rho; uint16_t n, nt, ns; };
void endCast(const char* why) {
  if (!castOpen) return;
  castOpen = false; state = S_PROC;
  if (castF) castF.close();
  if (serialDisplay) Serial.printf("Cast %u closed (%s), %lu samples\n", castNum, why, (unsigned long)csN);
  BCastDone cd = {}; cd.castNum = castNum; cd.startUtc = castStartUtc; cd.endUtc = nowUtc(); cd.lat = vmrLat; cd.lon = vmrLon;
  cd.toDraft = cfg.castToDraft; strncpy(cd.profName, castProfName, sizeof(cd.profName) - 1);
  cd.meanRho = NAN; cd.rangeTop = NAN; cd.rangeBottom = NAN; cd.draftUsed = NAN; cd.maxDepth = NAN;
  // 1. the down cast: from the shallowest point after the soak to the deepest sample
  uint32_t i0 = 0, iMax = 0; float pMax = -1;
  for (uint32_t i = 0; i < csN; i++) if (!isnan(cs[i].p) && cs[i].p > pMax) { pMax = cs[i].p; iMax = i; }
  uint32_t soakEnd = 0; while (soakEnd < csN && cs[soakEnd].ms - castStartMs < cfg.soakS * 1000) soakEnd++;
  if (soakEnd > iMax) soakEnd = 0;                               // no soak (lowered straight down): from the start
  float pMin = 1e9; i0 = soakEnd;
  for (uint32_t i = soakEnd; i <= iMax && i < csN; i++) if (!isnan(cs[i].p) && cs[i].p < pMin) { pMin = cs[i].p; i0 = i; }
  // 2. loop edit and 3. bins
  uint16_t nb = 0; Bin* bins = nullptr;
  if (csN && pMax > 0) {
    cd.maxDepth = depthFrom(pMax);
    nb = (uint16_t)min(2000.0f, floorf(cd.maxDepth / cfg.binM) + 1);
    bins = (Bin*)ps_calloc(nb, sizeof(Bin));
    if (!bins) { nb = 0; if (serialDisplay) Serial.println("No memory for the bins: profile skipped"); }
  }
  if (bins) {
    float run = -1;
    for (uint32_t i = i0; i <= iMax; i++) {
      const CS& s = cs[i]; if (isnan(s.p) || isnan(s.depth)) continue;
      if (s.p > run) run = s.p;
      if (s.p < run - LOOP_EDIT_DBAR) continue;                     // heave loop: dropped
      int k = (int)floorf(s.depth / cfg.binM); if (k < 0 || k >= nb) continue;
      Bin& b = bins[k]; b.p += s.p; b.d += s.depth; b.n++;
      if (!isnan(s.t)) { b.t += s.t; b.nt++; }
      if (!isnan(s.sp) && !isnan(s.rho)) { b.sp += s.sp; b.rho += s.rho; b.ns++; }
    }
  }
  // 4. mean density: the whole down cast, or to the deepest draft + 5 %
  float limit = 1e9;
  if (cfg.castToDraft) {
    if (isnan(draftDeep)) cd.reason = 1; else { limit = draftDeep * 1.05f; cd.draftUsed = draftDeep; if (isnan(cd.maxDepth) || cd.maxDepth < limit) cd.reason = 1; }
  }
  double rs = 0; uint16_t ru = 0, used = 0;
  for (uint16_t k = 0; k < nb; k++) {
    if (!bins[k].n) continue;
    used++;
    float dm = bins[k].d / bins[k].n;
    if (bins[k].ns && dm <= limit && cd.reason == 0) {
      rs += bins[k].rho / bins[k].ns; ru++;
      if (isnan(cd.rangeTop)) cd.rangeTop = dm;
      cd.rangeBottom = dm;
    }
  }
  cd.nBins = used; cd.nUsed = ru;
  if (cd.reason == 0) { if (ru) cd.meanRho = rs / ru; else cd.reason = 2; }
  // the profile file
  if (sdOk) {
    File pf = SD.open(castProfName, FILE_WRITE, true);
    if (pf) {
      char a[32], b[32]; utcStr(cd.startUtc, a, sizeof(a)); utcStr(cd.endUtc, b, sizeof(b));
      pf.printf("# SeaLabMOS cast %u, WTB %s, IWB %s\n# start %s, end %s, lat %.5f, lon %.5f\n", castNum, FW_VERSION, iwbFw, a, b, cd.lat, cd.lon);
      pf.printf("# down cast, loop edit %.2f dbar, %.2f m bins, soak %.0f s, tare %.4f dbar\n", LOOP_EDIT_DBAR, cfg.binM, cfg.soakS, tareDbar);
      if (cd.reason == 0) pf.printf("# mean in-situ density %.4f kg/m3 over %.2f-%.2f m (%u bins)%s\n", cd.meanRho, cd.rangeTop, cd.rangeBottom, ru, cfg.castToDraft ? " (to the deepest draft + 5 %)" : "");
      else pf.printf("# no mean density: %s\n", cd.reason == 1 ? "the cast didn't reach the deepest draft + 5 %" : "no salinity in the bins");
      for (int i = 0; i < NK_N; i++) {                              // Niskin bottles closed during this cast
        const NkRec& R = nk.r[i];
        if ((R.st != NK_FIRED && R.st != NK_NOANSWER) || R.cast != castNum) continue;
        char u[32]; utcStr(R.utc, u, sizeof(u));
        pf.printf("# bottle %d %s at %.2f m, %s, t %.4f C, sp %.4f, rho %.4f kg/m3 (%s)\n", i + 1, R.st == NK_FIRED ? "fired" : "fired? no answer",
                  R.depthM, u, R.t, R.sp, R.rho, nkSrcName(R.src));
      }
      pf.println("bin,p_dbar,depth_m,t_c,sp,rho_kgm3,n");
      for (uint16_t k = 0; k < nb; k++) {
        Bin& B = bins[k]; if (!B.n) continue;
        pf.printf("%u,%.4f,%.3f,", k, B.p / B.n, B.d / B.n);
        if (B.nt) pf.printf("%.4f,", B.t / B.nt); else pf.print(",");
        if (B.ns) pf.printf("%.4f,%.4f,", B.sp / B.ns, B.rho / B.ns); else pf.print(",,");
        pf.printf("%u\n", B.n);
      }
      pf.close();
    }
  }
  // to the VMR: the summary, then the bins
  queueAcked(T_CASTDONE, &cd, sizeof(cd));
  BProfile pr = {}; pr.castNum = castNum; pr.total = used;
  uint16_t idx = 0;
  for (uint16_t k = 0; k < nb; k++) {
    Bin& B = bins[k]; if (!B.n) continue;
    BBin& o = pr.b[pr.n++];
    o.p = B.p / B.n; o.depth = B.d / B.n; o.t = B.nt ? B.t / B.nt : NAN; o.sp = B.ns ? B.sp / B.ns : NAN; o.rho = B.ns ? B.rho / B.ns : NAN; o.n = B.n;
    idx++;
    if (pr.n == 9) { queueAcked(T_PROFILE, &pr, sizeof(pr)); pr.first = idx; pr.n = 0; }
  }
  if (pr.n) queueAcked(T_PROFILE, &pr, sizeof(pr));
  if (bins) free(bins);
  state = disarming ? S_OFF : S_READY; readySinceMs = millis();
  sendStatus();
}

// ----------------------------------------------------------------- files and firmware for the VMR
void listFiles(const BFList& q) {
  BFListR r = {}; r.dir = q.dir; r.start = q.start;
  File d = SD.open(q.dir ? "/casts" : "/sess");
  uint16_t i = 0;
  if (d) for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    if (!f.isDirectory()) {
      if (i >= q.start && r.n < 5) { strncpy(r.e[r.n].name, f.name(), 39); r.e[r.n].size = f.size(); r.n++; }
      i++;
    }
    f.close();
  }
  r.total = i;
  lnkSend(T_FLISTR, &r, sizeof(r));
}
void sendFile(const BFGet& q) {
  char path[64]; snprintf(path, sizeof(path), "%s/%s", q.dir ? "/casts" : "/sess", q.name);
  BFData r; r.reqId = q.reqId; r.off = q.off; r.status = 0; r.total = 0;
  File f = sdOk && !strchr(q.name, '/') ? SD.open(path) : File();
  if (!f) { r.status = 1; lnkSend(T_FDATA, &r, 13); return; }
  r.total = f.size();
  if (q.off > r.total || !f.seek(q.off)) { r.status = 1; lnkSend(T_FDATA, &r, 13); f.close(); return; }
  uint32_t left = min(q.len, r.total - q.off);
  if (!left) lnkSend(T_FDATA, &r, 13);
  while (left) {
    int n = f.read(r.d, min<uint32_t>(LNK_DATA, left)); if (n <= 0) break;
    if (!lnkSend(T_FDATA, &r, 13 + n)) break;                       // the VMR asks again from where it got to
    r.off += n; left -= n;
    esp_task_wdt_reset();
  }
  f.close();
}
// firmware: WTB's own (to its update partition) or the IWB's (staged on the card, then posted to it)
uint8_t fwTarget = 0; uint32_t fwSize = 0, fwNext = 0, fwCrc = 0; size_t fwTagSt = 0; bool fwTagOk = false; File fwF;
void fwAck(uint8_t st, const char* text) { BFwAck a = {}; a.status = st; a.next = fwNext; if (text) strncpy(a.text, text, sizeof(a.text) - 1); lnkSend(T_FWACK, &a, sizeof(a)); }
bool postToIwb(char* why, size_t wn) {
  File f = SD.open("/fw/iwb.bin"); if (!f) { snprintf(why, wn, "staged file missing"); return false; }
  NetworkClient c;
  if (!c.connect(IPAddress(IWB_IP), 80, 3000)) { f.close(); snprintf(why, wn, "IWB not answering on port 80"); return false; }
  const char* B = "----sealabmosfw";
  char head[200]; snprintf(head, sizeof(head), "--%s\r\nContent-Disposition: form-data; name=\"fw\"; filename=\"iwb.bin\"\r\nContent-Type: application/octet-stream\r\n\r\n", B);
  char tail[40]; snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", B);
  uint32_t len = strlen(head) + f.size() + strlen(tail);
  c.printf("POST /update HTTP/1.1\r\nHost: 10.77.0.2\r\nContent-Type: multipart/form-data; boundary=%s\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n", B, (unsigned long)len);
  c.print(head);
  uint8_t buf[1024]; int n;
  while ((n = f.read(buf, sizeof(buf))) > 0) { if (c.write(buf, n) != (size_t)n) break; esp_task_wdt_reset(); }
  f.close(); c.print(tail);
  uint32_t t0 = millis(); String resp;
  while (millis() - t0 < 30000 && (c.connected() || c.available())) { while (c.available()) resp += (char)c.read(); if (resp.indexOf("\r\n\r\n") > 0 && resp.length() > 20) break; delay(10); esp_task_wdt_reset(); }
  c.stop();
  bool ok = resp.startsWith("HTTP/1.1 200");
  snprintf(why, wn, ok ? "IWB updated, restarting" : "IWB refused the image");
  return ok;
}
void fwFrame(uint8_t type, const uint8_t* b, uint16_t len) {
  if (type == T_FWBEGIN) {
    const BFwBegin* q = (const BFwBegin*)b;
    fwTarget = q->target; fwSize = q->size; fwNext = 0; fwCrc = 0; fwTagSt = 0; fwTagOk = false;
    if (fwTarget == 1) { if (!Update.begin(fwSize)) { fwAck(2, "no room for the image"); fwTarget = 0; return; } }
    else if (fwTarget == 2) { if (!sdOk) { fwAck(2, "no microSD card"); fwTarget = 0; return; } SD.mkdir("/fw"); fwF = SD.open("/fw/iwb.bin", FILE_WRITE, true); if (!fwF) { fwAck(2, "can't write the card"); fwTarget = 0; return; } }
    fwAck(0, "begin");
  } else if (type == T_FWCHUNK) {
    if (!fwTarget || len < 4) return;
    const BFwChunk* q = (const BFwChunk*)b; uint16_t n = len - 4;
    if (q->off == fwNext && fwNext + n <= fwSize) {
      if (fwNext == 0 && q->d[0] != 0xE9) { fwAck(2, "not an ESP32 image"); if (fwTarget == 1) Update.abort(); else fwF.close(); fwTarget = 0; return; }
      fwCrc = lnkCrc32(fwCrc, q->d, n);
      if (!fwTagOk) fwTagOk = tagMatch(q->d, n, fwTarget == 1 ? WTB_TAG_X : SLM_TAG_X, 6, fwTagSt);
      bool ok = fwTarget == 1 ? Update.write((uint8_t*)q->d, n) == n : fwF.write(q->d, n) == n;
      if (!ok) { fwAck(2, "write failed"); if (fwTarget == 1) Update.abort(); else fwF.close(); fwTarget = 0; return; }
      fwNext += n;
    }
    fwAck(0, nullptr);                                              // cumulative: where it wants the next byte from
  } else if (type == T_FWEND) {
    if (!fwTarget) return;
    const BFwEnd* q = (const BFwEnd*)b;
    if (fwNext != fwSize || q->crc != fwCrc) { fwAck(2, "CRC or size mismatch"); if (fwTarget == 1) Update.abort(); else fwF.close(); fwTarget = 0; return; }
    if (!fwTagOk) { fwAck(2, fwTarget == 1 ? "not a WTB image (no WTB firmware tag)" : "not an IWB image (no IWB firmware tag)"); if (fwTarget == 1) Update.abort(); else fwF.close(); fwTarget = 0; return; }
    if (fwTarget == 1) {
      if (!Update.end(true)) { fwAck(2, "image check failed"); fwTarget = 0; return; }
      fwAck(1, "WTB updated, restarting"); delay(500); ESP.restart();
    } else {
      fwF.close(); fwAck(0, "sending to the IWB");
      char why[48]; bool ok = postToIwb(why, sizeof(why));
      fwAck(ok ? 1 : 2, why);
    }
    fwTarget = 0;
  }
}

// ----------------------------------------------------------------- frames from the VMR
void handleFrame(const RxF& f) {
  const LnkHdr* h = (const LnkHdr*)f.d;
  if (h->magic != LNK_MAGIC || h->ver != LNK_VER || sizeof(LnkHdr) + h->len > f.len) return;
  const uint8_t* b = f.d + sizeof(LnkHdr);
  bool fromVmr = vmrKnown && !memcmp(f.mac, vmrMac, 6);
  if (h->type == T_BEACON) {
    const BBeacon* q = (const BBeacon*)b;
    if (paired && fromVmr) { chanLocked = true; lastBeaconMs = millis(); }
    else if (!paired && q->pairOpen) {                               // unpaired: answer the VMR whose "Pair WTB" is open
      chanLocked = true; lastBeaconMs = millis();
      memcpy(vmrMac, f.mac, 6); vmrKnown = true; addVmrPeer(false);
      BHello hb = {}; strlcpy(hb.fw, FW_VERSION, sizeof(hb.fw)); strncpy(hb.name, SYSTEM_NAME, 15);
      lnkSend(T_HELLO, &hb, sizeof(hb));
    }
    return;
  }
  if (h->type == T_PAIROK && vmrKnown && !memcmp(f.mac, vmrMac, 6)) {
    paired = true; prefs.putBytes("vmr", vmrMac, 6); addVmrPeer(LNK_ENCRYPT);
    if (serialDisplay) Serial.println("Paired with the VMR");
    return;
  }
  if (!fromVmr || !paired) return;
  if (h->flags & F_ACKED) { BAck a = {h->seq, 0}; lnkSend(T_ACK, &a, sizeof(a)); }
  switch (h->type) {
    case T_ACK: { const BAck* a = (const BAck*)b; for (int i = 0; i < PEND_N; i++) if (pend[i].used && pend[i].seq == a->seq) pend[i].used = false; } break;
    case T_VMR: {
      const BVmr* q = (const BVmr*)b;
      if (q->utcSrc && q->utcMs > 0) {
        utcOffsetMs = q->utcMs - (int64_t)millis(); timeSrc = q->utcSrc;
        if (!timeKnown) { timeKnown = true; if (iwb && iwb.connected()) { char c[48]; snprintf(c, sizeof(c), "CMD,TIME,%lld", (long long)nowUtc()); iwbSend(c); lastTimeSentMs = millis(); } }
      }
      vmrLat = q->lat; vmrLon = q->lon; draftDeep = q->draftDeepM;
    } break;
    case T_SESSION: {
      const BSession* q = (const BSession*)b;
      manual = q->manual;
      if (!q->arm) { disarming = true; if (castOpen) endCast("disarmed"); else if (state != S_PROC) state = S_OFF; sendStatus(); }
      else { disarming = false; if (state == S_OFF) { state = iwbAlive ? S_READY : S_STARTING; readySinceMs = millis(); } }
      if (q->action == 1) startCast("by hand");
      else if (q->action == 2) endCast("by hand");
      else if (q->action == 3) iwbSend("CMD,TARE");
    } break;
    case T_CONFIG: memcpy(&cfg, b, min<size_t>(h->len, sizeof(cfg))); break;
    case T_FLIST: listFiles(*(const BFList*)b); break;
    case T_FGET: sendFile(*(const BFGet*)b); break;
    case T_FDEL: { const BFDel* q = (const BFDel*)b; if (!strchr(q->name, '/')) { char p[64]; snprintf(p, sizeof(p), "%s/%s", q->dir ? "/casts" : "/sess", q->name); if (strcmp(p, sessName) && !(castOpen && !strcmp(p, castRawName))) SD.remove(p); } } break;   // 1.11: not the open cast's file either
    case T_FWBEGIN: case T_FWCHUNK: case T_FWEND: fwFrame(h->type, b, h->len); break;
    case T_FIRE: if (h->len >= sizeof(BFire)) nkFrameFire((const BFire*)b); break;
    case T_NKCFG: nkFrameCfg((const BNkCfg*)b, h->len); break;
    case T_IWBCMD: { const BText* q = (const BText*)b; char c[220]; snprintf(c, sizeof(c), "CMD,%s", q->text); iwbSend(c); } break;
  }
}

void sendStatus() {
  BStatus s = {};
  strlcpy(s.wtbFw, FW_VERSION, sizeof(s.wtbFw)); strlcpy(s.iwbFw, iwbFw, sizeof(s.iwbFw));
  s.eth = ethUp; s.iwbAlive = iwbAlive; s.state = state; s.castOpen = castOpen; s.manual = manual; s.sdOk = sdOk; s.castNum = castNum;
  s.depthM = iwbAlive ? lastDepth : NAN; s.tareDbar = tareDbar; s.iwbV = iwbV; s.tubeT = tubeT; s.tubeRH = tubeRH; s.dewpt = dewpt; s.iwbCpu = iwbCpu;
  s.sdFreeMB = sdOk ? (uint32_t)((SD.totalBytes() - SD.usedBytes()) >> 20) : 0;
  const char* sn = strrchr(sessName, '/'); strncpy(s.rawName, sn ? sn + 1 : "", sizeof(s.rawName) - 1);
  lnkSend(T_STATUS, &s, sizeof(s));
}

// ----------------------------------------------------------------- buttons
Btn tareB = {TARE_PIN, true, 0}, startB = {START_PIN, true, 0};
bool pressed(Btn& b) {
  bool v = digitalRead(b.pin);
  if (v != b.last && millis() - b.ms > 40) { b.last = v; b.ms = millis(); return !v; }
  return false;
}

// ----------------------------------------------------------------- setup / loop
void openSession() {
  if (!sdOk || sessF) return;
  SD.mkdir("/sess"); SD.mkdir("/casts");
  stampName(sessName, sizeof(sessName), "sess", "session.csv");
  sessF = SD.open(sessName, FILE_WRITE, true);
  if (sessF) { sessF.printf("# SeaLabMOS WTB %s session, %s\n", FW_VERSION, SYSTEM_NAME); sessF.println(SESS_HDR); }
}
void setup() {
  if (serialDisplay) { Serial.begin(115200); delay(1000); Serial.printf("\nSeaLabMOS WTB %s %s\n", FW_VERSION, FW_TAG); }
  bootMs = millis();
  pinMode(TARE_PIN, INPUT_PULLUP); pinMode(START_PIN, INPUT_PULLUP);
  pinMode(ETHLED_PIN, OUTPUT); digitalWrite(ETHLED_PIN, HIGH);
  prefs.begin("wtb", false);
  nkLoad();
#if defined(PIN_I2C_POWER)
  pinMode(PIN_I2C_POWER, INPUT); delay(1);
  int i2cOff = digitalRead(PIN_I2C_POWER);                          // the STEMMA QT rail's off level differs between board revisions
  pinMode(PIN_I2C_POWER, OUTPUT); digitalWrite(PIN_I2C_POWER, !i2cOff); delay(100);
#endif
  Wire.begin(); Wire.setClock(NK_I2C_HZ);
  nkHwInit();
  csCap = (uint32_t)CAST_MAX_MIN * 60 * 5; cs = (CS*)ps_malloc(sizeof(CS) * csCap); if (!cs) csCap = 0;
  SPI.begin();
  sdOk = SD.begin(SD_CS_PIN, SPI, 10000000);
  if (serialDisplay) Serial.println(sdOk ? "microSD ok" : "microSD MISSING");
  ethSetup();
  lnkSetup();
  esp_task_wdt_config_t wc = {.timeout_ms = 15000, .idle_core_mask = 0, .trigger_panic = true};
  esp_task_wdt_reconfigure(&wc); esp_task_wdt_add(NULL);
}

void loop() {
  uint32_t now = millis();
  esp_task_wdt_reset();
  RxF f; while (xQueueReceive(rxQ, &f, 0) == pdTRUE) handleFrame(f);
  serviceScan();
  if (!sessF && (timeKnown || now - bootMs > 10000)) openSession();   // wait up to 10 s for the VMR's time to name the file
  iwbConnectService();
  readIwb();
  if (timeKnown && iwb && iwb.connected() && now - lastTimeSentMs > TIME_EVERY_MS) { char c[48]; snprintf(c, sizeof(c), "CMD,TIME,%lld", (long long)nowUtc()); iwbSend(c); lastTimeSentMs = now; }

  // states
  if (state == S_STARTING && iwbAlive) { state = S_READY; readySinceMs = now; }
  if (state == S_STARTING && now - bootMs > STARTING_FAULT_MS) state = S_FAULT;
  if (state == S_FAULT && iwbAlive) { state = S_READY; readySinceMs = now; }
  if (state == S_READY && cfg.idleMin && now - readySinceMs > (uint32_t)cfg.idleMin * 60000UL) { state = S_OFF; event(E_IDLE, "No cast for the idle time: switching off"); }

  if (pressed(tareB)) { iwbSend("CMD,TARE"); event(E_SENSOR, "Tare button"); }
  if (pressed(startB)) { if (castOpen) endCast("button"); else if (state == S_READY || state == S_FAULT) startCast("button"); }
  digitalWrite(ETHLED_PIN, (!ethUp || !iwbAlive) ? HIGH : LOW);

  static uint32_t nextStatus = 0;
  if ((int32_t)(now - nextStatus) >= 0) { nextStatus = now + 1000; sendStatus(); }
  nkService(now);
  servicePend();
  if (now - lastFlushMs > 1000) { lastFlushMs = now; if (sessF) sessF.flush(); if (castF) castF.flush(); }
  delay(1);
}
