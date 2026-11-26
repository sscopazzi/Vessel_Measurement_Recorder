// SPDX-FileCopyrightText: 2026 Aspect Wave LLC
// SPDX-License-Identifier: GPL-3.0-or-later
// ---- the box: its buttons (press, or latch with "hold"), knob, switches, and what its displays and lights show
(function () {
  const $ = id => document.getElementById(id);
  const bytes = Uint8Array.from(atob(WASM_B64), c => c.charCodeAt(0));
  const W = vmrWasm(bytes), V = W.V;
  const sim = new Sim(W), dash = new Dash(sim); sim.dash = dash;
  window.VMRDEMO = { handle: (m, u, b) => dash.handle(m, u, b), sim, W };

  // ---- buttons: pointer down / up, keyboard, and the hold latch beside each
  const down = {}, latched = {}, since = {};
  const units = [...document.querySelectorAll('.bu[data-btn]')];
  // a tap shorter than 80 ms is held to 80 ms, so the firmware's 20 ms loop always sees it
  const setDown = (k, v) => { if (!v && down[k] && performance.now() - since[k] < 80) { setTimeout(() => { if (!latched[k]) setDown(k, false); }, 80 - (performance.now() - since[k])); return; }
    if (!!down[k] === !!v) return; down[k] = !!v; since[k] = v ? performance.now() : 0; const u = document.querySelector('.bu[data-btn="' + k + '"] .hw'); if (u) u.classList.toggle('down', !!v); };
  const isDown = k => !!down[k] || !!latched[k];
  for (const u of units) {
    const k = u.dataset.btn, b = u.querySelector('.hw'), h = u.querySelector('.hold');
    const on = e => { e.preventDefault(); b.setPointerCapture && e.pointerId !== undefined && b.setPointerCapture(e.pointerId); setDown(k, true); };
    const off = () => { if (!latched[k]) setDown(k, false); };
    b.addEventListener('pointerdown', on); b.addEventListener('pointerup', off); b.addEventListener('pointercancel', off); b.addEventListener('lostpointercapture', off);
    b.addEventListener('keydown', e => { if ((e.key === ' ' || e.key === 'Enter') && !e.repeat) { e.preventDefault(); setDown(k, true); } });
    b.addEventListener('keyup', e => { if (e.key === ' ' || e.key === 'Enter') { e.preventDefault(); off(); } });
    b.addEventListener('contextmenu', e => e.preventDefault());
    h.addEventListener('click', () => { latched[k] = !latched[k]; h.setAttribute('aria-pressed', latched[k] ? 'true' : 'false'); setDown(k, latched[k]); });
    h.title = 'Keep this button down until you click again (so you can press another one with it)';
  }
  // ---- the knob: drag, wheel, arrow keys (0-100 %)
  const knob = $('knob'); let kv = 0.5, drag = null;
  const knobSet = v => { kv = Math.max(0, Math.min(1, v)); knob.style.transform = 'rotate(' + (-135 + 270 * kv) + 'deg)'; $('knobv').textContent = Math.round(kv * 100) + ' %'; knob.setAttribute('aria-valuenow', Math.round(kv * 100)); };
  knob.addEventListener('pointerdown', e => { drag = { y: e.clientY, x: e.clientX, v: kv }; knob.setPointerCapture(e.pointerId); knob.style.cursor = 'grabbing'; });
  knob.addEventListener('pointermove', e => { if (drag) knobSet(drag.v + ((drag.y - e.clientY) + (e.clientX - drag.x)) / 220); });
  knob.addEventListener('pointerup', () => { drag = null; knob.style.cursor = ''; });
  knob.addEventListener('wheel', e => { e.preventDefault(); knobSet(kv - Math.sign(e.deltaY) * 0.02); }, { passive: false });
  knob.addEventListener('keydown', e => { const d = { ArrowUp: 0.02, ArrowRight: 0.02, ArrowDown: -0.02, ArrowLeft: -0.02, PageUp: 0.1, PageDown: -0.1, Home: -1, End: 1 }[e.key]; if (d !== undefined) { e.preventDefault(); knobSet(kv + d); } });
  knobSet(0.5);
  const sw = (id, fn) => { const b = $(id); b.addEventListener('click', () => { const v = b.getAttribute('aria-checked') !== 'true'; b.setAttribute('aria-checked', v ? 'true' : 'false'); fn(v); }); };
  sw('knobsw', v => { V.knobSw = v ? 1 : 0; });
  sw('power', v => sim.setPower(v));

  // ---- the Niskin buttons with their rings
  const nk = $('nk');
  for (let i = 0; i < 6; i++) nk.insertAdjacentHTML('beforeend', '<div class="bu" data-btn="nk' + i + '"><div class="ring" id="ring' + i + '"><button class="hw sm" aria-label="Niskin bottle ' + (i + 1) + '">' + (i + 1) + '</button></div><button class="hold" aria-pressed="false">hold</button><div class="cap" id="nkc' + i + '"></div><div class="t"></div></div>');
  for (const u of nk.querySelectorAll('.bu')) units.push(u);
  for (const u of nk.querySelectorAll('.bu')) {
    const k = u.dataset.btn, b = u.querySelector('.hw'), h = u.querySelector('.hold');
    const on = e => { e.preventDefault(); b.setPointerCapture && b.setPointerCapture(e.pointerId); setDown(k, true); }, off = () => { if (!latched[k]) setDown(k, false); };
    b.addEventListener('pointerdown', on); b.addEventListener('pointerup', off); b.addEventListener('pointercancel', off); b.addEventListener('lostpointercapture', off);
    b.addEventListener('keydown', e => { if ((e.key === ' ' || e.key === 'Enter') && !e.repeat) { e.preventDefault(); setDown(k, true); } });
    b.addEventListener('keyup', e => { if (e.key === ' ' || e.key === 'Enter') { e.preventDefault(); off(); } });
    h.addEventListener('click', () => { latched[k] = !latched[k]; h.setAttribute('aria-pressed', latched[k] ? 'true' : 'false'); setDown(k, latched[k]); });
  }
  sim.ui = { isDown, wtbDown: () => [0, 1, 2, 3, 4, 5].map(i => isDown('nk' + i)), get knob() { return kv; }, battShow: p => { if (!battDrag) { $('batt').value = Math.round(p); $('battv').textContent = Math.round(p) + ' %'; } } };

  // ---- the simulator's controls
  const scen = $('scen'); for (const k in SCEN) scen.insertAdjacentHTML('beforeend', '<option value="' + k + '">' + SCEN[k].name + '</option>');
  let battDrag = false;
  const ctlShow = () => { const p = sim.p; $('hs').value = p.hs; $('hsv').textContent = p.hs.toFixed(1) + ' m'; $('tr').value = p.tr; $('trv').textContent = p.tr.toFixed(1) + ' s';
    $('wind').value = p.wind; $('windv').textContent = Math.round(p.wind) + ' kn'; $('batt').value = Math.round(sim.batt); $('battv').textContent = Math.round(sim.batt) + ' %'; $('usb').checked = sim.usb; scen.value = sim.scen; };
  scen.addEventListener('change', () => { sim.setScenario(scen.value); $('power').setAttribute('aria-checked', 'true'); ctlShow(); });
  $('hs').addEventListener('input', e => { sim.p.hs = +e.target.value; ctlShow(); });
  $('tr').addEventListener('input', e => { sim.p.tr = +e.target.value; ctlShow(); });
  $('wind').addEventListener('input', e => { sim.p.wind = +e.target.value; ctlShow(); });
  $('batt').addEventListener('pointerdown', () => battDrag = true); $('batt').addEventListener('change', () => battDrag = false);
  $('batt').addEventListener('input', e => { sim.batt = +e.target.value; sim.emptyN = 0; $('battv').textContent = Math.round(sim.batt) + ' %'; });
  $('usb').addEventListener('change', e => { sim.usb = e.target.checked; });
  let speed = 1;
  const speedShow = () => { for (const b of $('speed').children) b.setAttribute('aria-pressed', +b.dataset.s === speed ? 'true' : 'false'); };
  $('speed').addEventListener('click', e => { const s = +e.target.dataset.s; if (s) { speed = s; speedShow(); } });
  speedShow(); ctlShow();

  // ---- the dashboard: the firmware's page with its requests answered here
  const INJECT = '<script>(' + function () {
    const P = parent.VMRDEMO;
    window.fetch = function (url, opt) {
      opt = opt || {}; const u = typeof url === 'string' ? url : url.url, m = (opt.method || 'GET').toUpperCase();
      let b = opt.body; if (b instanceof URLSearchParams) b = b.toString(); if (typeof b !== 'string' && !(b instanceof FormData)) b = '';   // a file sent (FormData) as it is
      return new Promise((res, rej) => {
        if (opt.signal && opt.signal.aborted) return rej(new DOMException('Aborted', 'AbortError'));
        setTimeout(() => Promise.resolve(P.handle(m, u, b)).then(r => { if (!r) return rej(new TypeError('Failed to fetch (simulated: the VMR is not answering)'));
          res(new Response(r.body, { status: r.status, headers: Object.assign({ 'Content-Type': r.type }, r.headers || {}) })); }), 12 + Math.random() * 30);
      });
    };
    const X = function () { this.upload = {}; this.readyState = 0; this.status = 0; this.responseText = ''; this.headers = {}; };
    X.prototype.open = function (m, u) { this.m = m; this.u = u; };
    X.prototype.setRequestHeader = function () {};
    X.prototype.getResponseHeader = function () { return null; };
    X.prototype.abort = function () { this.onabort && this.onabort(); };
    // a file sent (FormData) is handed over as it is; its upload shows progress over about 2 s
    X.prototype.send = function (b) { const fd = b instanceof FormData, tot = fd ? [...b.values()].reduce((a, v) => a + (v.size || 0), 0) : 0;
      const fin = () => Promise.resolve(P.handle(this.m, this.u, fd ? b : typeof b === 'string' ? b : '')).then(r => {
        if (!r) { this.onerror && this.onerror(); return; } this.readyState = 4; this.status = r.status; this.responseText = this.response = r.body; this.onload && this.onload(); this.onreadystatechange && this.onreadystatechange(); });
      if (!tot) { setTimeout(fin, 300); return; }
      let k = 0; const step = () => { k++; this.upload.onprogress && this.upload.onprogress({ lengthComputable: true, loaded: Math.min(tot, tot * k / 10), total: tot }); if (k < 10) setTimeout(step, 200); else fin(); };
      setTimeout(step, 200); };
    window.XMLHttpRequest = X;
    addEventListener('DOMContentLoaded', () => { const d = document.createElement('div');
      d.textContent = 'DEMO · simulated data'; d.className = 'demobadge';
      const st = document.createElement('style');   // above the phone's bottom bar, not over it
      st.textContent = '.demobadge{position:fixed;right:8px;bottom:8px;z-index:99999;background:#f2b705;color:#1d1600;font:700 12px system-ui;padding:4px 8px;border-radius:5px;box-shadow:0 1px 4px #0005;pointer-events:none}@media (max-width:899px){.demobadge{bottom:calc(72px + env(safe-area-inset-bottom))}}';
      document.head.appendChild(st); document.body.appendChild(d); });
  } + ')();<\/script>';
  const reloadDash = () => { $('dash').srcdoc = DASH_HTML.replace('<head>', '<head>' + INJECT); };
  reloadDash();

  // ---- the displays
  const SCR_NAMES = ['Live', 'Graph', 'Position', 'Maxima', 'GPS', 'Clino', 'Marks', 'Sky', 'Dock', 'G', 'Waves', 'W stats', 'Devices', 'Wind', 'Weather', 'Wx graph', 'Tilt ball', 'Cast', 'Rec', 'Health', 'CTD', 'Roll', 'Download'];
  const oc = $('oled'), ox = oc.getContext('2d'), img = ox.createImageData(128, 64);
  const segSvg = $('seg');
  const SEGP = { a: 'M6 2h20l-3 4H9z', b: 'M27 3l3 3-2 22-4-3 2-19z', c: 'M27 35l-2 19 3 3 3-3 1-19-3-3z', d: 'M5 58h20l3-4H8z', e: 'M4 35l-1 19 3 3 4-3 1-16z', f: 'M5 6l-2 21 3 3 4-3 1-18z', g: 'M7 30h19l3 3-3 3H7l-3-3z' };
  const DIG = [12, 62, 135, 185], ORDER = ['a', 'b', 'c', 'd', 'e', 'f', 'g'];
  let svg = ''; [0, 1, 3, 4].forEach((slot, i) => { const x = DIG[i]; ORDER.forEach(s => svg += '<path id="s' + slot + s + '" transform="translate(' + x + ',10) skewX(-6)" d="' + SEGP[s] + '"/>'); svg += '<circle id="s' + slot + 'p" cx="' + (x + 33) + '" cy="68" r="2.6"/>'; });
  svg += '<circle id="col1" cx="117" cy="30" r="3"/><circle id="col2" cx="114" cy="52" r="3"/>';
  segSvg.innerHTML = svg;
  const segEl = {}; for (const el of segSvg.querySelectorAll('path,circle')) segEl[el.id] = el;
  const lcd = $('lcd'); for (let i = 0; i < 80; i++) lcd.insertAdjacentHTML('beforeend', '<span></span>'); const cells = [...lcd.children];
  const LCDRGB = [[255, 0, 0], [0, 255, 0], [0, 0, 255], [255, 255, 255], [255, 255, 0], [0, 255, 255], [255, 0, 255]];
  const ledSet = (el, on, c, lvl) => { el.style.background = on ? c : '#2a1b1b'; el.style.boxShadow = on ? '0 0 ' + (4 + 10 * lvl) + 'px ' + c + ', inset 0 -2px 3px #0004' : 'inset 0 -2px 3px #0008'; el.style.opacity = on ? 0.45 + 0.55 * lvl : 1; };
  const draw = () => {
    const off = !sim.power || sim.dead || sim.chargeOff;
    let lit = 0;
    if (!off && sim.bootT) {                                  // the boot: the two logos, then the list line by line
      const t = sim.t - sim.bootT; if (t < 2000) W.X.vmr_boot(0); else if (t < 4000) W.X.vmr_boot(1); else W.X.vmr_boot_list(Math.min(11, Math.floor((t - 4000) / 210) + 1)); lit = 1;
    } else if (!off) lit = W.X.vmr_draw();
    const fb = W.fb(), d = img.data, c = V.contrast / 255, g8 = Math.round(110 + 145 * c), on = lit ? [g8, g8, g8] : [0, 0, 0];
    for (let i = 0; i < 8192; i++) { const v = lit && fb[i]; d[i * 4] = v ? on[0] : 0; d[i * 4 + 1] = v ? on[1] : 0; d[i * 4 + 2] = v ? on[2] : 0; d[i * 4 + 3] = 255; }
    ox.putImageData(img, 0, 0); oc.style.opacity = lit ? 0.55 + 0.45 * c : 1;
    $('scrn').textContent = V.screen + 1;
    $('xbcap').textContent = 'Screen jump (' + (SCR_NAMES[V['P.xbScreen']] || '?') + ')';
    // 7-segment
    const sg = off || sim.bootT ? [0, 0, 0, 0, 0, 0, 0] : W.seg(), sl = sg[5] / 15;
    for (const slot of [0, 1, 3, 4]) { ORDER.forEach((s, i) => { segEl['s' + slot + s].style.fill = sg[slot] >> i & 1 ? 'rgba(255,40,30,' + (0.35 + 0.65 * sl) + ')' : '#2a0b09'; });
      segEl['s' + slot + 'p'].style.fill = sg[slot] & 0x80 ? 'rgba(255,40,30,' + (0.35 + 0.65 * sl) + ')' : '#2a0b09'; }
    const colon = sg[2] & 2; segEl.col1.style.fill = segEl.col2.style.fill = colon ? 'rgba(255,40,30,' + (0.35 + 0.65 * sl) + ')' : '#2a0b09';
    // LCD
    const ll = !off && !sim.bootT && W.X.vmr_lcd_lit(), rows = ll ? W.lcd() : null, col = V['P.lcdColor'] === 7 ? dash.mix : LCDRGB[V['P.lcdColor']] || LCDRGB[3];
    lcd.style.color = ll ? 'rgb(' + col.map(x => Math.round(40 + x * 0.84 * (0.35 + 0.65 * c))).join(',') + ')' : '#06080a';   // a negative LCD: the backlight's colour through the characters
    lcd.style.textShadow = ll ? '0 0 4px currentColor' : 'none';
    for (let r = 0; r < 4; r++) for (let i = 0; i < 20; i++) { const ch = rows ? rows[r][i] : 32; cells[r * 20 + i].textContent = ch === 0xDF ? '°' : ch < 32 ? ' ' : String.fromCharCode(ch); }
    // LEDs (duty 0-1023)
    const lo = off || sim.bootT ? [0, 0, 0] : [V['ledOut[0]'], V['ledOut[1]'], V['ledOut[2]']];
    ledSet($('ledMark'), lo[0] > 0, '#4fb3ff', lo[0] / 1023); ledSet($('ledRec'), lo[1] > 0, '#ff3b2f', lo[1] / 1023); ledSet($('ledFault'), lo[2] > 0, '#ffb000', lo[2] / 1023);
    // the WTB
    const wl = sim.wtbLeds(), S = sim.slm;
    for (let i = 0; i < 6; i++) { $('ring' + i).classList.toggle('on', !off && wl.rings[i]); const b = S.b[i];
      $('nkc' + i).textContent = b.st === 2 ? 'fired ' + b.z.toFixed(1) + ' m' : b.st === 1 ? (isNaN(b.plan) ? 'armed' : 'armed, ' + b.plan + ' m') : 'safe'; }
    ledSet($('ledReady'), !off && wl.ready, '#3ddc5a', 1); ledSet($('ledEth'), !off && wl.eth, '#ff3b2f', 1);
    $('wtbst').textContent = !S.armed ? 'OFF (relay open)' : ['OFF', 'STARTING', 'READY', 'IN A CAST ' + S.depth.toFixed(1) + ' m', 'PROCESSING', 'FAULT'][S.state];
    $('offnote').textContent = sim.dead ? 'Off: switch the power off and on' + (sim.batt < 1 && !sim.usb ? ' (charge it first: USB power)' : '') : sim.chargeOff ? 'Charge mode: press B or Screen to wake' : !sim.power ? 'Power off' : '';
    // the hold timers
    const now = performance.now();
    for (const u of units) { const k = u.dataset.btn, t = u.querySelector('.t'); if (t) t.textContent = isDown(k) && since[k] ? ((now - since[k]) / 1000).toFixed(1) + ' s' : ''; }
  };

  // ---- the loop: simulated time in 20 ms steps
  let last = performance.now(), acc = 0, frame = 0;
  const loop = now => {
    const anyDown = Object.keys(down).some(k => down[k]) || Object.keys(latched).some(k => latched[k]) || !!drag;
    const sp = anyDown ? 1 : speed;
    acc += Math.min(250, now - last) * sp; last = now;
    let n = 0;
    // the pins, as the firmware reads them
    V['btnScr.raw'] = isDown('scr') ? 1 : 0; V['btnB.raw'] = isDown('b') ? 1 : 0; V.markRaw = isDown('mark') ? 1 : 0;
    for (let i = 0; i < 5; i++) V['xbBtn[' + i + '].raw'] = isDown('xb' + i) ? 1 : 0;
    V.knobRaw = Math.round(kv * 4095);
    if (sim.chargeOff && (isDown('b') || isDown('scr'))) { sim.chargeOff = false; sim.power = false; sim.setPower(true); }
    while (acc >= 20 && n < 4000) { sim.step(); acc -= 20; n++; }
    if (n >= 4000) acc = 0;
    draw();
    requestAnimationFrame(loop);
  };
  requestAnimationFrame(loop);
})();
