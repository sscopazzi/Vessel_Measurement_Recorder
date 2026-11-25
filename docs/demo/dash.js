// ---- the dashboard's side: the VMR's web server, answered from the simulator (the page itself is the firmware's web/index.html)
class Dash {
  constructor(sim) {
    this.sim = sim;
    this.set = { gmax: 10, gmin: -10, gsecs: 60, sogavg: 10, sogdp: 1, zd: 0, rotdb: 3, pmax: 5, pmin: -5, beam: SHIP.beam, draft: SHIP.draft,
      len: SHIP.len, lbp: SHIP.lbp, dfwd: 10.2, daft: 10.8, dmid: 0, kdeck: 18.4, kbr: SHIP.kbr, ktop: 0, pwl: 0, cb: 0, cwp: 0, tpi: 0,
      disp: 0, gmlc: 0, vx: null, vy: null, vz: 0, rthr: 10, cond: 4, dockavg: 60, clm: 40, imx: 0, imz: 0, imf: 0, ledp: [1, 2, 3],
      vc: 2, vct: '', condt: '', tow: 0, towt: '', towl: 0, bt: 0, btt: '', blu: 0, btu: 0, bl: null, bw: null, bdf: null, bdm: null, bda: null, bton: null, voy: 'DEMO-01' };
    this.ext = { en: 0b101111, addr: [0x77, 0x44, 0x20, 0x48, 0x49], src: [0, 1, 0, 1, 0] };
    this.xb = [1, 2, 3, 4, 5]; this.xbs = 10; this.lcdcol = 3; this.mix = [255, 96, 0]; this.dark = 0;
    this.charts = []; this.chRev = 1;            // chart packs loaded in the demo (kept in this browser tab only)
    // routes (GPX text by name), one active: a coastal route the simulated ship is on, active from the start
    const W = [['Los Angeles approach', 33.70, -118.27], ['Point Vicente', 33.70, -118.45], ['Anacapa Passage', 34.05, -119.40], ['Point Conception', 34.32, -120.50],
      ['Point Arguello', 34.62, -120.80], ['Piedras Blancas', 35.62, -121.40], ['Point Sur', 36.28, -122.03], ['Monterey Bay', 36.75, -122.25], ['San Francisco approach', 37.76, -122.65]];
    this.routes = { 'LA-to-SF-coastal': '<?xml version="1.0" encoding="UTF-8"?>\n<gpx version="1.1" creator="VMR" xmlns="http://www.topografix.com/GPX/1/1"><rte><name>LA to SF coastal</name>' +
      W.map(([n, la, lo]) => '<rtept lat="' + la + '" lon="' + lo + '"><name>' + n + '</name></rtept>').join('') + '</rte></gpx>\n' };
    this.rtAct = 'LA-to-SF-coastal'; this.rtRev = 1;
  }
  // ---- chart packs (.vmrchart): read as the VMR reads them (CHARTS in VMR.ino)
  chartParse(buf, f) {
    if (buf.byteLength < 64) return null;
    const v = new DataView(buf), u32 = o => v.getUint32(o, true), f32 = o => v.getFloat32(o, true);
    const h = { tiles: u32(8), idx: u32(12), mo: u32(16), ml: u32(20), w: f32(24), s: f32(28), e: f32(32), n: f32(36),
      minz: v.getUint8(40), maxz: v.getUint8(41), px: v.getUint16(42, true), built: u32(44), chart: u32(48), data: u32(52), flen: u32(56), lib: u32(60) };
    if (String.fromCharCode(...new Uint8Array(buf, 0, 4)) !== 'VMRC' || v.getUint16(4, true) !== 1 || v.getUint16(6, true) !== 64 || h.flen !== buf.byteLength
      || !h.tiles || h.idx + 16 * h.tiles > h.data || h.minz > h.maxz) return null;
    const L = h.lib, lf = L >= h.data && L + 16 <= h.flen && String.fromCharCode(...new Uint8Array(buf, L, 4)) === 'VMRL'
      && u32(L + 4) > 0 && u32(L + 8) > 0 && L + 16 + u32(L + 4) + u32(L + 8) <= h.flen;   // it carries Leaflet
    let t = f; try { t = JSON.parse(new TextDecoder().decode(new Uint8Array(buf, h.mo, h.ml))).title || f; } catch (e) {}
    return { f, t, h, v, buf, lf };
  }
  chartTile(z, x, y) {
    const lon = (x, z) => x / 2 ** z * 360 - 180, lat = (y, z) => 180 / Math.PI * Math.atan(Math.sinh(Math.PI * (1 - 2 * y / 2 ** z)));
    for (let up = 0; up <= z; up++) {
      const zz = z - up, xx = x >> up, yy = y >> up, w = lon(xx, zz), e = lon(xx + 1, zz), n = lat(yy, zz), s = lat(yy + 1, zz);
      const key = BigInt(zz) << 48n | BigInt(xx) << 24n | BigInt(yy);
      for (const p of this.charts) {
        const h = p.h; if (zz < h.minz || zz > h.maxz || e <= h.w || w >= h.e || n <= h.s || s >= h.n) continue;
        let a = 0, b = h.tiles - 1;
        while (a <= b) { const m = (a + b) >> 1, k = p.v.getBigUint64(h.idx + 16 * m, true);
          if (k === key) { const off = p.v.getUint32(h.idx + 16 * m + 8, true), len = p.v.getUint32(h.idx + 16 * m + 12, true), d = new Uint8Array(p.buf, off, len);
            const ty = d[0] === 0x52 && d[8] === 0x57 ? 'image/webp' : d[0] === 0xFF ? 'image/jpeg' : 'image/png';
            return { status: 200, type: ty, body: d, headers: { 'X-Tile-Z': String(zz) } }; }
          if (k < key) a = m + 1; else b = m - 1; }
      }
    }
    return { status: 404, type: 'text/plain', body: 'no chart here' };
  }
  async chartUp(fd) {
    const E = (t, c = 500) => ({ status: c, type: 'application/json', body: JSON.stringify({ error: t }) }), file = fd.get('f');
    if (this.sim.V.logging) return E('Stop recording first: charts load only while not recording');
    if (!file || !/^[A-Za-z0-9_-]{1,39}\.vmrchart$/.test(file.name)) return E('Not a .vmrchart file, or its name has spaces or symbols');
    const f = file.name.replace(/\.vmrchart$/, ''), p = this.chartParse(await file.arrayBuffer(), f);
    if (!p) return E('Not a whole VMR chart pack: download it again');
    this.charts = this.charts.filter(c => c.f !== f).concat(p).sort((a, b) => b.h.maxz - a.h.maxz || (a.h.e - a.h.w) * (a.h.n - a.h.s) - (b.h.e - b.h.w) * (b.h.n - b.h.s));
    this.chRev++; this.sim.say && this.sim.say('Charts: ' + f + ' loaded');
    return { status: 200, type: 'application/json', body: JSON.stringify({ saved: f }) };
  }
  // a fetch from the page: { status, type, body } (or null = no answer: the hotspot is off, the VMR is off or restarting)
  handle(method, url, body) {
    const s = this.sim, V = s.V, u = new URL(url, 'http://vmr.local/'), path = u.pathname, q = Object.fromEntries(u.searchParams);
    const f = typeof body === 'string' && body ? Object.fromEntries(new URLSearchParams(body)) : {};
    const form = body && typeof body === 'object' && typeof body.get === 'function';   // a FormData (from the page's frame: instanceof can't tell)
    if (!s.power || s.dead || s.chargeOff || s.bootT || V.wifiOffByBtn || V.loggerMode || (s.wifiDownUntil && s.t < s.wifiDownUntil)) return null;
    const J = o => ({ status: 200, type: 'application/json', body: JSON.stringify(o) }), T = (t, c = 200) => ({ status: c, type: 'text/plain', body: t });
    if (method === 'GET') {
      if (path === '/data') return J(this.data(q.sky === '1'));
      if (path === '/logo.svg') return { status: 200, type: 'image/svg+xml', body: LOGO_SVG };
      if (path === '/wave') { const W = s.W.ws(), K = []; for (let k = 4; k <= 80; k++) K.push(k); return J({ df: 1 / 128, k0: 4, z: K.map(k => +W[k].toPrecision(4)), p: K.map(k => +W[81 + k].toPrecision(4)), r: K.map(k => +W[162 + k].toPrecision(4)) }); }
      if (path === '/wx') { const span = +(q.s || 1200), fine = span <= 1200, src = fine ? s.wxF : s.wxC, dt = fine ? 2 : 30, n = Math.min(src.length, Math.floor(span / dt)), a = src.slice(-n);
        return J({ dt, span, p: a.map(w => +w.p.toFixed(2)), t: a.map(w => +w.t.toFixed(1)), h: a.map(w => +w.h.toFixed(1)) }); }
      if (path === '/windh') { const span = +(q.s || 1200), fine = span <= 1200, src = fine ? s.windF : s.windC, dt = fine ? 2 : 30, n = Math.min(src.length, Math.floor(span / dt)), a = src.slice(-n);
        return J({ dt, on: true, ad: a.map(w => w.ad), as: a.map(w => w.as), td: a.map(w => w.td), ts: a.map(w => w.ts) }); }
      if (path === '/devices') return J({ devs: s.devs.map(d => ({ mac: d.mac.map(x => x.toString(16).toUpperCase().padStart(2, '0')).join(':'), name: d.name, kind: d.kind, on: d.on, me: !!d.me, onU: d.onU || s.unix - 300, onAgo: 300, offU: 0, offAgo: 0 })) });
      if (path === '/dlstat') return J({ st: 0, seq: 0, n: '', got: 0, tot: 0, ms: 0, sd: 0, net: 0, err: '' });
      if (path === '/marks.csv') return { status: 200, type: 'text/csv', body: this.marksCsv() };
      if (path === '/track') { this.trkTick(); const since = +(q.since || 0), cut = s.unix - 259200;
        return T('t,lat_e7,lon_e7\n' + this.trk.filter(p => p.t > since && p.t >= cut).map(p => p.t + ',' + Math.round(p.la * 1e7) + ',' + Math.round(p.lo * 1e7)).join('\n') + '\n'); }
      if (path === '/slm') return J(this.slmJson());
      if (path === '/charts') return J({ sd: true, rec: !!V.logging, rev: this.chRev, free: 31800000000 - this.charts.reduce((a, c) => a + c.h.flen, 0),
        packs: this.charts.map(c => ({ f: c.f, t: c.t, b: [c.h.w, c.h.s, c.h.e, c.h.n], z: [c.h.minz, c.h.maxz], px: c.h.px, n: c.h.tiles, size: c.h.flen, built: c.h.built, chart: c.h.chart, lf: c.lf })) });
      if (path === '/chart') return this.chartTile(+q.z, +q.x, +q.y);
      if (path === '/routes') return J({ act: this.rtAct, rev: this.rtRev, sd: true, routes: Object.keys(this.routes).sort().map(n => ({ n, size: this.routes[n].length })) });
      if (path === '/route') return this.routes[q.n] != null ? { status: 200, type: 'application/gpx+xml', body: this.routes[q.n] } : { status: 404, type: 'text/plain', body: 'no such route' };
      if (path === '/lf.js' || path === '/lf.css') return null;   // the demo's Leaflet comes from unpkg
      if (path === '/files') {
        if (q.dir === 'p' || q.dir === 'a') return J({ sd: true, total: 31900000000, used: 5000000, files: [] });
        const fl = (V.logging ? [{ n: s.logName, s: s.bytes, a: false }] : []).concat(s.files.map(x => ({ n: x.n, s: x.s, a: false })));
        return J({ sd: true, total: 31900000000, used: fl.reduce((a, x) => a + x.s, 0), arch: 0, archB: 0, files: fl });
      }
      if (path === '/serial') { const n = s.serSeq, since = +(q.since || 0), L = s.serial, fr = Math.max(n - L.length, since > 0 && since <= n ? since : n - 24, 0);
        return J({ seq: n, from: fr, lines: L.slice(L.length - (n - fr)) }); }
      if (path === '/plot') return J({ state: 3, pct: 0, err: 'Plot from file is not part of the demo' });
      if (path === '/dl' || path === '/zip' || path === '/wtbdl' || path === '/wtbfiles') return T('Downloads are not part of the demo (no files on a card here)', 404);
      return T('not in the demo', 404);
    }
    // ---- POST
    if (path === '/set') { this.doSet(f); return J({ ok: true }); }
    if (path === '/log') {
      if (f.state === 'start') { if (!V.logging) { s.recStart(s.t, (f.tag || '').replace(/[^\w-]/g, '').slice(0, 23)); if (f.cond) this.set.cond = +f.cond; } }
      else s.recStop();
      return J({ on: !!V.logging });
    }
    if (path === '/pause') { V.paused = f.on === '1' ? 1 : 0; if (V.paused) V.pausedAtMs = s.t >>> 0; if (V.logging) s.logEv(V.paused ? 'PAUSED' : 'RESUMED'); return J({ paused: !!V.paused }); }
    if (path === '/mark') { const n = s.addMark('M'); s.toast('M'); return J({ n, fix: !V.gpsOff, saved: true }); }
    if (path === '/cast') { const op = s.openCast();
      if (f.act === 'start') { if (op >= 0) return { status: 409, type: 'application/json', body: JSON.stringify({ error: 'A cast is already started: End cast first' }) }; s.castToggle(); return J({ n: s.marks.length, fix: !V.gpsOff, saved: true }); }
      if (op < 0) return { status: 409, type: 'application/json', body: JSON.stringify({ error: 'No cast started' }) };
      s.castToggle(); return J({ n: op + 1, fix: true, saved: true }); }
    if (path === '/marknote') { const m = s.marks[+f.n - 1]; if (m) m.note = (f.note || '').slice(0, 80); s.mkRev++; return J({ saved: true }); }
    if (path === '/markdel') { s.marks.pop(); s.syncMarks(); return J({ saved: true }); }
    if (path === '/markclear') { s.marks = []; s.syncMarks(); return J({ ok: true }); }
    if (path === '/trackclear') { this.trkTick(); this.trk = []; return J({ ok: true }); }
    if (path === '/chartup') return form ? this.chartUp(body) : J({ error: 'No file' });
    if (path === '/routeup') { const file = form && body.get('f'); if (!file || !/^[A-Za-z0-9_-]{1,39}\.gpx$/.test(file.name)) return { status: 500, type: 'application/json', body: JSON.stringify({ error: 'Bad route name' }) };
      return file.text().then(t => { const n = file.name.replace(/\.gpx$/, ''); this.routes[n] = t; this.rtRev++; return J({ saved: n }); }); }
    if (path === '/routedel') { delete this.routes[f.n]; if (this.rtAct === f.n) this.rtAct = ''; this.rtRev++; return J({ ok: true }); }
    if (path === '/routeact') { if (f.n && this.routes[f.n] == null) return { status: 500, type: 'application/json', body: JSON.stringify({ error: 'No such route' }) }; this.rtAct = f.n || ''; this.rtRev++; return J({ ok: true }); }
    if (path === '/chartdel') { this.charts = this.charts.filter(c => c.f !== f.f); this.chRev++; return J({ ok: true }); }
    if (path === '/event') { s.logEv(f.note || ''); return J({ n: s.logEvs.length }); }
    if (path === '/gps') { if (f.action === 'off' || f.action === 'on') { V.gpsOff = f.action === 'off' ? 1 : 0; s.say(V.gpsOff ? 'GPS off (standby)' : 'GPS on'); } return J({ ok: true }); }
    if (path === '/zero') {
      if (f.action === 'nudge') { const d = +f.d * 0.1; if (f.axis === 'roll') V['P.rollOffset'] = +(V['P.rollOffset'] - d).toFixed(2); else V['P.pitchOffset'] = +(V['P.pitchOffset'] - d).toFixed(2); V['P.zeroApplied'] = 1; }
      else if (f.action === 'clear') { V['P.rollOffset'] = 0; V['P.pitchOffset'] = 0; V['P.zeroApplied'] = 0; s.W.str('Level zero CLEARED'); s.W.X.vmr_msg(1500); }
      else { V['P.rollOffset'] = s.roll; V['P.pitchOffset'] = s.pitch; V['P.zeroApplied'] = 1; s.W.str('Level zero SET'); s.W.X.vmr_msg(1500); }
      return J({ zero: !!V['P.zeroApplied'], zr: V['P.rollOffset'], zp: V['P.pitchOffset'] });
    }
    if (path === '/slmset') { this.slmSet(f); return T('ok'); }
    if (path === '/logger') { if (!V.logging) return { status: 409, type: 'application/json', body: JSON.stringify({ error: 'Not recording: press START LOGGING first' }) };
      V.loggerMode = 1; s.say('Datalogger mode ON'); if (V.logging) s.logEv('DATALOGGER ON, GPS standby between rows'); return J({ ok: true }); }
    if (path === '/restart') { s.recStop(); s.setPower(false); s.setPower(true); return J({ ok: true }); }
    if (path === '/wifirestart') { s.wifiDownUntil = s.t + 4000; return J({ ok: true }); }
    if (path === '/hello') { s.devs[0].name = f.name || ''; s.devs[0].kind = f.kind || s.devs[0].kind; s.syncDevs(); return J({ ok: true }); }
    if (path === '/phonetime' || path === '/i2creset' || path === '/bufsave' || path === '/dlcancel' || path === '/trimcal') return J({ ok: true });
    if (path === '/fwunlock' || path === '/ota' || path === '/fwrelay') return { status: 403, type: 'application/json', body: JSON.stringify({ error: 'Firmware updates are not part of the demo' }) };
    if (path === '/plot' || path === '/plotsave' || path === '/archive' || path === '/delete') return { status: 409, type: 'application/json', body: JSON.stringify({ error: 'Not part of the demo' }) };
    return T('not in the demo', 404);
  }
  doSet(f) {
    const s = this.sim, V = s.V, num = k => +f[k];
    if ('screen' in f && V.screen !== num('screen') && s.W.X.vmr_screen_avail(num('screen'))) { V.screen = num('screen'); V.oledBlank = 0; }
    if ('segscr' in f) V['P.segScreen'] = num('segscr');
    if ('lcdscr' in f) V['P.lcdScreen'] = num('lcdscr');
    if ('lcdcyc' in f) V['P.lcdCycle'] = num('lcdcyc');
    if ('lcdcol' in f) { V['P.lcdColor'] = num('lcdcol'); this.lcdcol = num('lcdcol'); }
    if ('lcdmix' in f) { this.mix = f.lcdmix.split(',').map(Number); V['P.lcdColor'] = 7; this.lcdcol = 7; }
    if ('flip' in f) V['P.oledFlip'] = num('flip');
    if ('segflip' in f) V['P.segFlip'] = num('segflip');
    if ('sega' in f) { V['P.dispEn'] = (num('disp') & 7) | (this.dark << 4); delete f.disp; }
    if ('dark' in f) { this.dark = num('dark'); V['P.dispEn'] = (V['P.dispEn'] & 7) | (this.dark << 4); }
    if ('on' in f && !('slm' in f)) V.displayOn = f.on === '1' ? 1 : 0;
    if ('briweb' in f) V['P.briWeb'] = f.briweb === '1' ? 1 : 0;                  // the slider, or the knob
    if ('pct' in f && V['P.briWeb']) V.contrast = Math.round((Math.max(1, Math.min(100, num('pct'))) * 255 + 50) / 100);
    if ('units' in f) { V['P.units'] = num('units') & 3; this.unitsDec = !!(num('units') & 4); }
    if ('scrrot' in f) V['P.scrRot'] = num('scrrot');
    if ('segrot' in f) V['P.segRot'] = num('segrot');
    if ('lcdrot' in f) V['P.lcdRot'] = num('lcdrot');
    if ('segcd' in f) V.reqSegCd = num('segcd');
    if ('serol' in f) V.serOledOn = f.serol === '1' ? 1 : 0;
    if ('atdock' in f) { V['P.atDock'] = num('atdock'); if (V.logging) s.logEv(num('atdock') ? 'AT DOCK' : 'UNDERWAY'); }
    if ('resetmax' in f) { s.allMax = { rp: 0, rn: 0, pp: 0, pn: 0, yr: 0, trp: 0, trn: 0, tpp: 0, tpn: 0, tyr: 0 }; s.W.str('All-time max RESET'); s.W.X.vmr_msg(1500); }
    if ('clinoreset' in f) s.clino = { s: 0, p: 0, su: 0, pu: 0 };
    if ('greset' in f) s.g = { pkS: 0, pkP: 0, pkF: 0, pkA: 0, hi: 1, lo: 1 };
    if ('ballbest' in f) V['P.ballBest10'] = 0;
    if ('ballrot' in f) V['P.ballRot'] = num('ballrot');
    if ('posfmt' in f) { const i = { dm: 0, d: 1, dms: 2 }[f.posfmt]; if (i !== undefined) V['P.posFmt'] = i; }
    if ('wxspan' in f) { const i = [1200, 300, 3600, 10800].indexOf(num('wxspan')); if (i >= 0) V['P.wxSpan'] = i; }
    if ('battah' in f && num('battah') >= 0 && num('battah') <= 100) { V['P.battAh10'] = Math.round(num('battah') * 10); V['P.battDrawMa'] = 0; }
    if ('imu' in f) { V['P.imuOff'] = f.imu === '0' ? 1 : 0; if (!V['P.imuOff']) V['P.logRate'] = 0; }
    if ('lograte' in f && !V.logging && (V['P.imuOff'] || f.lograte === '0')) V['P.logRate'] = num('lograte');
    if ('charge' in f && f.charge === '1') { s.recStop(); s.chargeOff = true; s.say('Charge mode: everything off, deep sleep. Button B or the screen button wakes the VMR.'); }
    if ('zd' in f) { V['P.zdHours'] = num('zd'); }
    if ('gsecs' in f) { const G = [30, 60, 120, 300, 600], i = G.indexOf(num('gsecs')); if (i >= 0) V['P.graphSecIdx'] = i; }
    if ('clm' in f) V['P.clinoMax'] = num('clm');
    if ('xb' in f) { this.xb = f.xb.split(',').map(Number); this.xbs = +(f.xbs || 0); V['P.xbScreen'] = this.xbs; }
    if ('exten' in f) { this.ext.en = num('exten'); }
    for (const k of Object.keys(f)) if (k in this.set && typeof this.set[k] !== 'object') this.set[k] = isNaN(+f[k]) || f[k] === '' ? (f[k] === '' ? null : f[k]) : +f[k];
    for (const k of ['gmax', 'gmin']) if (k in f) V[k === 'gmax' ? 'P.graphMax' : 'P.graphMin'] = num(k);
  }
  slmSet(f) {
    const s = this.sim, S = s.slm;
    if ('arm' in f) s.slmArm(f.arm === '1');
    if ('act' in f) { if (f.act === '1') s.slmCastStart('dashboard'); else if (f.act === '2') s.slmCastEnd('dashboard'); else if (f.act === '3' && S.iwb) { S.tare = 10.13 + 0.01 * Math.random(); s.slmEv('IWB tare ' + S.tare.toFixed(4) + ' dbar'); } }
    if ('nkon' in f) S.nkOn = f.nkon === '1';
    if ('nkplan' in f) { const v = f.nkplan.split(','); for (let i = 0; i < 6; i++) S.b[i].plan = v[i] === '' || v[i] === undefined ? NaN : +v[i]; }
    const B = (k, fn) => { if (k in f) fn(+f[k] - 1); };
    B('nkarm', i => { S.b[i] = { ...S.b[i], st: 1, src: 0, z: NaN, t: NaN, sp: NaN, rho: NaN, u: 0 }; });
    B('nksafe', i => { S.b[i] = { ...S.b[i], st: 0, src: 0, z: NaN, t: NaN, sp: NaN, rho: NaN, u: 0 }; });
    B('nkfire', i => s.nkFire(i, 1));
    if ('nkarmall' in f) S.b.forEach(b => { if (b.st === 0) b.st = 1; });
    if ('nksafeall' in f) S.b.forEach(b => { if (b.st === 1) b.st = 0; });
    if ('nkreset' in f) S.b.forEach(b => Object.assign(b, { st: 0, src: 0, z: NaN, t: NaN, sp: NaN, rho: NaN, u: 0 }));
  }
  n(v, d) { return v === null || v === undefined || isNaN(v) ? null : +(+v).toFixed(d); }
  slmJson() {
    const s = this.sim, S = s.slm, n = this.n.bind(this), q = s.prof(Math.max(0, S.depth));
    const lv = S.iwb ? { p: n(S.depth * 1.0057, 3), d: n(S.depth, 2), t: n(S.depth > 0.3 ? q.t : s.wx.t + 0.4, 3), ec: S.depth > 0.3 ? Math.round(q.sp * 1300 + q.t * 900) : null, sp: S.depth > 0.3 ? n(q.sp, 3) : null, rho: S.depth > 0.3 ? n(q.rho, 3) : null } : null;
    const nk = { en: S.nkOn, armed: S.armed, wtb: S.armed, on: S.nkOn, hw: S.armed ? 15 : 0, rdy: S.armed && S.rdy && S.pend < 0, held: S.btnDown.reduce((a, d, i) => a | (d ? 1 << i : 0), 0), pend: S.pend, cast: S.open,
                 cap: S.armed && S.iwb ? n(S.cap, 2) : null, vin: S.armed && S.iwb ? 12.08 : null,   // the IWB reports them once it is up
                  rin: S.rdy || S.capT === undefined ? 0 : n(s.capNow().inS, 1), rch: 0.72, gap: 0.5, spd: 0.2,
                 b: S.b.map(b => [b.st, b.src, isNaN(b.plan) ? null : b.plan, n(b.z, 2), n(b.t, 3), n(b.sp, 3), n(b.rho, 3), b.u || null]),
                 ev: S.ev.filter(e => /Bottle/.test(e.t)).slice(0, 10) };
    return { slm: { on: true, armed: S.armed, dis: false, paired: true, pairing: false, wtb: S.armed, state: S.state, manual: false, toDraft: false, idle: true, eth: S.eth, iwb: S.iwb, cast: S.open ? S.castN : 0, castN: S.castN,
                    sd: S.armed, sdMB: 29800, fw: ['1.11', '1.11'], v: S.armed ? S.v : null, dew: S.armed ? 4.2 : null, tubeT: S.armed ? 13.1 : null, tubeRH: S.armed ? 41 : null, tare: S.armed ? n(S.tare, 4) : null, cpu: S.armed ? 38 : null,
                    lv, evRev: S.ev.length, eth12: { on: S.armed, up: S.eth }, nk },
             trace: S.trace, ev: S.ev, reply: '', cd: S.cd, prof: S.prof, fwr: { st: 0, pct: 0, t: '' } };
  }
  marksCsv() {
    const s = this.sim, HDR = 'mark,type,local_time,zd,utc_time,lat,lon,lat_deg,lon_deg,cog_degT,sog_kn,pressure_hpa,air_temp_c,rh_pct,gps_sats,gps_hdop,gps_fix_age_s,end_local_time,end_utc_time,end_lat,end_lon,end_lat_deg,end_lon_deg,duration_s,drift_m,note';
    const zd = s.V['P.zdHours'], iso = (u, z) => { const d = new Date((u - (z ? 0 : zd * 3600)) * 1000); return d.toISOString().slice(0, 19) + (z ? 'Z' : ''); };
    const dm = (e7, p, ng) => { const a = Math.abs(e7) / 1e7, d = Math.floor(a), m = (a - d) * 60; return d + ' ' + m.toFixed(4).padStart(7, '0') + ' ' + (e7 < 0 ? ng : p); };
    let o = HDR + '\n';
    s.marks.forEach((m, i) => {
      const ty = m.kind === 'M' ? 'mark' : m.kind === 'O' ? 'cast_open' : 'cast', fx = m.fix;
      const end = m.kind === 'C' ? [iso(m.endUnix), iso(m.endUnix, 1), dm(m.ela, 'N', 'S'), dm(m.elo, 'E', 'W'), (m.ela / 1e7).toFixed(7), (m.elo / 1e7).toFixed(7), m.dur, Math.round(m.drift)] : ['', '', '', '', '', '', '', ''];
      o += [i + 1, ty, iso(m.unix), (zd >= 0 ? '+' : '') + zd, iso(m.unix, 1), fx ? dm(m.la, 'N', 'S') : '', fx ? dm(m.lo, 'E', 'W') : '', fx ? (m.la / 1e7).toFixed(7) : '', fx ? (m.lo / 1e7).toFixed(7) : '',
            fx && m.sog > 0.3 ? Math.round(m.cog) : '', fx ? m.sog.toFixed(1) : '', m.pa.toFixed(2), m.ta.toFixed(2), m.rh.toFixed(1), fx ? 10 : '', fx ? '0.84' : '', fx ? '0.4' : '', ...end, '"' + m.note.replace(/"/g, '""') + '"'].join(',') + '\n';
    });
    return o;
  }
  // the track (the firmware's TRACK section): a point every 30 s while moving, 5 min while still. The demo starts with
  // 72 h behind the ship (made up: its course run backwards with slow turns, a 2-h station stop 30 h ago, and no fix
  // for 25 min 3 h ago and for 1 h 50 h ago, which the map shows as gaps)
  trkTick() {
    const s = this.sim;
    if (!this.trk) { this.trk = []; const D2R = Math.PI / 180, v = Math.max(s.p.sog || 0, 6) * 0.5144, c0 = (s.p.cog || 0) * D2R, H = 3600;
      let la = s.lat, lo = s.lon, t = s.unix, still = 0;
      while (t > s.unix - 72 * H) {
        const ago = s.unix - t, stop = ago > 30 * H && ago < 32 * H, dt = stop ? 300 : 30;
        if (!stop) { const c = c0 + 0.35 * Math.sin(ago / 9000) + 0.6 * Math.sin(ago / 61000);
          la -= v * dt * Math.cos(c) / 111320; lo -= v * dt * Math.sin(c) / (111320 * Math.cos(la * D2R)); }
        t -= dt;
        const noFix = (ago > 3 * H - 1500 && ago < 3 * H) || (ago > 50 * H && ago < 51 * H);
        if (!noFix) this.trk.unshift({ t, la, lo });
      }
      this.trkLast = s.unix; }
    const last = this.trk[this.trk.length - 1], dt = s.unix - (last ? last.t : 0);
    if (s.V['gps.gpsFix'] && (dt >= 300 || dt >= 30 && (s.p.sog || 0) > 0.03)) this.trk.push({ t: s.unix, la: s.lat, lo: s.lon });
  }
  data(sky) {
    this.trkTick();
    const s = this.sim, V = s.V, p = s.p, n = this.n.bind(this), gpsOn = !V.gpsOff, w = s.wxNow || s.wxAt(0), win = s.win || { rM: 0, rm: 0, pM: 0, pm: 0, yM: 0 }, A = s.allMax;
    const onUsb = s.usb, rate = V.battRate, full = onUsb && s.batt >= 99.5, hLeft = full ? null : onUsb ? (100 - s.batt) / 8 : s.batt / -rate;
    const bSt = full ? 2 : onUsb ? 1 : 3;                     // battEst: 1 charging, 2 full, 3 on the battery
    const wv = !V['P.atDock'] && !V['P.imuOff'], imu = !V['P.imuOff'];
    const graph = this.graph();
    const lt = u => s.ltStr(u);
    const op = s.openCast();
    const lcd = s.W.lcd().map(r => r.map(c => c === 0xDF ? '°' : String.fromCharCode(c)).join(''));
    const seg = s.W.seg();
    const d = {
      build: 'Nov 21 2026 16:00:00', ver: FW_VER, unix: s.unix, src: gpsOn ? 'GPS' : 'RTC', gpsTime: gpsOn, clino: [n(s.clino.p, 1), n(s.clino.s, 1)],
      gmet: { f: n(V.gFeltF, 3), s: n(V.gFeltS, 3), v: n(V.gVert, 3), pk: [n(s.g.pkF, 2), n(s.g.pkA, 2), n(s.g.pkS, 2), n(s.g.pkP, 2), n(s.g.hi, 2), n(s.g.lo, 2)] },
      dock: { trim: n(V.trimFt * 0.3048, 3), trimNow: n(V.trimFt * 0.3048, 3), ang: n(s.pitch, 3), list: n(s.roll, 2), listNow: n(s.roll, 2), L: SHIP.lbp, n: 60, cal: 0, win: 60, fill: 60 },
      paused: !!V.paused, pausedS: V.paused ? Math.floor((s.t - V.pausedAtMs) / 1000) : 0, otaMax: 1966080, fwSize: 1251328, serOled: !!V.serOledOn,
      battV: n(V.battV, 3), battPct: n(s.batt, 0), cpuT: n(V.cpuTempC, 1), wifi: { rssi: s.devs[0].on ? [-58] : [], me: s.devs[0].on ? -58 : null, ssid: V.apSsid, ch: 6 },
      gm: wv ? n(V.gmEst, 2) : null, gmC: n(s.gmC, 3), gmAvg: wv ? n(V.gmEst * 0.985, 2) : null, gmN: wv ? 73 : 0, gmLeft: 0,
      rs: s.rs ? [{ min: 10, n: Math.round(600 / p.tr), tp: n(s.rs.tp, 1), sig: n(s.rs.sig, 1), max: n(s.rs.max, 1), mean: n(s.rs.mean, 1), rms: n(s.rs.rms, 1), ovS: 0, ovP: 0 },
                  { min: 30, n: s.rs.n, tp: n(s.rs.tp, 1), sig: n(s.rs.sig, 1), max: n(s.rs.max, 1), mean: n(s.rs.mean, 1), rms: n(s.rs.rms, 1), ovS: s.rs.ovS, ovP: s.rs.ovP }] : [],
      battRate: n(rate, 1), charge: onUsb, fix: gpsOn, sats: gpsOn ? 10 : 0, inView: gpsOn ? 16 : 0, snr: gpsOn ? 41 : 0, gpsAge: 0,
      gpsDiag: { gp: 11, gl: 5, nmea: Math.floor(s.t / 200), nmeaHz: gpsOn ? 5.0 : 0, ant: 2, fixAge: gpsOn ? 0.4 : null, restarts: 0 }, fixQ: gpsOn ? 2 : 0, gpsHz: 1,
      lat: gpsOn ? +s.lat.toFixed(7) : null, lon: gpsOn ? +s.lon.toFixed(7) : null, alt: 18.4, sog: gpsOn ? n(p.sog, 1) : null, sogMs: gpsOn ? n(p.sog * KN, 2) : null, cog: gpsOn ? Math.round(p.cog) : null,
      sogAvg: gpsOn ? n(V.sogAvgKn, 1) : null, cogAvg: gpsOn && p.sog >= 0.3 ? n(V.cogAvgDeg, 2) : null, hdop: 0.84,
      roll: imu ? n(s.roll - V['P.rollOffset'], 1) : null, pitch: imu ? n(s.pitch - V['P.pitchOffset'], 1) : null, yaw: n(s.hdg, 1), rot: imu ? Math.round(s.yawRate) : null, mh: n(V.magHdg, 1), mvar: 13.5, mvs: gpsOn ? 2 : 0, mdev: gpsOn && p.sog > 3 ? 0.4 : null, mt: 47.3, md: 58, ma: 2,
      rollT: imu && V.rollPeriod === V.rollPeriod ? n(V.rollPeriod, 1) : null, rollTn: 6,
      m1: { rMax: n(win.rM, 1), rMin: n(win.rm, 1), pMax: n(win.pM, 1), pMin: n(win.pm, 1), yr: Math.round(win.yM) },
      all: { rMax: n(A.rp, 1), trMax: A.trp ? lt(A.trp) : '', rMin: n(A.rn, 1), trMin: A.trn ? lt(A.trn) : '', pMax: n(A.pp, 1), tpMax: A.tpp ? lt(A.tpp) : '', pMin: n(A.pn, 1), tpMin: A.tpn ? lt(A.tpn) : '', yr: Math.round(A.yr), tyr: A.tyr ? lt(A.tyr) : '' },
      zero: !!V['P.zeroApplied'], zr: n(V['P.rollOffset'], 2), zp: n(V['P.pitchOffset'], 2), zref: 0, zrefNow: 0, zrefTrim: 0, zrefHs: 0, sd: true,
      log: { cond: ['', 'docked', 'ballast (95%)', 'ballast (98%)', 'loaded (CP)', 'loaded (LB)'][this.set.cond] || '', ev: V.logging ? (s.logEvs || []).slice(0, 20) : [], on: !!V.logging, file: V.logging ? s.logName : '',
             sec: V.logging ? Math.floor((s.t - V.logStartMs) / 1000) : 0, rows: V.logging ? s.rows : 0, bytes: V.logging ? s.bytes : 0, drop: 0, gaps: 0, gapMax: 0, gapAt: 0, gapSt: '', lw: 31, lwAt: 0, lws: '', lwb: '',
             cpu: 'loop 31%, web 6%, sd 2%', rw: 0, rwB: 0, chk: 0, intr: false, gpsWait: false, daily: false, buf: V.logging ? 0 : 1200, bs: 0, bsn: '', bse: '', bss: 0, rate: V['P.imuOff'] ? V['P.logRate'] : 0, next: -1 },
      units: V['P.units'] | (this.unitsDec ? 4 : 0), bAh: V['P.battAh10'] / 10, bH: hLeft !== null && isFinite(hLeft) && hLeft > 0 ? n(hLeft, 1) : null, bMa: full ? 0 : Math.round(Math.abs(rate) * V['P.battAh10']), bSt, bEarly: false, bSet: true, bLow: !onUsb && s.batt < 20,
      disp: { ok: true, on: !!V.displayOn, scr: V.screen, pct: Math.round(V.contrast / 2.55), src: V['P.briWeb'] ? 'web' : 'pot', flip: V['P.oledFlip'], sflip: V['P.segFlip'], brot: V['P.ballRot'] & 3,
              bbest: V['P.ballBest10'], av: this.scrAv(), en: V['P.dispEn'] & 7, dark: V['P.dispEn'] >> 4, rot: V['P.scrRot'],
              seg: { ok: true, rot: V['P.segRot'], a: 0x70, scr: V['P.segScreen'], av: this.segAv(), flip: V['P.segFlip'], ...(V.cdSet ? { cd: Math.max(0, Math.ceil((V.cdEndMs - s.t) / 1000)), cdt: V.cdTotalS } : {}) },
              lcd: { ok: true, rot: V['P.lcdRot'], a: 0x20, scr: V['P.lcdScreen'], cyc: V['P.lcdCycle'], col: this.lcdcol, mix: this.mix, rgb: [4, 5, 6], av: 0b1111111111, txt: lcd } },
      set: { ...this.set, wxs: [1200, 300, 3600, 10800][V['P.wxSpan']] || 1200, pf: V['P.posFmt'], zd: V['P.zdHours'], gsecs: [30, 60, 120, 300, 600][V['P.graphSecIdx']], gmax: V['P.graphMax'], gmin: V['P.graphMin'], clm: V['P.clinoMax'] || 40 },
      reset: 'power-on', crash: '', i2cMiss: '', i2cNote: '',
    };
    d.wind = { on: true, ok: true, baud: 38400, tryb: 38400, n: Math.floor(s.t / 1000), bad: 0, rate: 0, y: 0.0, held: null, off: 0.0, z: 0.0, mc: true, mcv: 0.4,
               aa: Math.round(s.awa), as: n(s.aws, 1), a2: Math.round(s.wind2 ? s.wind2.a2 : s.awa), s2: n(s.wind2 ? s.wind2.s2 : s.aws, 1), g10: n(s.wind2 ? s.wind2.g10 : s.aws, 1),
               tr: gpsOn ? Math.round((s.twd - s.hdg + 360) % 360) : null, td: gpsOn ? Math.round(s.twd) : null, ts: gpsOn ? n(s.tws, 1) : null, sog: gpsOn ? n(p.sog, 1) : null };
    const E = this.ext, ok = E.en & 0b1111;
    d.ext = { en: E.en, ok, addr: E.addr, src: E.src, xb: this.xb, xbs: this.xbs, xbok: this.xb.map(v => v ? 1 : 0), scan: '', p: n(w.p, 2), tend: n(V.presTendSim, 1), tendH: 3.0, p5: n(w.p, 2), ta5: n(w.t, 2), rh5: n(w.h, 1), m5: 5.0,
              tb: n(w.t + 9.2, 1), ta: n(w.t, 1), rh: n(w.h, 1), pcf: 0b11111110, adc: [[Math.round(s.ui ? s.ui.knob * 255 : 128), 128, 255, 0, 40, 80, 160, 200], null] };
    const L = ['', '', ''], lo = [V['ledOut[0]'], V['ledOut[1]'], V['ledOut[2]']];
    d.leds = { s: [V.castOpenNow ? 'on (cast open)' : 'off', !V.logging ? 'off (not recording)' : V.paused ? 'slow blink (paused)' : 'on (recording)', V.faultLed ? 'FAULT in Morse' : 'off'], why: V.faultNow, sw: true, pct: Math.round(V.contrast / 2.55) };
    d.imuOff = !imu; d.imuOk = imu; d.atDock = V['P.atDock']; d.gpsOff = !!V.gpsOff; d.upS = Math.floor((s.t - V.upBase) / 1000); d.imuWrX = 1;
    d.wave = wv ? { n: 2048, need: 1024, dt: 500, te: n(V.wvTe, 1), tz: n(V.wvTz, 1), trs: n(V.wvTrs, 1), hs: n(V.wvHs, 2), hsm: n(V.wvHsMid, 2), tzm: n(V.wvTzMid, 1), cur: true, age: 12,
                       hsb: n(V.wvHsB, 2), hsbm: n(V.wvHsBMid, 2), tzb: n(V.wvTz, 1), bn: 2048 }
                  : { n: 0, need: 1024, dt: 500, te: null, tz: null, trs: null, hs: null, cur: false, age: -1 };
    d.gmNear = wv && Math.abs(p.tr - p.te) / p.te < 0.15; d.boxT = n(w.t + 9.2, 1);
    Object.assign(d, graph);
    if (sky && s.sky && gpsOn) d.sky = s.sky.map(x => [...x]);
    d.mk = { n: s.marks.length, rev: s.mkRev || 1, ok: true, f: s.marks.length ? ztime(s.unix0) + '_location_marks.csv' : '', open: op >= 0 ? op + 1 : 0 };
    if (op >= 0) { const m = s.marks[op], dm = s.dist(m.la, m.lo, Math.round(s.lat * 1e7), Math.round(s.lon * 1e7)), hm = s.ltStr(m.unix).slice(11, 16).replace(':', '');
      d.mk.cast = { n: op + 1, hm, t: s.unix - m.unix, sf: true, dm: Math.round(dm), db: Math.round(this.brg(m.la, m.lo, s.lat * 1e7, s.lon * 1e7)) }; }
    d.slm = this.slmJson().slm; d.bType = 0; d.bChem = 0; d.bA = null;
    d.heap = { free: 32800, big: 24500, min: 25600, pfree: 1890000, pbig: 1830000 };
    d.web = { dB: 11, dS: 84, dKB: 6420, dBr: 38, dSr: 910, dBb: 80, dSb: 3120, pS: 2140, pSb: 6050, pN: 1, wR: 15, wB: 120, dCut: 0, pCut: 0, pMem: 0 };
    d.trkN = this.trk.length; d.chRev = this.chRev; d.rtAct = this.rtAct; d.rtRev = this.rtRev;
    return d;
  }
  scrAv() { let m = 0; for (let i = 0; i < 23; i++) if (this.sim.W.X.vmr_screen_avail(i)) m |= 1 << i; return m >>> 0; }
  brg(a1, o1, a2, o2) { const k = Math.PI / 180e7, p1 = a1 * k, p2 = a2 * k, dl = (o2 - o1) * k; const b = Math.atan2(Math.sin(dl) * Math.cos(p2), Math.cos(p1) * Math.sin(p2) - Math.sin(p1) * Math.cos(p2) * Math.cos(dl)) * 180 / Math.PI; return b < 0 ? b + 360 : b; }
  segAv() { const V = this.sim.V, imu = !V['P.imuOff'], gps = !V.gpsOff; let m = 0; const set = (i, v) => { if (v) m |= 1 << i; };
    [imu, imu, imu, gps, gps, 1, 1, 1, 1, 1, 1, imu, imu, V.cdSet, imu && !V['P.atDock']].forEach((v, i) => set(i, v)); return m; }
  // the dashboard's graph: the last gsecs at 2 Hz, in min / max pairs when longer than 120 points (as the firmware sends it)
  graph() {                                              // as handleData: the chosen length, min / max pairs per column
    const s = this.sim, V = s.V, secs = [30, 60, 120, 300, 600][V['P.graphSecIdx']] || 60, nS = secs * 2, k = Math.floor((nS + 119) / 120);
    const H = s.gHist || [], avail = Math.min(H.length, nS), nB = Math.ceil(avail / k);
    const ser = idx => { const out = []; for (let bk = 0; bk < nB; bk++) { let lo = Infinity, hi = -Infinity;
      for (let age = (nB - bk) * k - 1; age >= (nB - 1 - bk) * k; age--) { if (age >= H.length) continue; const v = H[H.length - 1 - age][idx]; if (v === null || isNaN(v)) continue; lo = Math.min(lo, v); hi = Math.max(hi, v); }
      if (lo > hi) { out.push(null); if (k > 1) out.push(null); continue; } out.push(+lo.toFixed(2)); if (k > 1) out.push(+hi.toFixed(2)); } return out; };
    return { gSecs: secs, gK: k, gB: Math.floor(nS / k), gDt: +(k * 0.5).toFixed(2), gRoll: ser(0), gPitch: ser(1) };
  }
}
if (typeof module !== 'undefined') module.exports = { Dash };
