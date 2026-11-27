# SeaLabMOS WTB changelog

Newest first. From 2.0.0, [Semantic Versioning](https://semver.org) (MAJOR.MINOR.PATCH), as set out under Versions in `README.md`. Up to 1.11, hundredths for small changes, tenths for larger ones.

- **2.0.0** (26 Nov 2026): MAJOR: link version 2 (`vmr_link.h`), with firmware versions in 16 bytes (up to 15 characters). It works only with VMR 14.0.0 or later: update the IWB, then the WTB, through the VMR while it is still on 13.x, then the VMR. The IWB's version is passed on in full. The VMR's time frame is named VMR (`T_VMR`, `BVmr`), not SHIP. Licence headers (GPL-3.0-or-later with the GSW additional permission).
- **1.11** (21 Nov 2026): No crash when there is no memory for a cast's bins (the profile is skipped). A WTB file delete can't remove the open cast's raw file. The Niskin hardware line is printed only when it changes.
- **1.1** (18 Nov 2026): Niskin bottles: six buttons (5545), their rings and the ready LED (6318 at 0x21), firing through the IWB, planned depths, the BOTTLES / BOTTLE frames and each cast's `_bottles.csv`.
- **1.0** (14 Nov 2026): First release: ESP-NOW link to the VMR, Ethernet to the IWB, cast sessions, binned profiles, files and firmware relay.
