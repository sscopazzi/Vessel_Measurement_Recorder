// VMR demo: declarations the drawing code needs (as VMR.ino)
#define LOG_TAG_MAX 23
#define EXT_N 5
#define LOAD_COND_N 7
#define SLOG_LEN    120
#define SLOG_LINES  30
#define SKY_STALE_MS 15000UL
#define SKY_MAX      40
#define MARK_OLED_N 8
#define LOG_INTERVAL_MS          100UL
#define KNOTS_TO_MS 0.514444f
#define G_TRAIL 12
#define GRAPH_SAMPLE_MS          500UL
#define GRAPH_ENVELOPE_S         300
#define GPS_RATE_HZ                1
#define FW_MARK_HEAD 8
#define GRAPH_POINTS            1200
#define GRAPH_LENGTHS 5
#define LOG_RATE_N                 7
#define MARK_KEY       0xA5
#define M_TO_FT     3.28084f
#define GPS_FIX_TIMEOUT_MS      3000UL
#define LOGGER_PEEK_MS         10000UL
#define COG_AVG_MIN_KN           0.3f
#define IMU_SILENT_MS         5000UL
#define DISPLAY_INTERVAL_MS      200UL
#define SPLASH_MS               2000UL
#define BOOT_STEP_MS             500UL
#define BOOTLIST_MS             2000UL
#define WV_DT_MS   500
#define WV_N      2048
#define WV_MIN    1024
#define WV_SEG     256
#define WV_K0        4
#define WV_K1       80
#define DEV_N 4
#define RS_CYCLES   1024
struct Persist {
  uint32_t magic;
  int8_t   graphMin;
  int8_t   graphMax;
  uint8_t  zeroApplied;
  uint8_t  logActive;
  int8_t   zdHours;
  uint8_t  graphSecIdx;
  uint8_t  sogAvgS;
  uint8_t  rotDeadband;
  float    rollOffset, pitchOffset;
  float    rollMaxPos, rollMaxNeg, pitchMaxPos, pitchMaxNeg, yawRateMax;
  uint32_t tRollMaxPos, tRollMaxNeg, tPitchMaxPos, tPitchMaxNeg, tYawRateMax;
  uint8_t  oledFlip;
  int8_t   pitchGraphMin;
  int8_t   pitchGraphMax;
  uint8_t  dockAvg10;
  char     logTag[LOG_TAG_MAX + 1];
  uint16_t shipBeamCm;
  uint16_t shipDraftCm;
  uint16_t shipLengthDm;
  uint8_t  logCond;
  uint8_t  rollThrDeg;

  uint16_t shipLbpDm;
  uint16_t calFwdIn, calMidIn, calAftIn;
  uint8_t  trimCal;
  uint8_t  atDock;
  float    trimPitchOffset;
  uint32_t trimCalUnix;

  uint16_t draftFwdCm, draftAftCm;
  uint16_t keelDeckCm, keelBridgeCm, keelTopCm;
  uint16_t parWlDm;

  uint16_t draftMidCm;

  uint8_t  extEn;
  uint8_t  extAddr[EXT_N];
  uint8_t  scrSrc;
  uint8_t  potSrc;

  uint8_t  potSwSrc;

  uint16_t cbX1000;
  uint16_t cwpX1000;
  uint16_t tpiX10;
  uint16_t dispX10;

  uint8_t  markSrc;
  uint8_t  markKey;

  uint8_t  briWeb;
  uint8_t  briLevel;

  uint8_t  logRate;
  uint8_t  imuOff;

  uint16_t gmLcFt100;

  uint8_t  posFmt;
  uint8_t  ballRot;

  int16_t  vmrXdm;
  int16_t  vmrYcm;
  uint16_t vmrZcm;

  uint8_t  ledPin[3];
  uint8_t  pad18;

  uint8_t  windRate;
  uint8_t  pad19;
  int16_t  windOff10;
  uint16_t windZcm;

  int16_t  windYcm;

  int16_t  zeroRef100;

  int16_t  magVar10;

  uint8_t  mountX;
  uint8_t  mountZ;
  uint8_t  axFlip;
  uint8_t  clinoMax;
  uint8_t  dispEn;
  uint8_t  segAddr;
  uint8_t  lcdAddr;
  uint8_t  segScreen;
  uint8_t  lcdScreen;
  uint8_t  lcdColor;
  uint8_t  lcdRgb[3];
  uint8_t  lcdCycle;

  uint8_t  lcdMix[3];
  uint8_t  wxSpan;

  Voyage   vs;

  uint16_t ballBest10;

  uint8_t  segFlip;
  uint8_t  units;
  uint32_t scrRot;

  uint16_t segRot;
  uint16_t lcdRot;

  uint16_t battAh10;
  uint16_t battDrawMa;

  uint8_t  slmOn;
  uint8_t  wtbMac[6];
  uint8_t  slmFlags;
  uint8_t  battType;
  uint8_t  battChem;
  uint8_t  ethOn;
  uint8_t  ethCsPin, ethIntPin;
  uint8_t  flags31;

  uint8_t  xbSrc[5];
  uint8_t  xbScreen;
  uint16_t crc;
};
struct GpsData {
  float    gpsLat;
  float    gpsLon;
  float    gpsAlt;
  float    gpsSpeed;
  float    gpsAngle;
  bool     gpsFix;
  uint8_t  gpsSats;

  float    gpsHdop;
  uint8_t  gpsFixQ;
  uint32_t gpsLastFixMs;
  int32_t  latE7, lonE7;
};
struct LocMark {
  uint32_t unix, endUnix, startMs;
  int8_t   zd;
  char     kind;
  bool     fix, endFix;
  uint8_t  sats;
  int32_t  latE7, lonE7, endLatE7, endLonE7;
  float    cog, sog, pa, ta, rh, hdop, fixAge, durS, driftM;
  char     note[MARK_NOTE_MAX + 1];
};
struct Win { float rMax, rMin, pMax, pMin, yrMax; bool valid; };
struct SkySat { uint8_t prn, el; uint16_t az; uint8_t snr; uint32_t ms; };
struct DevSeen { uint8_t mac[6]; uint32_t ip, onUnix, offUnix, onMs, offMs; bool on; char name[24]; char kind[16]; };
struct RollStats { uint16_t n; float sig, maxAmp, mean, rms; uint16_t overS, overP; float tMed; };
enum { SCREEN_LIVE = 0, SCREEN_GRAPH, SCREEN_POSITION, SCREEN_ALLTIME, SCREEN_GPS,
       SCREEN_CLINO, SCREEN_MARKS, SCREEN_SKY, SCREEN_DOCK, SCREEN_GMETER, SCREEN_WAVES, SCREEN_WAVESTAT, SCREEN_DEVICES, SCREEN_WIND, SCREEN_WX, SCREEN_WXGRAPH, SCREEN_BALL, SCREEN_CAST, SCREEN_REC, SCREEN_HEALTH, SCREEN_CTD, SCREEN_ROLL, SCREEN_DL, NUM_SCREENS };
enum { OTA_IDLE, OTA_RX, OTA_OK, OTA_FAIL };
enum {
  ST_NONE = 0, ST_GPS, ST_IMU_READ, ST_IMU_RESET, ST_IMU_BEGIN, ST_RTC, ST_BINS, ST_BUTTONS,
  ST_POT, ST_WEBREQ, ST_BATT, ST_GRAPH, ST_LOG, ST_PERSIST, ST_DRAW, ST_OLED_SEND, ST_OLED_RETRY,
  ST_IDLE, ST_BOOT, ST_EXT, ST_DISP2, ST_BEAT, ST_WAVES, ST_WIND, ST_COUNT
};
struct MarkOled { uint16_t n; char kind; bool fix; char hm[6]; char lat[16]; char lon[16]; char info[36]; };
Persist P;
float roll = 0, pitch = 0;
GpsData gps = {NAN, NAN, NAN, NAN, NAN, false, 0, NAN, 0, 0, 0, 0};
MarkOled* markOled = nullptr;
volatile bool logging = false;
float *gRoll = nullptr, *gPitch = nullptr;
uint8_t screen = SCREEN_HEALTH;
float    rollPeriod = NAN;
volatile uint8_t  otaState = OTA_IDLE;
char     markOledHdr[24];
uint8_t  markOledCnt = 0;
bool     imuBootFail = false;
uint8_t  gpsInView[2] = {0, 0};
uint8_t  gpsBestSnr = 0;
uint8_t  gTrailN = 0, gTrailHead = 0;
uint16_t gHead = 0, gCount = 0;
bool displayOK = false, rtcOK = false, rtcValid = false, imuOK = false;
float sogAvgKn = NAN;
volatile bool serOledOn = false;
uint32_t pausedAtMs = 0;
bool     paused = false;
char otaNewVer[16] = "";
char     otaErr[64] = "";
volatile uint32_t otaDone = 0, otaTotal = 0, otaEndMs = 0;
bool     oledForced = false;
uint32_t msgUntil = 0;
char msgText[24] = "";
std::vector<LocMark> marks;
uint64_t logSlot = UINT64_MAX;
int8_t   gTrailX[G_TRAIL], gTrailY[G_TRAIL];
float    gPkF = 0, gPkA = 0, gPkS = 0, gPkP = 0, gPkHi = 1, gPkLo = 1;
float    gFeltF = 0, gFeltS = 0, gVert = 1;
float    dockRawPitch = NAN, dockRoll = NAN;
bool displayOn = true;
float cpuTempC = NAN;
float cogAvgDeg = NAN;
float clinoMaxS = 0, clinoMaxP = 0;
bool bootAnyFail = false;
float battV = NAN, battPct = NAN, battRate = NAN;
bool battOK = false, sdOK = false, eepromOK = false;
static const char* MONTHS[] = {"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"};
const char FW_MARK[] = "\x02VMRVER " FW_VERSION "\x03";
const uint32_t LOG_RATE_MS[LOG_RATE_N] = {100, 1000, 5000, 20000, 60000, 300000, 600000};
static const uint16_t GRAPH_SECS[GRAPH_LENGTHS] = {30, 60, 120, 300, 600};
float    rpMean = NAN, rpM2 = 0, rpPrevX = 0, rpMaxSince = 0, rpLp1 = 0, rpLp2 = 0;
bool peekOn = false;
uint32_t peekUntilMs = 0, peekDrawMs = 0;
uint16_t wvHead = 0, wvN = 0;
float    wvTe = NAN, wvTz = NAN, wvTrs = NAN, wvHs = NAN;
float  (*wvSpec)[WV_K1 + 1] = nullptr;
bool     wvCurrent = false;
uint32_t wvLastMs = 0;
DevSeen* devs = nullptr;
uint8_t devCount = 0;
portMUX_TYPE devMux = portMUX_INITIALIZER_UNLOCKED;
RollStats rStat[2];
uint32_t wvMs = 0, wvCalcMs = 0;
void zdText(char* out, size_t n) {
  if (P.zdHours) snprintf(out, n, "%+d", (int)P.zdHours);
  else           snprintf(out, n, "0");
}
uint32_t ltOf(uint32_t utc) { return utc - (int32_t)P.zdHours * 3600; }
uint16_t graphSecs() { return GRAPH_SECS[P.graphSecIdx < GRAPH_LENGTHS ? P.graphSecIdx : 1]; }
void fmtDM(char* out, size_t n, float v, char pos, char neg, bool isLon) {
  char hemi = v < 0 ? neg : pos;
  v = fabsf(v);
  int d = (int)v;
  float mnt = (v - d) * 60.0f;
  if (mnt >= 59.9995f) { d++; mnt = 0; }
  if (isLon) snprintf(out, n, "%3d%c%06.3f'%c", d, (char)248, mnt, hemi);
  else       snprintf(out, n, "%2d%c%06.3f'%c", d, (char)248, mnt, hemi);
}
uint8_t markSrcNow() { uint8_t v = P.markSrc & 15; return P.markKey == MARK_KEY && v >= 1 && v <= 8 ? v : 0; }
void markDM(char* out, size_t n, int32_t e7, char pos, char neg) {
  uint32_t a = e7 < 0 ? (uint32_t)(-(int64_t)e7) : (uint32_t)e7;
  uint32_t d = a / 10000000UL, mm = (uint32_t)(((uint64_t)(a % 10000000UL) * 60 + 500) / 1000);
  if (mm >= 600000) { d++; mm -= 600000; }
  snprintf(out, n, "%lu %02lu.%04lu %c", (unsigned long)d, (unsigned long)(mm / 10000), (unsigned long)(mm % 10000), e7 < 0 ? neg : pos);
}
uint32_t logMs() { return P.imuOff && P.logRate < LOG_RATE_N ? LOG_RATE_MS[P.logRate] : LOG_INTERVAL_MS; }
uint16_t dockAvgS() { return P.dockAvg10 ? P.dockAvg10 * 10 : 60; }
uint64_t logClockMs() { return clockSynced ? clockMsAt(millis()) : (uint64_t)millis(); }
void showMessage(const char* t, uint32_t ms = 1500) {
  strncpy(msgText, t, sizeof(msgText) - 1);
  msgText[sizeof(msgText) - 1] = 0;
  msgUntil = millis() + ms;
}
