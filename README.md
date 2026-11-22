# Vessel Measurement Recorder (VMR) + SeaLabMOS

**[Try the demo](https://sscopazzi.github.io/Vessel_Measurement_Recorder/docs/vmr-demo.html)**: the VMR box (OLED, 7-segment, LCD, buttons, knob and lights), the WTB's Niskin buttons and the dashboard, running in your browser on simulated data. It is a demo: nothing in it is a real measurement.

An open-source, weatherproof instrument built from off-the-shelf Adafruit parts. It logs ship motion, position, waves and weather to microSD at 10 Hz, marks events with one button, and serves a live dashboard to any phone or laptop over its own Wi-Fi. With SeaLabMOS it also runs a CTD on a tether and fires six Niskin bottles.

Aspect Wave LLC · Sophie Scopazzi · firmware VMR 12.32, WTB 1.11, IWB 1.11

## What's here

| Folder | Board | What it is |
| --- | --- | --- |
| `VMR/` | ESP32-S2 Feather in the VMR box | The recorder: motion, GPS, weather, marks, dashboard, and the SeaLabMOS link (`slm.ino`) |
| `SeaLabMOS_WTB/` | ESP32-S2 Feather in the VMR box | The wet-talk board: runs cast sessions, keeps the 5 Hz CTD data, owns the Niskin bottles |
| `SeaLabMOS_IWB/` | ESP32-S2 Feather in the CTD tube | The in-water board: Bar30, TSYS01, EZO-EC and SHT40, TEOS-10 salinity and density, the Niskin solenoids |
| `docs/` | | Overview and build manual, calculations, controls guide, bill of materials, interface spec, wiring sheets |
| `docs/vmr-demo.html` | browser | The demo (one file); its source and how to rebuild it are in `docs/demo/` |

`VMR/vmr_link.h` and `SeaLabMOS_WTB/vmr_link.h` must be the same file: it defines the ESP-NOW frames between the two.

The VMR runs on its own without the WTB and IWB; SeaLabMOS is switched on in the dashboard (card 16).

## Building

Arduino IDE 2.x with **esp32 by Espressif** 3.3.x. Board **Adafruit Feather ESP32-S2**, Partition Scheme **Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)** for all three.

- **VMR** libraries: Adafruit SH110X (+ GFX, BusIO), RTClib, Adafruit GPS Library, Adafruit BNO08x, Adafruit MAX1704X, Adafruit BMP3XX, Adafruit SHT31, Adafruit PCF8574, Adafruit ADS7830 (+ Unified Sensor).
- **WTB**: none beyond the core.
- **IWB**: Blue Robotics MS5837 and TSYS01, Adafruit SHT4x, Adafruit NeoPixel.

The dashboard page is kept readable in `VMR/web/index.html`. After editing it, run `python3 VMR/web/make_page.py`, which gzips it into the arrays at the end of `VMR.ino`, then compile.

Passwords and the ESP-NOW keys are in the source on purpose (`AP_PASS`, `FW_PASSWORD` in `VMR.ino`; `WIFI_PASS` in the IWB; `LNK_PMK` / `LNK_LMK` in `vmr_link.h`). Change them for your own units.

## Updating firmware

Sketch > Export Compiled Binary, then upload the `.ino.bin` in the dashboard's card 19, picking the target: VMR, WTB or IWB (the VMR relays the other two). Each image carries its board's tag (VMRVER, WTBVER, SLMVER) and is refused by the wrong board. A new Feather is flashed over USB.

## Reading the data

CSV files with `#` header lines describing every column: `pd.read_csv(path, comment='#')`, or `VMR/vmr_reader.py`.

## Versions

Small changes step the hundredths (12.32 → 12.33), larger ones the tenths. History: `VMR/CHANGELOG.md`, `SeaLabMOS_WTB/CHANGELOG.md`, `SeaLabMOS_IWB/CHANGELOG.md`. What it can't do: `VMR/KNOWN_LIMITATIONS.md`.

## Credits and licences

Designed by Sophie Scopazzi, Aspect Wave LLC, with Claude (Anthropic). The TEOS-10 routines in `teos10.c` are a subset of GSW-C (BSD-3-Clause). Licence for the rest: **[to add]**.
