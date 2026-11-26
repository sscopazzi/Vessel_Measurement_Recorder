# VMR demo: source

`docs/index.html` is one self-contained page, served from GitHub Pages at
https://sscopazzi.github.io/Vessel_Measurement_Recorder/. It shows the VMR box and the WTB box with their displays, buttons and lights next to the dashboard, all running on **simulated data**.

## What is real and what is simulated

| Part | Where it comes from |
| --- | --- |
| OLED screens (all 23, the hold menu, confirmations, messages, boot screens) | The firmware's own drawing code (`VMR.ino`, `drawCtd()` in `slm.ino`), compiled to WebAssembly |
| Screen button, B, Mark, knob and knob switch, optional buttons, hold menu, screen order, LEDs, 7-segment, LCD | The firmware's own code (`serviceButtons`, `serviceMarkBtn`, `serviceXb`, `servicePot`, `serviceLeds`, `segCompose`, `lcdCompose` and the rest listed in `wasm/extract.py`), compiled to WebAssembly |
| Dashboard | The firmware's own page, `VMR/web/index.html`, unchanged |
| The dashboard's answers (`/data`, `/slm`, `/wave` and so on) | `dash.js`, a stand-in for the VMR's web server |
| The ship, sea, wind, weather, GPS, battery, WTB and IWB | `sim.js`, made-up values |
| The boxes, their controls and the page | `box.js`, `template.html` |

## Rebuilding

After a firmware change:

```sh
cd docs/demo/wasm && sh build_wasm.sh ~/Arduino/libraries   # vmr.wasm (needs: pip install ziglang)
cd .. && python3 build_demo.py                              # ../index.html
```

`build_wasm.sh` copies the drawing and input code out of `VMR.ino` and `slm.ino` unchanged (`extract.py`), adds the Adafruit GFX library, and compiles it with the stand-ins in `wasm/` for the hardware: the sensors' values and the button pins are variables the page writes, and the requests the firmware hands to its other tasks (a mark, a cast, arming SeaLabMOS, Wi-Fi off) come back to the page as events. `build_demo.py` packs the page, the scripts, `vmr.wasm`, the dashboard and its logo into the one HTML file.

## Licence

The demo's source here is GPL-3.0-or-later (each file's SPDX header). `vmr.wasm` also contains third-party code: the Adafruit GFX Library (BSD-2-Clause) with its Tom Thumb (BSD-3-Clause) and FreeSansBold (GPL-3.0-or-later) fonts, the Adafruit SH110X splash image (BSD-3-Clause), and the compiler's wasi-libc and libc++ (MIT, Apache-2.0). Their notices are in `THIRD_PARTY_NOTICES.md` at the top of the repository.

If a firmware change adds a function or variable that the copied code uses, `build_wasm.sh` stops with a compile error naming it; add it to `wasm/gluew.inc` (or to the list in `extract.py` when it should be the firmware's own).
