# VMR firmware

The Vessel Measurement Recorder's sketch for the Adafruit ESP32-S2 Feather.

| File | What it is |
| --- | --- |
| `VMR.ino` | The recorder. Its header comment describes the hardware, controls, screens, dashboard cards, files and program structure |
| `slm.ino` | SeaLabMOS: the WTB link, relay, CTD, Niskin bottles, 12 V battery (INA260) and laptop port |
| `slm_types.h`, `pend_ring.h` | Types and the log ring buffer, in headers so the Arduino builder's prototypes see them |
| `vmr_link.h` | ESP-NOW frames; identical to `SeaLabMOS_WTB/vmr_link.h` |
| `web/index.html`, `web/logo.svg`, `web/make_page.py` | The dashboard; `make_page.py` packs it into `VMR.ino` |
| `vmr_reader.py` | Reads the CSV files into pandas (`read_vmr`, `slow`, `waves`, `fixes`, `ctd`, `summary`) |
| `CHANGELOG.md`, `KNOWN_LIMITATIONS.md` | History, and what it can't do |
| `.vmr_files/` | Released firmware images (`VMR_vX.vmr`), each committed with its source |

Build: see the top README. The physical controls are in `docs/VMR_Controls.docx`, the wiring in `docs/wiring/`.
