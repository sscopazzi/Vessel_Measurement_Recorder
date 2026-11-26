// SPDX-FileCopyrightText: 2026 Aspect Wave LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <math.h>
#include <string>
using std::isnan;
#define radians(d) ((d)*0.017453292519943295)
struct String{ std::string v; String(const char*c=""):v(c){} unsigned length()const{return v.size();} const char* c_str()const{return v.c_str();} };
#define ARDUINO 100
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t*)(a))
#define pgm_read_word(a) (*(const uint16_t*)(a))
#define pgm_read_dword(a) (*(const uint32_t*)(a))
#define pgm_read_pointer(a) (*(void* const*)(a))
typedef bool boolean;
class __FlashStringHelper;
#include "Print.h"
