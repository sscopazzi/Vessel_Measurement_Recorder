# Aspect Wave VMR™ (Vessel Measurement Recorder) + SeaLabMOS

### ▶ [Try the system demo](https://sscopazzi.github.io/Vessel_Measurement_Recorder/)
The demo requires no hardware and no sign-up. It runs in your browser with the VMR box (OLED, 7-segment, LCD, buttons, knob and lights), the WTB's Niskin buttons and the dashboard, running in your browser on simulated data. It is a demo: nothing in it is a real measurement.

An open source, weather-resistant instrument built from off-the-shelf Adafruit (and other) parts. It logs ship motion, position, waves and weather to microSD at up to 10 Hz, marks events with one button, and serves a live dashboard to any phone or laptop over its own Wi-Fi hotspot while fully offline from the internet, with a chart of NOAA charts, the ship's track and planned routes. With the optional SeaLabMOS system, it also runs a CTD on a tether and fires six Niskin bottles. The VMR is not for navigation.

Aspect Wave LLC · Sophie Scopazzi · firmware VMR 14.0.0, WTB 2.0.0, IWB 2.0.0

## Features

- **Motion:** roll, pitch and rate of turn, with maxima, roll statistics and the roll period
- **Stability:** a GM cross-check from the natural roll period (IMO IS Code formula)
- **Waves:** heave Hs from the IMU, pressure Hs from the barometer, and a fused Hs; encounter and roll periods
- **At the dock:** trim and list, calibrated from entered draft marks
- **Position and marks:** GPS, location marks and CTD casts with one button press (physical and Wi-Fi dashboard)
- **Chart:** NOAA chart packs (see releases) stored on the microSD card (no internet needed), the track (72 h up to all of it), routes with ETAs, GPX import and export
- **Weather:** pressure and its 3-h change, air temperature, humidity and dew point (SHT30 with frost heater, or SHT40), true and apparent wind with an optional Calypso ULP
- **Magnetic:** magnetic heading, with variation from NOAA's World Magnetic Model 2025 (unreliable on large steel ships - testing ongoing with smaller vessels)
- **Alarms:** anchor watch, BNWAS and limits, on the OLED, Fault LED, dashboard, and an optional piezo or vibration motor for auditory and haptic feedback
- **Power:** LiPo fuel gauge with time left; 12 V battery monitoring (INA260) with SeaLabMOS; Datalogger mode, which sleeps the system between rows
- **On the box:** 23 OLED screens, an optional 7-segment and 20×4 LCD. A physical knob, buttons and status LEDs
- **Dashboard:** up to 4 connected devices (phones or laptops), with dark, day and red night modes, plot from file and file previews, downloads, settings backups, and over-the-air updates for the VMR, WTB and IWB
- **SeaLabMOS (optional):** a tethered CTD with TEOS-10 salinity and density, and six Niskin bottles fired from the dashboard, the WTB or at planned depths (in development)

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

In the dashboard's Firmware update card, pick the target (VMR, WTB or IWB: the VMR relays the other two) and its image: a release's `VMR_v<version>.vmr`, `WTB_v<version>.wtb` or `IWB_v<version>.iwb`, or the `.ino.bin` of your own build (Sketch > Export Compiled Binary). Each image carries its board's tag (VMRVER, WTBVER, SLMVER) and is refused by the wrong board. A new Feather is flashed over USB. Released images are on the [Releases](https://github.com/sscopazzi/Vessel_Measurement_Recorder/releases) page, attached to each version's tag: `VMR_v14.0.0.vmr`, `WTB_v2.0.0.wtb`, `IWB_v2.0.0.iwb`.

## Reading the data

CSV files with `#` header lines describing every column: `pd.read_csv(path, comment='#')`, or `VMR/vmr_reader.py`.

## Versions

The VMR, WTB and IWB each carry a [Semantic Versioning](https://semver.org) number, MAJOR.MINOR.PATCH (`FW_VERSION` in each sketch). Semantic versioning starts at VMR 14.0.0 and WTB / IWB 2.0.0; versions before that (VMR 13.52, WTB and IWB 1.11 and earlier) used tenths and hundredths.

- **MAJOR** when something that depends on the firmware stops working with the version before:
  - the CSV's columns or format (a column renamed, moved or removed, so `vmr_reader.py` or a user's script breaks)
  - the VMR / WTB / IWB link (`vmr_link.h`): all three boards then need updating together
  - the settings layout, when settings or backups don't carry over from the version before
  - the chart pack format (`.vmrchart` files that need rebuilding)
- **MINOR** for a new feature that keeps all of those working (new CSV columns added at the end, new settings that carry over).
- **PATCH** for a fix that changes no feature.

A version is up to 15 characters (`LNK_FW` in `vmr_link.h`). The three boards are numbered separately: a MAJOR change to the link steps all three, and they are updated together (boards on different link versions ignore each other).

### Releasing

1. Set `FW_VERSION` in each sketch that changed, and add its entry to that board's `CHANGELOG.md`.
2. Build each changed board (Sketch > Export Compiled Binary) and rename each `.ino.bin` for its board: `VMR_v<version>.vmr`, `WTB_v<version>.wtb` and `IWB_v<version>.iwb`.
3. Commit, then tag the commit with the VMR's version and push the tag: `git tag -a v14.0.0 -m "VMR 14.0.0, WTB 2.0.0, IWB 2.0.0"`, then `git push origin v14.0.0`. (In VS Code: Source Control > ... > Tags > Create Tag, then push.) A release with only a WTB or IWB change is tagged `wtb-v2.0.1` or `iwb-v2.0.1`.
4. On GitHub, Releases > Draft a new release, pick the tag, title it like the tag message, paste the changelog entries, and attach the images. Firmware images are not committed to the repository.

History: `VMR/CHANGELOG.md`, `SeaLabMOS_WTB/CHANGELOG.md`, `SeaLabMOS_IWB/CHANGELOG.md`. What it can't do: `VMR/KNOWN_LIMITATIONS.md`.

## Hardware sources

The bill of materials (`docs/VMR_Bill_of_Materials_v12.2.docx`) and the WTB/IWB interface spec (`docs/VMR_WTB_IWB_Interface_Spec.docx`) are here as editable Word files, next to their PDFs. The wiring sheets in `docs/wiring/` are exports only: their editable sources, and the enclosure details (the Pelican 1400 panel cut-outs and drilling, mounts, 3D-print files), are not in the repository yet.

## Credits and licences

Designed by Sophie Scopazzi, Aspect Wave LLC.

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
