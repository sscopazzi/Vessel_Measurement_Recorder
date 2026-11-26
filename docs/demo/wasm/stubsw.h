// SPDX-FileCopyrightText: 2026 Aspect Wave LLC
// SPDX-License-Identifier: GPL-3.0-or-later
// VMR demo: more state the screens read
#define BATT_AVG_S 600
float battRateAvg = NAN; uint32_t battAvgSince = 0; uint8_t battSrc = 0;
uint8_t extMask = 0;
bool extOn(int i) { return extMask >> i & 1; }
bool havePres() { return extOn(EXT_BMP); } bool haveSht() { return extOn(EXT_SHT); }
bool wxOn() { return extOn(EXT_BMP) || extOn(EXT_SHT); }
bool loggerMode = false, wifiOffByBtn = false, infoOn = false, holdShow = false, scrComboUsed = false, logWanted = false;
uint8_t cfmAct = 255; uint32_t cfmMs = 0; int contrast = 128; uint32_t gpsRmcMs = 0;
bool gpsOff = false; bool slmArmed = false; bool ctdCastOpen = false; bool slmDisarming = false;
const char* slmHoldBig() { return slmArmed ? "DISARM" : "ARM CAST"; } const char* slmHoldDet() { return "WTB + IWB on"; }
uint32_t upBase = 0; int64_t esp_timer_get_time() { return (int64_t)(uint32_t)(fakeNow - upBase) * 1000LL; }
bool sdLock(uint32_t) { return true; } void sdUnlock() {}
void markPersistDirty(bool) {}
bool loggingActive() { return logging || logWanted; }
size_t strlcpy(char* d, const char* s, size_t n) { size_t l = strlen(s); if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; } return l; }
size_t strlcat(char* d, const char* s, size_t n) { size_t l = strlen(d); return l + strlcpy(d + l, s, n > l ? n - l : 0); }

float markDist(int32_t la1, int32_t lo1, int32_t la2, int32_t lo2) {
  const double k = M_PI / 180e7;
  double p1 = la1 * k, p2 = la2 * k, dp = (la2 - la1) * k, dl = ((double)lo2 - lo1) * k;
  double h = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  return (float)(2 * 6371008.8 * asin(sqrt(h)));
}

float markBrg(int32_t la1, int32_t lo1, int32_t la2, int32_t lo2) {
  const double k = M_PI / 180e7;
  double p1 = la1 * k, p2 = la2 * k, dl = ((double)lo2 - lo1) * k;
  double b = atan2(sin(dl) * cos(p2), cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl)) * 180.0 / M_PI;
  return (float)(b < 0 ? b + 360.0 : b);
}

float presTendSim = 1.2f; float presTendency() { return presTendSim; }
uint64_t clockMsAt(uint32_t ms) { return (uint64_t)T_UNIX * 1000ULL + (ms % 1000); }
float wvHsB = 2.63f, wvHsBMid = 2.20f, wvTzB = 9.2f, wvHsBc = NAN, wvHsF = NAN, wvHsFMid = NAN;
bool wvAnBOnly = false;                            // the pressure alone (the IMU off): the page sets it
struct WvGain { uint32_t magic, unix; float agg; float g[81]; };
WvGain wvGkDemo = {0, 0, 1.9f, {}}; WvGain* wvGk = &wvGkDemo;
bool darkNow() { return false; }                   // (a screens-off firmware update: not in the demo)
float battRateLast = NAN;

uint8_t cfmBy = 255;
uint32_t clinoMaxSu = 0, clinoMaxPu = 0;
bool slmCastLine(char*, size_t) { return false; }
#include "vmr_link.h"
BStatus slmSt = {}; BLive slmLv = {}; uint32_t slmLvMs = 0; bool slmLinkFresh() { return slmArmed; }
struct DlJob {
  volatile uint8_t st = 0;
  volatile bool cancel = false;
  bool zip = false;
  char name[48] = "";
  char path[96] = "";
  const char* dir = "/";
  void* e = nullptr;
  size_t count = 0;
  volatile uint32_t got = 0;
  uint32_t tot = 0, t0 = 0, t1 = 0, sdMs = 0, netMs = 0;
  uint16_t seq = 0;
  char err[48] = "";
};
DlJob dl;
struct DlUi { uint32_t ms = 0; bool nat = false; uint8_t st = 0; uint32_t got = 0, tot = 0, el = 0; uint16_t closedSeq = 0xFFFF; char name[40] = ""; };
DlUi dlUi;
void drawCtd();
