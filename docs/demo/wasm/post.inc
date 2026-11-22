// VMR demo: more declarations (as VMR.ino)
constexpr float R2D = 57.2957795f, D2R = 0.0174532925f;
#define BALL_G        600.0f
#define BALL_R        3
#define BALL_RING     7
#define BALL_TRAIL    8
#define WX_FINE_S    2
#define WX_FINE_N    600
#define WX_COARSE_S  30
#define WX_COARSE_N  360
struct WxPt { float p, t, h; };
WxPt    *wxFine = nullptr, *wxCoarse = nullptr;
struct CastOled { uint16_t n; bool open, fix; uint32_t unix, startMs; int32_t latE7, lonE7; float durS, driftM; char hm[6]; };
CastOled castOled = {};
float    ballX = 31.5f, ballY = 31.5f, ballVx = 0, ballVy = 0;
uint32_t ballMs = 0, ballInMs = 0;
bool     ballArmed = false;
int8_t   ballTx[BALL_TRAIL], ballTy[BALL_TRAIL];
uint8_t  ballTrHead = 0, ballTrN = 0;
char     faultWhy[2][96] = {"", ""};
volatile uint8_t faultWhyIdx = 0;
enum { EXT_BMP = 0, EXT_SHT, EXT_PCF, EXT_ADC1, EXT_ADC2 };
#define DISP_OLED 1
#define DISP_SEG  2
#define DISP_DARK(b) ((b) << 4)
enum { SEG_ROLL = 0, SEG_PITCH, SEG_ROT, SEG_SOG, SEG_COG, SEG_TIME, SEG_BATT, SEG_PRES, SEG_TEMP, SEG_RH,
       SEG_WIND, SEG_RP, SEG_GM, SEG_CD, SEG_HS, SEG_N };
enum { BE_NONE = 0, BE_CHG, BE_FULL, BE_BATT, BE_UNKNOWN };
#define CONFIRM_MS 5000UL
enum { HG_RESTART = 0, HG_WIFI, HG_INFO, HG_BALL, HG_FLIP, HG_CD, HG_PAUSE, HG_REC, HG_OLEDOFF, HG_CANCEL, HG_CAST, HG_GPS, HG_N };
static const uint8_t HG_BASE[11] = {HG_RESTART, HG_WIFI, HG_INFO, HG_BALL, HG_FLIP, HG_CD, HG_PAUSE, HG_REC, HG_OLEDOFF, HG_GPS, HG_CANCEL};
#define BATT_FULL_V 4.15f
enum { BS_UNKNOWN = 0, BS_USB, BS_BATT };
static const char* const SEG_NAME[SEG_N] = {"roll", "pitch", "rate of turn", "SOG", "COG", "time (LT)", "battery",
                                           "pressure", "air temp", "humidity", "wind", "roll period", "GM", "countdown", "heave Hs"};
char    apSsid[33] = "Vessel Measurement Recorder";
static const char* AP_PASS = "somestupidcode";
float   extPa = NAN, extTb = NAN, extTa = NAN, extRh = NAN;
uint16_t wxFineHead = 0, wxFineN = 0, wxCoarseHead = 0, wxCoarseN = 0;
char logName[64] = "";
uint32_t sdChkFails = 0;
uint32_t sdRewrites = 0, sdRewroteB = 0, ckNoteB = 0;
uint32_t pendDropped = 0;
uint32_t logStartMs = 0, logRows = 0;
uint64_t logBytes = 0;
uint32_t rowGaps = 0, rowGapMax = 0, rowGapUnix = 0, rowPrevMs = 0;
#define unitC() ((P.units & 1) != 0)
#define unitM() ((P.units & 2) != 0)
#define tShow(c) (unitC() ? (float)(c) : (float)(c) * 1.8f + 32)
#define tUnit() (unitC() ? 'C' : 'F')
#define lShow(m) (unitM() ? (float)(m) : (float)(m) * M_TO_FT)
#define lUnit() (unitM() ? "m" : "ft")
