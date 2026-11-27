# Changelog

All notable changes to the Vessel Measurement Recorder (VMR) firmware and dashboard, newest first.

Version numbers: from 14.0.0, [Semantic Versioning](https://semver.org) (MAJOR.MINOR.PATCH), as set out under Versions in `README.md`. Up to 13.51, a hundredths step (10.6 → 10.61) was a small change or fix, a tenths step (10.6 → 10.7) a larger one, and a whole number a big one. Dates are build dates. "Fixes" means small corrections with no change you would notice.

## 14.0.0 · 26 Nov 2026

MAJOR: the link to the WTB changed, so the VMR, WTB and IWB are updated together.

- **Link version 2** (`vmr_link.h`): firmware versions travel in 16 bytes (up to 15 characters) in the BEACON, HELLO, STATUS and FWBEGIN frames. A board on link version 1 and one on version 2 ignore each other: the SeaLabMOS card shows NO WTB until both are updated. Order, all from the dashboard's Firmware update card: with the VMR still on 13.x, update the IWB to 2.0.0, then the WTB to 2.0.0, then the VMR to 14.0.0. (Over USB, any order.)
- **Semantic versioning** from here: VMR 14.0.0, WTB and IWB 2.0.0. Released images are attached to each version's GitHub Release instead of being kept in the repository (`VMR/.vmr_files` is gone).
- **Wi-Fi name** (the System card): the hotspot is "VMR" and a name typed there (up to 21 letters, digits, spaces and . _ - ( ) #), or "VMR" and the unit's 4-character tag (was "Vessel Measurement Recorder" and the tag). Save restarts the hotspot: phones join the new network with the same password. A settings restore keeps the unit's own name. Settings from 13.5x carry over, and backups made with 13.5x restore (the backup list no longer marks them as another version).
- **Credits**: the CSV header's `credits` line is `Aspect Wave VMR - Copyright (c) 2026 Aspect Wave LLC - GPL-3.0-or-later`; the dashboard footer reads © 2026 Aspect Wave LLC.

## 12.33–13.51 · 21–26 Nov 2026

- **13.51**: The licences: GPL-3.0-or-later for the firmware and dashboard, CERN-OHL-S-2.0 for the hardware, CC BY-SA 4.0 for the docs, MIT for `vmr_reader.py` (`LICENSE`, `LICENSES/`, `REUSE.toml`, `THIRD_PARTY_NOTICES.md`, `TRADEMARKS.md`). The About dialog shows them. `vmr_reader.py` reads the fused Hs columns (`fused_hs_m`, `fused_hs_mid_m`, `fused_coh`, `baro_gain`). The demo is `docs/index.html`, published at sscopazzi.github.io/Vessel_Measurement_Recorder.
- **13.5**: SHT30 heater (the Add-ons card, on by default): when the humidity has sat at 100 % for over a minute, the heater runs 2 min and the readings are marked heating, then settling until the temperature is back within 0.2 °C (3–15 min), with events in the file; Heat now button. Wind: under 1 kn SOG the true wind's direction comes only from the wind meter's north-mark heading, typed in the Add-ons card (none without it). The chart's past track is kept in a hidden file on the microSD card instead of memory, for 24 h up to all of it. Clear EEPROM in the System card. Drafts in decimal inches, kept to the tenth in the trim calibration. The route table's ETAs at the present SOG; the Voyage calculation card adds the times at the planned speeds. The link's VMR time frame is named VMR, not SHIP.
- **13.41**: Plot from file runs in its own task with its stack in PSRAM: plotting a large active file no longer leaves phones unable to rejoin the Wi-Fi.
- **13.4**: Alarms (the Alarms card): anchor watch, BNWAS, and limits for roll, heave Hs, true wind, battery and GPS lost, shown on the OLED, Fault LED and dashboard, with an optional piezo or DRV2605L vibration motor. Power-user settings. Red (night) mode fixes. The EBL starts from the right origin.
- **12.33–13.32**: not logged one by one. From the repository's commits: Fused Hs (the IMU's and the pressure's heave combined), a maximum chart scale option, chart packs for the US west coast, and Leaflet kept on the microSD card.

## 12.x · 16–21 Nov 2026

- **12.32**: Fixes. With the battery empty, the shutdown no longer keeps the wake buttons' pull-ups, and the RTC power domain they need, switched on: nothing wakes it then, so they were only a small drain. Card 18 says the time left waits for 20 min of the battery voltage, not the fuel gauge.
- **12.31**: At 3.30 V the VMR shuts down and stays off: no timer or button wake. It starts again only when the power switch is cycled (or RESET pressed), charged or not. The repo has a browser demo, `docs/vmr-demo.html`: the firmware's own OLED, button, LED, 7-segment and LCD code and the real dashboard, run on simulated data.
- **12.3**: Battery % on the 3.7 V LiPo comes from the cell voltage on a measured discharge curve (4.20 V = 100 %, 3.575 V = 20 %, 3.30 V = 0 %), from one-minute means (the median of the last three): the fuel gauge's own % read far low on these cells (12 % at 3.70 V with 43 % of the charge left). Time left is the slope of a straight line through up to the last 4 h of that %, shown from 20 min and started again whenever the VMR changes its own load. On USB, and for 3 min after USB comes out, the gauge's % is shown. New last CSV column `batt_gauge_pct` (the gauge's own %). At 3.30 V (a minute of readings) the VMR shuts down cleanly: a SeaLabMOS session is disarmed (the WTB closes an open cast and its files), the recording gets a BATTERY EMPTY row and is closed, settings are saved, then everything goes off.
- **12.21**: Datalogger mode is written into the CSV: DATALOGGER ON / OFF events (saying how the GPS is run) and two header lines (`datalogger`, `datalogger_gps`). New Start option in card 7, "keep the GPS awake until each row has a fix": in Datalogger sleep the GPS stays awake until it has a fix, at most 2 min past the row, instead of getting only the 20 s before each row (a 4-day 1-min recording near a window had a fix on 31 % of rows).
- **12.2**: Low battery fixed. Near empty, the VMR's own lighter load (Wi-Fi off, CPU 80 MHz) lifted the cell voltage enough to look like USB going in: it read as charging and the low-battery fault cleared at 3.52 V. A voltage step now has to clear the three readings before it and hold on the next, none is taken for 30 s after the VMR changes its own load, and the fault clears only when the gauge also says charging. The 12 V battery is low under 20 % or at its chemistry's floor (LiFePO4 12.0 V, Li-ion 3S 10.8 V, lead-acid 11.9 V), never while charging; card 18 shows LOW. The 20x4 LCD uses all four rows, with a new Cast screen (duration, CTD, the six bottles and the capacitor, next planned depth or fired / armed counts) that comes up by itself when a cast opens. AW9523 LED driver (Adafruit 4886) as an add-on in card 16: the indicator LEDs and LCD colours on its pins, dimmed in 256 steps. Drafts can be typed as decimal feet (card 18, Lengths). Niskin requests are refused unless the session is armed and the WTB answers.
- **12.1**: Niskin bottles (card 22, Fire Control): six bottles fired by the IWB from a 10,000 µF capacitor - arm, fire, safe, reset, planned depths with the recharge gap checked against the lowering speed; each firing logged with depth, temperature, salinity and density, on the WTB's card, in the profile header and as a location mark (BOTTLE n FIRED).
- **12.05**: Battery selects and Install no longer rewritten every second (they reset while being picked).
- **12.04**: Plot from file: rows further apart than a column (a slow log rate) are joined, not left as gaps between dots.
- **12.03**: Card 21: show / hide temperature, salinity and density; mean in-situ density from the top of the cast down to a typed depth (0.25 m bins).
- **12.02**: OLED layout touches.
- **12.01**: The waves period axis fits the data. Show on OLED unticked keeps downloads off the OLED.
- **12.0**: Optional buttons in card 17 (Cast, Record, Pause, Wi-Fi hotspot, Screen jump), each on a PCF8574 or Feather pin. GPS off / on as a knob position. Each OLED screen shows its number; a Download screen while a download runs. Card 16 renamed Add Ons.

## 11.6–11.91 · 13–16 Nov 2026

- **11.91**: microSD back at 20 MHz, with 32 KB reads, for faster downloads.
- **11.9**: Files download as ordinary browser downloads (one file or a .zip), with a bigger TCP send window.
- **11.84 / 11.83**: Internal RAM freed for the Wi-Fi: buffers and task stacks moved to PSRAM.
- **11.82**: loop() back at priority 1: at 2, phones dropped off the hotspot.
- **11.81**: The Roll screen's digits sit in fixed cells.
- **11.8**: The recording is written by its own task (sdTask) from a ring buffer, so a slow card never holds up the 10 Hz rows. New Roll screen. Weather: 5-min means, and the 3-h change from 5-min means. CPU shares per task in card 12.
- **11.7**: SeaLabMOS: the VMR runs the WTB and IWB (CTD on a tether) over ESP-NOW - a latching relay powers them for an armed cast session; card 21 CTD profile; the WTB's 1-s means in the CSV (`iwb_*`); profiles to /casts/; the WTB's card in card 9; firmware for the WTB and IWB relayed through card 19. 12 V battery through an INA260 (`batt_a`), with its chemistry and capacity. Optional laptop port (Wiz5500 Ethernet). Pin 13 for an LED.
- **11.62**: Downloads in 1 MB pieces (2 MB with Fast, which pauses the live data).
- **11.61**: Health shows the battery estimate; resume after a pause keeps the screen.
- **11.6**: Downloads with a progress pop-up. Plot from file draws heave Hs (motion and pressure) and runs at loop priority (a 43 MB file no longer takes 15 min). Clear plot.

## 11.x · 12 Nov 2026

- **11.52**: The 7-segment shows roll, pitch and rate of turn 10 times a second (the rest 4). OLED off from the knob now comes back only with the screen button, leaving the knob free for the 7-segment's brightness. Fixed Save image on the Waves card (broken in 11.5).
- **11.51**: Fixed Pressure Hs never appearing: it needed pressure over the whole window, and the BMP388's settling second at start-up always left it a few samples short. It now uses the newest part of the window it covers, once it has 8.5 min, and a brief loop hold-up no longer restarts it.
- **11.5**: The synchronous / parametric rolling check (Te / Tr) is gone everywhere: card 6, the graph's shaded bands, the GM note, the OLED and the CSV (`wave_tr_src`, `wave_te_tr`, `wave_flag` dropped). Card 6 is pared down to the numbers, with the explanations behind Show instructions, one unit throughout, and a new Vessel heave avg (the mean of Heave Hs and Pressure Hs). The OLED Wave stats screen shows the pressure Hs.
- **11.4**: Heave Hs from the pressure: the BMP388 is read 10 times a second and analysed beside the acceleration, shown under Heave Hs in card 6 and logged as `baro_hs_m`, `baro_hs_mid_m` and `baro_tp_s`. The weather pressure is now each second's mean. New knob position 8 turns the OLED off; a button or a turn of the knob brings it back. Record moves to 79–86 % and Cancel to 94–100 %.
- **11.3**: The sketch header is rewritten to describe the VMR as it is now, and its version history moved here. Added this changelog, `KNOWN_LIMITATIONS.md`, the CSV reader `vmr_reader.py` and a one-page controls guide. 4.7 KB of static RAM freed: the GPS object and the OLED graph buffers moved to PSRAM.
- **11.22**: Picking Alaska-class tanker fills in its beam, LOA, LBP and keel heights. It asks before replacing values already entered.
- **11.21**: Every card's how-to text sits behind its "Show instructions" button. Page and header text checked against the code.
- **11.2**: Magnetic variation comes from the GPS position (NOAA World Magnetic Model 2025) instead of being typed in.
- **11.1**: Units setting in card 18: °F or °C, feet or metres, everywhere but the CSV (default °F and feet). The OLED Waves header shows the wave height (VH / MH), RP and PP, with a line and label at the strongest peak.
- **11.04**: The Recording screen says recording can also be started with the knob.
- **11.03**: Boots into the Health screen. Cancel is a knob position like the others.
- **11.02**: Wi-Fi restart / off / on run in their own task, fixing a ~200 ms loop stall. The Wi-Fi name, password and link replace the QR code. Cancel moves to the top of the knob.
- **11.01**: The brightness holds at 30 % while choosing a hold action, and the hold panel reads KNOB SEL 1–8.
- **11.0**: Hold the screen button 1 s to pick an action with the knob: restart Wi-Fi, Wi-Fi off / on, Wi-Fi details, tilt ball, flip the displays, countdown, start / stop recording. Risky actions ask for confirmation. The 7-segment can be flipped. CPU and box temperatures shown in °F.

## 10.x · 7–12 Nov 2026

- **10.93**: USB plugged in or pulled out is detected at once from the jump in battery voltage. Shows the time to full while charging.
- **10.92**: Health screen: local time with its source (G / R / W) at the top.
- **10.91**: Health screen layout.
- **10.9**: New Health screen: uptime, battery and time left, draw in mA, brightness, temperatures, Wi-Fi and fault reasons. Battery capacity (Ah) in card 18. Position, GPS and Sky are greyed out when the GPS is off.
- **10.81**: A screen whose sensor is off is greyed out in card 3 and skipped by the button.
- **10.8**: Tick boxes for the 7-segment readings and LCD screens. The countdown starts only from the dashboard.
- **10.74**: mbar everywhere, including the CSV (`pressure_mbar`). The 7-segment shows mbar to 0.1. Faster 7-segment stepping with screen + Mark.
- **10.73**: The open cast is shown at the bottom of card 8. Delete last and Clear move to the top right. hPa becomes mbar.
- **10.72**: The Cast screen shows only during a cast; the OLED switches to it and back by itself.
- **10.71**: Full code review: fixes and speed-ups.
- **10.7**: Hold Mark 1.5 s for a cast (the Cast button is gone). The screen button acts when let go. Tick boxes choose the OLED rotation. New Cast and Recording screens. Heave on the 7-segment.
- **10.61**: The tilt ball's best time is kept through restarts.
- **10.6**: Turn ball 90° for how the OLED sits.
- **10.5**: Tilt ball screen.
- **10.4**: Fixed the clock going wrong after 24.8 days. Stack and PSRAM shown in card 18.
- **10.3**: Each display has its own On / Off. New 7-segment countdown with a light show at zero.
- **10.2**: OLED Weather and Weather graph screens. SHT40 support. `box_temp_c` column. Each unit gets its own Wi-Fi name and channel.
- **10.1**: Rows the microSD card dropped are rewritten from RAM. The clock is corrected from GPS smoothly while recording. I2C failures counted by device.
- **10.0**: Towing astern while recording, the wire-out arrows also sit above card 1.

## 9.x · 7 Nov 2026

- **9.19**: Vessel class (towboat, Alaska-class tanker, other) and this recording's set-up in card 7, with no pop-up at Start. Set-up changes while recording become events in the file. Barge details for towboats.
- **9.18**: Custom LCD backlight colour. Wave spectra widened to 2–35 s.
- **9.17**: Waves Reset. Pin mappings and Cards behind their buttons. Reset maxima in card 5. With no OLED, the screen button steps the LCD. A screen button can be set to None.
- **9.16**: A knob can be set to None (slider only). The 1-s refresh no longer disturbs an open dropdown.
- **9.15**: Rows on a fixed 100-ms grid, a true 10 rows a second (was 9.74).
- **9.14**: Loop timing per stage and 10 Hz row gaps in the hardware check. USB serial no longer holds up the loop.
- **9.13**: Clear EEPROM option for firmware updates.
- **9.12**: Default LEDs: Mark on pin 9, Recording on A3, Fault on A4. PCF8574 INT moved to pin 5. OLED buttons A and C no longer used.
- **9.11**: The knob always on the ADS7830, freeing A5.
- **9.1**: LCD cycling (roll / position / weather). Rate of turn on the 7-segment. GM plot band ±3 %. Fault LED flashes FAULT in Morse.
- **9.0**: Adds the 7-segment and 16×2 RGB LCD. IMU mounting set in card 16 (chip direction, X arrow, flips). The clinometer scale is shared with the OLED. Wave flags at ±5 %.

## 8.x · 3–6 Nov 2026

- **8.63**: I2C read failures counted, and the driver renewed after a short read (fixes rare crashes).
- **8.62**: The last 20 min kept while not recording (Save Last 20min). Other plots: Waves, Weather, Wind rose.
- **8.61**: GM plot: the whole file against the CargoMax GM.
- **8.6**: A recording resumes in the same file after a crash.
- **8.51**: Wind rose timing. Slow wind meter rates.
- **8.5**: Card 1 labelled. Mag row with variation, magnetic heading and deviation. `yaw_deg` column dropped.
- **8.4**: Level zero joins the ship data card and can be set from the drafts (deck slope).
- **8.32**: Wind meter wording.
- **8.31**: Under 1 kn SOG the true wind is the apparent wind.
- **8.3**: Wind screen, wind rose, mast motion correction and wind meter settings.
- **8.2**: Calypso ULP wind meter over RS485. Slow columns written once a second.
- **8.12**: Quarter-mean draft when a midships draft is given.
- **8.11**: LED behaviour.
- **8.1**: Controls on the I2C boards by default. Pin mappings card. Mark, Recording and Fault LEDs.
- **8.02**: Cards renumbered.
- **8.01**: Fixes.
- **8.0**: Firmware update password. Magnetometer test. Where the VMR sits on the ship, for heave at midships. Card order per phone.

## 7.x · 2–3 Nov 2026

- **7.43**: Cards on this phone (hide cards). Heave at midships.
- **7.42**: OLED GPS screen: the position in large letters, in the chosen format.
- **7.41**: OLED Wave stats and Wi-Fi devices screens.
- **7.4**: OLED Waves screen. Ship data checked before saving. The page is served gzipped (about 3× faster). Buffers moved to PSRAM.
- **7.34**: 256 KB PSRAM row buffer: rows survive a microSD outage.
- **7.33**: GM still shown near resonance, with a note (it used to be blanked).
- **7.32**: GM and Pause text.
- **7.31**: Roll period Tr from the roll spectrum. Optional loading computer GM, compared in plots.
- **7.3**: GM from the roll spectrum peak (the felt roll period read GM high).
- **7.21**: Location marks: Delete last and Clear.
- **7.2**: Marks: New file. Plot messages.
- **7.11**: The Mark button leaves Datalogger mode.
- **7.1**: PCF8574 buttons can wake the VMR from Datalogger sleep.
- **7.02**: Datalogger text.
- **7.01**: Datalogger messages.
- **7.0**: The BNO085 can be switched off. Slower logging rates (1 s–10 min). Datalogger mode sleeps between rows.

## 6.x · 1 Nov 2026

- **6.51**: Welcome box with the Aspect Wave logo.
- **6.5**: Renamed Vessel **Measurement** Recorder. Welcome box for new phones.
- **6.4**: Charge mode (deep sleep). Devices on the hotspot shown by name.
- **6.3**: Card 2 views: Graph, Clinometer, G-meter. Numbered events.
- **6.25**: GPS sentence rate shown.
- **6.24**: Fixes.
- **6.23**: The OLED is set up again after a bus reset.
- **6.22**: Cast button. Start / Stop cast on the page.
- **6.21**: GPS settings card. Lat / lon format per phone.
- **6.2**: Location marks on the OLED (replaces the stern view). Mark button. Marks file on the microSD card.
- **6.1**: Location marks with notes.
- **6.0**: Fixes.

## 5.x · 31 Oct – 1 Nov 2026

- **5.9**: Clinometer zoom. Drag pointers on the G-meter's vertical g.
- **5.8**: Clinometer and G-meter scales.
- **5.7**: Scale arrows for the clinometer and G-meter views.
- **5.6**: Sky plot card.
- **5.5**: Show / hide yaw and GPS in card 1.
- **5.4**: GM moves to the Maxima card. Sky plot and G-meter drawn on the page.
- **5.3**: Diagnostics: page and data timing. Copy buttons.
- **5.2**: Memory shown in the System card.
- **5.1**: Fixes. (There is no 5.0.)

## 4.x · 30–31 Oct 2026

- **4.9**: The recording is checked against the card every minute.
- **4.8**: Plot from file restarts a short read.
- **4.7**: microSD clock lowered to 10 MHz.
- **4.6**: A warning when recording with no microSD card.
- **4.5**: Fixes.
- **4.4**: One ADS7830. Save to System for plots. GPS on / off. Credits.
- **4.3**: Brightness from one place, the knob or the slider. Displacement in LT or MT.
- **4.2**: Knob and switch on the Feather or the I2C boards, or None.
- **4.1**: Steadier knob brightness. Reset I2C bus includes the add-ons. Developer settings section.
- **4.0**: At the dock: a 30 s–5 min averaging window.

## 3.x · 29–30 Oct 2026

- **3.9**: Plot from file reads files with long headers.
- **3.8**: The IMU's power rail is held low during a power-cycle, so the BNO085 really resets. Long log lines are no longer cut.
- **3.7**: Gentler recovery when the IMU goes quiet. I2C back at 400 kHz.
- **3.6**: Fixes.
- **3.5**: I2C at 100 kHz, as a trial.
- **3.4**: No more hang when the BNO085 drops off the bus mid-command.
- **3.3**: The CPU slows to 80 MHz with no phone connected.
- **3.2**: The Dock state is kept in the VMR. Save image for the Waves card.
- **3.1**: Wave columns in the CSV.
- **3.0**: Waves card: encounter period, roll period, synchronous / parametric flags, heave.

## 2.x · 29 Oct 2026

- **2.9**: Hull-form checks (Cb, Cwp, TPI, displacement).
- **2.8**: More plot items.
- **2.7**: Trim and list columns, from the dock calibration.
- **2.6**: GM in feet in the CSV. Add-on faults in the hardware check.
- **2.5**: GM in feet, with metres beside it.
- **2.4**: Weather graph.
- **2.3**: Add-on part descriptions.
- **2.2**: I2C add-ons card (BMP388, SHT30, PCF8574, ADS7830) and the Weather card.
- **2.1**: G-meter layout.
- **2.0**: Archive selected files.

## 1.x · 29 Oct 2026

- **1.9**: microSD Archive tab.
- **1.8**: A new file each day option. Archive.
- **1.7**: Drafts in feet-inches.
- **1.6**: The IMU is power-cycled once if it does not start. Copy works on plain http.
- **1.5**: Fixes.
- **1.4**: Fixes.
- **1.3**: Firmware update progress on the OLED.
- **1.2**: Cards renumbered. Wi-Fi health. Instructions behind buttons.
- **1.1**: Ship data card, stern view to scale, hardware check.
- **1.0**: First VMR (Vessel Motion Recorder). Wi-Fi dashboard with motion, graph, OLED control, maxima, roll statistics, level zero, GM estimate, recording, microSD files, plot from file, serial monitor, GPS diagnostics, system and firmware update over Wi-Fi. Charge mode.

## 0 · 24 Oct 2026: Ship Motion Monitor

- The starting sketch: roll, pitch and yaw from the BNO085. Five OLED screens: live, roll graph, position, all-time maxima and GPS. OLED buttons for graph limits, level zero and maxima reset. Knob for brightness, switch for display on / off. Daily CSV on microSD at 10 Hz. GPS-set RTC. A basic Wi-Fi hotspot dashboard.
