// VMR demo: the firmware's OLED drawing, buttons, LEDs, 7-segment and LCD code built for the browser (build_wasm.sh).
// The page writes the sensors' state and the button pins into it, calls vmr_tick() every 20 ms and reads back the screens.
#include "Adafruit_GFX.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <vector>
using std::min; using std::max;
uint64_t clockMsAt(uint32_t);
#define SH110X_WHITE 1
#define SH110X_BLACK 0
#define SH110X_INVERSE 2
#define SH110X_DISPLAYON 0xAF
#define SH110X_DISPLAYOFF 0xAE
class PageOLED: public Adafruit_GFX{ public: uint8_t buf[1024];
 PageOLED():Adafruit_GFX(64,128){memset(buf,0,1024);}
 void drawPixel(int16_t x,int16_t y,uint16_t c) override{ if(x<0||y<0||x>=width()||y>=height())return;   // rotation 1, or 3 (Flip)
  int16_t px=getRotation()==3?y:WIDTH-1-y, py=getRotation()==3?HEIGHT-1-x:x;
  uint8_t&b=buf[px+(py/8)*WIDTH]; if(c==1) b|=1<<(py&7); else if(c==0) b&=~(1<<(py&7)); else b^=1<<(py&7);}
 bool get(int16_t x,int16_t y){ int16_t px=WIDTH-1-y, py=x; return buf[px+(py/8)*WIDTH]>>(py&7)&1;}   // the panel as mounted
 void clearDisplay(){memset(buf,0,1024);} void display(){} uint8_t* getBuffer(){return buf;} void oled_command(uint8_t){} void setContrast(uint8_t){} };
PageOLED display;
#include "shimw.h"
#include "pre.inc"
#include "decl.inc"
#include "stubs2w.h"
#include "post.inc"
#include "stubsw.h"
#include <Fonts/TomThumb.h>
#include "FreeSansBold24pt7b.h"
#include "extra.inc"
#include "logo.inc"
#include "gluew.inc"
#include "fw.inc"
#include "draw.inc"
#include "ui.inc"
#include "bridge.inc"
