#!/bin/sh
# SPDX-FileCopyrightText: 2026 Aspect Wave LLC
# SPDX-License-Identifier: GPL-3.0-or-later
# VMR demo: builds vmr.wasm, the firmware's screens, buttons, LEDs, 7-segment and LCD code for the browser.
# Needs zig (pip install ziglang) and the Arduino libraries Adafruit GFX and Adafruit SH110X.
# usage: sh build_wasm.sh [Arduino libraries folder]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
LIB=${1:-$HOME/Arduino/libraries}
VMR=$HERE/../../../VMR
B=$HERE/build
VER=$(sed -n 's/^#define FW_VERSION *"\([^"]*\)".*/\1/p' "$VMR/VMR.ino")
rm -rf "$B"; mkdir -p "$B/Fonts"
cp "$LIB/Adafruit-GFX-Library/Adafruit_GFX.cpp" "$LIB/Adafruit-GFX-Library/Adafruit_GFX.h" "$LIB/Adafruit-GFX-Library/gfxfont.h" "$LIB/Adafruit-GFX-Library/glcdfont.c" "$B/"
cp "$LIB/Adafruit-GFX-Library/Fonts/TomThumb.h" "$B/Fonts/"
cp "$LIB/Adafruit-GFX-Library/Fonts/FreeSansBold24pt7b.h" "$LIB/Adafruit_SH110X/splash.h" "$B/"
python3 "$HERE/extract.py" "$VMR" "$B"
python3 -m ziglang c++ --target=wasm32-wasi -Os -std=c++17 -w -DARDUINO=100 -DFW_VERSION=\"$VER\" -fno-exceptions -mexec-model=reactor \
  -I"$B" -I"$HERE" -I"$HERE/shim" -I"$VMR" -Wl,--export-dynamic -Wl,--strip-all \
  "$HERE/w.cpp" "$B/Adafruit_GFX.cpp" -o "$HERE/vmr.wasm"
rm -rf "$B"
echo "vmr.wasm (firmware $VER): $(wc -c < "$HERE/vmr.wasm") bytes"
