# Aspect Wave VMR™ (Vessel Measurement Recorder) + SeaLabMOS

**[Try the demo](https://sscopazzi.github.io/Vessel_Measurement_Recorder/)**: the VMR box (OLED, 7-segment, LCD, buttons, knob and lights), the WTB's Niskin buttons and the dashboard, running in your browser on simulated data. It is a demo: nothing in it is a real measurement.

An open-source, weatherproof instrument built from off-the-shelf Adafruit parts. It logs ship motion, position, waves and weather to microSD at 10 Hz, marks events with one button, and serves a live dashboard to any phone or laptop over its own Wi-Fi. With SeaLabMOS it also runs a CTD on a tether and fires six Niskin bottles.

Aspect Wave LLC · Sophie Scopazzi · firmware VMR 13.51, WTB 1.11, IWB 1.11

## What's here

| Folder | Board | What it is |
| --- | --- | --- |
| `VMR/` | ESP32-S2 Feather in the VMR box | The recorder: motion, GPS, weather, marks, dashboard, and the SeaLabMOS link (`slm.ino`) |
| `SeaLabMOS_WTB/` | ESP32-S2 Feather in the VMR box | The wet-talk board: runs cast sessions, keeps the 5 Hz CTD data, owns the Niskin bottles |
| `SeaLabMOS_IWB/` | ESP32-S2 Feather in the CTD tube | The in-water board: Bar30, TSYS01, EZO-EC and SHT40, TEOS-10 salinity and density, the Niskin solenoids |
| `docs/` | | Overview and build manual, calculations, controls guide, known limitations, bill of materials, interface spec, wiring sheets |
| `docs/index.html` | browser | The demo (one file); its source and how to rebuild it are in `docs/demo/` |
| `charts/` | | The tools that build the NOAA chart packs (`.vmrchart`), and the Leaflet copy each pack carries |

`VMR/vmr_link.h` and `SeaLabMOS_WTB/vmr_link.h` must be the same file: it defines the ESP-NOW frames between the two.

The VMR runs on its own without the WTB and IWB; SeaLabMOS is switched on in the dashboard's Add-ons card.

## Building

Arduino IDE 2.x with **esp32 by Espressif** 3.3.x. Board **Adafruit Feather ESP32-S2**, Partition Scheme **Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)** for all three.

- **VMR** libraries: Adafruit SH110X (+ GFX, BusIO), RTClib, Adafruit GPS Library, Adafruit BNO08x, Adafruit MAX1704X, Adafruit BMP3XX, Adafruit SHT31, Adafruit PCF8574, Adafruit ADS7830 (+ Unified Sensor).
- **WTB**: none beyond the core.
- **IWB**: Blue Robotics MS5837 and TSYS01, Adafruit SHT4x, Adafruit NeoPixel.

The dashboard page is kept readable in `VMR/web/index.html`. After editing it, run `python3 VMR/web/make_page.py`, which gzips it into the arrays at the end of `VMR.ino`, then compile.

Passwords and the ESP-NOW keys are in the source on purpose (`AP_PASS`, `FW_PASSWORD` in `VMR.ino`; `WIFI_PASS` in the IWB; `LNK_PMK` / `LNK_LMK` in `vmr_link.h`). Change them for your own units.

## Updating firmware

Sketch > Export Compiled Binary, then upload the `.ino.bin` in the dashboard's Firmware update card, picking the target: VMR, WTB or IWB (the VMR relays the other two). Each image carries its board's tag (VMRVER, WTBVER, SLMVER) and is refused by the wrong board. A new Feather is flashed over USB. Released VMR images are in `VMR/.vmr_files/` (`VMR_v13.51.vmr` is the `.ino.bin` of this source).

## Reading the data

CSV files with `#` header lines describing every column: `pd.read_csv(path, comment='#')`, or `VMR/vmr_reader.py`.

## Versions

Small changes step the hundredths (13.51 → 13.52), larger ones the tenths. History: `VMR/CHANGELOG.md`, `SeaLabMOS_WTB/CHANGELOG.md`, `SeaLabMOS_IWB/CHANGELOG.md`. What it can't do: `VMR/KNOWN_LIMITATIONS.md`.

## Hardware sources

The bill of materials (`docs/VMR_Bill_of_Materials_v12.2.docx`) and the WTB/IWB interface spec (`docs/VMR_WTB_IWB_Interface_Spec.docx`) are here as editable Word files, next to their PDFs. The wiring sheets in `docs/wiring/` are exports only: their editable sources, and the enclosure details (the Pelican 1400 panel cut-outs and drilling, mounts, 3D-print files), are not in the repository yet.

## Credits and licences

Designed by Sophie Scopazzi, Aspect Wave LLC, with Claude (Anthropic).

- Hardware design: CERN-OHL-S-2.0
- Firmware, dashboard, demo and tools: GPL-3.0-or-later. The WTB and IWB firmware carry an additional
  permission for the TEOS-10 GSW code.
- `vmr_reader.py` and the analysis notebook: MIT
- Documentation and data: CC BY-SA 4.0

Each file's licence is in its SPDX header or in `REUSE.toml`. The full texts are in `LICENSES/`, with an
overview in `LICENSE`. The TEOS-10 routines in `teos10.c` and `gsw_internal_const.h` are copied
unchanged from GSW-C (SCOR/IAPSO WG127) under the GSW licence, which allows redistribution without
modification only, so don't edit those two files. Other third-party code: `THIRD_PARTY_NOTICES.md`.

"Aspect Wave", "Aspect Wave VMR", "VMR" and the logo are trademarks of Aspect Wave LLC: see
`TRADEMARKS.md`. If you build your own unit, you're welcome to say it's based on the Aspect Wave VMR.
If you distribute one, give it your own name.

Contributions: see `CONTRIBUTING.md`.
