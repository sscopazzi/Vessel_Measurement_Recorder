# Known limitations

What the Vessel Measurement Recorder (VMR) cannot do, or does only approximately. Read this before relying on a number.

## Not a certified instrument

- The VMR is not type-approved and is not a navigation, stability or loading instrument. Use the ship's approved loading computer, gyro and instruments for decisions.
- The accuracy figures for its parts are the manufacturers' datasheet values. The accuracy of the assembled VMR as a system has not been measured.

## Motion

- **Roll and pitch** come from the BNO085's Game Rotation Vector (gyro + accelerometer). The datasheet gives no dynamic roll / pitch accuracy, and the VMR's own has not been measured. The box must be fixed rigidly to the ship, and level zero set with the ship upright; any wobble shows up as roll and pitch.
- **No heading.** Yaw is relative and drifts, so the VMR logs the rate of turn, not a heading. COG stands in for heading wherever one is needed.
- **The BNO085 drops off the I2C bus** now and then. The VMR recovers by itself (re-requesting reports, then power-cycling the sensor), but the motion columns are blank for the seconds that takes. Recoveries are counted in the Hardware check card.

## GM

- **GM is a cross-check, not a stability figure.** It uses the IMO roll-period formula (IS Code 2008), whose C coefficient is an empirical fit, so it is only as good as the beam, draft and length entered in the Level zero and ship data card.
- It needs 8.5 min of steady motion and a clear roll peak. None is given at the dock, while paused or in calm water.
- When the ship is rolling hard at the wave period (forced rolling), the roll spectrum's peak can sit off the natural roll and pull GM off.
- It has been compared with the loading computer on only a few loading conditions so far.

## Waves

- **Heave Hs, Pressure Hs and Fused Hs are the ship's heave, not the wave height.** They are worked out over 1.6–20 s at the VMR, or moved to midships. The hull filters out short waves.
- The encounter period is the pitch spectrum's peak and the roll period the roll spectrum's peak. The VMR does not check for synchronous or parametric rolling (IMO MSC.1/Circ.1228).
- Estimates need 8.5 min of steady motion and are off at the dock or while paused.
- **Pressure Hs** is the same heave measured from the BMP388: 1 m of rise is only about 0.12 mbar. Anything else that moves the pressure in the wave band lands in it too: wind gusts on the bridge, doors, ventilation. It reads high in those conditions, and it needs the BMP388 working for at least the newest 8.5 min of the window.
- **Fused Hs** takes the motion's Hs wherever the motion and the pressure agree, and the pressure's (brought to the motion's scale by its gain) for long swell where they do not. Where neither this window nor the learned gain has a value it falls back on the motion's.
- **With the IMU off**, the pressure Hs is corrected by the gain learned while the IMU was on. That holds only while the sensor and where it sits stay as they were when the gain was learned; after moving the VMR or changing the box's venting, use Forget learned gain and let it learn again with the IMU on.

## Alarms

- **The anchor watch measures from the GPS antenna, not the bow or the anchor.** The circle has to allow for the scope and the length of the vessel, and GPS scatter (a few metres) moves the distance even at rest. Meant for small craft; a large ship's swing circle is better watched on its own instruments.
- The anchor alarm also comes on after 60 s with no GPS fix: the position is then unknown.
- **BNWAS** is reset only by a control on the VMR itself (button B, the screen button, Mark, an optional button or a knob turn), never from the dashboard. It is not a type-approved BNWAS and has no second-stage call to another cabin.
- Sound needs a piezo on free Feather pins (Pin mappings card); vibration needs the DRV2605L at 0x5A, which an AW9523 at 0x5A blocks. An alarm set to vibrate with no motor answering sounds instead. With neither fitted, alarms are visual only (OLED, Fault LED, dashboard).
- No alarms in Datalogger mode: the VMR sleeps between rows there.

## Trim and list (At the dock)

- The VMR measures the deck slope where it is mounted. Hull bending changes that slope near the ends, and 0.1° is about 1.6 ft of trim on a 280 m ship. Calibrate from the draft marks, and again after big loading changes.
- Without a calibration the trim follows level zero, so it is only as good as that.

## Magnetic

- **The magnetometer is unreliable on a steel hull.** The magnetic heading includes the ship's deviation, and it does not feed roll, pitch or anything else.
- Deviation is worked out against COG, not a gyro, so current and leeway end up in it.
- **Variation** comes from NOAA's World Magnetic Model 2025 at the GPS position. That has the model's own uncertainty: a few tenths of a degree at mid latitudes, much more near the magnetic poles. **The model is valid only to the end of 2029**; after that the firmware needs WMM2030. The PA1616D leaves the NMEA variation field empty.

## Wind

- True wind uses COG as the heading, so current and leeway put it off. Under 1 kn SOG, where COG means nothing, the true wind's direction comes only from the wind meter's north-mark heading typed in the Add-ons card (with none typed, only the speed and gusts are given), and its speed is the apparent speed.
- Motion correction for a masthead sensor is only as good as its height and offset entered in the Add-ons card, and the drafts in the Level zero and ship data card.
- Asking the wind meter at a set rate works only on units bought as weather stations, or set to on-demand with Calypso's software.

## Weather

- The BMP388 sits inside the box: its temperature is the box temperature, not the air. Air temperature and humidity come only from the SHT30 / SHT40, whose readings depend on where it is placed (sun, radiation, airflow).
- With the SHT30 heater ticked in the Add-ons card (the default), the heater runs for 2 min once the humidity has sat at 100 % for over a minute. Until the temperature is back within 0.2 °C of where it was (up to 15 min), the SHT30 reads warm and dry: the readings are still shown and logged, marked as heating or settling on the OLED and the dashboard, with the heater's events in the file.
- Pressure is at the sensor's height. Sea-level pressure needs keel to bridge and the drafts in the Level zero and ship data card.
- The 3-h pressure change needs 3 h of readings after a restart.
- The CPU temperature is the chip's own sensor. It reads well above the air, which makes it a check on a box cooking in the sun, not a thermometer.

## Position and time

- GPS at 1 Hz: about 3 m CEP (2.5 m with SBAS) per the PA1616D datasheet. COG means nothing when the ship is stopped.
- Without GPS the clock runs on the RTC and drifts. An open dashboard page can set the clock from a phone until GPS time arrives.

## Battery

- **3.7 V LiPo:** there is no current sensor. The % comes from the cell voltage on a discharge curve measured on one 2,500 mAh cell, at about 27 mA, at room temperature. A heavier load pulls the voltage down, so it reads a few % low; cold shifts it too. Near 3.90 V the curve is flat for hours and 1 mV is about 0.35 %, so the % moves in jumps there.
- Time left is the slope of that % over up to the last 4 h, started again whenever the VMR changes its own load, and shown from 20 min. Replayed over the test run it was 4 % off at the median and 16 % at the 90th percentile; it is less steady in its first hour and on the flat stretch near 3.90 V. The draw in mA needs the battery's Ah in the System card.
- On USB, and for 3 min after USB comes out, the % is the fuel gauge's own (a charging cell sits near 4.2 V whatever it holds). The gauge's own % is logged as `batt_gauge_pct` on every row: on these cells it reads far low.
- At 3.30 V the VMR shuts down and stays off: it does not wake or restart by itself, even once charged. Switch it off and on (or press RESET) to start it. About 3.5 % of the charge is below 3.30 V and is not used.
- In Datalogger sleep the battery is read only while the VMR is awake for a row, so the shutdown comes about 3 rows after the cell reaches 3.30 V (30 min at 10-min rows). The cell falls about 0.1 V an hour there, so the last rows can be written a little under 3.30 V.
- USB in / out is judged from a 0.04 V step in cell voltage. Steps within 30 s of the VMR changing its own load (Wi-Fi, CPU speed, Datalogger, displays, GPS) are ignored, so plugging or unplugging USB in that window is seen only once the gauge's rate turns (minutes). A charger too weak to lift the cell 0.04 V is never seen as a step.
- The low-battery fault (under 20 %) clears only when USB is seen and the gauge's rate is positive: a few minutes after plugging in.
- **12 V battery (INA260):** % is counted from the measured current against the capacity entered in the System card, started from a resting-voltage curve; with no capacity it is the curve alone, which reads high under load and low while charging. A wrong capacity or chemistry puts % off until the next full charge resets it. The INA260 measures only the current through it: a charger wired on the battery side of it is not seen.
- The low floors (LiFePO4 12.0 V, Li-ion 3S 10.8 V, lead-acid 11.9 V) are voltages under load: a heavy load (the IWB's capacitor charging, a Niskin firing) can dip to the floor briefly. Low is never shown while charging.

## Recording

- A recording is resumed after a crash (panic or watchdog), but not after a power loss: press Start again for a new file.
- Rows wait in a 256 KB buffer (about 3 min at 10 Hz) while the microSD card is busy or failing. A longer outage loses rows, and the file says how many.
- Datalogger sleep between rows works only at 1 min and slower with the IMU off. PCF8574 buttons wake it only with the INT wire to pin 5.
- In Datalogger sleep the GPS is in standby between rows and gets 20 s before each row to find a fix: with a poor sky view many rows have no position. The Start option "keep the GPS awake until each row has a fix" (the Recording card) waits up to 2 min past the row, which costs battery while it waits; a row whose fix comes late gets its position on a later row.
- The column groups left out (Recording card's Columns) are fixed for a recording at its Start; changing them takes effect at the next Start.

## SeaLabMOS (WTB, IWB, CTD)

- The link to the WTB is ESP-NOW on the hotspot's channel: range is that of the Feathers' antennas, and steel decks shorten it. With the link down the VMR shows NO WTB and logs nothing from the CTD; the WTB keeps its own 5 Hz files.
- The CSV gets the WTB's 1-s means only. The full-rate samples are on the WTB's card (the microSD files card's WTB tab, one file at a time).
- Depth and density are TEOS-10 from the sea pressure, the VMR's latitude and the EZO-EC salinity. Salinity is only as good as the conductivity cell's calibration (EC_GAIN); the EZO-EC's accuracy limits it.
- The pressure is tared in air (Tare, or the WTB's button): a tare taken in the water offsets every depth.
- An armed session survives a reset of the VMR but not a power-up: after a power loss, arm again.

## Niskin bottles

- A bottle fires only with the session armed, a CTD cast open, the WTB answering and the capacitor charged. After a firing the capacitor recharges from the 12 V through 47 Ω (RC 0.47 s; ready at 95 % of the supply, up to about 1.4 s from empty) before the next can fire.
- Planned depths fire on the way down only. Two planned depths closer than the lowering speed × the recharge time × 2 (and never under 0.5 m) are refused; if the capacitor is not ready in time and the CTD goes 1 m past, the bottle stays armed and an event says so.
- "Fired" means the IWB pulsed the solenoid; a capacitor drop under 0.4 V is flagged as no current drawn. Nothing sees the bottle close: check it on deck. A Fire request not delivered within 3 s is dropped, so it can never go off late.

## Displays and LEDs

- The AW9523 drives LEDs by sinking a constant current (about 18 mA at full): LEDs on it go from its VIN to the pin with no resistor. Its pins cannot drive the LCD's backlight harder than the LCD's own resistors allow. A PCF8574 pin only switches an LED or backlight colour on and off.
- Feather pins A4 and 9 belong to the relay with SeaLabMOS on, and A0 (or the pin chosen) to the laptop port's chip select: LEDs must go elsewhere (the AW9523).

## Wi-Fi and dashboard

- Plain http and a password written in the source: enough to make a casual user pause, not security.
- Up to 4 phones at once. The hotspot has no internet.
- Downloads are limited by the ESP32's hotspot. One day at 10 Hz is roughly 80 MB, so for weeks of data pull the microSD card.
- Updates over Wi-Fi need the "Minimal SPIFFS" partition scheme, flashed once over USB.
- A settings restore (System card) restarts the VMR; the all-time maxima stay as they are on this unit.

## Demo (docs/index.html)

- Its numbers come from a simulator: a made-up ship in a narrow-band sea, a steady wind with gusts, a weather trend and a battery drawn at a fixed rate. They show how the VMR behaves, not how well it measures.
- The OLED screens, buttons, knob, LEDs, 7-segment and LCD run the firmware's own code, and the dashboard is the firmware's own page. What answers the dashboard is a stand-in written for the demo, so some cards are simplified; downloads, Plot from file, archiving, settings restore and firmware updates are not in it, and the alarms show but make no sound.
- Time can run ×10 or ×60; it runs at ×1 while a button is down.

## Firmware

- Static RAM on the ESP32-S2 is nearly full. New features must put their large buffers on the heap (PSRAM), not in static memory.
- Some figures in the docs are estimates still to be measured: how long Plot from file takes for 24 h at 10 Hz, the Wi-Fi download speed and battery life.
