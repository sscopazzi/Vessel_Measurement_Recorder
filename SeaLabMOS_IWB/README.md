# SeaLabMOS IWB (in-water board)

The wired SeaLabMOS CTD: an ESP32-S2 Feather with an Ethernet FeatherWing in the tube, powered over the network cable (passive PoE, 12 V) from the WTB.

- Samples the Bar30 (pressure) and TSYS01 (temperature) at 5 Hz and the EZO-EC at about 1 Hz; SHT40 in the tube's air (dew point = leak sign).
- Practical salinity (PSS-78) from EC × EC_GAIN, and in-situ density (TEOS-10, Absolute Salinity taken as Reference Salinity).
- Streams `$SLM` (samples), `$SLH` (housekeeping) and `$SLF` (firing capacitor) lines on TCP 7700; keeps 30 min of samples for replay.
- Fires the Niskin bottles: a 6318 solenoid driver (0x20), solenoids on channels 0–5, from a 10,000 µF capacitor charged through 47 Ω; the capacitor is read on A5.
- Firmware: through the VMR and WTB, or over its own Wi-Fi on deck for the first 10 min after power-up (`/update`).

The line formats and commands are in `docs/VMR_WTB_IWB_Interface_Spec`, the wiring in `docs/wiring/IWB_1.11_wiring.png`.
