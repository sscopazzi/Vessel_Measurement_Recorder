// SPDX-FileCopyrightText: 2026 Aspect Wave LLC
// SPDX-License-Identifier: GPL-3.0-or-later
// ---- the firmware's own screen, button and display code, compiled to WebAssembly (see docs/demo/README)
function vmrWasm(bytes) {
  let mem;
  const wasi = {
    fd_write: (fd, iov, n, out) => { const v = new DataView(mem.buffer); let t = 0; for (let i = 0; i < n; i++) t += v.getUint32(iov + i * 8 + 4, true); v.setUint32(out, t, true); return 0; },
    fd_close: () => 0, fd_seek: () => 0, proc_exit: () => 0,
  };
  const inst = new WebAssembly.Instance(new WebAssembly.Module(bytes), { wasi_snapshot_preview1: wasi });
  const X = inst.exports; mem = X.memory;
  if (X._initialize) X._initialize();
  X.vmr_init();
  const cstr = p => { const u = new Uint8Array(mem.buffer); let e = p; while (u[e]) e++; return new TextDecoder('latin1').decode(u.subarray(p, e)); };
  const vars = {};
  for (const ln of cstr(X.vmr_vars()).split('\n')) { if (!ln) continue; const [n, t, a] = ln.split(' '); vars[n] = [t, +a]; }
  const V = new Proxy({}, {
    get(_, k) {
      const e = vars[k]; if (!e) throw Error('no var ' + k); const [t, a] = e, d = new DataView(mem.buffer);
      switch (t) { case 'f': return d.getFloat32(a, true); case 'd': return d.getFloat64(a, true); case 'b': return d.getUint8(a); case 'c': return d.getInt8(a);
        case 'h': return d.getInt16(a, true); case 'H': return d.getUint16(a, true); case 'i': return d.getInt32(a, true); case 'I': return d.getUint32(a, true);
        case 'Q': return Number(d.getBigUint64(a, true)); case 's': return cstr(a); }
    },
    set(_, k, v) {
      const e = vars[k]; if (!e) throw Error('no var ' + k); const [t, a] = e, d = new DataView(mem.buffer);
      if (v === null || v === undefined) v = NaN;
      if (typeof v === 'boolean') v = v ? 1 : 0;
      switch (t) { case 'f': d.setFloat32(a, v, true); break; case 'd': d.setFloat64(a, v, true); break; case 'b': d.setUint8(a, v); break; case 'c': d.setInt8(a, v); break;
        case 'h': d.setInt16(a, v, true); break; case 'H': d.setUint16(a, v, true); break; case 'i': d.setInt32(a, v, true); break; case 'I': d.setUint32(a, v >>> 0, true); break;
        case 'Q': d.setBigUint64(a, BigInt(Math.max(0, Math.round(v))), true); break;
        case 's': { const u = new Uint8Array(mem.buffer); const s = String(v); for (let i = 0; i < s.length; i++) u[a + i] = s.charCodeAt(i) & 255; u[a + s.length] = 0; } }
      return true;
    },
  });
  const str = s => { V.scratch = String(s).slice(0, 500); };
  return {
    X, V, str, mem,
    fb: () => new Uint8Array(mem.buffer, X.vmr_fb(), 8192),
    ws: () => new Float32Array(mem.buffer, vars['WS'][1], 3 * 81),
    events: () => cstr(X.vmr_events()).split('\n').filter(Boolean),
    seg: () => Array.from(new Uint8Array(mem.buffer, X.vmr_seg(), 7)),
    lcd: () => { const p = X.vmr_lcd(), u = new Uint8Array(mem.buffer, p, 84); const r = []; for (let i = 0; i < 4; i++) r.push(Array.from(u.subarray(i * 21, i * 21 + 20))); return r; },
  };
}
if (typeof module !== 'undefined') module.exports = vmrWasm;
