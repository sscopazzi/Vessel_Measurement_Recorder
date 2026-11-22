// VMR demo: the sensors' state, written by the page
#define MARK_NOTE_MAX_ 80

SkySat sky[SKY_MAX];
float yawDeg = 0, rotNow = 0;
uint32_t T_UNIX = 1793713200u;
bool gpsFake = false;
extern bool gpsOff; bool gpsFixValid() { return gps.gpsFix && !gpsOff; }
char clockSrc = 'G';
uint32_t utcNow(char& src) { src = clockSrc; return T_UNIX; }
bool imuOffFake = false; bool imuFresh() { return !imuOffFake; }
bool accFresh() { return true; }
float rotShown() { return rotNow; }
float trimFt = 0;
float trimMetres(float) { return trimFt * 0.3048f; }
uint16_t dockFill() { return dockAvgS(); }
uint32_t usedMask[8] = {0};
bool skyIsUsed(uint8_t prn) { return usedMask[prn >> 5] >> (prn & 31) & 1; }
Win win1 = {0, 0, 0, 0, 0, false};
Win window1m() { return win1; }
struct { int n = 0; int softAPgetStationNum() { return n; } } WiFi;
#define ESP_OK 0
struct wifi_sta_info_t { int rssi; };
struct wifi_sta_list_t { wifi_sta_info_t sta[4]; int num; };
int staFakeN = 0, staFakeRssi = -58;
int esp_wifi_ap_get_sta_list(wifi_sta_list_t* l) { l->num = staFakeN; for (int i = 0; i < staFakeN; i++) l->sta[i].rssi = staFakeRssi - 7 * i; return 0; }
struct SerLog { std::vector<std::string> v; void printf(const char*,...){} void println(const char*){} uint32_t seq() { return v.size(); }
  bool get(uint32_t k, char* out) { if (k >= v.size()) return false; strncpy(out, v[k].c_str(), SLOG_LEN); out[SLOG_LEN] = 0; return true; } } SLog;
#define Log SLog
void printAt(int x, int y, const char* s);
void printCentered(int y, const char* s);
void bootWait(uint32_t) {}

float wvHsMid = NAN, wvTzMid = NAN;
#define WIND_SEC_N 600
bool windOnFake = false;
bool windActive() { return windOnFake; }
bool windFresh() { return windOnFake; }
float windS1 = NAN, windA1 = NAN, windAwa = NAN, windAwsKn = NAN;
float windA2 = NAN, windS2 = NAN, windG2 = NAN, windG10 = NAN;
bool windMcOn = true, windLocked = true;
extern float sogAvgKn, cogAvgDeg; bool gpsFixValid();
void windTrue(float awa, float aws, float& trel, float& tdir, float& tkn) {
  trel = tdir = tkn = NAN;
  if (isnan(awa) || isnan(aws) || !gpsFixValid() || isnan(sogAvgKn)) return;
  float a = awa * 0.0174532925f, v = sogAvgKn;
  float x = aws * cosf(a) - v, y = aws * sinf(a);
  tkn = sqrtf(x * x + y * y);
  trel = atan2f(y, x) * 57.2957795f; if (trel < 0) trel += 360;
  float h = isnan(cogAvgDeg) ? 0 : cogAvgDeg;
  tdir = fmodf(h + trel, 360.0f);
}
void windMeans(int n, float& a, float& s, float& g) { a = windA2; s = windS2; g = (n > 200) ? windG10 : windG2; }
#define WV_KSEND 3
#define ps_malloc malloc
