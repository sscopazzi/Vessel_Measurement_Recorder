#!/usr/bin/env python3
# Builds docs/vmr-demo.html: the page (template.html), the simulator and box (*.js), the firmware's screen
# and button code compiled to WebAssembly (wasm/vmr.wasm, see wasm/build_wasm.sh) and the dashboard (VMR/web/index.html).
import base64, json, re, sys, os
here = os.path.dirname(os.path.abspath(__file__))
vmr = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', '..', 'VMR')
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, '..', 'vmr-demo.html')
wasm = sys.argv[3] if len(sys.argv) > 3 else os.path.join(here, 'wasm', 'vmr.wasm')
ver = re.search(r'#define FW_VERSION\s+"([^"]+)"', open(os.path.join(vmr, 'VMR.ino'), encoding='utf-8').read()).group(1)
page = open(os.path.join(vmr, 'web', 'index.html'), encoding='utf-8').read()
logo = open(os.path.join(vmr, 'web', 'logo.svg'), encoding='utf-8').read()
js = ''
for f in ('wasmcore.js', 'sim.js', 'dash.js', 'box.js'):
    s = open(os.path.join(here, f), encoding='utf-8').read()
    js += re.sub(r'^if \(typeof module.*$', '', s, flags=re.M) + '\n'
esc = lambda s: json.dumps(s).replace('</', '<\\/')
t = open(os.path.join(here, 'template.html'), encoding='utf-8').read()
t = t.replace('%VER%', ver)
t = t.replace('%WASM_B64%', 'const WASM_B64 = "' + base64.b64encode(open(wasm, 'rb').read()).decode() + '";')
t = t.replace('%DASH_HTML%', 'const DASH_HTML = ' + esc(page) + ';')
t = t.replace('%LOGO_SVG%', 'const LOGO_SVG = ' + esc(logo) + ';')
t = t.replace('%JS%', js.replace('</script', '<\\/script'))
open(out, 'w', encoding='utf-8').write(t)
print(out, len(t) // 1024, 'kB, firmware', ver)
