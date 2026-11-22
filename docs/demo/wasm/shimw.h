// VMR demo: Arduino and FreeRTOS stand-ins
#include <ctime>
#include <string>
#define MARK_NOTE_MAX 80
#include <vector>
#define DEG_TO_RAD 0.017453292519943295f
#define RAD_TO_DEG 57.29577951308232f
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x)
#define portEXIT_CRITICAL(x)
#ifndef FW_VERSION
#define FW_VERSION "?"
#endif
uint32_t fakeNow = 0;
unsigned long millis() { return fakeNow; }
void crumb(int) {}
struct { void println(const char*){} void printf(const char*,...){} } Log;
struct DateTime { struct tm t; uint32_t u;
  DateTime(uint32_t x) { u=x; time_t y = x; gmtime_r(&y, &t); }
  unsigned year() { return t.tm_year + 1900; } unsigned month() { return t.tm_mon + 1; } unsigned day() { return t.tm_mday; }
  unsigned dayOfTheWeek() { return t.tm_wday; } unsigned hour() { return t.tm_hour; } unsigned minute() { return t.tm_min; } unsigned second() { return t.tm_sec; } uint32_t unixtime(){return u;} };
struct Btn { bool raw=false, down=false, pressed=false, released=false; int8_t ext=0;
  void update(){ pressed = raw && !down; released = !raw && down; down = raw; }
  bool isDown() const {return down;} };
typedef Btn Button;
Btn btnA, btnB, btnC, btnScr;
uint32_t baseUnix = 0, baseMillis = 0; bool clockSynced = true;
