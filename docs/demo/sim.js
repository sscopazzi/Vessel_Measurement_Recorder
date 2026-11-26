// SPDX-FileCopyrightText: 2026 Aspect Wave LLC
// SPDX-License-Identifier: GPL-3.0-or-later
const CAP_VS = 12.08, CAP_TAU = 470;   // the demo's 12 V supply at the IWB and the Niskin capacitor's RC (ms)
// ---- the simulator: made-up sea, ship, weather, battery and SeaLabMOS, fed to the firmware code (wasm) and the dashboard
const SCEN = {
  underway: { name: 'Underway, moderate sea', hs: 2.0, tr: 13.5, te: 8.5, wind: 18, wdir: 320, sog: 13.4, cog: 302, lat: 34.21, lon: -120.83,
              p: 1016.4, ptr: -0.6, t: 14.2, rh: 78, batt: 76, dock: 0, slm: 0, rec: 1 },
  heavy:    { name: 'Heavy weather', hs: 5.5, tr: 15.2, te: 10.4, wind: 41, wdir: 245, sog: 6.8, cog: 262, lat: 57.42, lon: -145.6,
              p: 991.8, ptr: -4.2, t: 6.1, rh: 93, batt: 58, dock: 0, slm: 0, rec: 1 },
  dock:     { name: 'At the dock', hs: 0.1, tr: 9.0, te: 6.0, wind: 7, wdir: 280, sog: 0, cog: 0, lat: 33.7535, lon: -118.2172,
              p: 1013.0, ptr: 0.4, t: 17.5, rh: 64, batt: 91, dock: 1, slm: 0, rec: 0 },
  ctd:      { name: 'CTD cast with Niskin bottles', hs: 1.0, tr: 11.0, te: 7.0, wind: 11, wdir: 300, sog: 0.4, cog: 140, lat: 36.7982, lon: -121.9035,
              p: 1018.2, ptr: 0.2, t: 13.4, rh: 82, batt: 83, dock: 0, slm: 1, rec: 1 },
};
const SHIP = { beam: 32.2, draft: 10.5, len: 190.0, lbp: 182.0, kbr: 31.0, hVmr: 14.0, xVmr: 62 };   // m (a made-up ship)
const LIPO = [[4.20, 100], [4.15, 98.7], [4.125, 97.1], [4.10, 95.3], [4.075, 93.4], [4.05, 90.9], [4.025, 85.3], [4.0, 82.1], [3.975, 79.8], [3.95, 77.3],
  [3.925, 74.1], [3.90, 66.0], [3.875, 61.0], [3.85, 58.1], [3.825, 55.6], [3.80, 53.4], [3.775, 51.0], [3.75, 48.7], [3.725, 46.2], [3.70, 43.2], [3.675, 39.5],
  [3.65, 34.7], [3.625, 28.9], [3.60, 23.9], [3.575, 20.0], [3.55, 16.5], [3.525, 13.7], [3.50, 11.2], [3.475, 8.6], [3.45, 6.2], [3.425, 4.1], [3.40, 2.8],
  [3.375, 1.8], [3.35, 1.1], [3.325, 0.5], [3.30, 0]];
const pctToV = p => { if (p >= 100) return 4.2; if (p <= 0) return 3.30; for (let i = 1; i < LIPO.length; i++) if (p >= LIPO[i][1]) { const [v1, p1] = LIPO[i - 1], [v0, p0] = LIPO[i]; return v0 + (p - p0) / (p1 - p0) * (v1 - v0); } return 3.3; };
const rnd = (() => { let s = 20261121; return () => { s ^= s << 13; s ^= s >>> 17; s ^= s << 5; return ((s >>> 0) / 4294967296); }; })();
const gauss = () => { let u = 0, v = 0; while (!u) u = rnd(); while (!v) v = rnd(); return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v); };
const D2R = Math.PI / 180, R2D = 180 / Math.PI, KN = 0.514444;
const pad = (n, k = 2) => String(n).padStart(k, '0');
const ztime = u => { const d = new Date(u * 1000); return d.getUTCFullYear() + pad(d.getUTCMonth() + 1) + pad(d.getUTCDate()) + '_' + pad(d.getUTCHours()) + pad(d.getUTCMinutes()) + pad(d.getUTCSeconds()) + 'Z'; };

// a narrow-band sea: components spread round one frequency, phases integrated (a period change stays smooth)
class Band {
  constructor(n, spread) { this.c = []; for (let i = 0; i < n; i++) this.c.push({ k: 1 + spread * (2 * i / (n - 1) - 1) + 0.01 * gauss(), ph: 2 * Math.PI * rnd(), w: 0.6 + 0.8 * rnd() });
    const s = Math.sqrt(this.c.reduce((a, c) => a + c.w * c.w / 2, 0)); for (const c of this.c) c.w /= s; }   // unit standard deviation
  step(dt, f) { let v = 0, d = 0, a = 0; for (const c of this.c) { const w = 2 * Math.PI * f * c.k; c.ph += w * dt; const s = Math.sin(c.ph), q = Math.cos(c.ph);
    v += c.w * s; d += c.w * w * q; a -= c.w * w * w * s; } return [v, d, a]; }
}

class Sim {
  constructor(W) {
    this.W = W; this.V = W.V; this.speed = 1; this.usb = false;
    this.rollB = new Band(14, 0.09); this.pitchB = new Band(12, 0.12); this.heaveB = new Band(12, 0.14); this.yawB = new Band(6, 0.2);
    this.marks = []; this.serial = []; this.serSeq = 0; this.events = []; this.files = [];
    this.devs = [{ mac: [0x4F, 0x12, 0x9A], name: '', kind: 'This browser', on: true, me: true, onU: 0 }];
    this.power = true; this.dead = false; this.bootT = 0;
    this.setScenario('underway', true);
  }
  // ---- scenario: the sliders' starting values and the state of the VMR
  setScenario(k, first) {
    const s = SCEN[k]; this.scen = k; this.p = { ...s };
    const V = this.V, W = this.W;
    const tz = Math.round(new Date().getTimezoneOffset() / 60);       // the zone description from this computer's clock
    this.unix0 = Math.floor(Date.now() / 1000) - 3 * 3600;             // the VMR has been on 3 h
    this.t = 3 * 3600 * 1000;
    V.fakeNow = this.t; V.upBase = 0; V.T_UNIX = this.unix;
    V['P.zdHours'] = Math.max(-12, Math.min(12, tz));
    this.lat = s.lat; this.lon = s.lon; this.hdg = s.cog;
    this.wx = { p: s.p, t: s.t, rh: s.rh };
    this.batt = s.batt; this.usb = false;
    V['P.atDock'] = s.dock; V['P.slmOn'] = 1; V['P.lcdColor'] = 3; V['P.lcdScreen'] = k === 'ctd' ? 9 : k === 'heavy' ? 4 : 0; V['P.lcdCycle'] = 0;
    V['P.shipBeamCm'] = Math.round(SHIP.beam * 100); V['P.shipDraftCm'] = Math.round(SHIP.draft * 100); V['P.keelBridgeCm'] = Math.round(SHIP.kbr * 100);
    V.windOnFake = 1; V.windMcOn = 1; V.windLocked = 1;
    V.paused = 0; V.loggerMode = 0; V.gpsOff = 0; V.wifiOffByBtn = 0; V.imuOffFake = 0; V['P.imuOff'] = 0;
    V.cfmAct = 255; V.holdShow = 0; V.infoOn = 0; V.serOledOn = 0;
    V.screen = s.slm ? 20 : s.dock ? 8 : 0;
    // history: 10 min of graph, 20 min / 3 h of weather, 10 min of wind, as after 3 h on
    W.X.vmr_graph_clear(); W.X.vmr_wx_clear(); this.wxF = []; this.wxC = []; this.windF = []; this.windC = []; this.r1m = []; this.gHist = []; this.peaks = [];
    this.allMax = { rp: 0, rn: 0, pp: 0, pn: 0, yr: 0, trp: 0, trn: 0, tpp: 0, tpn: 0, tyr: 0 }; this.clino = { s: 0, p: 0, su: 0, pu: 0 };
    this.g = { pkS: 0, pkP: 0, pkF: 0, pkA: 0, hi: 1, lo: 1 };
    this.gust = 0; this.wmean = null; this.awHist = [];
    this.stepN = 0; this.lastWx = this.lastWxC = this.lastWind = this.lastGraph = this.lastSec = this.lastMin = 0;
    for (let i = 0; i < 1200; i++) this.motion(0.5, true);                // 10 min of motion, pushed at 2 Hz
    for (let i = 360; i > 0; i--) { const w = this.wxAt(-i * 30); this.wxC.push(w); W.X.vmr_wx_push(1, w.p, w.t, w.h); }
    for (let i = 600; i > 0; i--) { const w = this.wxAt(-i * 2); this.wxF.push(w); W.X.vmr_wx_push(0, w.p, w.t, w.h); }
    for (let i = 0; i < 360; i++) this.windStep(30, true);
    this.windC = this.windF.splice(0);
    for (let i = 0; i < 300; i++) this.windStep(2, true);
    // the recording, marks, SeaLabMOS
    this.marks = []; this.files = [
      { n: ztime(this.unix0 - 86400 * 2 - 4000) + '_leg1.csv', s: 41288321 }, { n: ztime(this.unix0 - 86400 - 1300) + '_leg1.csv', s: 39012034 }];
    V.logging = 0; V.logWanted = 0;
    if (s.rec) this.recStart(this.t - (2 * 3600 + 41 * 60) * 1000, k === 'ctd' ? 'cast_day' : 'leg2');
    if (k === 'underway') { this.addMark('M', -5400, ''); this.addMark('M', -2700, 'Whales to port'); }
    this.slmReset(s.slm);
    this.serial = []; this.say('Scenario: ' + s.name + ' (simulated)');
    this.power = true; this.dead = false; this.bootT = 0; this.emptyN = 0; this.emptyAt = 0; this.chargeOff = false;
    this.syncMarks(); this.syncDevs();
    if (!first) { W.str('DEMO: ' + s.name.toUpperCase().slice(0, 20)); }
  }
  get unix() { return this.unix0 + Math.floor(this.t / 1000); }
  say(t) { this.serSeq++; this.serial.push(t); if (this.serial.length > 200) this.serial.shift(); this.W.str(t.slice(0, 118)); this.W.X.vmr_slog_add(); }

  // ---- the weather: pressure trend, the day's temperature, humidity
  wxAt(dtS) {
    const p = this.p, hr = ((this.unix + dtS) / 3600 - this.V['P.zdHours']) % 24;
    return { p: this.wx.p + p.ptr / 3 * dtS / 3600 + 0.12 * Math.sin((this.unix + dtS) / 900),
             t: this.wx.t + 1.4 * Math.sin((hr - 9) / 24 * 2 * Math.PI) + 0.15 * Math.sin((this.unix + dtS) / 300),
             h: Math.min(100, this.wx.rh - 4 * Math.sin((hr - 9) / 24 * 2 * Math.PI) + 0.8 * Math.sin((this.unix + dtS) / 420)) };
  }
  // ---- motion: roll, pitch, heave and yaw from the sea state; dt in s; history = filling in at boot
  motion(dt, history) {
    const p = this.p, V = this.V, dock = V['P.atDock'];
    const sr = dock ? 0.12 : 1.55 * p.hs, sp = dock ? 0.05 : 0.42 * p.hs, sh = p.hs / 4;
    const [rv, rd, ra] = this.rollB.step(dt, 1 / p.tr), [pv, pd, pa] = this.pitchB.step(dt, 1 / p.te), [hv, hd, ha] = this.heaveB.step(dt, 1 / p.te * 1.05);
    const [yv, yd] = this.yawB.step(dt, 1 / (p.te * 3)), ya = p.sog > 1 ? 0.6 + 0.1 * p.hs : 0.05;   // yaw, deg
    const list = dock ? -0.42 : 0.35, trim = dock ? 0.86 : -0.2;
    this.roll = list + sr * rv; this.pitch = trim + sp * pv; this.heave = sh * hv;
    this.rollA = sr * ra * D2R; this.pitchA = sp * pa * D2R; this.heaveA = sh * ha;
    this.yawRate = ya * yd * 60;                                     // deg/min
    this.hdg = (p.cog + ya * yv + 360) % 360;
    if (history) { this.W.X.vmr_graph_push(this.roll, this.pitch); this.track(dt); }
  }
  track(dt) {                                                          // 1-min window, peaks, maxima (every 0.5 s)
    const now = this.t / 1000, r = this.roll, pt = this.pitch;
    const G = this.gHist || (this.gHist = []); G.push(this.V['P.imuOff'] ? [NaN, NaN] : [r - this.V['P.rollOffset'], pt - this.V['P.pitchOffset']]); if (G.length > 1200) G.shift();
    this.r1m.push([now, r, pt, Math.abs(this.yawRate)]); while (this.r1m.length && this.r1m[0][0] < now - 60) this.r1m.shift();
    const pk = this.pk || (this.pk = { last: r, dir: 0, ext: r });
    const d = Math.sign(r - pk.last); if (d && pk.dir && d !== pk.dir) { this.peaks.push([now, pk.last]); while (this.peaks.length && this.peaks[0][0] < now - 1800) this.peaks.shift(); }
    if (d) pk.dir = d; pk.last = r;
    const A = this.allMax, u = this.unix;
    if (r > A.rp) { A.rp = r; A.trp = u; } if (r < A.rn) { A.rn = r; A.trn = u; } if (pt > A.pp) { A.pp = pt; A.tpp = u; } if (pt < A.pn) { A.pn = pt; A.tpn = u; }
    if (Math.abs(this.yawRate) > A.yr) { A.yr = Math.abs(this.yawRate); A.tyr = u; }
    const C = this.clino; if (r > C.s) { C.s = r; C.su = u; } if (r < C.p) { C.p = r; C.pu = u; }
  }
  windStep(dt, history) {
    const p = this.p, de = Math.min(dt, 2); this.gust += (-this.gust * 0.08 + 0.35 * gauss() * Math.sqrt(p.wind + 1)) * de;
    const tws = Math.max(0, p.wind + this.gust), twd = (p.wdir + 6 * Math.sin(this.t / 400000) + 360) % 360;
    const ve = p.sog * Math.sin(p.cog * D2R), vn = p.sog * Math.cos(p.cog * D2R);
    const we = -tws * Math.sin(twd * D2R) - ve, wn = -tws * Math.cos(twd * D2R) - vn;   // the air past the ship
    const aws = Math.hypot(we, wn), awd = (Math.atan2(-we, -wn) * R2D + 360) % 360;
    this.aws = aws; this.awa = (awd - this.hdg + 360) % 360; this.tws = tws; this.twd = twd;
    const step = history || this.t - this.lastWind >= 2000;
    if (step) {
      this.lastWind = this.t;
      this.windF.push({ ad: Math.round(this.awa), as: +aws.toFixed(1), td: Math.round(twd), ts: +tws.toFixed(1) }); if (this.windF.length > 600) this.windF.shift();
      if (this.windF.length % 15 === 0) { this.windC.push(this.windF[this.windF.length - 1]); if (this.windC.length > 360) this.windC.shift(); }
    }
  }
  // ---- marks (the page keeps the list; the firmware's copy is rebuilt from it)
  addMark(kind, dtS, note, bottle) {
    const fix = !this.V.gpsOff;
    const m = { kind, unix: this.unix + (dtS || 0), fix, la: Math.round(this.lat * 1e7), lo: Math.round(this.lon * 1e7), note: note || '', startMs: this.t + (dtS || 0) * 1000,
                sog: this.p.sog, cog: this.p.cog, pa: this.wx.p, ta: this.wx.t, rh: this.wx.rh, sats: 10, hdop: 0.9, dur: NaN, drift: NaN, bottle };
    this.marks.push(m); this.syncMarks();
    if (this.V.logging) this.logEv(kind === 'O' ? 'CAST ' + this.marks.length + ' START' : 'LOCATION MARK ' + this.marks.length);
    return this.marks.length;
  }
  openCast() { for (let i = this.marks.length - 1; i >= 0; i--) if (this.marks[i].kind === 'O') return i; return -1; }
  castToggle() {
    const i = this.openCast();
    if (i < 0) { this.addMark('O'); this.toast('O'); return; }
    const m = this.marks[i]; m.kind = 'C'; m.endUnix = this.unix; m.dur = this.unix - m.unix; m.ela = Math.round(this.lat * 1e7); m.elo = Math.round(this.lon * 1e7);
    m.drift = this.dist(m.la, m.lo, m.ela, m.elo);
    this.syncMarks(); this.toast('C'); if (this.V.logging) this.logEv('CAST ' + (i + 1) + ' STOP');
    this.say('Location mark ' + (i + 1) + ' (cast): ' + m.dur + ' s, ' + Math.round(m.drift) + ' m');
  }
  dist(a1, o1, a2, o2) { const k = Math.PI / 180e7, p1 = a1 * k, p2 = a2 * k, dp = (a2 - a1) * k, dl = (o2 - o1) * k;
    const h = Math.sin(dp / 2) ** 2 + Math.cos(p1) * Math.cos(p2) * Math.sin(dl / 2) ** 2; return 2 * 6371008.8 * Math.asin(Math.sqrt(h)); }
  toast(k, b) { this.V.markToastKind = k.charCodeAt(0); this.V.markToastBottle = b || 0; this.V.markToastN = 1; }
  syncMarks() {
    const X = this.W.X; X.vmr_marks_clear();
    for (const m of this.marks) X.vmr_mark_add(m.kind.charCodeAt(0), m.unix, m.fix ? 1 : 0, m.la, m.lo, m.dur, m.drift, m.startMs >>> 0);
    X.vmr_marks_done(); this.mkRev = (this.mkRev || 0) + 1;
  }
  syncDevs() { this.devs.forEach((d, i) => { this.W.str((d.name || '') + '|' + d.kind); this.W.X.vmr_dev_set(i, d.on ? 1 : 0, d.onU || this.unix - 300, d.offU || 0, ...d.mac); }); this.W.X.vmr_devs(this.devs.length); }
  // ---- recording
  recStart(atMs, tag) {
    const V = this.V; V.logging = 1; V.logWanted = 1; V.logStartMs = atMs >>> 0; V.logRows = 0; V.logBytes = 0;
    this.logName = ztime(this.unix0 + Math.floor(atMs / 1000)) + (tag ? '_' + tag : '') + '.csv'; V.logName = '/' + this.logName;
    this.logEvs = []; this.rowAcc = 0;
    const el = (this.t - atMs) / 1000; this.rows = Math.floor(el * 1000 / this.logMs()); this.bytes = this.rows * 412 + 9000;
    this.say('SD: recording to /' + this.logName);
  }
  recStop() { const V = this.V; if (!V.logging) return; this.files.unshift({ n: this.logName, s: this.bytes }); V.logging = 0; V.logWanted = 0; this.say('SD: recording stopped, file closed'); }
  logMs() { const R = [100, 1000, 5000, 20000, 60000, 300000, 600000]; return this.V['P.imuOff'] ? R[this.V['P.logRate']] || 100 : 100; }
  logEv(t) { (this.logEvs || (this.logEvs = [])).unshift({ n: this.logEvs.length + 1, t: this.ltStr(this.unix), note: t }); this.say('Event ' + this.logEvs.length + ': ' + t); }
  ltStr(u) { const d = new Date((u - this.V['P.zdHours'] * 3600) * 1000); return d.getUTCFullYear() + '-' + pad(d.getUTCMonth() + 1) + '-' + pad(d.getUTCDate()) + ' ' + pad(d.getUTCHours()) + ':' + pad(d.getUTCMinutes()) + ':' + pad(d.getUTCSeconds()); }

  // ---- SeaLabMOS: the WTB (cast session, Niskin bottles) and the IWB (CTD) in the water
  slmReset(arm) {
    this.slm = { armed: !!arm, state: arm ? 2 : 0, stT: 0, cast: 0, castN: 3, open: false, depth: 0, phase: 0, maxD: 0, trace: [], ev: [], cd: null, prof: [], tare: 10.1325,
                 eth: !!arm, iwb: !!arm, v: 12.1, cap: arm ? 11.6 : 0, rdy: !!arm, pend: -1, held: 0, nkOn: true, refuse: [0, 0, 0, 0, 0, 0],
                 b: [0, 1, 2, 3, 4, 5].map(i => ({ st: arm ? 1 : 0, src: 0, plan: [10, 30, 50, NaN, NaN, NaN][i], z: NaN, t: NaN, sp: NaN, rho: NaN, u: 0 })), btnDown: [0, 0, 0, 0, 0, 0] };
    if (arm) this.slmEv('Ethernet up'), this.slmEv('IWB tare ' + this.slm.tare.toFixed(4) + ' dbar');
  }
  slmEv(t) { this.slm.ev.unshift({ u: this.unix, t }); if (this.slm.ev.length > 30) this.slm.ev.pop(); }
  prof(z) { const f0 = z < 8 ? 0 : Math.min(1, (z - 8) / 27), f = 0.5 - 0.5 * Math.cos(Math.PI * f0);
    const t = 15.8 - 5.4 * f - 0.03 * Math.max(0, z - 35), sp = 31.42 + 1.78 * f + 0.016 * Math.max(0, z - 35);
    const p = z * 1.0057, rho = 1000 + 28.1 - 0.0735 * (t - 10) - 0.0047 * (t - 10) ** 2 + 0.785 * (sp - 35) + 0.0045 * p; return { t, sp, p, rho }; }
  slmArm(on) {
    const S = this.slm;
    if (on && !S.armed) { this.slmReset(false); this.slm.armed = true; this.slm.state = 1; this.slm.stT = this.t; this.say('SeaLabMOS: armed (WTB + IWB on)'); }
    else if (!on && S.armed) { if (S.open) this.slmCastEnd('disarm'); S.armed = false; S.state = 0; S.iwb = S.eth = false; S.cap = 0; S.rdy = false; S.capT = undefined; this.say('SeaLabMOS: disarmed (WTB + IWB off)'); }
  }
  slmCastStart(by) {
    const S = this.slm; if (!S.armed || S.open || !(S.state === 2 || S.state === 5)) return false;
    S.open = true; S.state = 3; S.cast++; S.castN = Math.max(S.castN, 0) + 1; S.phase = 1; S.depth = 0; S.maxD = 0; S.trace = []; S.target = 76;
    this.slmEv('Cast ' + S.castN + ' start (' + by + ')');
    if (this.openCast() < 0) { this.castToggle(); S.opened = true; }
    return true;
  }
  slmCastEnd(by) {
    const S = this.slm; if (!S.open) return;
    S.open = false; S.state = 4; S.stT = this.t; S.phase = 0;
    const bins = []; for (let z = 0.25; z <= S.maxD; z += 0.5) { const q = this.prof(z); bins.push([+q.p.toFixed(3), +z.toFixed(3), +q.t.toFixed(4), +q.sp.toFixed(4), +q.rho.toFixed(4), 5]); }
    S.prof = bins; const mr = bins.length ? bins.reduce((a, b) => a + b[4], 0) / bins.length : NaN;
    S.cd = { n: S.castN, start: this.unix - 300, end: this.unix, lat: this.lat, lon: this.lon, max: +S.maxD.toFixed(1), rho: +mr.toFixed(3), top: 0.25, bot: +(bins.length ? bins[bins.length - 1][1] : 0).toFixed(2),
             draft: null, bins: bins.length, used: bins.length, toDraft: 0, reason: 0, raw: ztime(this.unix) + '_cast' + pad(S.castN, 3) + '_raw.csv', vmrFile: ztime(this.unix) + '_cast' + pad(S.castN, 3) + '.csv', got: 99 };
    this.slmEv('Cast ' + S.castN + ' done: ' + S.maxD.toFixed(1) + ' m, ' + mr.toFixed(3) + ' kg/m3 (' + by + ')');
    if (S.opened && this.openCast() >= 0) { this.castToggle(); } S.opened = false;
  }
  // the Niskin capacitor: charged from the 12 V through 47 ohm (RC 0.47 s), ready at 95 % of the supply (1.4 s from empty)
  capCharge() { const S = this.slm; S.cap0 = Math.min(S.cap || 0, CAP_VS * 0.999); S.capT = this.t; S.rdy = false; }
  capNow() { const S = this.slm, f0 = 1 - S.cap0 / CAP_VS, dt = this.t - S.capT, v = CAP_VS * (1 - f0 * Math.exp(-dt / CAP_TAU)),
    need = CAP_TAU * Math.log(f0 / 0.05);   // ms from cap0 to 95 %
    return { v, ready: dt >= need, inS: Math.max(0, (need - dt) / 1000) }; }
  nkFire(i, src) {
    const S = this.slm, B = S.b[i];
    if (!S.nkOn || B.st !== 1 || !S.open || !S.rdy || S.pend >= 0) { S.refuse[i] = this.t + 1000; return false; }
    S.pend = i; S.pendT = this.t; S.pendSrc = src; return true;
  }
  slmStep(dt) {
    const S = this.slm, V = this.V;
    if (!S.armed) { V.slmArmed = 0; V.ctdCastOpen = 0; V.slmBtMs = 0; V.slmLvMs = 0; return; }
    if (S.state === 1 && this.t - S.stT > 8000) { S.state = 2; S.eth = S.iwb = true; this.slmEv('Ethernet up'); this.slmEv('IWB tare ' + S.tare.toFixed(4) + ' dbar'); this.capCharge(); }
    if (S.state === 4 && this.t - S.stT > 5000) S.state = 2;
    if (S.open) {                                         // the winch: down at 0.5 m/s to 76 m, a 20 s soak there, up at 1 m/s
      if (S.phase === 1) { S.depth += 0.5 * dt; if (S.depth >= S.target) { S.depth = S.target; S.phase = 2; S.soak = 0; } }
      else if (S.phase === 2) { S.soak += dt; if (S.soak > 20) S.phase = 3; }
      else if (S.phase === 3) { S.depth -= 1.0 * dt; if (S.depth <= 0.1) { S.depth = 0; this.slmCastEnd('on depth'); } }
      S.maxD = Math.max(S.maxD, S.depth);
      for (let i = 0; i < 6; i++) { const B = S.b[i]; if (S.phase === 1 && B.st === 1 && !isNaN(B.plan) && S.depth >= B.plan && S.pend < 0 && S.rdy) this.nkFire(i, 3); }
    } else S.depth = Math.max(0, S.depth - dt);
    if (S.pend >= 0 && this.t - S.pendT > 400) {           // the IWB closes it: the bottle's record and a mark on the VMR
      const i = S.pend, B = S.b[i], q = this.prof(S.depth);
      Object.assign(B, { st: 2, src: S.pendSrc, z: S.depth, t: q.t, sp: q.sp, rho: q.rho, u: this.unix }); S.pend = -1; S.cap = 9.4; this.capCharge();
      const why = S.pendSrc === 3 ? 'planned depth' : S.pendSrc === 2 ? 'button' : 'dashboard';
      this.slmEv('Bottle ' + (i + 1) + ' fired at ' + S.depth.toFixed(2) + ' m (' + why + ')');
      this.addMark('M', 0, 'Bottle ' + (i + 1) + ' ' + S.depth.toFixed(1) + ' m', i + 1); this.toast('B', i + 1);
    }
    if (S.capT !== undefined) { const r = this.capNow(); S.cap = r.v; if (r.ready && !S.rdy) S.rdy = true; }
    if (S.open && (this._trT || 0) + 1000 <= this.t) { this._trT = this.t; const q = this.prof(S.depth); if (S.phase === 1) S.trace.push([+q.p.toFixed(3), +q.t.toFixed(3), +q.sp.toFixed(3), +q.rho.toFixed(3), +S.depth.toFixed(2)]); }
    // to the firmware's copy (what the WTB sends: status, live, bottles)
    V.slmArmed = 1; V.ctdCastOpen = S.open ? 1 : 0;
    V['slmSt.state'] = S.state; V['slmSt.iwbAlive'] = S.iwb ? 1 : 0; V['slmSt.eth'] = S.eth ? 1 : 0; V['slmSt.castNum'] = S.castN; V['slmSt.castOpen'] = S.open ? 1 : 0; V['slmSt.iwbV'] = S.v;
    if (S.iwb) { const q = this.prof(Math.max(0, S.depth)); V.slmLvMs = this.t >>> 0; V['slmLv.depth'] = S.depth; V['slmLv.t'] = S.depth > 0.3 ? q.t : this.wx.t + 0.4; V['slmLv.sp'] = S.depth > 0.3 ? q.sp : NaN; V['slmLv.rho'] = S.depth > 0.3 ? q.rho : NaN; }
    V.slmBtMs = this.t >>> 0; V['slmBt.on'] = S.nkOn ? 1 : 0; V['slmBt.ready'] = S.rdy && S.pend < 0 ? 1 : 0; V['slmBt.pend'] = S.pend < 0 ? 255 : S.pend;
    V['slmBt.readyInS'] = S.rdy ? 0 : this.capNow().inS;
    for (let i = 0; i < 6; i++) { V['slmBt.st' + i] = S.b[i].st === 2 ? 2 : S.b[i].st; V['slmBt.planM' + i] = S.b[i].plan; }
  }
  // the WTB box's lights
  wtbLeds() {
    const S = this.slm, on = S.armed && S.nkOn, rings = [];
    for (let i = 0; i < 6; i++) {
      let l = on && S.b[i].st === 1;
      if (l && S.btnDown[i] && !S.btnDone?.[i]) l = Math.floor(this.t / 125) % 2 === 0;
      if (on && i === S.pend) l = true;
      if (on && S.refuse[i] > this.t) l = Math.floor(this.t / 60) % 2 === 0;
      rings.push(l);
    }
    return { rings, ready: on && S.iwb && S.rdy && S.pend < 0, eth: S.armed && (!S.eth || !S.iwb) };
  }
  wtbButtons(down, tare, start) {                          // down: the six bottle buttons; held 1.5 s fires
    const S = this.slm; S.btnDone = S.btnDone || [0, 0, 0, 0, 0, 0]; S.btnAt = S.btnAt || [0, 0, 0, 0, 0, 0];
    for (let i = 0; i < 6; i++) {
      if (down[i] && S.armed) { if (!S.btnDown[i]) { S.btnDown[i] = 1; S.btnAt[i] = this.t; S.btnDone[i] = 0; } else if (!S.btnDone[i] && this.t - S.btnAt[i] >= 1500) { S.btnDone[i] = 1; this.nkFire(i, 2); } }
      else { S.btnDown[i] = 0; S.btnDone[i] = 0; }
    }
    if (tare && !this._tare && S.armed && S.iwb) { this.slmEv('Tare button'); S.tare = 10.13 + 0.01 * rnd(); this.slmEv('IWB tare ' + S.tare.toFixed(4) + ' dbar'); }
    if (start && !this._start && S.armed) { if (S.open) this.slmCastEnd('button'); else this.slmCastStart('button'); }
    this._tare = tare; this._start = start;
  }

  // ---- one step of simulated time (20 ms)
  step() {
    const dt = 0.02, V = this.V, W = this.W, p = this.p;
    this.t += 20; V.fakeNow = this.t >>> 0; V.T_UNIX = this.unix;
    if (!this.power || this.dead || this.chargeOff) { if (this.usb) this.batt = Math.min(100, this.batt + 8 / 3600 * dt); return; }
    if (this.bootT) { if (this.t >= this.bootUntil) this.bootT = 0; else return; }
    this.motion(dt, false);
    this.windStep(dt, false);
    if (this.t - this.lastGraph >= 500) { this.lastGraph = this.t; if (!V['P.imuOff']) W.X.vmr_graph_push(this.roll, this.pitch); this.track(0.5); }
    // the IMU's values to the firmware (as serviceImu leaves them)
    const imu = !V['P.imuOff'];
    V.imuOffFake = imu ? 0 : 1;
    V.roll = this.roll - V['P.rollOffset']; V.pitch = this.pitch - V['P.pitchOffset']; V.rotNow = this.yawRate; V.yawDeg = this.hdg; V.magHdg = (this.hdg - 13.5 + 360) % 360;
    const g = this.g, gs = SHIP.hVmr * this.rollA / 9.81, gf = SHIP.xVmr * this.pitchA / 9.81 * 0.35, gv = 1 + this.heaveA / 9.81;
    V.gFeltS = gs; V.gFeltF = gf; V.gVert = gv;
    g.pkS = Math.max(g.pkS, gs); g.pkP = Math.max(g.pkP, -gs); g.pkF = Math.max(g.pkF, gf); g.pkA = Math.max(g.pkA, -gf); g.hi = Math.max(g.hi, gv); g.lo = Math.min(g.lo, gv);
    V.gPkS = g.pkS; V.gPkP = g.pkP; V.gPkF = g.pkF; V.gPkA = g.pkA; V.gPkHi = g.hi; V.gPkLo = g.lo;
    this.wtbButtons(this.ui ? this.ui.wtbDown() : [0, 0, 0, 0, 0, 0], this.ui && this.ui.isDown('tare'), this.ui && this.ui.isDown('start'));
    this.slmStep(dt);
    if (this.t - this.lastSec >= 1000) { this.lastSec = this.t; this.second(); }
    // the firmware's loop: buttons, screens, LEDs
    W.X.vmr_tick();
    this.handleEvents();
  }
  second() {
    const V = this.V, p = this.p, W = this.W, dock = V['P.atDock'];
    // GPS
    const gpsOn = !V.gpsOff;
    const vn = p.sog * KN * Math.cos(p.cog * D2R), ve = p.sog * KN * Math.sin(p.cog * D2R);
    this.lat += vn / 111320; this.lon += ve / (111320 * Math.cos(this.lat * D2R));
    V['gps.gpsFix'] = gpsOn ? 1 : 0; V['gps.gpsLat'] = this.lat; V['gps.gpsLon'] = this.lon; V['gps.latE7'] = Math.round(this.lat * 1e7); V['gps.lonE7'] = Math.round(this.lon * 1e7);
    V['gps.gpsAlt'] = 18.4 + 0.6 * gauss(); V['gps.gpsSpeed'] = p.sog * KN * (1 + 0.01 * gauss()); V['gps.gpsAngle'] = p.cog + gauss(); V['gps.gpsSats'] = gpsOn ? 10 : 0; V['gps.gpsHdop'] = 0.84;
    V['gps.gpsFixQ'] = gpsOn ? 2 : 0; V['gps.gpsLastFixMs'] = gpsOn ? this.t >>> 0 : 0; V.gpsRmcMs = this.t >>> 0;
    V['gpsInView[0]'] = gpsOn ? 11 : 0; V['gpsInView[1]'] = gpsOn ? 5 : 0; V.gpsBestSnr = gpsOn ? 41 : 0;
    V.sogAvgKn = gpsOn ? p.sog + 0.05 * gauss() : NaN; V.cogAvgDeg = gpsOn && p.sog >= 0.3 ? (p.cog + 0.4 * gauss() + 360) % 360 : NaN;
    V.clockSrc = gpsOn ? 'G'.charCodeAt(0) : 'R'.charCodeAt(0);
    if (!this.sky) { this.sky = [[3, 72, 40, 41, 1], [6, 35, 120, 33, 1], [9, 15, 250, 22, 0], [12, 55, 300, 38, 1], [17, 8, 190, 0, 0], [19, 40, 20, 30, 1], [24, 25, 80, 27, 1],
                                 [66, 60, 200, 36, 1], [67, 20, 330, 24, 0], [75, 45, 150, 0, 0], [81, 10, 60, 18, 0], [82, 80, 270, 40, 1], [46, 30, 210, 25, 1], [14, 50, 170, 35, 1]]; }
    if (this.stepN++ % 30 === 0) { W.X.vmr_sky(gpsOn ? this.sky.length : 0); if (gpsOn) this.sky.forEach((s, i) => { s[2] = (s[2] + 1) % 360; W.X.vmr_sky_set(i, s[0], s[1], s[2], Math.max(0, s[3] + Math.round(gauss())), s[4]); }); }
    // the 1-min window, roll statistics, period, waves, GM
    const R = this.r1m; if (R.length > 20) { let rM = -99, rm = 99, pM = -99, pm = 99, yM = 0; for (const x of R) { rM = Math.max(rM, x[1]); rm = Math.min(rm, x[1]); pM = Math.max(pM, x[2]); pm = Math.min(pm, x[2]); yM = Math.max(yM, x[3]); }
      V['win1.rMax'] = rM; V['win1.rMin'] = rm; V['win1.pMax'] = pM; V['win1.pMin'] = pm; V['win1.yrMax'] = yM / 60; V['win1.valid'] = 1; this.win = { rM, rm, pM, pm, yM }; }
    const A = this.allMax; V['P.rollMaxPos'] = A.rp; V['P.rollMaxNeg'] = A.rn; V['P.pitchMaxPos'] = A.pp; V['P.pitchMaxNeg'] = A.pn; V['P.yawRateMax'] = A.yr / 60;
    V['P.tRollMaxPos'] = A.trp; V['P.tRollMaxNeg'] = A.trn; V['P.tPitchMaxPos'] = A.tpp; V['P.tPitchMaxNeg'] = A.tpn; V['P.tYawRateMax'] = A.tyr;
    V.clinoMaxS = this.clino.s; V.clinoMaxP = this.clino.p; V.clinoMaxSu = this.clino.su; V.clinoMaxPu = this.clino.pu;
    const sr = dock ? 0.12 : 1.55 * p.hs, Tz = p.tr * 0.95;
    V.rollPeriod = sr > 0.3 ? Tz + 0.25 * gauss() : NaN;
    const amps = this.peaks.map(x => Math.abs(x[1] - (dock ? -0.42 : 0.35)));
    const nR = Math.floor(amps.length / 2), srt = [...amps].sort((a, b) => b - a), top = srt.slice(0, Math.max(1, Math.floor(srt.length / 3)));
    this.rs = { n: nR, sig: top.length ? top.reduce((a, b) => a + b, 0) / top.length : NaN, max: srt[0] || 0, mean: dock ? -0.42 : 0.35, rms: sr, ovS: this.peaks.filter(x => x[1] > 10).length, ovP: this.peaks.filter(x => x[1] < -10).length, tp: Tz };
    V['rStat[0].n'] = nR; V['rStat[0].tMed'] = Tz; V['rStat[0].sig'] = this.rs.sig; V['rStat[0].maxAmp'] = this.rs.max; V['rStat[0].mean'] = this.rs.mean; V['rStat[0].rms'] = sr;
    V['rStat[0].overS'] = this.rs.ovS; V['rStat[0].overP'] = this.rs.ovP;
    const wv = !dock && !V['P.imuOff'];
    V.wvN = wv ? 2048 : 0; V.wvCurrent = wv ? 1 : 0; V.wvLastMs = wv ? (this.t - 12000) >>> 0 : 0; V.wvCalcMs = (this.t - 12000) >>> 0;
    const hs = p.hs; V.wvHs = wv ? hs * (1 + 0.02 * gauss()) : NaN; V.wvTz = wv ? p.te * 1.02 : NaN; V.wvTe = wv ? p.te : NaN; V.wvTrs = wv ? p.tr : NaN;
    V.wvHsMid = wv ? hs * 0.88 : NaN; V.wvTzMid = wv ? p.te * 1.04 : NaN; V.wvHsB = wv ? hs * 1.04 : NaN; V.wvHsBMid = wv ? hs * 0.9 : NaN;
    if (wv && this.stepN % 30 === 1) {                    // the spectra, every 30 s as the firmware: heave m2/Hz, pitch and roll deg2/Hz, 1/128 Hz bins
      const S = W.ws(), gs = (f0, var_, w) => { for (let k = 0; k <= 80; k++) { const f = k / 128; S[this._row * 81 + k] = var_ / (w * Math.sqrt(2 * Math.PI)) * Math.exp(-0.5 * ((f - f0) / w) ** 2) * (1 + 0.15 * gauss()); } };
      this._row = 0; gs(1 / (p.te * 1.02), (hs / 4) ** 2, 0.008); this._row = 1; gs(1 / p.te, (0.42 * hs) ** 2, 0.009); this._row = 2; gs(1 / p.tr, sr ** 2, 0.006);
      for (let i = 0; i < 3 * 81; i++) if (S[i] < 0) S[i] = 0;
    }
    const C = 0.373 + 0.023 * SHIP.beam / SHIP.draft - 0.043 * SHIP.len / 100; this.gmC = C;
    V.gmC = C; V.gmEst = wv ? (2 * C * SHIP.beam / p.tr) ** 2 : NaN;
    V.dockRawPitch = this.pitch; V.dockRoll = this.roll; V.trimFt = Math.tan(this.pitch * D2R) * SHIP.lbp * 3.28084;
    // weather (every 2 s; the 3-h graph every 30 s), the wind's means
    const w = this.wxAt(0); this.wxNow = w;
    V.extPa = w.p * 100; V.extTa = w.t; V.extRh = w.h; V.extTb = w.t + 9.2; V.presTendSim = p.ptr + 0.1 * Math.sin(this.unix / 3000); V.cpuTempC = w.t + 26;
    if (this.t - this.lastWx >= 2000) { this.lastWx = this.t; this.wxF.push(w); if (this.wxF.length > 600) this.wxF.shift(); W.X.vmr_wx_push(0, w.p, w.t, w.h); }
    if (this.t - this.lastWxC >= 30000) { this.lastWxC = this.t; this.wxC.push(w); if (this.wxC.length > 360) this.wxC.shift(); W.X.vmr_wx_push(1, w.p, w.t, w.h); }
    const F = this.windF, n2 = F.slice(-60), n10 = F.slice(-300), vm = a => { let x = 0, y = 0, s = 0; for (const q of a) { x += Math.cos(q.ad * D2R); y += Math.sin(q.ad * D2R); s += q.as; } return [(Math.atan2(y, x) * R2D + 360) % 360, s / a.length]; };
    const [a2, s2] = vm(n2); V.windA2 = a2; V.windS2 = s2; V.windG2 = Math.max(...n2.map(q => q.as)); V.windG10 = Math.max(...n10.map(q => q.as));
    V.windAwa = this.awa; V.windAwsKn = this.aws; V.windA1 = this.awa; V.windS1 = this.aws;
    this.wind2 = { a2, s2, g10: V.windG10 };
    // the battery: the voltage-based % and its slope (or charging on USB)
    this.battStep();
    // the recording
    if (V.logging && !V.paused) { const n = 1000 / this.logMs(); this.rowAcc += n; const k = Math.floor(this.rowAcc); this.rowAcc -= k; this.rows += k; this.bytes += k * 412; }
    V.logRows = this.rows || 0; V.logBytes = this.bytes || 0; V.baseUnix = this.unix; V.baseMillis = this.t >>> 0;
    if (V['P.imuOff'] && this.logMs() >= 1000) { const lm = this.logMs(), s = Math.floor(this.unix * 1000 / lm); if (s !== this._slot) { this._slot = s; V.ledHeartMs = this.t >>> 0; } V.logSlot = s; }
    V.bufSecs = V.logging ? 0 : 1200;
    // the hotspot: this browser on it (unless the Wi-Fi is off)
    const wifi = !V.wifiOffByBtn && !V.loggerMode; V.staFakeN = wifi ? 1 : 0; V['WiFi.n'] = wifi ? 1 : 0; this.devs[0].on = wifi;
    if (this.stepN % 10 === 0) this.syncDevs();
    // what is wrong now (the Fault LED)
    const f = []; if (V.battPct < 20 && !this.usb) f.push('battery low');
    V.faultNow = f.join('; ');
    if (this.unix - (this._alive || 0) >= 60) { this._alive = this.unix; const up = Math.floor(this.t / 1000);
      this.say('[alive ' + Math.floor(up / 3600) + 'h ' + Math.floor(up / 60) % 60 + 'm] IMU ok | GPS ' + (gpsOn ? 'fix, 10 used / 16 in view, best 41 dB-Hz' : 'off') + ' | log ' + (V.logging ? 'REC' : 'off') + ' | Wi-Fi ' + (wifi ? 'on' : 'off')); }
  }
  battStep() {
    const V = this.V;
    const draw = 0.150 * (V['P.imuOff'] ? 0.6 : 1) * (V.wifiOffByBtn || V.loggerMode ? 0.7 : 1);   // A (made up: the whole box)
    const rate = this.usb ? 8 : -draw / 2.5 * 100;           // %/h (the Feather charges at about 200 mA)
    this.batt = Math.max(0, Math.min(100, this.batt + rate / 3600));
    if (this.ui) this.ui.battShow(this.batt);
    V.battOK = 1; V.battPct = this.batt; V.battV = pctToV(this.batt) + (this.usb ? 0.08 : 0);
    V.battSrc = this.usb ? 1 : 2; V.battRate = rate; V.battSlopeOk = 1;
    if (this.usb) { V.battRateAvg = rate; if (!V.battAvgSince) V.battAvgSince = Math.max(1, this.t - 300000) >>> 0; } else { V.battAvgSince = 0; V.battRateAvg = NaN; }
    // empty: 12 reads at or below 3.30 V (5 s apart in the firmware: here a minute's worth), then the shutdown
    if (!this.usb && V.battV <= 3.301) { this.emptyN++; } else this.emptyN = 0;
    if (this.emptyN >= 60 && !this.emptyAt) { this.emptyAt = this.t; this.W.str('BATTERY EMPTY'); this.W.X.vmr_msg(60000); if (this.slm.armed) this.slmArm(false);
      if (V.logging) this.logEv('BATTERY EMPTY (3.30 V): SHUTDOWN'); this.say('Battery empty: shut down. Charge it, then switch it off and on.'); }
    if (this.emptyAt && this.t - this.emptyAt > 8000) { this.recStop(); this.dead = true; this.emptyAt = 0; }
  }
  // ---- what the firmware asked for (events, requests it leaves for the other task)
  handleEvents() {
    const V = this.V;
    for (const e of this.W.events()) {
      if (e === 'slm:arm') this.slmArm(true); else if (e === 'slm:disarm') this.slmArm(false);
      else if (e === 'alarm:ctl') { if (this.dash) this.dash.alCtl = true; }
      else if (e === 'wifi:off') this.say('Wi-Fi OFF (screen button)'); else if (e === 'wifi:on') this.say('Wi-Fi ON (screen button)');
      else if (e === 'wifi:restart') { this.say('Wi-Fi restart'); this.wifiDownUntil = this.t + 4000; }
      else if (e === 'gps:off') this.say('GPS off (standby)'); else if (e === 'gps:on') this.say('GPS on (hot start)');
      else if (e === 'logger:off') { this.say('Datalogger mode OFF'); if (V.logging) this.logEv('DATALOGGER OFF'); }
    }
    const r = V.reqMarkBtn;
    if (r) { V.reqMarkBtn = 0; if (r === 1) { this.addMark('M'); this.toast('M'); } else {
      const S = this.slm; if (S.armed && S.open) this.slmCastEnd('Mark button'); else this.castToggle(); } }
    if (V.logWanted !== V.logging) { if (V.logWanted) { this.recStart(this.t, 'demo'); } else this.recStop(); }
  }
  // ---- the power switch: off, and on again (the boot screens)
  setPower(on) {
    if (on === this.power) return;
    this.power = on;
    if (!on) { this.recStop(); return; }
    if (this.batt <= 0.2 && !this.usb) { this.dead = true; return; }      // nothing left: it doesn't start
    this.dead = false; this.bootT = this.t; this.emptyN = 0; this.chargeOff = false; this.V.upBase = this.t >>> 0;
    const V = this.V; V.paused = 0; V.loggerMode = 0; V.cfmAct = 255; V.holdShow = 0; V.infoOn = 0; V.wifiOffByBtn = 0; V.serOledOn = 0; V.msgUntil = 0;
    V.screen = 19; this.slmArm(false); this.say('[boot] power-on'); this.bootUntil = this.t + 6500;
  }
}
if (typeof module !== 'undefined') module.exports = { Sim, SCEN, pctToV };
