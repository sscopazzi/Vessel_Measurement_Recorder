# SPDX-FileCopyrightText: 2026 Aspect Wave LLC
# SPDX-License-Identifier: GPL-3.0-or-later
# VMR demo: copies the firmware's own code into the wasm build, unchanged.
#   fw.inc:   helpers the screens use (displays lit, sensors present, the battery estimate, the bottles' status)
#   draw.inc: VMR.ino from printAt() to the end of drawScreen(), and drawCtd() from slm.ino (every OLED screen)
#   ui.inc:   the buttons, knob, screens' order, hold menu, LEDs, 7-segment, LCD, Datalogger peek, boot list and
#             marks screen (the functions listed below)
# usage: python3 extract.py <the VMR folder> <the build folder>
import re, sys, os
vdir, bdir = sys.argv[1], sys.argv[2]
src = open(os.path.join(vdir, 'VMR.ino'), encoding='utf-8').read()
lines = src.split('\n')
slm = open(os.path.join(vdir, 'slm.ino'), encoding='utf-8').read().split('\n')
def func(sig, lines=lines):
    for i, l in enumerate(lines):
        if l.startswith(sig):
            depth = 0; out = []; started = False
            for j in range(i, len(lines)):
                s = lines[j]; out.append(s)
                # strip strings/chars/comments for brace counting
                t = re.sub(r'"(\\.|[^"\\])*"', '""', s); t = re.sub(r"'(\\.|[^'\\])*'", "''", t); t = t.split('//')[0]
                depth += t.count('{') - t.count('}')
                if '{' in t: started = True
                if started and depth == 0: return '\n'.join(out)
    sys.exit('not found: ' + sig)
names = [
 'struct Cd {',
 'bool segAvail(', 'bool lcdAvail(', 'float airTempC(', 'float windShownKn(', 'uint8_t segFont(', 'void segText(', 'bool cdFx(', 'void segCompose(',
 'void lcdPad(', 'void lcdRight(', 'void lcdKV(', 'void lcdTurn(', 'void lcdCompose(',
 'void displayStep(', 'void segStep(',
 'bool screenNeedsImu(', 'bool screenAvail(', 'bool screenInCycle(', 'void serviceCastScreen(', 'void serviceDlScreen(', 'void serviceScreenAvail(', 'void screenStep(',
 'int brightPct(', 'int knobSelPct(', 'uint8_t hgCount(', 'uint8_t hgAt(', 'uint8_t hgPos(', 'int hgTop(', 'uint8_t holdBand(', 'bool holdOk(', 'bool cdRunning(',
 'void holdLabel(', 'bool holdNeedsConfirm(', 'void wifiSetOff(', 'void holdDo(', 'void oledBlankSet(', 'void cfmCancel(', 'void serviceButtons(', 'void serviceXb(',
 'void serviceMarkBtn(', 'void servicePot(', 'void setPaused(', 'void serviceLeds(', 'bool dlShowNow(',
 'void drawLoggerPeek(', 'void bootDraw(', 'void bootShow(', 'void bootText(', 'void bootLine(', 'void markOledBuild(',
 'static void rtRhumbF(', 'static void rtLegFrom(', 'void drawVoyage(',
]
pre = ['// ---- copied from VMR.ino and slm.ino by extract.py (do not edit here)']
for n in ['bool dispLit(', 'bool dispShown(', 'bool haveImuNow(', 'bool haveGpsNow(', 'bool haveWindNow(', 'int ledDutyNow(', 'void ledMarkEvent(',
          'bool battSettled(', 'uint8_t battEst(']: pre.append(func(n))
for n in ['bool nkEnabled(', 'bool slmBtFresh(', 'void slmBtGet(']: pre.append(func(n, slm))
open(os.path.join(bdir, 'fw.inc'), 'w').write('\n'.join(pre) + '\n')
out = ['// ---- copied from VMR.ino by extract.py (do not edit here)']
for n in names: out.append(func(n))
txt='\n\n'.join(out)+'\n'
# serviceLeds(): its pin writes at the end become ledOut[], which the page reads
a=txt.index('  bool sw = ledSwitchOn();')
b=txt.index('\n}', a)
txt=txt[:a]+'''  bool sw = ledSwitchOn();
  int duty = ledDutyNow();
  for (int i = 0; i < LED_N; i++) ledOut[i] = test >= 0 ? (test == i ? 1023 : 0) : (on[i] && sw ? duty : 0);'''+txt[b:]
open(os.path.join(bdir, 'ui.inc'), 'w').write(txt)
a = next(i for i, l in enumerate(lines) if l.startswith('void printAt('))
b = next(i for i, l in enumerate(lines) if l.startswith('void drawScreen()'))
d = func('void drawScreen()').count('\n')
open(os.path.join(bdir, 'draw.inc'), 'w').write('\n'.join(lines[a:b + d + 1]) + '\n' + func('void drawCtd()', slm) + '\n')
print('fw.inc:', len(pre) - 1, 'functions; ui.inc:', len(out) - 1, 'functions; draw.inc:', b + d + 1 - a, 'lines')
