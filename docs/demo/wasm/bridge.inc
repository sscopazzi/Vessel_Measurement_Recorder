// ---- demo bridge: the functions the page calls (wasm exports)
#define WX extern "C" __attribute__((visibility("default")))
#include <splash.h>
uint8_t fb[128 * 64];                 // the OLED as it looks: 1 = lit, row by row (128 x 64)
char scratch[512];                    // strings from JS
char lcdOut[LCD_H][LCD_W + 1];
uint8_t segOut[8];                    // digit, digit, colon, digit, digit, brightness 0-15, lit
std::string varTab;

#define V(x, t) do { char b[96]; snprintf(b, sizeof b, "%s %c %lu\n", #x, t, (unsigned long)(uintptr_t)&(x)); varTab += b; } while (0)
WX const char* vmr_vars() {
  if (!varTab.empty()) return varTab.c_str();
  // f float, d double, b bool/uint8, c int8, h int16, H uint16, i int32, I uint32, Q uint64
  V(fakeNow, 'I'); V(upBase, 'I'); V(T_UNIX, 'I'); V(clockSrc, 'c');
  V(roll, 'f'); V(pitch, 'f'); V(rollPeriod, 'f'); V(yawDeg, 'f'); V(rotNow, 'f'); V(magHdg, 'f'); V(trimFt, 'f');
  V(gps.gpsLat, 'f'); V(gps.gpsLon, 'f'); V(gps.gpsAlt, 'f'); V(gps.gpsSpeed, 'f'); V(gps.gpsAngle, 'f'); V(gps.gpsFix, 'b');
  V(gps.gpsSats, 'b'); V(gps.gpsHdop, 'f'); V(gps.gpsFixQ, 'b'); V(gps.gpsLastFixMs, 'I'); V(gps.latE7, 'i'); V(gps.lonE7, 'i');
  V(gpsInView[0], 'b'); V(gpsInView[1], 'b'); V(gpsBestSnr, 'b'); V(gpsRmcMs, 'I'); V(gpsOff, 'b');
  V(sogAvgKn, 'f'); V(cogAvgDeg, 'f');
  V(win1.rMax, 'f'); V(win1.rMin, 'f'); V(win1.pMax, 'f'); V(win1.pMin, 'f'); V(win1.yrMax, 'f'); V(win1.valid, 'b');
  V(clinoMaxS, 'f'); V(clinoMaxP, 'f'); V(clinoMaxSu, 'I'); V(clinoMaxPu, 'I');
  V(dockRawPitch, 'f'); V(dockRoll, 'f');
  V(gFeltF, 'f'); V(gFeltS, 'f'); V(gVert, 'f'); V(gPkF, 'f'); V(gPkA, 'f'); V(gPkS, 'f'); V(gPkP, 'f'); V(gPkHi, 'f'); V(gPkLo, 'f');
  V(wvTe, 'f'); V(wvTz, 'f'); V(wvTrs, 'f'); V(wvHs, 'f'); V(wvHsMid, 'f'); V(wvTzMid, 'f'); V(wvHsB, 'f'); V(wvHsBMid, 'f');
  V(wvN, 'H'); V(wvCurrent, 'b'); V(wvLastMs, 'I'); V(wvCalcMs, 'I'); V(WS, 'f');
  V(rStat[0].n, 'H'); V(rStat[0].tMed, 'f'); V(rStat[0].sig, 'f'); V(rStat[0].maxAmp, 'f'); V(rStat[0].mean, 'f'); V(rStat[0].rms, 'f');
  V(rStat[0].overS, 'H'); V(rStat[0].overP, 'H');
  V(gmC, 'f'); V(gmEst, 'f');
  V(extMask, 'b'); V(extPa, 'f'); V(extTb, 'f'); V(extTa, 'f'); V(extRh, 'f'); V(presTendSim, 'f');
  V(windOnFake, 'b'); V(windS1, 'f'); V(windA1, 'f'); V(windAwa, 'f'); V(windAwsKn, 'f'); V(windA2, 'f'); V(windS2, 'f'); V(windG2, 'f'); V(windG10, 'f');
  V(windMcOn, 'b'); V(windLocked, 'b');
  V(battOK, 'b'); V(battV, 'f'); V(battPct, 'f'); V(battRate, 'f'); V(battRateAvg, 'f'); V(battAvgSince, 'I'); V(battSrc, 'b'); V(battSlopeOk, 'b');
  V(cpuTempC, 'f');
  V(logging, 'b'); V(logWanted, 'b'); V(paused, 'b'); V(pausedAtMs, 'I'); V(logStartMs, 'I'); V(logRows, 'I'); V(logBytes, 'Q');
  V(rowGaps, 'I'); V(rowGapMax, 'I'); V(pendDropped, 'I'); V(logSlot, 'Q'); V(baseUnix, 'I'); V(baseMillis, 'I'); V(bufSecs, 'I');
  V(sdOK, 'b'); V(imuOK, 'b'); V(imuOffFake, 'b'); V(imuBootFail, 'b'); V(displayOK, 'b'); V(displayOn, 'b'); V(oledBlank, 'b');
  V(loggerMode, 'b'); V(peekOn, 'b'); V(peekUntilMs, 'I'); V(wifiOffByBtn, 'b'); V(infoOn, 'b'); V(holdShow, 'b'); V(cfmAct, 'b'); V(cfmBy, 'b');
  V(contrast, 'i'); V(screen, 'b'); V(msgUntil, 'I'); V(serOledOn, 'b'); V(otaState, 'b');
  V(staFakeN, 'i'); V(staFakeRssi, 'i'); V(WiFi.n, 'i');
  V(knobRaw, 'i'); V(knobSw, 'b'); V(markRaw, 'b');
  V(btnB.raw, 'b'); V(btnScr.raw, 'b');
  V(xbBtn[0].raw, 'b'); V(xbBtn[1].raw, 'b'); V(xbBtn[2].raw, 'b'); V(xbBtn[3].raw, 'b'); V(xbBtn[4].raw, 'b');
  V(reqMarkBtn, 'b'); V(markToastN, 'b'); V(markToastKind, 'c'); V(markToastBottle, 'b'); V(castOpenNow, 'b');
  V(ledOut[0], 'i'); V(ledOut[1], 'i'); V(ledOut[2], 'i'); V(faultLed, 'b'); V(ledHeartMs, 'I'); V(sdDownMs, 'I');
  V(cdSet, 'b'); V(cdEndMs, 'I'); V(cdTotalS, 'H'); V(reqSegCd, 'h');
  V(slmArmed, 'b'); V(slmDisarming, 'b'); V(ctdCastOpen, 'b'); V(slmLvMs, 'I'); V(slmBtMs, 'I');
  V(slmSt.state, 'b'); V(slmSt.iwbAlive, 'b'); V(slmSt.eth, 'b'); V(slmSt.castNum, 'H'); V(slmSt.castOpen, 'b'); V(slmSt.iwbV, 'f');
  V(slmLv.depth, 'f'); V(slmLv.t, 'f'); V(slmLv.sp, 'f'); V(slmLv.rho, 'f');
  V(slmBt.on, 'b'); V(slmBt.ready, 'b'); V(slmBt.pend, 'b'); V(slmBt.readyInS, 'f');
  for (int i = 0; i < NK_N; i++) { char b[96]; snprintf(b, sizeof b, "slmBt.st%d b %lu\nslmBt.planM%d f %lu\n", i, (unsigned long)(uintptr_t)&slmBt.st[i], i, (unsigned long)(uintptr_t)&slmBt.planM[i]); varTab += b; }
  V(dl.st, 'b'); V(dl.got, 'I'); V(dl.tot, 'I'); V(dl.t0, 'I'); V(dl.t1, 'I'); V(dl.seq, 'H'); V(dlUi.closedSeq, 'H');
  V(ballBefore, 'b');
  V(P.zdHours, 'c'); V(P.graphMin, 'c'); V(P.graphMax, 'c'); V(P.graphSecIdx, 'b'); V(P.trimCal, 'b'); V(P.dockAvg10, 'b'); V(P.atDock, 'b');
  V(P.rollMaxPos, 'f'); V(P.rollMaxNeg, 'f'); V(P.pitchMaxPos, 'f'); V(P.pitchMaxNeg, 'f'); V(P.yawRateMax, 'f');
  V(P.tRollMaxPos, 'I'); V(P.tRollMaxNeg, 'I'); V(P.tPitchMaxPos, 'I'); V(P.tPitchMaxNeg, 'I'); V(P.tYawRateMax, 'I');
  V(P.units, 'b'); V(P.oledFlip, 'b'); V(P.segFlip, 'b'); V(P.dispEn, 'b'); V(P.segScreen, 'b'); V(P.lcdScreen, 'b'); V(P.lcdCycle, 'b'); V(P.lcdColor, 'b');
  V(P.scrRot, 'I'); V(P.segRot, 'H'); V(P.lcdRot, 'H'); V(P.imuOff, 'b'); V(P.logRate, 'b'); V(P.slmOn, 'b'); V(P.flags31, 'b'); V(P.battType, 'b');
  V(P.battAh10, 'H'); V(P.battDrawMa, 'H'); V(P.xbScreen, 'b'); V(P.ballBest10, 'H'); V(P.ballRot, 'b'); V(P.clinoMax, 'b'); V(P.posFmt, 'b');
  V(P.wxSpan, 'b'); V(P.briWeb, 'b'); V(P.potSwSrc, 'b'); V(P.shipBeamCm, 'H'); V(P.shipDraftCm, 'H'); V(P.keelBridgeCm, 'H');
  V(P.zeroApplied, 'b'); V(P.rollOffset, 'f'); V(P.pitchOffset, 'f');
  V(faultNow, 's'); V(logName, 's'); V(apSsid, 's'); V(scratch, 's');
  return varTab.c_str();
}

static const uint8_t XB_PRESENT = 0x1F;
WX void vmr_init() {
  display.setRotation(1); display.cp437(true); display.setTextWrap(false);
  displayOK = true; displayOn = true; imuOK = true; sdOK = true; battOK = true; eepromOK = true;
  memset(&P, 0, sizeof P);
  P.zdHours = 8; P.graphMin = -10; P.graphMax = 10; P.graphSecIdx = 1; P.trimCal = 0; P.dockAvg10 = 6; P.clinoMax = 0;
  P.markSrc = 1; P.markKey = MARK_KEY;
  P.dispEn = DISP_OLED | DISP_SEG | DISP_LCD;
  // the firmware's default rotation: all screens but All-time, Sky, Dock and Tilt ball (picked on the dashboard)
  P.scrRot = ((1UL << NUM_SCREENS) - 1) & ~((1UL << SCREEN_ALLTIME) | (1UL << SCREEN_SKY) | (1UL << SCREEN_DOCK) | (1UL << SCREEN_BALL));
  P.segRot = 0x7FFF & ~(1 << SEG_CD); P.lcdRot = (1 << LCD_N) - 1; P.lcdScreen = LCD_MOTION;
  P.battAh10 = 25; P.potSwSrc = SW_FEATHER; P.xbScreen = SCREEN_WAVES;
  P.slmOn = 1; P.flags31 = 1 | 2 | 4;            // Niskin bottles on
  extMask = (1 << EXT_BMP) | (1 << EXT_SHT) | (1 << EXT_PCF) | (1 << EXT_ADC1);
  gRoll = new float[GRAPH_POINTS]; gPitch = new float[GRAPH_POINTS]; gHead = 0; gCount = 0;
  markOled = new MarkOled[MARK_OLED_N]; memset(markOled, 0, sizeof(MarkOled) * MARK_OLED_N);
  wxFine = new WxPt[WX_FINE_N]; wxCoarse = new WxPt[WX_COARSE_N]; wxFineN = wxCoarseN = wxFineHead = wxCoarseHead = 0;
  devs = new DevSeen[DEV_N]; memset(devs, 0, sizeof(DevSeen) * DEV_N); devCount = 0;
  memset(WS, 0, sizeof WS); wvSpec = WS;
  strcpy(apSsid, "Vessel Measurement Recorder C4B0");
  screen = SCREEN_LIVE;
  for (auto& b : xbBtn) b.ext = 1;
  markOledBuild();
  slmBt.pend = 0xFF;
  for (int i = 0; i < NK_N; i++) slmBt.planM[i] = NAN;
  slmLv.depth = slmLv.t = slmLv.sp = slmLv.rho = NAN; slmSt.iwbV = NAN;
}
WX char* vmr_scratch() { return scratch; }
WX const char* vmr_events() { static char out[512]; strcpy(out, evq); evq[0] = 0; return out; }

WX void vmr_graph_push(float r, float p) { gRoll[gHead] = r; gPitch[gHead] = p; gHead = (gHead + 1) % GRAPH_POINTS; if (gCount < GRAPH_POINTS) gCount++; }
WX void vmr_graph_clear() { gHead = gCount = 0; }
WX void vmr_wx_push(int coarse, float p, float t, float h) {
  if (coarse) { wxCoarse[wxCoarseHead] = {p, t, h}; wxCoarseHead = (wxCoarseHead + 1) % WX_COARSE_N; if (wxCoarseN < WX_COARSE_N) wxCoarseN++; }
  else        { wxFine[wxFineHead] = {p, t, h};     wxFineHead = (wxFineHead + 1) % WX_FINE_N;       if (wxFineN < WX_FINE_N) wxFineN++; }
}
WX void vmr_wx_clear() { wxFineN = wxCoarseN = wxFineHead = wxCoarseHead = 0; }
WX void vmr_sky(int n) { for (int i = 0; i < SKY_MAX; i++) if (i >= n) sky[i].prn = 0; memset(usedMask, 0, sizeof usedMask); }
WX void vmr_sky_set(int i, int prn, int el, int az, int snr, int used) {
  if (i < 0 || i >= SKY_MAX) return;
  sky[i] = {(uint8_t)prn, (uint8_t)el, (uint16_t)az, (uint8_t)snr, fakeNow};
  if (used) usedMask[prn >> 5] |= 1u << (prn & 31);
}
// marks: the page keeps the list; it is copied here whole after each change (scratch = the note)
WX void vmr_marks_clear() { marks.clear(); }
WX void vmr_mark_add(int kind, uint32_t unix, int fix, int32_t la, int32_t lo, float dur, float drift, uint32_t startMs) {
  LocMark m; memset(&m, 0, sizeof m); m.kind = (char)kind; m.unix = unix; m.zd = P.zdHours; m.fix = fix; m.latE7 = la; m.lonE7 = lo;
  m.durS = dur; m.driftM = drift; m.startMs = startMs;
  m.cog = m.sog = m.pa = m.ta = m.rh = m.hdop = m.fixAge = NAN;
  marks.push_back(m);
}
WX void vmr_marks_done() {
  markOledBuild();
  bool open = false; for (auto& m : marks) if (m.kind == 'O') open = true;
  castOpenNow = open;
}
WX void vmr_dev_set(int i, int on, uint32_t onU, uint32_t offU, int m0, int m1, int m2) {   // scratch: "name|kind"
  if (i < 0 || i >= DEV_N) return;
  DevSeen& d = devs[i]; memset(&d, 0, sizeof d);
  uint8_t mac[6] = {(uint8_t)m0, (uint8_t)m1, (uint8_t)m2, (uint8_t)m0, (uint8_t)m1, (uint8_t)m2}; memcpy(d.mac, mac, 6);
  const char* bar = strchr(scratch, '|');
  size_t n = bar ? (size_t)(bar - scratch) : strlen(scratch);
  if (n > 23) n = 23; memcpy(d.name, scratch, n); d.name[n] = 0;
  if (bar) strlcpy(d.kind, bar + 1, sizeof d.kind);
  d.on = on; d.onUnix = onU; d.offUnix = offU;
  if (i + 1 > devCount) devCount = i + 1;
}
WX void vmr_devs(int n) { devCount = n; }
WX void vmr_slog_add() { SLog.v.push_back(scratch); if (SLog.v.size() > SLOG_LINES) SLog.v.erase(SLog.v.begin()); }
WX void vmr_msg(uint32_t ms) { showMessage(scratch, ms); }
WX void vmr_dl_name() { strlcpy(dl.name, scratch, sizeof dl.name); }
WX void vmr_reset_max() { P.rollMaxPos = P.rollMaxNeg = P.pitchMaxPos = P.pitchMaxNeg = P.yawRateMax = 0; P.tRollMaxPos = P.tRollMaxNeg = P.tPitchMaxPos = P.tPitchMaxNeg = P.tYawRateMax = 0; }

// one pass of the loop's input, screen and LED services (every 20 ms of simulated time)
WX void vmr_tick() {
  uint32_t now = millis();
  serviceButtons();
  serviceXb(now);
  serviceMarkBtn(now);
  servicePot();
  int16_t cd = reqSegCd;
  if (cd > 0) { reqSegCd = -1; cdTotalS = cd; cdEndMs = now + (uint32_t)cd * 1000UL; cdSet = true; P.segScreen = SEG_CD; segRamValid = false; }
  serviceCastScreen();
  serviceDlScreen();
  serviceScreenAvail();
  serviceBall(now);
  serviceLeds(now);
  if (peekOn && (int32_t)(millis() - peekUntilMs) > 0) peekOn = false;
}
// the OLED: 1 = drawn (fb filled), 0 = dark
WX int vmr_draw() {
  bool lit;
  display.setRotation(oledRotation());            // (the dashboard's Flip sets P.oledFlip only)
  if (loggerMode) {
    lit = peekOn;
    if (lit) drawLoggerPeek();
  } else {
    oledForced = paused || otaState != OTA_IDLE || serOledOn || imuWarnOn();   // as serviceOledForce
    lit = displayOK && (dispLit(DISP_OLED) || oledForced);
    if (lit) drawScreen();
  }
  if (!lit) return 0;
  for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) fb[y * 128 + x] = display.get(x, y);
  return 1;
}
WX uint8_t* vmr_fb() { return fb; }
WX int vmr_contrast() { return contrast; }
WX void vmr_boot(int step) {                   // the boot sequence as the firmware shows it (step 0-2)
  display.setRotation(oledRotation()); display.clearDisplay();
  if (step == 0) display.drawBitmap((128 - splash2_width) / 2, (64 - splash2_height) / 2, splash2_data, splash2_width, splash2_height, 1);
  else if (step == 1) display.drawBitmap((128 - AWLOGO_W) / 2, (64 - AWLOGO_H) / 2, awLogo, AWLOGO_W, AWLOGO_H, 1);
  for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) fb[y * 128 + x] = display.get(x, y);
}
WX void vmr_boot_list(int n) {                 // the boot list, its first n lines
  static const char* const L[][2] = {{"EEPROM", "OK"}, {"Battery", "OK"}, {"RTC", "OK"}, {"GPS", "OK"}, {"IMU", "OK"}, {"SD", "OK"},
                                     {"BMP388", "OK"}, {"SHT30", "OK"}, {"PCF8574", "OK"}, {"ADS7830", "OK"}, {"WiFi AP", "OK"}};
  display.setRotation(oledRotation()); bootN = 0;
  for (int i = 0; i < n && i < 11; i++) { bootText(L[i][0], L[i][1], false); }
  if (!n) bootDraw();
  for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) fb[y * 128 + x] = display.get(x, y);
}
// the 7-segment: segOut = its RAM (digit, digit, colon, digit, digit), brightness, lit
WX uint8_t* vmr_seg() {
  uint32_t now = millis();
  bool lit = (P.dispEn & DISP_SEG) && dispLit(DISP_SEG);
  uint8_t ram[5]; int lvl = constrain(contrast * 16 / 256, 0, 15), fl = -1;
  if (lit && cdFx(now, ram, fl)) { if (fl >= 0) lvl = fl; }
  else if (lit) { char t[16]; bool colon; segCompose(t, sizeof t, colon); segText(t, colon, ram); }
  else memset(ram, 0, 5);
  if (P.segFlip) {
    auto rot = [](uint8_t v) -> uint8_t {
      return (uint8_t)((v & 1) << 3 | (v >> 3 & 1) | (v >> 1 & 1) << 4 | (v >> 4 & 1) << 1 | (v >> 2 & 1) << 5 | (v >> 5 & 1) << 2 | (v & 0x40));
    };
    uint8_t o[5] = {rot(ram[4]), rot(ram[3]), (uint8_t)(ram[2] & 0x02), rot(ram[1]), rot(ram[0])}; memcpy(ram, o, 5);
  }
  memcpy(segOut, ram, 5); segOut[5] = lvl; segOut[6] = lit;
  return segOut;
}
// the LCD: 4 rows of 20 (HD44780 characters: 0xDF the degree sign)
WX char* vmr_lcd() {
  bool lit = (P.dispEn & DISP_LCD) && dispLit(DISP_LCD);
  if (lit) lcdCompose(lcdOut, millis()); else for (auto& r : lcdOut) { memset(r, ' ', LCD_W); r[LCD_W] = 0; }
  return &lcdOut[0][0];                           // the page reads 20 characters a row (vmr_lcd_lit: lit or not)
}
WX int vmr_lcd_lit() { return (P.dispEn & DISP_LCD) && dispLit(DISP_LCD); }
WX void vmr_seg_step() { segStep(); }
WX int vmr_screen_avail(int s) { return screenAvail(s); }
WX int vmr_hold_band() { return holdBand(); }
WX int vmr_num_screens() { return NUM_SCREENS; }
