// SPDX-FileCopyrightText: 2026 Aspect Wave LLC
// SPDX-License-Identifier: GPL-3.0-or-later
// ---- demo glue: what the copied firmware functions (ui.inc) need, with the hardware left out.
// JS (the demo page) writes the sensor state and the button pins; events it has to act on go in evq.
char evq[512] = "";
void ev(const char* s) { size_t k = strlen(evq); if (k + strlen(s) + 2 < sizeof(evq)) { strcat(evq, s); strcat(evq, "\n"); } }

#define HOLD_MS 1000UL
#define INFO_MS 180000UL
#define HOLD_CONTRAST 77
#define POT_STEP 6
#define USE_POT_SWITCH 1
#define MARK_HOLD_MS 1500UL
#define LOGGER_SLEEP_MIN_MS 60000UL
#define LED_DUTY_MIN 12
#define LED_MARK_BLINK_MS 250
#define LED_WAKE_FLASH_MS 100
#define LED_HEARTBEAT_MS 60
#define LED_MORSE_UNIT_MS 120
#define FAULT_HOLD_MS 5000
#define DISP_LCD 4
#define SEG_DEF_ADDR 0x70
enum { LED_MARK = 0, LED_REC, LED_FAULT, LED_N };
static const char MORSE_FAULT[] = "1010111010001011100010101110001011101010001110000000";
enum { XB_CAST = 0, XB_REC, XB_PAUSE, XB_WIFI, XB_SCR, XB_N };
enum { LCD_MOTION = 0, LCD_POS, LCD_NAV, LCD_WX, LCD_WIND, LCD_WAVES, LCD_TIME, LCD_SYS, LCD_MAX, LCD_CAST, LCD_N };
#define LCD_W 20
#define LCD_H 4
#define DEG ((char)0xDF)
enum { SW_FEATHER = 0, SW_NONE = 9 };
enum { POT_NONE = 17 };

Button xbBtn[XB_N];
uint32_t lastDrawMs = 0, infoMs = 0, lastPotMs = 0, ledMarkEvMs = 0, ledMarkEvLen = 0, ledTestMs = 0, ledHeartMs = 0, sdDownMs = 0;
uint32_t faultRawSince = 0, faultLedStart = 0;
bool oledBlank = false, ledMarkEvLevel = false, faultLed = false, reqLedTest = false, segRamValid = false, lcdShownValid = false;
int holdPrevContrast = -1, contrastBeforeOta = -1;
float potEma = -1;
uint8_t ballBefore = 0, lcdCycleIdx = 0, markToastN = 0, markToastBottle = 0; char markToastKind = 'M';
volatile uint8_t reqMarkBtn = 0;
volatile bool castOpenNow = false;
const char* ledState[LED_N] = {"", "", ""};
uint8_t ledOnPin[LED_N] = {1, 2, 3};
int ledOut[LED_N] = {0, 0, 0};
const char* sysEvent = nullptr; bool pauseRowDue = false;
struct { int a, b, c; } rsCur;
char pendTag[24] = ""; uint8_t pendCond = 255;
uint32_t lcdCycleMs = 0, segMs = 0, lcdMs = 0;
float gmC = NAN, gmEst = NAN;
int8_t lastSwState = -1;
// the knob (0-4095, -1 = none) and its switch, the Mark button: written by JS
int knobRaw = 2048; bool knobSw = true; bool markRaw = false;
int potRawNow() { return knobRaw; }
bool potSwOn() { return knobSw; }
bool swOnPcf() { return false; }
bool extPinLow(int) { return markRaw; }
struct ExtSt { bool ok; } ext[EXT_N] = {{true}, {true}, {true}, {true}, {true}};
bool awPin(uint8_t) { return false; }
long map(long x, long a, long b, long c, long d) { return (x - a) * (d - c) / (b - a) + c; }
uint32_t esp_random() { static uint32_t s = 2463534242u; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
portMUX_TYPE dlUiMux = 0, markOledMux = 0, slmMux = 0;

void setContrastLevel(int c) { contrast = constrain(c, 1, 255); }
void setDisplayOn(bool on) { displayOn = on; lastDrawMs = 0; }
uint8_t oledRotation() { return P.oledFlip ? 3 : 1; }
float magHeading() { extern float magHdg; return magHdg; }
float magHdg = NAN;
uint32_t bufSecs = 0; uint32_t bufSeconds() { return bufSecs; }

// SeaLabMOS: the bottles' status as the WTB sends it (the page fills slmBt and slmBtMs)
BBottles slmBt = {}; uint32_t slmBtMs = 0;
void slmArm(bool on) { ev(on ? "slm:arm" : "slm:disarm"); }

// actions that go to the demo's simulator
bool wifiJobStart(uint8_t jb) { ev(jb == 1 ? "wifi:restart" : jb == 2 ? "wifi:off" : "wifi:on"); return true; }
void gpsSetOff(bool off) { gpsOff = off; ev(off ? "gps:off" : "gps:on"); }
void setLoggerMode(bool on) { loggerMode = on; ev(on ? "logger:on" : "logger:off"); }
void loggerPeek() { peekOn = true; peekUntilMs = millis() + LOGGER_PEEK_MS; peekDrawMs = 0; }

bool ledSwitchOn() { return P.potSwSrc == SW_NONE ? true : lastSwState != 0; }
// what is wrong now: the demo's simulator writes faultNow ("" = nothing); the Fault LED waits FAULT_HOLD_MS, as the firmware
char faultNow[96] = "";
bool battLow = false;
void faultCheck(uint32_t now) {
  uint8_t k = faultWhyIdx ^ 1;
  strlcpy(faultWhy[k], faultNow, sizeof(faultWhy[k]));
  faultWhyIdx = k;
  if (!faultNow[0]) { faultRawSince = 0; faultLed = false; return; }
  if (!faultRawSince) faultRawSince = now ? now : 1;
  if (!faultLed && now - faultRawSince >= FAULT_HOLD_MS) { faultLed = true; if (!faultLedStart) faultLedStart = now ? now : 1; }
}
bool battSlopeOk = false;
uint8_t battEst12(float& h, int& ma, bool& early) { h = NAN; ma = 0; early = false; return BE_UNKNOWN; }
void displayStep(); void segStep(); bool screenAvail(uint8_t s); bool cdRunning(); bool dlShowNow();
bool cdSet = false; uint32_t cdEndMs = 0; uint16_t cdTotalS = 0; volatile int16_t reqSegCd = -1;
#define CD_BOOM_MS  2000
#define CD_STAT_MS  3500
#define CD_FX_MS    6000
void holdLabel(uint8_t b, char* big, char* det); uint8_t hgCount(); uint8_t hgAt(uint8_t i); uint8_t hgPos(uint8_t b); int hgTop(int i); uint8_t holdBand();
void screenStep(); void oledBlankSet(bool b); void setPaused(bool on);
uint32_t scrDownMs = 0;
float WS[3][81];

// alarms: the page's simulator keeps them (dash.js); these say what the OLED shows. A VMR control acknowledges (an event to it).
struct AlarmSt { bool on, acked; uint32_t since, ackMs, lastHit; };
AlarmSt al[AL_N];
static const char* const AL_NAME[AL_N] = {"BNWAS", "ANCHOR", "ROLL", "HEAVE HS", "WIND", "BATTERY", "GPS"};
uint8_t bnStage = 0; uint32_t bnLeftDemo = 0; float anchorDistM = NAN; uint32_t gpsNoFixMs = 0;
uint8_t shtHeatSt = 0;                 // the SHT30's heater (dash.js sets it)
uint32_t bnLeftS(uint32_t) { return bnLeftDemo; }
bool alarmShowNow() {
  if (loggerMode) return false;
  if (bnStage) return true;
  for (int i = 1; i < AL_N; i++) if (al[i].on && !al[i].acked && P.alOut[i] != AO_OFF) return true;
  return false;
}
bool alarmCtl(uint32_t now) { bool busy = alarmShowNow(); ev("alarm:ctl"); if (busy) lastDrawMs = 0; return busy; }

// the Voyage screen: the demo has no active route on the VMR side (rtN 0: "No active route")
struct RtPt { float la, lo; char n[17]; };
RtPt rtPtsDemo[2] = {}; RtPt* rtPts = rtPtsDemo; uint16_t rtN = 0; char rtName[24] = "";
void drawVoyage();
