// SPDX-FileCopyrightText: 2026 Aspect Wave LLC
// SPDX-License-Identifier: GPL-3.0-or-later
// SeaLabMOS: types used by slm.ino, here so the sketch's automatic prototypes see them
#pragma once
#include "vmr_link.h"
struct SlmRx { uint8_t mac[6]; uint8_t len; uint8_t d[LNK_MAX]; };
struct TracePt { float d, t, sp, rho, z; };   // sea pressure (dbar), temperature, salinity; in-situ density and depth (m)
struct SlmEv { uint32_t unix; uint8_t code; char text[64]; };
struct VPend { bool used; uint8_t type; uint16_t seq, len; uint8_t d[LNK_MAX - sizeof(LnkHdr)]; uint32_t lastMs, bornMs; };
struct InaState { float a = NAN, aAvg = NAN, ahUsed = NAN; uint32_t lastMs = 0; };     // the 12 V battery (INA260)
struct FwRelay { volatile uint8_t st = 0; uint8_t target = 0; uint32_t size = 0, crc = 0; volatile uint32_t acked = 0; char text[48] = ""; };   // st 0 idle, 1 sending, 2 done, 3 failed
// defined in slm.ino, used earlier in VMR.ino
extern bool slmArmed, slmDisarming, ctdCastOpen;
extern InaState ina;
extern FwRelay fwr;
// zeroed arrays in PSRAM, made before setup() (static RAM is the Wi-Fi's)
struct SlmPs { template <class T> static T* make(size_t n) { void* p = heap_caps_calloc(n, sizeof(T), MALLOC_CAP_SPIRAM); if (!p) p = calloc(n, sizeof(T)); return (T*)p; } };
#define slmPs SlmPs::make
#define psNew SlmPs::make                    // any zeroed array in PSRAM (internal RAM if there is none); free() it
