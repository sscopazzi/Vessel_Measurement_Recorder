# SeaLabMOS WTB (wet-talk board)

An ESP32-S2 Feather in the VMR's box with an Ethernet FeatherWing and a microSD breakout. Powered with the IWB through the VMR's latching relay.

- Talks to the VMR over encrypted ESP-NOW (`vmr_link.h`, the same file as in `VMR/`).
- Reads the IWB's `$SLM` / `$SLH` / `$SLF` lines over TCP (10.77.0.2:7700), keeps every 5 Hz sample on its card, asks for a replay after a link drop.
- Runs cast sessions: casts start and stop on depth or by hand; each down cast is loop-edited, binned at 0.25 m and averaged for density.
- Niskin bottles: six buttons on a PCF8574 (5545, 0x20), held 1.5 s to fire; their rings and the ready LED on a 6318 driver (0x21); fires through the IWB, from the VMR's Fire control card, or at planned depths.
- Buttons: Tare (A0), Start cast (A1); Ethernet-down LED (A2).

Files: `/sess/` (every sample of a session), `/casts/` (each cast's raw, profile and `_bottles.csv`).

The interface is in `docs/VMR_WTB_IWB_Interface_Spec`, the wiring in `docs/wiring/WTB_1.11_wiring.png`.
