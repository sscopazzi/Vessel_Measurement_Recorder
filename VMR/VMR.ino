/*
  ============================================================================
   VESSEL MOTION RECORDER (VMR)
   Roll / pitch / yaw logger with OLED screens, SD logging, EEPROM maxima,
   GPS-disciplined RTC, and a Wi-Fi hotspot dashboard.

   Written by Sophie Scopazzi (Aspect Wave LLC) and Claude (Anthropic),
   2026.
  ============================================================================

  HARDWARE
    - Adafruit ESP32-S2 Feather w/ w.FL antenna (MAX17048 fuel gauge @ 0x36)
    - Adalogger FeatherWing (PCF8523 RTC @ 0x68, microSD CS = pin 10)
    - Ultimate GPS FeatherWing (PA1616D) on Serial1 (RX/TX pins), 9600 baud
    - FeatherWing OLED 128x64 (SH1107 @ 0x3C), buttons A=9, B=6, C=5
    - 24LC32 I2C EEPROM breakout (0x50) - STEMMA QT
    - BNO085 9-DOF breakout (0x4A) - STEMMA QT
    - Momentary pushbutton (screen select)       -> pin 12 and GND
    - 10K linear pot w/ switch:
        pot ends -> 3V and GND, wiper -> A5 (GPIO 8)
        switch   -> pin 11 and GND   (switch closed = display ON)
    - Latching power switch between battery and Feather (hardware only)

    A5 is used for the pot on purpose: it is on ADC1. A0-A4 are on ADC2,
    which does not read reliably while Wi-Fi is running on the ESP32.

  ARDUINO IDE SETUP
    Board:  "Adafruit Feather ESP32-S2"  (esp32 by Espressif core)
    Partition Scheme: "Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)"
      for firmware updates over Wi-Fi (card 16). The default "TinyUF2 No
      OTA" also runs everything else, but has no slot for an update.
    Libraries (Library Manager):
      Adafruit SH110X        (pulls in Adafruit GFX + Adafruit BusIO)
      RTClib                 (Adafruit)
      Adafruit GPS Library
      Adafruit BNO08x
      Adafruit MAX1704X
    SD, WiFi, WebServer are part of the ESP32 core.
    No EEPROM library needed - the 24LC32 is driven directly over Wire.

  CONTROLS
    Screen button (pin 12) or OLED button B ... next screen
      (while paused, B resumes)
    Screen 2 (roll graph, length set on the dashboard: 30 s / 1 / 2 / 5 / 10 min):
      Button A ................................ graph TOP (starboard) up 5 deg (wraps)
      Button C ................................ graph BOTTOM (port) down 5 deg (wraps)
    Screen 3 (position):
      Hold A + C for 3 s ...................... toggle level-zero (tare)
    Screen 4 (all-time maxima):
      Hold A + C for 3 s ...................... reset all-time maxima
    Screen 5 (GPS): display only
    Screen 6 (clinometer):
      Hold A + C for 3 s ...................... reset the drag pointers
    Screen 10 (G-meter):
      Hold A + C for 3 s ...................... reset the peaks
    Pot ....................................... OLED brightness
    Pot switch ................................ OLED on/off
    The dashboard can also pick the screen, brightness and on/off. The
    last change wins: the pot takes brightness back only when it is
    physically turned, and the switch acts only when it is flipped.
    Recording to the microSD card is started/stopped from the dashboard.

  BOOT: the Adafruit logo, then the Aspect Wave LLC logo, 4 s each on every
    boot (power-on, after a firmware update, any restart) at the lowest
    brightness, even with the OLED switched off; then the boot list, then
    the pot and switch apply.

  SCREENS
    1  Local time (ZD) + live roll/pitch/yaw/rate of turn + 1-minute max
    2  Roll graph over the chosen length (limits set with A / C or dashboard)
    3  Local + UTC time, lat/lon, averaged SOG / COG, GPS sats used / in
       view and best signal, then health: battery, phones on the Wi-Fi
       (weakest signal), CPU temperature, SD and IMU status
    4  All-time maxima from EEPROM with the date each was set
    5  GPS detail: fix type, sats, HDOP, lat/lon, altitude, SOG, COG (instantaneous), fix age
    6  Clinometer: needle = live roll on a +/-40 deg scale (starboard down
       swings right, as seen looking forward), with drag pointers at the
       largest roll each way since reset (hold A+C, or the dashboard)
    7  Stern view: the hull seen from astern tilting with the real roll
       over a fixed sea line; pitch on a +/-6 deg bar at the right
    8  Sky plot: every satellite in view (GSV) by elevation and azimuth,
       north up; filled = used in the fix (GSA), bigger = stronger signal
    9  At the dock: trim in feet-inches (by the stern / head) and list to
       0.01 deg, 60 s means; "UNCAL" until calibrated from the drafts
    10 G-meter: the felt push in the deck plane (up = forward, right =
       starboard; gravity included, so a starboard heel puts the dot to
       starboard), 0.3 s smoothed, with a short trail, rings every 0.1 g
       (0.2 g once a peak passes 0.3 g), peak-hold ticks (hold A+C to
       reset) and vertical g on the bar (0.5-1.5 g, 1 g = still)

  WI-FI DASHBOARD
    Join network AP_SSID (password AP_PASS), browse to http://192.168.4.1
    Header: local time (big) with ZD selector; UTC, date, battery and CPU
      temperature. ZD uses the nautical sign: UTC = LT + ZD (PDT is ZD +7).
      Until GPS time arrives, an open dashboard sets the clock (and RTC)
      from the phone or laptop on the hotspot ("time from WiFi") and,
      once per boot, ZD from its time zone. GPS takes over as soon as it has a fix.
      CPU temperature is the ESP32-S2's own die sensor: it reads above the
      air, more so with Wi-Fi busy - a check for a box cooking in the sun.
    Top right: Pause (tall) with the ZD selector under it; Logger (datalogger
      mode), Charge and the theme (Day / Night / Red) beside them.
    Controls: on/off buttons are highlighted when on; settings use < >
    arrows.

    Numbered cards, top to bottom:
    1  GPS: position; SOG and COG averaged over the SOG / COG average
       setting (1-60 s, default 10; set in card 14). COG is the direction of the average velocity, so
       it wraps through 000/360 correctly and near-zero speeds barely
       count; it shows -- below COG_AVG_MIN_KN. The small line has the
       instantaneous SOG / COG, altitude, sats and HDOP.
    2  Graph: roll (left axis, starboard up) and pitch (right axis, bow up
       up), 30 s / 1 / 2 / 5 / 10 min. Arrows above and below set each
       axis's top and bottom in 1 deg steps, 1-70 deg (default +/-5); the
       roll limits are shared with the OLED graph (A / C buttons step 5).
       5 and 10 min (here and on the OLED) draw an amplitude envelope: the
       highest and lowest angle within a sliding window of at least one
       roll (1.25 x the roll period, 8-30 s; 15 s until one is measured).
       Roll period: median of the last 9 full rolls (zero-crossings about
       the 60 s mean, 0.3 deg hysteresis); also on the OLED graph screen.
       The Views button (remembered per phone) adds a choice of view in
       place of the graph: Clinometer (with Reset drag pointers), Stern
       view, Sky plot or G-meter (with Reset peaks) - the same as OLED
       screens 6-8 and 10, from the live data
       (the satellite list is only sent while the sky plot is shown).
    3  Motion: live roll / pitch / yaw / rate of turn (deg/min). The Yaw
       button (per phone) hides yaw, rate of turn and the ROT maxima row.
       ROT deadband: smoothed ROT below it shows as 0 (screens + maxima;
       the SD log keeps the raw rate).
    4  OLED display: screen (all 10), brightness, On / Off, Flip (rotate 180 deg).
    5  Maxima: last 1 minute and all-time (the reset is in card 15).
    6  Roll statistics, last 10 or 30 min: significant roll (mean of the
       highest third of roll amplitudes, like significant wave height),
       largest roll, RMS roll about the mean, mean list, rolls counted,
       and rolls whose peak went beyond a set angle to starboard / port.
       A roll = one full cycle accepted by the roll-period detector; its
       amplitude is half its peak-to-peak swing about the running mean.
    7  Level zero: Set / Clear (same as holding A+C on screen 3), plus
       arrows that shift the roll or pitch reading 0.1 deg per tap.
    8  GM estimate from the roll period: enter beam, draft and LOA (kept
       in EEPROM). GM = (2 C B / T)^2, C = 0.373 + 0.023 B/d - 0.043 L/100
       (IMO IS Code 2008, A 2.3), with L = LOA - LOA_TO_LWL_M. A rough
       cross-check only: it assumes free rolling (rolling forced by beam
       seas at the wave period gives a wrong value). The card shows the
       mean of the last 10 min (a 5-s sample of the estimate), with a
       countdown until the window is full and a Restart button that
       clears it; new particulars also restart it. Display only: the
       CSV gm_m column is still the instantaneous estimate.
       LBP (optional) is used for the trim in 8b; GM keeps LOA - 5 m.
    8b At the dock (shown with the "At the dock" button in card 8;
       remembered per phone; also OLED screen 9): trim in feet = L x
       tan(pitch), by the stern / head, and list to 0.01 deg, both 60 s
       means of 1 s means, plus the live values and the draft difference
       across the beam that the list gives. L = LBP, or LOA - 5 m.
       Calibrate: type the forward and aft drafts read at the marks (and
       midships, optional) as feet-inches (33-04); the VMR stores the
       pitch offset that makes the last minute's mean equal that trim
       (EEPROM). Without it the trim follows level zero. Midships shows
       the hog / sag and quarter-mean draft at the time. The sensor reads
       the deck slope where it is mounted and hull bending changes that
       slope near the ends: 0.1 deg is about 1.6 ft on 280 m, so
       recalibrate when the loading changes much.
    9  Recording: START / STOP LOGGING. Nothing is written until Start.
       START asks for an optional name (added to the file name) and the
       loading condition (written in the file header); every Start opens
       a new file. While recording, "Mark event" (optional note) tags the
       next row in the event / event_note columns.
    10 microSD files: file manager (see below).
    11 Plot from file: pick a CSV and a span (last 1-48 h or whole file),
       tap the items to plot (Roll, Pitch, Battery V, SOG, Roll period,
       GM; "5-min mean" adds a 5-minute average line to every panel;
       remembered per phone), then Make plot. A task at idle priority
       reads the file (sampling and logging carry on) and sends 600
       columns (min / max / mean for roll and pitch, means for the rest),
       the mean of every value over the whole span (in each panel title
       once there is a minute of data: list for roll, trim for pitch) and
       the event markers. The phone draws a PNG in the page's theme
       colours, with local time from the file's ZD and each axis rounded
       out to whole units of its data (degrees, knots, s; 0.1 V and
       0.1 m). Roll period and GM also get a dashed line at that
       whole-span mean. Long-press the image or tap Save image.
       Untested estimate: 1-3 min for 24 h at 10 Hz.
    Developer settings (button below card 11; remembered per phone). The
    button shows a warning count if the hardware check finds anything.
    12 Hardware check (red when something is wrong): a crash / watchdog /
       brownout restart (where the loop was, core-dump backtrace; Sketch >
       Export Compiled Binary gives the .elf for decoding), I2C parts not
       answering, IMU recoveries (see below).
    13 Serial monitor: the last 10 lines the VMR printed (the same text as
       the USB Serial Monitor, 115200 baud; the VMR keeps the last 24).
       "Show on OLED" (latching) puts them on the OLED in a small font
       (32 characters x 10 lines), newest at the bottom, even if the OLED
       is switched off, until tapped again;
       the OLED then goes back to its screen. Not kept across a restart.
    14 GPS diagnostics: module talking or silent, fix type, satellites
       used / in view (GPS and GLONASS), strongest signal (dB-Hz), antenna
       in use (internal / external active / fault), HDOP, plus Restart GPS
       (re-send set-up) and Cold start (forget saved satellite data,
       search from scratch).
       Also the SOG / COG average (1-60 s, default 10).
    15 System: Restart VMR (closes the log file first); Wi-Fi health
       (this phone's signal at the VMR, phones connected, this page's own
       update timing - no extra traffic); Reset I2C bus; Reset maxima.
    16 Firmware update over Wi-Fi: upload the compiled app image
       (Sketch > Export Compiled Binary, VMR.ino.bin). The VMR checks it is
       an ESP32-S2 app, writes it to the spare app slot, verifies it and
       restarts into it; anything wrong and the old firmware keeps running.
       Logging stops when the upload starts and the new firmware starts
       fresh, not recording. Settings, level zero and maxima are kept.
       The OLED shows the update even if it is switched off (full
       contrast): the Aspect Wave logo over a progress bar.
       Needs a partition scheme with OTA, flashed once over USB: Tools >
       Partition Scheme > "Minimal SPIFFS (1.9MB APP with OTA/128KB
       SPIFFS)". The default TinyUF2 No OTA scheme has no spare slot (the
       card says so).

    PAUSE (top right): for moving the VMR. Live values keep updating, but
      no rows are logged and nothing is counted: maxima, the 1-min window,
      roll period and statistics, and the 10-min GM average all hold. While
      recording, an event row "PAUSED" is written as it stops and
      "RESUMED" when rows start again, so the gap shows in the file and on
      plots. The OLED shows a full-screen PAUSED notice (even if switched
      off). Resume here or with OLED button B. Not kept across a restart. If it is mounted differently after
      the move, set Level zero again (works while paused) before Resume.

    CHARGE (top right): low-power mode for charging. Turns off the OLED,
      logging, the GPS (standby) and the BNO085's sensors, drops the CPU
      to 80 MHz and lowers Wi-Fi power. Wi-Fi and the battery gauge stay on
      (the STEMMA QT rail too: it powers the I2C pull-ups the gauge needs).
      Not kept across a restart.
    LOGGER (top right): datalogger mode, only while recording (it never
      starts a recording itself). Recording continues; Wi-Fi and the
      OLED switch off and the CPU slows. The red LED blips every 5 s.
      Press OLED button B or the screen button to bring them back. Not
      kept across a restart.
    The hotspot has no internet; if your phone offers to "use cellular
    instead", decline / choose "stay connected".

  MICROSD FILE MANAGER (dashboard)
    Downloads and deletes are only available while logging is STOPPED
    (enforced by the firmware, not just the page).
    Lists the files on the card. Tap the arrow to download one file;
    select several (or "Download all") to get them as one .zip
    (uncompressed). Delete selected / Delete all are confirmed first.
    Downloads land in Downloads (Android) or Files > Downloads (iOS);
    tap the .zip there to unpack it.
    - The live dashboard numbers pause while a download is running.
    - Speed is limited by the ESP32 Wi-Fi hotspot (untested estimate:
      well under 1 MB/s). One day at 10 Hz is roughly 80 MB, so whole
      days take minutes each. For weeks of data, pull the card.
    - One .zip is limited to 4 GB and ZIP_MAX_FILES files.

  IMU ON I2C (BNO085 via STEMMA QT)
    The BNO085 stretches the I2C clock; Adafruit lists it as not working
    well with ESP32 parts, and it can drop out. Its driver also retries a
    failed write forever, so the firmware only sends it commands when it
    answers on I2C.
    Every report is read through imuEventHandler(): the library's
    getSensorEvent() keeps only the last report of each transfer, which
    lost the orientation whenever the gyro report shared its transfer
    and caused false "silent IMU" power-cycles while data was flowing.
    If no orientation arrives for IMU_SILENT_MS while the loop is polling
    (time the loop spends elsewhere, e.g. on the SD card, doesn't count):
      1. if the BNO085 answers on I2C, the reports are requested again;
      2. if it is still silent, or not answering, the STEMMA QT rail is
         power-cycled and the bus cleared. A power-cycle that brings no
         data back doubles the wait before the next (10 s ... 2 min).
    Both are counted in the Hardware check card. System card: "Reset I2C
    bus" does step 2 by hand. If the driver still hangs, the main-loop
    watchdog restarts the VMR (recording stops; press Start again).

  SIGN CONVENTION / MOUNTING
    Mount the BNO085 flat, chip side UP, X arrow toward the bow. Y then
    points to PORT (right-handed axes). Roll is about the fore-aft axis,
    pitch about the athwartships axis.
    Roll is shown as S (starboard side down) or P (port side down).
    In the SD log roll_deg is signed: + = starboard down, - = port down.
    Pitch: + = bow up, - = bow down.
    Yaw increases, and rate of turn is +, when turning to STARBOARD
    (clockwise seen from above), like a ship's heading.
    Check once: starboard down must read S, bow up must read +, a turn to
    starboard must read ROT +. If any is backwards, set ROLL_FLIP /
    PITCH_FLIP / YAW_FLIP to 1.
    Yaw uses the Game Rotation Vector by default (gyro + accel, no
    magnetometer), so it is a RELATIVE heading that drifts slowly - not a
    compass heading. On a steel hull the magnetometer is unreliable anyway.
    Yaw rate is shown and logged as rate of turn in deg/min. Its maximum
    is peak |rate of turn|, since a maximum of a 0-360 heading has no
    meaning.

  SD LOG
    Written only between START and STOP on the dashboard. Every Start
    opens a new file named by the UTC time START was tapped in the name
    box, plus the name typed there (letters, digits, - and _, up to 23):
    /YYYYMMDD_HHMMSSZ_name.csv, or /YYYYMMDD_HHMMSSZ.csv with no name
    (no valid clock yet: /NOCLK_001_name.csv ...). If power is lost while
    recording, it is NOT resumed: only START LOGGING starts a recording
    (or creates a file). The dashboard says the last recording was cut
    off. Times in the log are UTC.
    The file starts with metadata lines beginning with # (system, firmware
    build, credits, device id, file name, name suffix, loading condition, start
    time and time source, ZD, log / GPS / IMU rates, axis conventions,
    level-zero offsets, ship particulars, notes on the derived columns).
    Read it with pandas as pd.read_csv(path, comment='#').
    Columns: utc_time, millis, roll_deg, pitch_deg, yaw_deg, yaw_rate_dpm,
             gps_lat, gps_lon, gps_alt_m, gps_speed_ms, gps_speed_kn,
             gps_angle_deg, gps_fix, gps_sats, gps_hdop, gps_fix_quality,
             gps_age_ms, batt_v, batt_pct, zero_applied, roll_period_s,
             gm_m, event, event_note, acc_fwd_g, acc_stbd_g, acc_up_g
    acc_*: the BNO085 accelerometer (gravity included, i.e. specific
    force, what is felt) in ship axes, in g, the mean of the ~5 samples in
    each row; level and still = 0, 0, +1. Heeled to starboard acc_stbd_g
    is negative (the deck pushes up-slope); level-zero tilt is removed
    when it is on. Subtract gravity using roll / pitch for motion only.
    They come after event_note so older files keep the same column
    numbers.
    roll_period_s and gm_m are blank until a period is measured (gm_m also
    when no ship particulars are entered); event / event_note are blank
    except on the row a marker was written to. Event notes are plain ASCII
    up to 40 characters (commas, quotes and # are replaced by spaces).
    Rows are written at LOG_INTERVAL_MS (10 Hz) for the IMU. The GPS
    columns are filled only on the first row after a new GPS sentence
    (once per fix interval) and left blank on the rows in between, so no
    GPS value is ever duplicated. A fix-lost report is logged too
    (gps_fix = 0, sats filled, position blank). gps_age_ms is the delay
    from the fix arriving to the row being written (< 100 ms).

  GPS RATE (GPS_RATE_HZ)
    1, 5 or 10 Hz. A higher rate gives more fixes per second, not more
    accurate fixes. Per the PA1616D datasheet: 3.0 m CEP / 0.1 m/s
    unaided, 2.5 m / 0.05 m/s with SBAS - and SBAS only runs at <= 5 Hz.
    So 10 Hz switches SBAS off and is slightly LESS accurate per fix.
    1 Hz (default): ship position, SOG and COG change slowly, the motion
    data comes from the IMU, and 1 Hz runs at the GPS's stock 9600 baud
    with SBAS on. Above 1 Hz the GPS is switched to 115200 baud: RMC+GGA
    at 10 Hz is ~1.5 kB/s, more than 9600 baud carries.

  PROGRAM STRUCTURE
    loop() (the Arduino task) reads the GPS and IMU, keeps time, draws the
      OLED and writes the SD log. It is the only task that uses the I2C bus.
    webTask serves the dashboard. Changes asked for on the page are put in
      req* variables and applied by loop() in applyWebRequests(), so only
      one task ever touches I2C, the OLED or the settings image P. (A
      firmware update is written to flash by webTask as it arrives; loop()
      only draws its progress and restarts.)
    Log replaces Serial: everything printed also goes to a 24-line copy
      for the dashboard's serial monitor card and the OLED.
    plotTask is started by the "Plot from file" card and runs at idle
      priority. All SD access (loop, web, plot) goes through sdLock().
    Settings and all-time maxima live in P (struct Persist) and are saved
      to the EEPROM by servicePersist(); older layouts are migrated on load.
    crumb(ST_...) records what loop() is doing in RTC memory, so after a
      watchdog restart the dashboard can say where it was stuck.
    Comments like "---- ZIPCORE BEGIN / END ----" mark code that off-device
      tests extract and compile; keep them.

  SPEED UNITS
    The GPS reports speed over ground in knots. gpsSpeed is stored in m/s
    (knots x 0.514444). The SD log has both m/s and knots; screens show
    knots and m/s. COG (gpsAngle) is course over ground, not ship's head,
    and is meaningless when the ship is stopped.
  ============================================================================
*/

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <RTClib.h>
#include <Adafruit_GPS.h>
#include <Adafruit_BNO08x.h>
#include <Adafruit_MAX1704X.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <vector>
#include <algorithm>
#include <new>
#include "esp_system.h"
#include "esp_core_dump.h"
#include "esp_task_wdt.h"
#include "esp_attr.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include <Update.h>
#include <Fonts/TomThumb.h>            // 4 x 6 px font for the serial monitor on the OLED

// ============================== USER SETTINGS ===============================
static const char* AP_SSID = "Vessel Motion Recorder";   // up to 32 characters
static const char* AP_PASS = "somestupidcode";  // 8-63 characters (a nod to Feed)

// ---- Sensor mounting / signs (see SIGN CONVENTION in the header)
#define USE_GAME_ROTATION_VECTOR 1        // 1 = no magnetometer (recommended on steel ships)
#define ROLL_FLIP                0        // 1 if a tilt to starboard reads P
#define PITCH_FLIP               0        // 1 if bow up reads negative
#define YAW_FLIP                 0        // 1 if a turn to starboard reads ROT negative
#define USE_POT_SWITCH           1        // 0 if the pot switch is NOT wired (else display stays off)

// ---- Rates
#define IMU_REPORT_US          20000UL    // 50 Hz IMU reports
#define LOG_INTERVAL_MS          100UL    // SD log rate (10 Hz)
#define GPS_RATE_HZ                1      // 1, 5 or 10 (10 Hz disables SBAS - see header)
#define DISPLAY_INTERVAL_MS      200UL    // OLED refresh (5 Hz)
#define GRAPH_SAMPLE_MS          500UL    // graph sample spacing (2 Hz)
#define GRAPH_POINTS            1200      // 1200 x 0.5 s = 10 min (longest graph length); 2 x 4.8 KB heap
#define GRAPH_WEB_BUCKETS        120      // dashboard graph is sent as at most this many columns
#define GRAPH_ENVELOPE_S         300      // graphs this long or longer draw the roll/pitch envelope

// ---- Derived values
#define ROT_SMOOTH_S             1.0f     // rate of turn smoothing (s) for screens/maxima
#define ROLL_PERIOD_HYST_DEG     0.3f     // roll must swing past +/- this (about its mean) to count a cycle
#define ROLL_PERIOD_MEAN_S      60.0f     // running mean removed first, so list/heel doesn't matter
#define ROLL_PERIOD_CYCLES         9      // median of this many recent cycles
#define COG_AVG_MIN_KN           0.3f     // averaged COG shows -- below this averaged speed
#define LOA_TO_LWL_M             5.0f     // GM estimate: waterline length taken as LOA minus this
#define TARE_TAU_S              5.0f      // level-zero uses a 5 s running mean, not one sample
#define WARMUP_MS              10000UL    // ignore all-time maxima for first 10 s of IMU data

// ---- IMU recovery (see IMU ON I2C in the header)
#define IMU_SILENT_MS         5000UL      // no orientation this long (while being polled) -> recover the IMU
#define IMU_RECOVER_GAP_MS   10000UL      // power-cycles at most this often; doubles after each one
#define IMU_RECOVER_GAP_MAX_MS 120000UL   //   that brings no data back, up to this
#define I2C_POWER_OFF_MS          250      // STEMMA QT rail held off during a power-cycle (resets BNO085 + EEPROM)
#define I2C_POWER_SETTLE_MS       300      // wait after power-on before talking to them

// ---- Time
#define RTC_MAX_DRIFT_S            2      // re-set RTC from GPS if off by more than this (0 = never)
#define GPS_FIX_TIMEOUT_MS      3000UL    // fix older than this counts as lost

// ---- Storage
#define MAXIMA_SAVE_MIN_MS     60000UL    // at most one EEPROM write of new maxima per minute
#define SETTINGS_SAVE_DELAY_MS  2000UL    // save settings 2 s after the last change
#define SD_SPI_HZ           20000000UL    // microSD SPI clock (falls back to 4 MHz if this fails)
#define SD_PEND_BYTES          16384      // RAM buffer for log rows while the card is busy (~15 s)
#define ZIP_MAX_FILES            400      // max files in one .zip download

// ---- Screen, buttons, pot
#define HOLD_ACTION_MS          3000UL    // A+C hold time
#define GRAPH_STEP_DEG             5      // OLED buttons A / C (dashboard arrows step 1 deg)
#define GRAPH_LIMIT_DEG           70      // graph limits can be set 1..70 deg either way
#define GRAPH_DEFAULT_DEG          5      // graph limits after a settings reset: +/-5 deg
#define POT_TAKEOVER_COUNTS      160      // pot must move this far (of 4095, ~4%) to override the web brightness
#define SPLASH_MS               4000UL    // Adafruit logo on every boot, at lowest brightness
#define BOOTLIST_MS             5000UL    // hold the boot status list
#define BOOTLIST_FAIL_MS       12000UL    // hold longer if anything failed
#define LOGGER_BLINK_MS         5000UL    // datalogger mode: red LED blip this often

// ---- Diagnostics
#define LOOP_WDT_MS             8000      // main loop stuck this long -> automatic restart + report
#define HEARTBEAT_MS           10000      // "[alive]" status line on USB serial

// ================================== PINS ====================================
#define SD_CS           10
#define BUTTON_A         9
#define BUTTON_B         6
#define BUTTON_C         5
#define SCREEN_BTN_PIN  12
#define POT_PIN          8                // A5 (ADC1)
#define POT_SW_PIN      11

#define OLED_ADDR     0x3C
#define EEPROM_ADDR   0x50
#define BNO_ADDR      0x4A
#define EEPROM_BASE   0x0000
#define EEPROM_PAGE   32

// ================================ OBJECTS ===================================
// Aspect Wave LLC logo, 1 bit, rows MSB first (drawBitmap): full size for boot,
// small for the firmware update screen
#define AWLOGO_W 97
#define AWLOGO_H 62
static const uint8_t awLogo[] PROGMEM = {
  0x18, 0x00, 0x0F, 0x00, 0x0F, 0xC0, 0x03, 0xF8, 0x00, 0x7C, 0x00, 0x7F, 0x80, 0x1C, 0x00, 0x1F,
  0x80, 0x0F, 0xE0, 0x03, 0xF8, 0x00, 0xFE, 0x00, 0x3F, 0x80, 0x1C, 0x00, 0x30, 0x80, 0x0C, 0x30,
  0x03, 0x00, 0x00, 0xC6, 0x00, 0x0C, 0x00, 0x3C, 0x00, 0x30, 0x80, 0x0C, 0x30, 0x03, 0x00, 0x00,
  0x86, 0x00, 0x0C, 0x00, 0x34, 0x00, 0x18, 0x00, 0x0C, 0x30, 0x03, 0x00, 0x00, 0x80, 0x00, 0x0C,
  0x00, 0x26, 0x00, 0x0E, 0x00, 0x0C, 0x60, 0x03, 0xE0, 0x00, 0x80, 0x00, 0x0C, 0x00, 0x66, 0x00,
  0x07, 0x00, 0x0F, 0xC0, 0x03, 0xE0, 0x00, 0x80, 0x00, 0x0C, 0x00, 0x66, 0x00, 0x01, 0x80, 0x0C,
  0x00, 0x03, 0x00, 0x00, 0x80, 0x00, 0x0C, 0x00, 0x66, 0x00, 0x10, 0xC0, 0x0C, 0x00, 0x03, 0x00,
  0x00, 0x82, 0x00, 0x0C, 0x00, 0x7F, 0x00, 0x30, 0xC0, 0x0C, 0x00, 0x03, 0x00, 0x00, 0xC6, 0x00,
  0x0C, 0x00, 0xC3, 0x00, 0x19, 0x80, 0x0C, 0x00, 0x03, 0x00, 0x00, 0xCE, 0x00, 0x0C, 0x00, 0xC3,
  0x00, 0x0F, 0x00, 0x0C, 0x00, 0x03, 0xF8, 0x00, 0x7C, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x00, 0x38,
  0x00, 0x0E, 0x07, 0x03, 0x80, 0x00, 0xE1, 0xFF, 0xFF, 0x80, 0xF0, 0x00, 0x7C, 0x00, 0x1E, 0x0F,
  0x83, 0xC0, 0x01, 0xE1, 0xFF, 0xFF, 0x80, 0xF0, 0x00, 0x7C, 0x00, 0x1E, 0x0F, 0x83, 0xC0, 0x01,
  0xE1, 0xFF, 0xFF, 0x80, 0x70, 0x00, 0x7C, 0x00, 0x1C, 0x0F, 0x81, 0xC0, 0x01, 0xC1, 0xE0, 0x00,
  0x00, 0x78, 0x00, 0x7C, 0x00, 0x3C, 0x1F, 0xC1, 0xC0, 0x03, 0xC1, 0xC0, 0x00, 0x00, 0x78, 0x00,
  0xFE, 0x00, 0x3C, 0x1F, 0xC1, 0xE0, 0x03, 0xC1, 0xC0, 0x00, 0x00, 0x38, 0x00, 0xFE, 0x00, 0x3C,
  0x1D, 0xC1, 0xE0, 0x03, 0x81, 0xC0, 0x00, 0x00, 0x38, 0x00, 0xFE, 0x00, 0x38, 0x3D, 0xC0, 0xE0,
  0x03, 0x81, 0xC0, 0x00, 0x00, 0x3C, 0x00, 0xEF, 0x00, 0x78, 0x39, 0xE0, 0xE0, 0x07, 0x81, 0xC0,
  0x00, 0x00, 0x3C, 0x01, 0xEF, 0x00, 0x78, 0x38, 0xE0, 0xF0, 0x07, 0x81, 0xC0, 0x00, 0x00, 0x1C,
  0x01, 0xEF, 0x00, 0x70, 0x38, 0xE0, 0xF0, 0x07, 0x01, 0xC0, 0x00, 0x00, 0x1C, 0x01, 0xC7, 0x00,
  0x70, 0x78, 0xE0, 0x70, 0x07, 0x01, 0xC0, 0x00, 0x00, 0x1E, 0x03, 0xC7, 0x80, 0xF0, 0x70, 0xF0,
  0x70, 0x0F, 0x01, 0xC0, 0x00, 0x00, 0x1E, 0x03, 0xC7, 0x80, 0xF0, 0x70, 0x70, 0x78, 0x0F, 0x01,
  0xC0, 0x00, 0x00, 0x0E, 0x03, 0xC3, 0x80, 0xE0, 0x70, 0x70, 0x38, 0x0E, 0x01, 0xC0, 0x00, 0x00,
  0x0F, 0x03, 0x83, 0x80, 0xE0, 0xF0, 0x78, 0x38, 0x0E, 0x01, 0xFF, 0xF0, 0x00, 0x0F, 0x07, 0x83,
  0xC1, 0xE0, 0xE0, 0x78, 0x38, 0x1E, 0x01, 0xFF, 0xF0, 0x00, 0x07, 0x07, 0x83, 0xC1, 0xE0, 0xE0,
  0x38, 0x3C, 0x1C, 0x01, 0xFF, 0xF0, 0x00, 0x07, 0x07, 0x01, 0xC1, 0xC1, 0xE0, 0x38, 0x1C, 0x1C,
  0x01, 0xC0, 0x00, 0x00, 0x07, 0x87, 0x01, 0xC3, 0xC1, 0xE0, 0x3C, 0x1C, 0x1C, 0x01, 0xC0, 0x00,
  0x00, 0x07, 0x8F, 0x01, 0xE3, 0xC1, 0xC0, 0x3C, 0x1E, 0x3C, 0x01, 0xC0, 0x00, 0x00, 0x03, 0x8F,
  0x01, 0xE3, 0x81, 0xC0, 0x1C, 0x1E, 0x38, 0x01, 0xC0, 0x00, 0x00, 0x03, 0x8E, 0x00, 0xE3, 0x83,
  0xC0, 0x1C, 0x0E, 0x38, 0x01, 0xC0, 0x00, 0x00, 0x03, 0xCE, 0x00, 0xF7, 0x83, 0xC0, 0x1E, 0x0E,
  0x38, 0x01, 0xC0, 0x00, 0x00, 0x03, 0xDE, 0x00, 0xF7, 0x83, 0x80, 0x0E, 0x0F, 0x78, 0x01, 0xC0,
  0x00, 0x00, 0x01, 0xDE, 0x00, 0x77, 0x07, 0x80, 0x0E, 0x0F, 0x70, 0x01, 0xC0, 0x00, 0x00, 0x01,
  0xFC, 0x00, 0x7F, 0x07, 0x80, 0x0F, 0x07, 0x70, 0x01, 0xC0, 0x00, 0x00, 0x01, 0xFC, 0x00, 0x7F,
  0x07, 0x00, 0x0F, 0x07, 0xF0, 0x01, 0xC0, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x7F, 0x07, 0x00, 0x07,
  0x07, 0xE0, 0x01, 0xC0, 0x00, 0x00, 0x00, 0xF8, 0x00, 0x3E, 0x0F, 0x00, 0x07, 0x03, 0xE0, 0x01,
  0xC0, 0x00, 0x00, 0x00, 0xF8, 0x00, 0x3E, 0x0F, 0x00, 0x07, 0x83, 0xE0, 0x01, 0xFF, 0xFF, 0x80,
  0x00, 0xF8, 0x00, 0x3E, 0x0E, 0x00, 0x07, 0x83, 0xE0, 0x01, 0xFF, 0xFF, 0x80, 0x00, 0x78, 0x00,
  0x3E, 0x0E, 0x00, 0x03, 0x83, 0xC0, 0x01, 0xFF, 0xFF, 0x80, 0x00, 0x70, 0x00, 0x1C, 0x0C, 0x00,
  0x01, 0x81, 0x80, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x2C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x43, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xC0, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x81, 0x07, 0x80, 0x00, 0x00, 0x80, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81,
  0x04, 0x80, 0x00, 0x7F, 0x00, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0x81, 0x04, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x04, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0xF1, 0xC3, 0x00,
};
#define AWLOGO_SMALL_W 66
#define AWLOGO_SMALL_H 42
static const uint8_t awLogoSmall[] PROGMEM = {
  0x30, 0x07, 0x80, 0x78, 0x07, 0xC0, 0x38, 0x07, 0xC0, 0x30, 0x04, 0x80, 0x4C, 0x04, 0x00, 0x4C,
  0x01, 0x00, 0x70, 0x0C, 0x80, 0x4C, 0x04, 0x00, 0x44, 0x01, 0x00, 0x58, 0x06, 0x00, 0x4C, 0x04,
  0x00, 0x40, 0x01, 0x00, 0x58, 0x03, 0x80, 0x78, 0x07, 0x00, 0x40, 0x01, 0x00, 0x48, 0x00, 0x80,
  0x40, 0x04, 0x00, 0x40, 0x01, 0x00, 0xF8, 0x0C, 0x80, 0x40, 0x04, 0x00, 0x4C, 0x01, 0x00, 0x8C,
  0x07, 0x80, 0x40, 0x07, 0xC0, 0x78, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x0E, 0x00, 0xE3,
  0x86, 0x00, 0xE7, 0xFF, 0xC0, 0xE0, 0x0E, 0x00, 0xC3, 0x87, 0x00, 0xE7, 0xFF, 0xC0, 0x60, 0x1E,
  0x01, 0xC7, 0xC7, 0x00, 0xC7, 0x00, 0x00, 0x60, 0x1F, 0x01, 0xC7, 0xC7, 0x00, 0xC7, 0x00, 0x00,
  0x70, 0x1F, 0x01, 0x86, 0xC3, 0x01, 0xC7, 0x00, 0x00, 0x70, 0x1B, 0x01, 0x86, 0xE3, 0x81, 0xC7,
  0x00, 0x00, 0x30, 0x3B, 0x03, 0x8E, 0xE3, 0x81, 0x87, 0x00, 0x00, 0x38, 0x33, 0x83, 0x8C, 0x61,
  0x81, 0x87, 0x00, 0x00, 0x38, 0x33, 0x83, 0x0C, 0x61, 0x83, 0x87, 0x00, 0x00, 0x18, 0x71, 0x83,
  0x0C, 0x71, 0xC3, 0x07, 0x00, 0x00, 0x18, 0x71, 0x87, 0x1C, 0x31, 0xC3, 0x07, 0xFE, 0x00, 0x1C,
  0x61, 0xC7, 0x18, 0x30, 0xC3, 0x07, 0xFE, 0x00, 0x1C, 0x61, 0xC6, 0x18, 0x38, 0xC7, 0x07, 0x00,
  0x00, 0x0C, 0xE0, 0xCE, 0x38, 0x38, 0xE6, 0x07, 0x00, 0x00, 0x0C, 0xE0, 0xEE, 0x38, 0x18, 0xE6,
  0x07, 0x00, 0x00, 0x0E, 0xC0, 0xEC, 0x30, 0x18, 0x66, 0x07, 0x00, 0x00, 0x0F, 0xC0, 0x6C, 0x30,
  0x1C, 0x6E, 0x07, 0x00, 0x00, 0x07, 0xC0, 0x7C, 0x70, 0x1C, 0x7C, 0x07, 0x00, 0x00, 0x07, 0x80,
  0x7C, 0x70, 0x0C, 0x3C, 0x07, 0x00, 0x00, 0x07, 0x80, 0x78, 0x60, 0x0C, 0x3C, 0x07, 0x00, 0x00,
  0x03, 0x80, 0x38, 0xE0, 0x0E, 0x3C, 0x07, 0xFF, 0xC0, 0x03, 0x80, 0x38, 0xE0, 0x0E, 0x38, 0x07,
  0xFF, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x11, 0x40, 0x01,
  0xE0, 0x1F, 0xFF, 0xFF, 0xFF, 0xF0, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x98, 0xC0,
};

// ---- Serial monitor copy ----------------------------------------------------
// Everything the sketch prints goes through Log: it is sent to the USB Serial
// Monitor as before and the last SLOG_LINES lines are kept for the dashboard
// (developer settings) and the OLED. Lines longer than SLOG_LEN are cut in the
// copy only. Several tasks print, so the copy is guarded by a spinlock.
#define SLOG_LINES  24
#define SLOG_LEN    120
class TeeLog : public Print {
 public:
  void begin(unsigned long baud) { Serial.begin(baud); }
  size_t write(uint8_t c) override { Serial.write(c); keep(&c, 1); return 1; }
  size_t write(const uint8_t* b, size_t n) override { Serial.write(b, n); keep(b, n); return n; }
  using Print::write;
  uint32_t seq() const { return lineSeq; }          // lines completed since boot
  // copy line number i (0 = first since boot); false once it has scrolled out
  bool get(uint32_t i, char* out) {
    bool ok = false;
    portENTER_CRITICAL(&mux);
    if (i < lineSeq && lineSeq - i <= SLOG_LINES) { memcpy(out, lines[i % SLOG_LINES], SLOG_LEN + 1); ok = true; }
    portEXIT_CRITICAL(&mux);
    return ok;
  }
 private:
  void keep(const uint8_t* b, size_t n) {
    portENTER_CRITICAL(&mux);
    for (size_t i = 0; i < n; i++) {
      char c = (char)b[i];
      if (c == '\r') continue;
      if (c == '\n') {
        cur[curLen] = 0;
        memcpy(lines[lineSeq % SLOG_LINES], cur, SLOG_LEN + 1);
        lineSeq = lineSeq + 1;
        curLen = 0;
      } else if (curLen < SLOG_LEN) {
        cur[curLen++] = (c >= 0x20 && c <= 0x7E) ? c : (c == '\t' ? ' ' : '?');
      }
    }
    portEXIT_CRITICAL(&mux);
  }
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  char lines[SLOG_LINES][SLOG_LEN + 1] = {};
  char cur[SLOG_LEN + 1] = {};
  uint16_t curLen = 0;
  volatile uint32_t lineSeq = 0;
};
TeeLog Log;

// postclk = 400k so the OLED driver does not drop the shared bus back to 100 kHz
Adafruit_SH1107   display(64, 128, &Wire, -1, 400000, 400000);
RTC_PCF8523       rtc;
Adafruit_GPS      GPS(&Serial1);
Adafruit_BNO08x   bno08x(-1);
Adafruit_MAX17048 maxlipo;
WebServer         server(80);

// ================================= STATUS ===================================
bool displayOK = false, rtcOK = false, rtcValid = false, imuOK = false;
bool battOK = false, sdOK = false, eepromOK = false;

// ================================== IMU =====================================
float rawRoll = 0, rawPitch = 0, yawDeg = 0, yawRate = 0;
float roll = 0, pitch = 0;
bool haveImu = false;
uint32_t lastImuMs = 0, imuStartMs = 0, lastImuRetryMs = 0, lastImuRecoverMs = 0, imuRecoveries = 0;
uint32_t lastImuPollMs = 0;            // last serviceImu() call (a stalled loop is not IMU silence)
uint32_t imuQuietRefMs = 0;            // silence is timed from here: last orientation, or a restart
                                       // of the count (kept apart from lastImuMs, which marks real data)
uint32_t imuSoftRecoveries = 0;        // reports re-requested from an answering, silent BNO085
uint32_t imuRecoverGapMs = IMU_RECOVER_GAP_MS;
bool     imuSoftTried = false;         // soft step done for this silence; next step is a power-cycle
volatile bool reqI2cReset = false;     // dashboard "Reset I2C bus"
// Filled by imuEventHandler() for every report in a transfer
bool     imuGotOrient = false;
float    imuQr, imuQi, imuQj, imuQk;
int      imuEvents = 0;
int8_t   i2cPowerOff = -1;             // level of PIN_I2C_POWER that switches the STEMMA QT rail OFF
bool     imuSessionOpen = false;       // the BNO08x library's single session slot is in use
bool     imuStarted = false;           // begin_I2C() has succeeded (session + callbacks are good)
bool     imuNeedReports = false;       // after a power-cycle: re-enable reports once it answers
float tareRollAvg = 0, tarePitchAvg = 0;
// ---- ROLLPERIOD STATE BEGIN ----
float    rpMean = NAN, rpM2 = 0, rpPrevX = 0, rpMaxSince = 0, rpLp1 = 0, rpLp2 = 0;
uint32_t rpPrevMs = 0, rpLastCrossMs = 0;
bool     rpArmed = false;
float    rpHist[ROLL_PERIOD_CYCLES];
float    rpMinSince = 0, rpPkS = -INFINITY, rpPkP = INFINITY;   // this cycle: lowest x, highest / lowest roll

// ---- Roll statistics (dashboard card): every full roll cycle the period
// detector accepts is stored with its single amplitude (half the peak-to-peak
// swing about the running mean) and its peak angles (including any list).
// Roll mean and RMS come from 10 s blocks of every IMU sample.
#define RS_CYCLES   1024                 // > 30 min of rolls down to a 2 s period
#define RS_BLOCK_MS 10000UL
#define RS_BLOCKS   180                  // 30 min of 10 s blocks
struct RollCycle { uint32_t ms; int16_t amp10, pkS10, pkP10; };   // deg x 10
struct RsBlock   { float sum, sumSq; uint16_t n; };
struct RollStats { uint16_t n; float sig, maxAmp, mean, rms; uint16_t overS, overP; };
RollCycle* rsCyc = nullptr;              // heap, RS_CYCLES (12 KB; static RAM is full)
float*     rsSort = nullptr;             // heap, RS_CYCLES: scratch for the sort
uint16_t  rsHead = 0, rsCount = 0;
RsBlock   rsBlk[RS_BLOCKS], rsCur = {0, 0, 0};
uint16_t  rsBlkHead = 0, rsBlkCount = 0;
uint32_t  rsCurStartMs = 0, lastStatsMs = 0;
static const uint16_t RS_WIN_MIN[2] = {10, 30};
RollStats rStat[2];
float     gmC = NAN, gmEst = NAN;        // GM estimate from the roll period (m)
// Dashboard-only 10-min GM mean: one gmEst sample every 5 s (NaN when unavailable).
// The CSV still logs gmEst per row; this window only feeds card 8.
#define GM_WIN_S      600
#define GM_SAMPLE_MS  5000
#define GM_SLOTS      (GM_WIN_S * 1000 / GM_SAMPLE_MS)
float     gmWin[GM_SLOTS];
uint16_t  gmWinHead = 0, gmWinCount = 0;
uint32_t  gmWinSampleMs = 0;
float     gmAvg = NAN;                   // mean of the valid samples in the window
uint8_t  rpCount = 0, rpIdx = 0;
float    rollPeriod = NAN;             // s, median of recent full roll cycles (NaN = not measurable)
// ---- ROLLPERIOD STATE END ----

// =============================== PERSISTENT =================================
#define PERSIST_MAGIC    0x564D5237UL   // "VMR7"
#define PERSIST_MAGIC_V6 0x564D5236UL   // "VMR6": migrated on load (keeps maxima/settings)
#define PERSIST_MAGIC_V5 0x564D5235UL   // "VMR5": migrated on load (keeps maxima/settings)
#define PERSIST_MAGIC_V4 0x564D5234UL   // "VMR4": migrated on load
#define PERSIST_MAGIC_V3 0x564D5233UL   // "VMR3": migrated on load
// Older EEPROM layouts, kept only so loadPersist() can carry settings and
// maxima over from earlier firmware. Each newer layout adds fields at the end.
struct PersistV3 {
  uint32_t magic;
  int8_t   graphMin, graphMax;
  uint8_t  zeroApplied, logActive;
  int8_t   zdHours;
  uint8_t  graphSecIdx, sogAvgS, reserved;
  float    rollOffset, pitchOffset;
  float    rollMaxPos, rollMaxNeg, pitchMaxPos, pitchMaxNeg, yawRateMax;
  uint32_t tRollMaxPos, tRollMaxNeg, tPitchMaxPos, tPitchMaxNeg, tYawRateMax;
  uint16_t crc;
};
struct PersistV4 {
  uint32_t magic;
  int8_t   graphMin, graphMax;
  uint8_t  zeroApplied, logActive;
  int8_t   zdHours;
  uint8_t  graphSecIdx, sogAvgS, rotDeadband;
  float    rollOffset, pitchOffset;
  float    rollMaxPos, rollMaxNeg, pitchMaxPos, pitchMaxNeg, yawRateMax;
  uint32_t tRollMaxPos, tRollMaxNeg, tPitchMaxPos, tPitchMaxNeg, tYawRateMax;
  uint8_t  oledFlip, pad;
  uint16_t crc;
};
#define LOG_TAG_MAX 23                  // longest file-name suffix typed at Start
struct PersistV5 {
  uint32_t magic;
  int8_t   graphMin, graphMax;
  uint8_t  zeroApplied, logActive;
  int8_t   zdHours;
  uint8_t  graphSecIdx, sogAvgS, rotDeadband;
  float    rollOffset, pitchOffset;
  float    rollMaxPos, rollMaxNeg, pitchMaxPos, pitchMaxNeg, yawRateMax;
  uint32_t tRollMaxPos, tRollMaxNeg, tPitchMaxPos, tPitchMaxNeg, tYawRateMax;
  uint8_t  oledFlip;
  int8_t   pitchGraphMin, pitchGraphMax;
  uint8_t  pad;
  char     logTag[LOG_TAG_MAX + 1];
  uint16_t crc;
};
// Loading condition chosen at Start (written to the CSV header; 0 = not given)
static const char* const LOAD_COND[] = {"", "docked", "ballast (95%)", "ballast (98%)", "loaded (CP)", "loaded (LB)"};
#define LOAD_COND_N 6
struct PersistV6 {
  uint32_t magic;
  int8_t   graphMin;        // roll graph bottom (port, negative), deg
  int8_t   graphMax;        // roll graph top (starboard), deg
  uint8_t  zeroApplied;
  uint8_t  logActive;     // 1 = recording when last saved (for resume after power loss)
  int8_t   zdHours;       // zone description, whole hours (-12..+12); UTC = LT + ZD
  uint8_t  graphSecIdx;   // index into GRAPH_SECS
  uint8_t  sogAvgS;       // SOG / COG average, 1-60 s
  uint8_t  rotDeadband;   // rate of turn below this (deg/min) shows as 0
  float    rollOffset, pitchOffset;
  float    rollMaxPos, rollMaxNeg, pitchMaxPos, pitchMaxNeg, yawRateMax;
  uint32_t tRollMaxPos, tRollMaxNeg, tPitchMaxPos, tPitchMaxNeg, tYawRateMax;
  uint8_t  oledFlip;      // 1 = OLED rotated 180 degrees
  int8_t   pitchGraphMin; // pitch graph bottom (bow down, negative), deg - dashboard only
  int8_t   pitchGraphMax; // pitch graph top (bow up), deg
  uint8_t  pad;
  char     logTag[LOG_TAG_MAX + 1];   // suffix of the recording's file name (kept for resume)
  uint16_t shipBeamCm;    // ship particulars for the GM estimate (0 = not set)
  uint16_t shipDraftCm;
  uint16_t shipLengthDm;  // length overall (LOA), 0.1 m
  uint8_t  logCond;       // loading condition (index into LOAD_COND) of the current / last recording
  uint8_t  rollThrDeg;    // roll statistics: count rolls beyond this angle
  uint16_t crc;
};
struct Persist {
  uint32_t magic;
  int8_t   graphMin;        // roll graph bottom (port, negative), deg
  int8_t   graphMax;        // roll graph top (starboard), deg
  uint8_t  zeroApplied;
  uint8_t  logActive;     // 1 = recording when last saved (for resume after power loss)
  int8_t   zdHours;       // zone description, whole hours (-12..+12); UTC = LT + ZD
  uint8_t  graphSecIdx;   // index into GRAPH_SECS
  uint8_t  sogAvgS;       // SOG / COG average, 1-60 s
  uint8_t  rotDeadband;   // rate of turn below this (deg/min) shows as 0
  float    rollOffset, pitchOffset;
  float    rollMaxPos, rollMaxNeg, pitchMaxPos, pitchMaxNeg, yawRateMax;
  uint32_t tRollMaxPos, tRollMaxNeg, tPitchMaxPos, tPitchMaxNeg, tYawRateMax;
  uint8_t  oledFlip;      // 1 = OLED rotated 180 degrees
  int8_t   pitchGraphMin; // pitch graph bottom (bow down, negative), deg - dashboard only
  int8_t   pitchGraphMax; // pitch graph top (bow up), deg
  uint8_t  pad;
  char     logTag[LOG_TAG_MAX + 1];   // suffix of the recording's file name (kept for resume)
  uint16_t shipBeamCm;    // ship particulars for the GM estimate (0 = not set)
  uint16_t shipDraftCm;
  uint16_t shipLengthDm;  // length overall (LOA), 0.1 m
  uint8_t  logCond;       // loading condition (index into LOAD_COND) of the current / last recording
  uint8_t  rollThrDeg;    // roll statistics: count rolls beyond this angle
  // ---- V7: trim at the dock (card 8b, OLED screen 9)
  uint16_t shipLbpDm;     // length between perpendiculars, 0.1 m (0 = use LOA - LOA_TO_LWL_M)
  uint16_t calFwdIn, calMidIn, calAftIn;   // drafts read at the trim calibration, inches (mid 0 = not read)
  uint8_t  trimCal;       // 1 = trim calibrated from the draft marks
  uint8_t  pad2;
  float    trimPitchOffset;   // sensor pitch at calibration minus the pitch the drafts give, deg
  uint32_t trimCalUnix;   // when it was calibrated (0 = no clock)
  uint16_t crc;
};
static_assert(offsetof(Persist, oledFlip) == offsetof(PersistV4, oledFlip), "V4 prefix must match");
static_assert(offsetof(Persist, shipBeamCm) == offsetof(PersistV5, crc), "V5 prefix must match");
static_assert(offsetof(Persist, rotDeadband) == offsetof(PersistV3, reserved), "V3 prefix must match");
static_assert(offsetof(Persist, shipLbpDm) == offsetof(PersistV6, crc), "V6 prefix must match");
Persist P;
bool persistDirty = false, persistUrgent = false;
uint32_t persistChangeMs = 0, lastPersistSaveMs = 0;

// ============================ 1-MINUTE WINDOW ===============================
struct Bin { float rMax, rMin, pMax, pMin, yrMax; bool used; };
struct Win { float rMax, rMin, pMax, pMin, yrMax; bool valid; };
Bin bins[60];
uint8_t binIdx = 0;
uint32_t binStartMs = 0;

// ================================= GRAPH ====================================
#define GRAPH_LENGTHS 5
static const uint16_t GRAPH_SECS[GRAPH_LENGTHS] = {30, 60, 120, 300, 600};
float *gRoll = nullptr, *gPitch = nullptr;   // heap, GRAPH_POINTS each
uint16_t gHead = 0, gCount = 0;        // gHead = next slot to write
float sogHist[60];                     // 1 Hz SOG (knots) for the moving average
uint8_t sogIdx = 0;
uint32_t lastSogTickMs = 0;
float sogAvgKn = NAN;
float velE[60], velN[60];               // 1 Hz velocity components (knots) for the COG average
float cogAvgDeg = NAN;                 // COG of the averaged velocity, deg true (NAN = too slow / no fix)
uint32_t lastGraphMs = 0;

// ================================== GPS =====================================
#define KNOTS_TO_MS 0.514444f
struct GpsData {
  float    gpsLat;        // decimal degrees, positive = North
  float    gpsLon;        // decimal degrees, positive = East
  float    gpsAlt;        // meters above MSL (from GGA; noisy, use with care at sea)
  float    gpsSpeed;      // m/s (converted from GPS knots - see SPEED UNITS note)
  float    gpsAngle;      // degrees true (course over ground)
  bool     gpsFix;
  uint8_t  gpsSats;
  // extras (diagnostics)
  float    gpsHdop;       // horizontal dilution of precision
  uint8_t  gpsFixQ;       // 0 = none, 1 = GPS, 2 = DGPS/SBAS
  uint32_t gpsLastFixMs;  // millis() of the last RMC with a valid fix
};
GpsData gps = {NAN, NAN, NAN, NAN, NAN, false, 0, NAN, 0, 0};
bool gpsNewForLog = false;      // set on each RMC, cleared when the SD row is written
// GPS link health: is the module talking, and what can it see?
uint32_t gpsLastNmeaMs = 0;     // millis() of the last complete NMEA sentence (0 = none yet)
uint32_t gpsNmeaCount = 0;      // sentences received since boot
uint32_t gpsRestarts = 0;       // restarts from the dashboard's GPS diagnostics card
volatile int8_t reqGps = 0;     // dashboard: 1 = restart (re-send set-up), 2 = cold start
uint8_t  gpsInView[2] = {0, 0}; // satellites in view (GSV): [0] GPS, [1] GLONASS
uint8_t  gpsBestSnr = 0;        // strongest signal over the last 5 s, dB-Hz (GSV)
// Sky plot (OLED screen 8, dashboard view): every satellite in view from GSV
// (elevation, azimuth, signal) and the ones used in the fix from GSA.
// NMEA PRNs are unique across systems: GPS 1-32, SBAS 33-64, GLONASS 65-96.
#define SKY_MAX      40
#define SKY_STALE_MS 15000UL          // a satellite not reported for this long is dropped
struct SkySat { uint8_t prn, el; uint16_t az; uint8_t snr; uint32_t ms; };
SkySat   sky[SKY_MAX];
uint32_t skyUsed[4] = {0, 0, 0, 0};   // bit per PRN (0-127) used in the fix
uint32_t skyUsedMs = 0;               // last GSA (a fix lists one GSA per system)
uint8_t  gpsSnrAcc = 0;
uint32_t gpsSnrMs = 0;
uint32_t gpsRmcMs = 0;          // millis() of that RMC

// ================================== TIME ====================================
uint32_t baseUnix = 0, baseMillis = 0;
bool clockSynced = false, resyncPending = false, driftChecked = false;
uint8_t lastRtcSec = 255;
uint32_t lastRtcPollMs = 0, lastResyncMs = 0, lastDriftCheckMs = 0;

// =================================== SD =====================================
File logFile;
char logName[64] = "";
uint32_t lastLogMs = 0, lastFlushMs = 0, lastSdRetryMs = 0;
SemaphoreHandle_t sdMutex = nullptr;   // serialises SD access between logger and web task
char*    pend = nullptr;               // log rows waiting to be written (loop task only; heap)
size_t   pendCap = 0, pendLen = 0;      // rides out short SD-busy moments (e.g. a web file listing)
uint32_t pendDropped = 0;              // rows lost because the buffer filled
// Logging state. logWanted is set by the web task; loop() applies it.
volatile bool logWanted = false;
volatile bool logging = false;
char     recFile[64] = "";             // this recording's CSV, named when Start is pressed
char     pendTag[LOG_TAG_MAX + 1] = "";  // suffix sent with Start (web task -> loop)
uint8_t  pendCond = 255;               // loading condition sent with Start (255 = keep the last one)
// Event markers (dashboard): written into the event / event_note columns of the next row
#define EVENT_NOTE_MAX 40
struct LogEvent { uint16_t n; uint32_t unix; char note[EVENT_NOTE_MAX + 1]; };
volatile bool reqEvent = false;
char     reqEventNote[EVENT_NOTE_MAX + 1] = "";
LogEvent evList[5];                    // newest first, this recording
uint16_t evCount = 0;                  // events in this recording
volatile int32_t reqBeamCm = -1, reqDraftCm = -1, reqLengthDm = -1, reqLbpDm = -1;   // ship particulars
volatile int8_t  reqRollThr = -1;
// Plot from file (card 11), filled by plotTask()
volatile uint8_t plotState = 0;          // 0 idle, 1 working, 2 done, 3 failed
volatile uint8_t plotPct = 0;
char     plotFile[96];
uint32_t plotHours = 0;                  // 0 = whole file
String   plotJson;                       // result (state 2)
char     plotErr[64] = "";

bool     logInterrupted = false;       // was recording when the VMR last restarted / lost power (not resumed)
uint32_t logStartUnix = 0;             // UTC of the last Start (0 = no clock then)
uint32_t logStartMs = 0, logRows = 0;
uint64_t logBytes = 0;

// ================================ WEB FILES =================================
// ---- ZIPTYPES BEGIN ----
#define ZIP_NAME_MAX 48
struct ZipEntry {
  char     name[ZIP_NAME_MAX];
  uint32_t size;
  uint32_t crc;
  uint32_t offset;
  uint16_t dosTime, dosDate;
};
typedef bool (*ZipSinkFn)(void* ctx, const uint8_t* buf, size_t n);
typedef int  (*ZipReadFn)(void* ctx, size_t idx, uint8_t* buf, size_t want);
// ---- ZIPTYPES END ----
struct ZipCtx {
  ZipEntry*      e;
  File           f;
  int            cur;
  NetworkClient* client;
};
#define WEB_BUF_BYTES 8192
uint8_t* webBuf = nullptr;             // web task I/O buffer (heap)

// ================================ BATTERY ===================================
float battV = NAN, battPct = NAN, battRate = NAN;   // battRate: %/hour from the gauge (+ = charging)
float cpuTempC = NAN;                  // ESP32-S2 die temperature (runs warmer than the air around it)
uint32_t lastBattMs = 0;   // also paces retries when no battery/gauge is found

// ================================ DISPLAY ===================================
enum { SCREEN_LIVE = 0, SCREEN_GRAPH, SCREEN_POSITION, SCREEN_ALLTIME, SCREEN_GPS,
       SCREEN_CLINO, SCREEN_STERN, SCREEN_SKY, SCREEN_DOCK, SCREEN_GMETER, NUM_SCREENS };
uint8_t screen = SCREEN_LIVE;
bool displayOn = true;
int contrast = 128;
float potEma = -1;
bool potControl = true;                // false = brightness last set from the web page
float potAtOverride = 0;
int8_t lastSwState = -1;
// Requests from the web task, applied by loop() (keeps the I2C bus single-owner)
volatile int16_t reqScreen = -1, reqContrast = -1;
volatile int8_t  reqDisplayOn = -1;
volatile int8_t  reqZero = -1;          // 1 = set level zero, 0 = clear (from the web page)
volatile int8_t  reqGraphMax = 0, reqGraphMin = 0, reqResetMax = 0;   // graph limits / reset all-time maxima
volatile int8_t  reqGmReset = 0, reqClinoReset = 0, reqGReset = 0;         // 1 = restart the 10-min GM window (card 8)
volatile int8_t  reqPitchMax = 0, reqPitchMin = 0;                    // pitch graph limits (dashboard)
volatile int8_t  reqGraphIdx = -1, reqSogAvg = 0, reqZd = 0, reqZdSet = 0;
volatile int16_t reqNudgeRoll = 0, reqNudgePitch = 0;                  // level-zero nudges, 0.1 deg steps
volatile int8_t  reqCharge = -1;       // 1 = enter charge mode, 0 = leave
volatile int8_t  reqLogger = -1;       // 1 = enter datalogger mode (Wi-Fi + OLED off)
volatile uint32_t reqLoggerAtMs = 0;
volatile int8_t  reqFlip = -1, reqRotDb = -1, reqRestart = 0;
// Pause (moving the VMR): live values carry on, but no rows are logged and
// nothing is counted (maxima, roll period / statistics, GM window).
volatile int8_t  reqPause = -1;        // 1 = pause, 0 = resume (dashboard)
bool     paused = false;               // RAM only: a restart always comes up running
uint32_t pausedAtMs = 0;
bool     pauseRowDue = false;          // the PAUSED marker row is still to be written
const char* sysEvent = nullptr;        // "PAUSED" / "RESUMED" marker for the next logged row
// Firmware update over Wi-Fi (card 16). The web task receives and writes the
// image; the loop task shows progress on the OLED and restarts.
enum { OTA_IDLE, OTA_RX, OTA_OK, OTA_FAIL };
volatile uint8_t  otaState = OTA_IDLE;
volatile uint32_t otaDone = 0, otaTotal = 0, otaEndMs = 0;
volatile int8_t   reqOtaReboot = 0;
char     otaErr[64] = "";
uint32_t otaMax = 0;                   // size of the free app slot (0 = partition scheme without OTA)
uint32_t fwSize = 0;                   // size of the running firmware
bool     oledForced = false;           // OLED held on for the pause / update / serial screens
volatile bool serOledOn = false;       // card 13 "Show on OLED" (latching; RAM only)
int      contrastBeforeOta = -1;
volatile bool    reqPhone = false;     // phone time waiting to be applied
volatile uint32_t reqPhoneUnix = 0, reqPhoneRxMs = 0;
volatile int8_t  reqPhoneZd = 0;
bool loggerMode = false, dispBeforeLogger = true;
volatile bool webPaused = false, webIdle = false;
uint32_t lastBlinkMs = 0;
float rotSmooth = 0;                   // rate of turn, deg/s, smoothed over ROT_SMOOTH_S
float clinoMaxS = 0, clinoMaxP = 0;    // clinometer drag pointers (largest roll each way since reset)
// Acceleration (BNO085 accelerometer, gravity included = what is felt), in g,
// ship axes. Logged as the mean over each row (acc_fwd_g, acc_stbd_g,
// acc_up_g: specific force, level and still = 0, 0, +1). The G-meter (OLED
// screen 10, dashboard view) shows the FELT push: the opposite of the
// horizontal specific force, i.e. towards the low side when heeled.
float    accF = NAN, accS = NAN, accU = NAN;          // latest sample
float    gFeltF = 0, gFeltS = 0, gVert = 1;           // 0.3 s smoothed, for the G-meter
double   accSumF = 0, accSumS = 0, accSumU = 0;       // mean over each log row
uint16_t accSumN = 0;
uint32_t lastAccMs = 0;
float    gPkF = 0, gPkA = 0, gPkS = 0, gPkP = 0, gPkHi = 1, gPkLo = 1;   // peak hold (felt): fwd, aft, stbd, port, vertical max / min
#define G_TRAIL 12                                     // OLED trail: last positions of the dot (one per frame)
int8_t   gTrailX[G_TRAIL], gTrailY[G_TRAIL];
uint8_t  gTrailN = 0, gTrailHead = 0;
// At the dock (card 8b, OLED screen 9): 60 s means of the sensor's raw pitch
// and of roll, kept as 1 s means so a minute of berth motion averages out.
#define DOCK_AVG_S   60
#define DOCK_CAL_MIN 30                 // seconds of data needed before calibrating
float    dockSecP[DOCK_AVG_S], dockSecR[DOCK_AVG_S];
uint8_t  dockHead = 0, dockCount = 0, dockN = 0;
double   dockAccP = 0, dockAccR = 0;
uint16_t dockAccN = 0;
uint32_t dockSecMs = 0;
float    dockRawPitch = NAN, dockRoll = NAN;
volatile int8_t   reqTrimCal = 0;       // 1 = calibrate from reqCal* drafts, -1 = clear the calibration
volatile uint16_t reqCalF = 0, reqCalM = 0, reqCalA = 0;   // inches
char clockSrc = '-';                   // 'R' clock from RTC/GPS, 'P' from a phone
bool gpsTimeSeen = false, phoneZdDone = false;
volatile bool    chargeMode = false;
bool dispBeforeCharge = true;
uint32_t lastPotMs = 0, lastDrawMs = 0, lastOledRetryMs = 0;
char msgText[24] = "";
uint32_t msgUntil = 0;

static const char* MONTHS[] = {"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"};

// ============================== DIAGNOSTICS =================================
String resetReason = "unknown";
String crashReport = "";               // filled at boot if the last reset was a crash
String i2cMissing = "", i2cBusNote = "";
uint32_t lastBeatMs = 0, beatCount = 0;
// Breadcrumbs in RTC memory: survive a crash/watchdog restart (not power-off),
// so the next boot can say what the main loop was doing when it died.
enum {
  ST_NONE = 0, ST_GPS, ST_IMU_READ, ST_IMU_RESET, ST_IMU_BEGIN, ST_RTC, ST_BINS, ST_BUTTONS,
  ST_POT, ST_WEBREQ, ST_BATT, ST_GRAPH, ST_LOG, ST_PERSIST, ST_DRAW, ST_OLED_SEND, ST_OLED_RETRY,
  ST_IDLE, ST_BOOT, ST_COUNT
};
static const char* const STAGE_NAMES[ST_COUNT] = {
  "-", "GPS serial", "IMU read", "IMU re-enable (I2C)", "IMU begin/recovery", "RTC (I2C)",
  "1-min stats", "buttons", "pot/switch", "web requests", "battery gauge (I2C)", "graph buffer",
  "SD logging", "EEPROM save (I2C)", "drawing screen", "OLED transfer (I2C)", "OLED re-detect (I2C)",
  "idle/yield", "boot (setup)"
};
#define CRUMB_MAGIC 0x564D5231UL           // "VMR1"
RTC_NOINIT_ATTR uint32_t crumbMagic;
RTC_NOINIT_ATTR uint32_t crumbStage;
RTC_NOINIT_ATTR uint32_t crumbScreen;
RTC_NOINIT_ATTR uint32_t crumbUptime;
inline void crumb(uint32_t st) { crumbStage = st; }

// ================================ BUTTONS ===================================
struct Button {
  uint8_t pin = 0;
  bool stable = HIGH, lastRead = HIGH, pressed = false, released = false;
  uint32_t lastChange = 0, pressStart = 0;
  void begin(uint8_t p) {
    pin = p;
    pinMode(pin, INPUT_PULLUP);
    stable = lastRead = digitalRead(pin);
    lastChange = millis();
  }
  void update() {
    pressed = released = false;
    bool r = digitalRead(pin);
    uint32_t now = millis();
    if (r != lastRead) { lastRead = r; lastChange = now; }
    if (now - lastChange > 25 && r != stable) {
      stable = r;
      if (stable == LOW) { pressed = true; pressStart = now; }
      else               { released = true; }
    }
  }
  bool isDown() const { return stable == LOW; }
  uint32_t downFor() const { return isDown() ? millis() - pressStart : 0; }
};
Button btnA, btnB, btnC, btnScr;
bool comboFired = false;

// ============================================================================
//                                 HELPERS
// ============================================================================
float wrap180(float a) {
  while (a > 180.0f)  a -= 360.0f;
  while (a <= -180.0f) a += 360.0f;
  return a;
}

bool imuFresh() { return haveImu && (millis() - lastImuMs < 1000); }

bool gpsFixValid() { return gps.gpsFix && gps.gpsLastFixMs != 0 && (millis() - gps.gpsLastFixMs < GPS_FIX_TIMEOUT_MS); }

uint32_t nowUnix() { return baseUnix + (millis() - baseMillis) / 1000; }

uint16_t crc16(const uint8_t* d, size_t n) {
  uint16_t c = 0xFFFF;
  while (n--) {
    c ^= (uint16_t)(*d++) << 8;
    for (int i = 0; i < 8; i++) c = (c & 0x8000) ? (c << 1) ^ 0x1021 : (c << 1);
  }
  return c;
}

void showMessage(const char* t, uint32_t ms = 1500) {
  strncpy(msgText, t, sizeof(msgText) - 1);
  msgText[sizeof(msgText) - 1] = 0;
  msgUntil = millis() + ms;
}

// Current UTC as unix seconds. src = 'G' (GPS fix), 'R' (RTC-backed clock),
// 'P' (clock set from a phone), '-' (no time yet -> 0)
uint32_t utcNow(char& src) {
  if (gpsFixValid() && GPS.year >= 24) {
    src = 'G';
    return DateTime(2000 + GPS.year, GPS.month, GPS.day, GPS.hour, GPS.minute, GPS.seconds).unixtime();
  }
  if (clockSynced) { src = clockSrc; return nowUnix(); }
  src = '-';
  return 0;
}

// Local time from UTC with the nautical zone description: LT = UTC - ZD
uint32_t ltOf(uint32_t utc) { return utc - (int32_t)P.zdHours * 3600; }

void zdText(char* out, size_t n) {
  if (P.zdHours) snprintf(out, n, "%+d", (int)P.zdHours);
  else           snprintf(out, n, "0");
}

uint16_t graphSecs() { return GRAPH_SECS[P.graphSecIdx < GRAPH_LENGTHS ? P.graphSecIdx : 1]; }

// degrees + decimal minutes, e.g. "38\xF806.124'N"
void fmtDM(char* out, size_t n, float v, char pos, char neg, bool isLon) {
  char hemi = v < 0 ? neg : pos;
  v = fabsf(v);
  int d = (int)v;
  float mnt = (v - d) * 60.0f;
  if (mnt >= 59.9995f) { d++; mnt = 0; }
  if (isLon) snprintf(out, n, "%3d%c%06.3f'%c", d, (char)248, mnt, hemi);
  else       snprintf(out, n, "%2d%c%06.3f'%c", d, (char)248, mnt, hemi);
}

// ============================================================================
//                                  EEPROM
// ============================================================================
bool eepromWrite(uint16_t addr, const uint8_t* data, size_t len) {
  while (len > 0) {
    size_t room = EEPROM_PAGE - (addr % EEPROM_PAGE);
    size_t n = len < room ? len : room;
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    Wire.write(data, n);
    if (Wire.endTransmission() != 0) return false;
    uint32_t t0 = millis();                       // ACK polling (~5 ms write cycle)
    while (true) {
      Wire.beginTransmission(EEPROM_ADDR);
      if (Wire.endTransmission() == 0) break;
      if (millis() - t0 > 20) return false;
    }
    addr += n; data += n; len -= n;
  }
  return true;
}

bool eepromRead(uint16_t addr, uint8_t* data, size_t len) {
  while (len > 0) {
    size_t n = len < 32 ? len : 32;
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)EEPROM_ADDR, (int)n) != (int)n) return false;
    for (size_t i = 0; i < n; i++) data[i] = Wire.read();
    addr += n; data += n; len -= n;
  }
  return true;
}

void persistDefaults() {
  memset(&P, 0, sizeof(P));
  P.magic = PERSIST_MAGIC;
  P.graphMin = -GRAPH_DEFAULT_DEG;
  P.graphMax = GRAPH_DEFAULT_DEG;
  P.graphSecIdx = 1;      // 1 min
  P.sogAvgS = 10;
  P.rotDeadband = 3;
  P.pitchGraphMin = -GRAPH_DEFAULT_DEG;
  P.pitchGraphMax = GRAPH_DEFAULT_DEG;
  P.rollThrDeg = 10;
}

bool savePersist() {
  if (!eepromOK) return false;
  P.magic = PERSIST_MAGIC;
  P.crc = crc16((const uint8_t*)&P, offsetof(Persist, crc));
  bool ok = eepromWrite(EEPROM_BASE, (const uint8_t*)&P, sizeof(P));
  Log.printf("EEPROM save %s\n", ok ? "OK" : "FAILED");
  return ok;
}

bool persistValid(const Persist& t) {
  return t.graphMin < 0 && t.graphMin >= -GRAPH_LIMIT_DEG &&
         t.graphMax > 0 && t.graphMax <= GRAPH_LIMIT_DEG &&
         t.zdHours >= -12 && t.zdHours <= 12 && t.graphSecIdx < GRAPH_LENGTHS &&
         t.sogAvgS >= 1 && t.sogAvgS <= 60 && t.rotDeadband <= 60 && t.oledFlip <= 1 &&
         t.pitchGraphMin < 0 && t.pitchGraphMin >= -GRAPH_LIMIT_DEG &&
         t.pitchGraphMax > 0 && t.pitchGraphMax <= GRAPH_LIMIT_DEG &&
         memchr(t.logTag, 0, sizeof(t.logTag)) != nullptr &&
         t.logCond < LOAD_COND_N && t.rollThrDeg >= 1 && t.rollThrDeg <= 45 &&
         t.trimCal <= 1 && (!t.trimCal || (isfinite(t.trimPitchOffset) && fabsf(t.trimPitchOffset) < 45));
}

void loadPersist() {
  Persist t;
  bool ok = eepromRead(EEPROM_BASE, (uint8_t*)&t, sizeof(t));
  if (ok && (t.magic == PERSIST_MAGIC_V3 || t.magic == PERSIST_MAGIC_V4 || t.magic == PERSIST_MAGIC_V5 ||
             t.magic == PERSIST_MAGIC_V6)) {
    // older layout: same leading fields, newer fields added at the end (zero = default)
    bool v3 = t.magic == PERSIST_MAGIC_V3, v5 = t.magic == PERSIST_MAGIC_V5, v6 = t.magic == PERSIST_MAGIC_V6;
    size_t crcAt = v3 ? offsetof(PersistV3, crc) : v5 ? offsetof(PersistV5, crc) : v6 ? offsetof(PersistV6, crc)
                      : offsetof(PersistV4, crc);
    uint16_t oldCrc;
    memcpy(&oldCrc, (const uint8_t*)&t + crcAt, sizeof(oldCrc));
    if (oldCrc == crc16((const uint8_t*)&t, crcAt)) {
      Persist n;
      memset(&n, 0, sizeof(n));
      memcpy(&n, &t, crcAt);                            // identical prefix
      n.magic = PERSIST_MAGIC;
      if (v3) { n.rotDeadband = 3; n.oledFlip = 0; }
      if (!v5 && !v6) { n.pitchGraphMin = -GRAPH_DEFAULT_DEG; n.pitchGraphMax = GRAPH_DEFAULT_DEG; n.pad = 0; n.logTag[0] = 0; }
      if (!v6) n.rollThrDeg = 10;
      if (persistValid(n)) {
        P = n;
        Log.println("EEPROM: settings + maxima migrated to new layout");
        savePersist();
        return;
      }
    }
  }
  if (ok && t.magic == PERSIST_MAGIC &&
      t.crc == crc16((const uint8_t*)&t, offsetof(Persist, crc)) && persistValid(t)) {
    P = t;
    Log.println("EEPROM: settings + maxima loaded");
  } else {
    persistDefaults();
    Log.println("EEPROM: blank or invalid, writing defaults");
    savePersist();
  }
}

void markPersistDirty(bool urgent) {
  persistDirty = true;
  if (urgent) { persistUrgent = true; persistChangeMs = millis(); }
}

void servicePersist() {
  if (!persistDirty || !eepromOK) return;
  uint32_t now = millis();
  bool due = persistUrgent ? (now - persistChangeMs >= SETTINGS_SAVE_DELAY_MS)
                           : (now - lastPersistSaveMs >= MAXIMA_SAVE_MIN_MS);
  if (!due) return;
  if (savePersist()) { persistDirty = false; persistUrgent = false; }
  lastPersistSaveMs = now;
}

// ============================================================================
//                              1-MINUTE BINS
// ============================================================================
void resetBin(Bin& b) {
  b.rMax = -1e9f; b.rMin = 1e9f; b.pMax = -1e9f; b.pMin = 1e9f; b.yrMax = 0; b.used = false;
}

void serviceBins() {
  uint32_t now = millis();
  if (now - binStartMs >= 61000UL) {
    for (int i = 0; i < 60; i++) resetBin(bins[i]);
    binIdx = 0; binStartMs = now;
    return;
  }
  while (now - binStartMs >= 1000UL) {
    binStartMs += 1000UL;
    binIdx = (binIdx + 1) % 60;
    resetBin(bins[binIdx]);
  }
}

Win window1m() {
  Win w = {-1e9f, 1e9f, -1e9f, 1e9f, 0, false};
  for (int i = 0; i < 60; i++) {
    const Bin& b = bins[i];
    if (!b.used) continue;
    w.valid = true;
    if (b.rMax > w.rMax) w.rMax = b.rMax;
    if (b.rMin < w.rMin) w.rMin = b.rMin;
    if (b.pMax > w.pMax) w.pMax = b.pMax;
    if (b.pMin < w.pMin) w.pMin = b.pMin;
    if (b.yrMax > w.yrMax) w.yrMax = b.yrMax;
  }
  if (!w.valid) { w.rMax = w.rMin = w.pMax = w.pMin = w.yrMax = NAN; }
  return w;
}

// ============================================================================
//                                   IMU
// ============================================================================
void imuEnableReports() {
#if USE_GAME_ROTATION_VECTOR
  bno08x.enableReport(SH2_GAME_ROTATION_VECTOR, IMU_REPORT_US);
#else
  bno08x.enableReport(SH2_ROTATION_VECTOR, IMU_REPORT_US);
#endif
  bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED, IMU_REPORT_US);
  bno08x.enableReport(SH2_ACCELEROMETER, IMU_REPORT_US);   // gravity included (felt acceleration)
}

// The BNO08x library has ONE session slot, and begin_I2C() opens a new session
// without closing the old one. A second open fails AND wipes the pointer to the
// first, after which it can never be closed (closing then crashes). So: close
// any session we opened before trying again, and once the IMU has started,
// never call begin_I2C() again (recovery reuses the session, see imuRecoverI2C).
bool imuBegin() {
  if (!imuAcks()) return false;                // never start the driver on a silent sensor
  if (imuSessionOpen) { sh2_close(); imuSessionOpen = false; }
  imuSessionOpen = true;                       // begin_I2C runs sh2_open once the sensor answers
  if (!bno08x.begin_I2C(BNO_ADDR, &Wire)) return false;
  imuStarted = true;
  sh2_setSensorCallback(imuEventHandler, nullptr);   // see imuEventHandler()
  imuEnableReports();
  return true;
}

void quatToEuler(float qr, float qi, float qj, float qk) {
  float sqr = qr * qr, sqi = qi * qi, sqj = qj * qj, sqk = qk * qk;
  float n = sqi + sqj + sqk + sqr;
  if (n <= 0) return;
  float yaw = atan2f(2.0f * (qi * qj + qk * qr), (sqi - sqj - sqk + sqr));
  float sp  = -2.0f * (qi * qk - qj * qr) / n;
  if (sp > 1.0f) sp = 1.0f;
  if (sp < -1.0f) sp = -1.0f;
  float pit = asinf(sp);
  float rol = atan2f(2.0f * (qj * qk + qi * qr), (-sqi - sqj + sqk + sqr));
  rawRoll  = rol * RAD_TO_DEG;             // + = starboard side down (X to bow, chip up)
#if ROLL_FLIP
  rawRoll  = -rawRoll;
#endif
  rawPitch = -pit * RAD_TO_DEG;            // + = bow up
#if PITCH_FLIP
  rawPitch = -rawPitch;
#endif
  yawDeg   = -yaw * RAD_TO_DEG;            // quaternion yaw is CCW-positive; make it heading-like
#if YAW_FLIP
  yawDeg   = -yawDeg;
#endif
  yawDeg   = fmodf(yawDeg + 360.0f, 360.0f);
}

// ---- ROLLPERIOD BEGIN ----
// Roll period: time between upward zero-crossings of roll about its running,
// trend-following mean. Roll is first low-passed (2 stages, ~1 Hz) to strip
// noise and vibration; the delay is the same at every crossing, so the period
// is unaffected. Hysteresis: a cycle counts only if it swung beyond +/-HYST
// both ways. Crossing times are interpolated between IMU samples. Result =
// median of the last ROLL_PERIOD_CYCLES cycles (at least 3), cleared after
// 60 s without a cycle or after a data gap.
// Each accepted cycle is also stored for the roll statistics (rsCyc).
void updateRollPeriod(uint32_t now) {
  if (isnan(rpMean) || now - rpPrevMs > 2000) {     // first sample, or after a gap
    rpMean = rpM2 = rpLp1 = rpLp2 = roll; rpPrevMs = now; rpPrevX = 0; rpMaxSince = 0;
    rpArmed = false; rpLastCrossMs = 0; rpCount = 0; rollPeriod = NAN;
    rpMinSince = 0; rpPkS = -INFINITY; rpPkP = INFINITY;
    return;
  }
  float dt = (now - rpPrevMs) / 1000.0f;
  float a = dt / (0.15f + dt);                         // 2 x first-order, tau 0.15 s
  rpLp1 += a * (roll - rpLp1);
  rpLp2 += a * (rpLp1 - rpLp2);
  // Double-exponential mean (2*m1 - m2): follows a steadily changing list
  // without lagging behind it, which a plain running mean would.
  float k = dt / (ROLL_PERIOD_MEAN_S + dt);
  rpMean += k * (rpLp2 - rpMean);
  rpM2   += k * (rpMean - rpM2);
  float x = rpLp2 - (2.0f * rpMean - rpM2);
  if (x < -ROLL_PERIOD_HYST_DEG) rpArmed = true;
  if (rpArmed && rpPrevX < 0 && x >= 0) {             // upward zero-crossing
    uint32_t tc = rpPrevMs + (uint32_t)((now - rpPrevMs) * (-rpPrevX / (x - rpPrevX)));
    if (rpLastCrossMs && rpMaxSince > ROLL_PERIOD_HYST_DEG) {
      float per = (tc - rpLastCrossMs) / 1000.0f;
      if (per >= 2.0f && per <= 60.0f) {
        rpHist[rpIdx] = per;
        rpIdx = (rpIdx + 1) % ROLL_PERIOD_CYCLES;
        if (rpCount < ROLL_PERIOD_CYCLES) rpCount++;
        if (rsCyc) {                                  // for the roll statistics card
          RollCycle& c = rsCyc[rsHead];
          c.ms = now;
          c.amp10 = (int16_t)lroundf(5.0f * (rpMaxSince - rpMinSince));   // half of peak-to-peak, x10
          c.pkS10 = (int16_t)lroundf(10.0f * rpPkS);
          c.pkP10 = (int16_t)lroundf(10.0f * rpPkP);
          rsHead = (rsHead + 1) % RS_CYCLES;
          if (rsCount < RS_CYCLES) rsCount++;
        }
      }
    }
    rpLastCrossMs = tc;
    rpArmed = false;
    rpMaxSince = x;
    rpMinSince = x;
    rpPkS = -INFINITY; rpPkP = INFINITY;
  }
  if (x > rpMaxSince) rpMaxSince = x;
  if (x < rpMinSince) rpMinSince = x;
  if (roll > rpPkS) rpPkS = roll;
  if (roll < rpPkP) rpPkP = roll;
  rpPrevX = x;
  rpPrevMs = now;

  if (rpLastCrossMs && now - rpLastCrossMs > 60000UL) rpCount = 0;   // motion stopped
  if (rpCount >= 3) {
    float t[ROLL_PERIOD_CYCLES];
    for (int i = 0; i < rpCount; i++) t[i] = rpHist[i];
    for (int i = 1; i < rpCount; i++)                    // insertion sort, n <= 9
      for (int k = i; k > 0 && t[k - 1] > t[k]; k--) { float q = t[k]; t[k] = t[k - 1]; t[k - 1] = q; }
    rollPeriod = (rpCount % 2) ? t[rpCount / 2] : 0.5f * (t[rpCount / 2 - 1] + t[rpCount / 2]);
  } else {
    rollPeriod = NAN;
  }
}
// ---- ROLLPERIOD END ----

// Every IMU sample: 10 s blocks of roll sum / sum of squares (mean list, RMS)
void rollStatsSample(uint32_t now) {
  if (rsCur.n == 0) rsCurStartMs = now;
  rsCur.sum += roll; rsCur.sumSq += roll * roll; rsCur.n++;
  if (now - rsCurStartMs >= RS_BLOCK_MS) {
    rsBlk[rsBlkHead] = rsCur;
    rsBlkHead = (rsBlkHead + 1) % RS_BLOCKS;
    if (rsBlkCount < RS_BLOCKS) rsBlkCount++;
    rsCur = {0, 0, 0};
  }
}

// GM from the roll period, IMO IS Code 2008 (Part A, 2.3): T = 2 C B / sqrt(GM),
// C = 0.373 + 0.023 B/d - 0.043 L/100  ->  GM = (2 C B / T)^2
void updateGm() {
  if (!P.shipBeamCm || !P.shipDraftCm || !P.shipLengthDm) { gmC = gmEst = NAN; return; }
  float B = P.shipBeamCm / 100.0f, d = P.shipDraftCm / 100.0f;
  float L = fmaxf(P.shipLengthDm / 10.0f - LOA_TO_LWL_M, 1.0f);   // formula wants waterline length
  gmC = 0.373f + 0.023f * (B / d) - 0.043f * (L / 100.0f);
  if (isnan(rollPeriod) || rollPeriod <= 0 || gmC <= 0) { gmEst = NAN; return; }
  float r = 2.0f * gmC * B / rollPeriod;
  gmEst = r * r;
}

// Roll statistics over the last 10 and 30 min (every 5 s)
// Restart the dashboard GM window (button on card 8, new ship particulars)
void gmWindowReset() {
  gmWinHead = gmWinCount = 0;
  gmWinSampleMs = 0;
  gmAvg = NAN;
}

// Add the current estimate every 5 s and refresh the windowed mean
void gmWindowTick(uint32_t now) {
  if (gmWinSampleMs && now - gmWinSampleMs < GM_SAMPLE_MS) return;
  gmWinSampleMs = now;
  gmWin[gmWinHead] = imuFresh() ? gmEst : NAN;
  gmWinHead = (gmWinHead + 1) % GM_SLOTS;
  if (gmWinCount < GM_SLOTS) gmWinCount++;
  float sum = 0; uint16_t n = 0;
  for (uint16_t i = 0; i < gmWinCount; i++) if (!isnan(gmWin[i])) { sum += gmWin[i]; n++; }
  gmAvg = n ? sum / n : NAN;
}

// ---- At the dock: trim from the pitch, list from the roll -------------------
// Trim = LBP x tan(pitch), + = by the stern (bow up). The pitch is measured
// from the trim calibration (drafts read at the marks) when there is one, else
// from level zero. Close one 1 s mean per second; refresh the 60 s means.
void serviceDock(uint32_t now) {
  if (now - dockSecMs < 1000) return;
  dockSecMs = (now - dockSecMs > 3000) ? now : dockSecMs + 1000;
  dockSecP[dockHead] = dockAccN ? (float)(dockAccP / dockAccN) : NAN;
  dockSecR[dockHead] = dockAccN ? (float)(dockAccR / dockAccN) : NAN;
  dockAccP = dockAccR = 0; dockAccN = 0;
  dockHead = (dockHead + 1) % DOCK_AVG_S;
  if (dockCount < DOCK_AVG_S) dockCount++;
  double sp = 0, sr = 0;
  int n = 0;
  for (int i = 0; i < dockCount; i++) if (!isnan(dockSecP[i])) { sp += dockSecP[i]; sr += dockSecR[i]; n++; }
  dockN = n;
  dockRawPitch = n ? (float)(sp / n) : NAN;
  dockRoll = n ? (float)(sr / n) : NAN;
}

// Length for the trim: LBP if entered, else LOA - LOA_TO_LWL_M (NaN = no length)
float trimLengthM() {
  if (P.shipLbpDm) return P.shipLbpDm / 10.0f;
  return P.shipLengthDm ? fmaxf(P.shipLengthDm / 10.0f - LOA_TO_LWL_M, 1.0f) : NAN;
}

// Trim angle (deg, + = by the stern) from a raw sensor pitch
float trimAngle(float rawP) {
  return rawP - (P.trimCal ? P.trimPitchOffset : P.zeroApplied ? P.pitchOffset : 0.0f);
}

// Trim in metres (+ = by the stern) from a raw sensor pitch
float trimMetres(float rawP) {
  float L = trimLengthM();
  if (isnan(rawP) || isnan(L)) return NAN;
  return L * tanf(trimAngle(rawP) * DEG_TO_RAD);
}

// Calibrate from drafts read at the marks (inches): the offset makes the
// 60 s mean pitch give exactly the trim the drafts show. Loop task.
void trimCalibrate(uint16_t fwd, uint16_t mid, uint16_t aft) {
  float L = trimLengthM();
  if (isnan(L) || isnan(dockRawPitch) || dockN < DOCK_CAL_MIN) return;   // checked by the web handler too
  float trueDeg = atanf((aft - (float)fwd) * 0.0254f / L) * RAD_TO_DEG;
  P.trimPitchOffset = dockRawPitch - trueDeg;
  P.trimCal = 1;
  P.calFwdIn = fwd; P.calMidIn = mid; P.calAftIn = aft;
  P.trimCalUnix = clockSynced ? nowUnix() : 0;
  markPersistDirty(true);
  Log.printf("Trim calibrated: F %u-%02u M %u-%02u A %u-%02u, L %.1f m, offset %.3f deg\n",
             fwd / 12, fwd % 12, mid / 12, mid % 12, aft / 12, aft % 12, L, P.trimPitchOffset);
}

void updateRollStats(uint32_t now) {
  if (lastStatsMs && now - lastStatsMs < 5000) return;
  lastStatsMs = now;
  updateGm();
  if (!rsCyc || !rsSort) return;
  float* a = rsSort;
  for (int w = 0; w < 2; w++) {
    uint32_t win = RS_WIN_MIN[w] * 60000UL;
    RollStats& st = rStat[w];
    st = {0, NAN, NAN, NAN, NAN, 0, 0};
    int n = 0;
    for (int i = 0; i < rsCount; i++) {
      const RollCycle& c = rsCyc[(rsHead + RS_CYCLES - 1 - i) % RS_CYCLES];
      if (now - c.ms > win) break;                    // newest first: the rest are older
      a[n++] = c.amp10 / 10.0f;
      if (c.pkS10 >= (int)P.rollThrDeg * 10) st.overS++;
      if (c.pkP10 <= -(int)P.rollThrDeg * 10) st.overP++;
    }
    st.n = n;
    if (n >= 3) {
      std::sort(a, a + n, [](float x, float y) { return x > y; });
      int k = (n + 2) / 3;                            // highest third
      float sum = 0;
      for (int i = 0; i < k; i++) sum += a[i];
      st.sig = sum / k;
      st.maxAmp = a[0];
    }
    double s1 = rsCur.sum, s2 = rsCur.sumSq;          // blocks inside the window + the open one
    uint32_t cnt = rsCur.n;
    int nb = (int)(win / RS_BLOCK_MS) - 1;
    for (int i = 0; i < rsBlkCount && i < nb; i++) {
      const RsBlock& b = rsBlk[(rsBlkHead + RS_BLOCKS - 1 - i) % RS_BLOCKS];
      s1 += b.sum; s2 += b.sumSq; cnt += b.n;
    }
    if (cnt > 50) {
      double m = s1 / cnt, v = s2 / cnt - m * m;
      st.mean = (float)m;
      st.rms = (float)sqrt(v > 0 ? v : 0);
    }
  }
}

// Rate of turn shown on screens and used for maxima: smoothed, deadband applied (deg/min)
float rotShown() {
  float r = rotSmooth * 60.0f;
  return fabsf(r) < P.rotDeadband ? 0.0f : r;
}

void updateAllTime() {
  uint32_t ts = clockSynced ? nowUnix() : 0;
  bool ch = false;
  if (roll  > P.rollMaxPos)  { P.rollMaxPos  = roll;  P.tRollMaxPos  = ts; ch = true; }
  if (roll  < P.rollMaxNeg)  { P.rollMaxNeg  = roll;  P.tRollMaxNeg  = ts; ch = true; }
  if (pitch > P.pitchMaxPos) { P.pitchMaxPos = pitch; P.tPitchMaxPos = ts; ch = true; }
  if (pitch < P.pitchMaxNeg) { P.pitchMaxNeg = pitch; P.tPitchMaxNeg = ts; ch = true; }
  float ayr = fabsf(rotShown()) / 60.0f;
  if (ayr > P.yawRateMax)    { P.yawRateMax  = ayr;   P.tYawRateMax  = ts; ch = true; }
  if (ch) markPersistDirty(false);
}

void onOrientation() {
  uint32_t now = millis();
  if (!haveImu) {
    haveImu = true;
    imuStartMs = now;
    tareRollAvg = rawRoll;
    tarePitchAvg = rawPitch;
  } else {
    float dt = (now - lastImuMs) / 1000.0f;
    float a = dt / (TARE_TAU_S + dt);
    tareRollAvg  += a * (rawRoll  - tareRollAvg);
    tarePitchAvg += a * (rawPitch - tarePitchAvg);
  }
  lastImuMs = now;
  imuQuietRefMs = now;

  roll  = wrap180(rawRoll  - (P.zeroApplied ? P.rollOffset  : 0.0f));
  pitch = wrap180(rawPitch - (P.zeroApplied ? P.pitchOffset : 0.0f));
  if (paused) return;                 // moving: live values only, nothing counted
  dockAccP += rawPitch; dockAccR += roll; dockAccN++;          // at-the-dock means

  Bin& b = bins[binIdx];
  if (roll  > b.rMax) b.rMax = roll;
  if (roll  < b.rMin) b.rMin = roll;
  if (pitch > b.pMax) b.pMax = pitch;
  if (pitch < b.pMin) b.pMin = pitch;
  if (fabsf(rotShown()) / 60.0f > b.yrMax) b.yrMax = fabsf(rotShown()) / 60.0f;
  b.used = true;

  updateRollPeriod(now);
  rollStatsSample(now);
  if (now - imuStartMs > WARMUP_MS) {
    updateAllTime();
    if (roll > clinoMaxS) clinoMaxS = roll;           // clinometer drag pointers
    if (roll < clinoMaxP) clinoMaxP = roll;
  }
}

// STEMMA QT rail (BNO085 + EEPROM, and the Feather's I2C pull-ups)
void stemmaPower(bool on) {
#if defined(PIN_I2C_POWER)
  if (i2cPowerOff < 0) return;
  digitalWrite(PIN_I2C_POWER, on ? !i2cPowerOff : i2cPowerOff);
#else
  (void)on;
#endif
}

bool imuAcks() {
  Wire.beginTransmission(BNO_ADDR);
  return Wire.endTransmission() == 0;
}

// The Adafruit/CEVA BNO08x driver retries a failed I2C write forever, so a
// silent BNO085 is never sent commands: it is power-cycled and restarted.
void imuRecoverI2C(uint32_t now) {
  lastImuRecoverMs = now;
  imuRecoveries++;
  imuSoftTried = false;
  Log.printf("IMU: power-cycling STEMMA QT (recovery %lu)\n", (unsigned long)imuRecoveries);
  crumb(ST_IMU_BEGIN);
  Wire.end();
  stemmaPower(false);
  delay(I2C_POWER_OFF_MS);
  stemmaPower(true);
  delay(I2C_POWER_SETTLE_MS);
  i2cBusClear();
  Wire.begin();
  Wire.setClock(400000);
  if (imuStarted) {
    // Keep the existing driver session: the rebooted BNO085 announces a reset,
    // the library reports it through wasReset(), and the reports are re-enabled.
    imuOK = true;
    imuNeedReports = true;                     // backstop in case the reset notice is missed
    Log.printf("IMU: power-cycled, %s\n", imuAcks() ? "answering" : "not answering yet");
  } else {
    imuOK = imuBegin();
    Log.printf("IMU: start %s\n", imuOK ? "OK" : "failed");
  }
  imuQuietRefMs = millis();                    // give it IMU_SILENT_MS to start reporting
}

// Every report the BNO085 sends goes through here. (The Adafruit getSensorEvent()
// keeps only the LAST report of each I2C transfer; the BNO085 often packs the
// orientation and gyro reports into one transfer, so orientation was silently
// dropped for long stretches, which looked like a dead sensor and triggered
// needless power-cycles.)
// One accelerometer sample (m/s2, sensor axes X bow / Y port / Z up) -> ship
// axes in g, mounting tilt removed when level zero is on, then the log sums,
// the G-meter smoothing and its peak hold.
void onAccel(float ax, float ay, float az) {
  const float G = 9.80665f;
  float f = ax / G, st = -ay / G, u = az / G;         // forward, starboard, up
#if PITCH_FLIP
  f = -f;
#endif
#if ROLL_FLIP
  st = -st;
#endif
  if (P.zeroApplied) {                                 // undo the tilt level zero measured (roll, then pitch)
    float r = P.rollOffset * DEG_TO_RAD, pp = P.pitchOffset * DEG_TO_RAD;
    float s1 = st * cosf(r) + u * sinf(r), u1 = -st * sinf(r) + u * cosf(r);
    float f1 = f * cosf(pp) - u1 * sinf(pp), u2 = f * sinf(pp) + u1 * cosf(pp);
    f = f1; st = s1; u = u2;
  }
  uint32_t now = millis();
  bool gap = now - lastAccMs > 1000;
  lastAccMs = now;
  accF = f; accS = st; accU = u;
  accSumF += f; accSumS += st; accSumU += u; accSumN++;
  const float a = (IMU_REPORT_US / 1e6f) / (0.3f + IMU_REPORT_US / 1e6f);   // 0.3 s smoothing
  if (gap) { gFeltF = -f; gFeltS = -st; gVert = u; }
  else { gFeltF += a * (-f - gFeltF); gFeltS += a * (-st - gFeltS); gVert += a * (u - gVert); }
  if (!paused && haveImu && now - imuStartMs > WARMUP_MS) {   // peak hold on the smoothed values
    gPkF = fmaxf(gPkF, gFeltF);  gPkA = fmaxf(gPkA, -gFeltF);
    gPkS = fmaxf(gPkS, gFeltS);  gPkP = fmaxf(gPkP, -gFeltS);
    gPkHi = fmaxf(gPkHi, gVert); gPkLo = fminf(gPkLo, gVert);
  }
}

bool accFresh() { return lastAccMs && millis() - lastAccMs < 1000; }
void gPeaksReset() { gPkF = gPkA = gPkS = gPkP = 0; gPkHi = gPkLo = gVert; }

void imuEventHandler(void* cookie, sh2_SensorEvent_t* ev) {
  (void)cookie;
  sh2_SensorValue_t v;
  if (sh2_decodeSensorEvent(&v, ev) != SH2_OK) return;
  imuEvents++;
  switch (v.sensorId) {
    case SH2_GAME_ROTATION_VECTOR:
      imuQr = v.un.gameRotationVector.real; imuQi = v.un.gameRotationVector.i;
      imuQj = v.un.gameRotationVector.j;    imuQk = v.un.gameRotationVector.k;
      imuGotOrient = true;
      break;
    case SH2_ROTATION_VECTOR:
      imuQr = v.un.rotationVector.real; imuQi = v.un.rotationVector.i;
      imuQj = v.un.rotationVector.j;    imuQk = v.un.rotationVector.k;
      imuGotOrient = true;
      break;
    case SH2_GYROSCOPE_CALIBRATED:
      yawRate = -v.un.gyroscope.z * RAD_TO_DEG;          // Z up: CCW+; negate so stbd turn = +
#if YAW_FLIP
      yawRate = -yawRate;
#endif
      rotSmooth += (IMU_REPORT_US / 1e6f) / (ROT_SMOOTH_S + IMU_REPORT_US / 1e6f) * (yawRate - rotSmooth);
      break;
    case SH2_ACCELEROMETER:
      onAccel(v.un.accelerometer.x, v.un.accelerometer.y, v.un.accelerometer.z);
      break;
    default: break;
  }
}

// Silent IMU, in steps (each only after IMU_SILENT_MS of silence while the loop
// was actually polling it):
//   1. it answers on I2C: just ask for the reports again (no reset)
//   2. still silent, or not answering: power-cycle the STEMMA QT rail.
//      Each power-cycle that brings no data back doubles the wait before the
//      next one (10 s, 20 s, ... 2 min).
void imuSilent(uint32_t now) {
  if (!imuSoftTried && imuAcks()) {
    imuSoftTried = true;
    imuSoftRecoveries++;
    Log.printf("IMU: silent but answering, re-requesting reports (%lu)\n", (unsigned long)imuSoftRecoveries);
    crumb(ST_IMU_RESET);
    imuEnableReports();
    imuQuietRefMs = now;
    return;
  }
  if (now - lastImuRecoverMs < imuRecoverGapMs) return;
  imuRecoverI2C(now);
  imuRecoverGapMs = min(imuRecoverGapMs * 2, IMU_RECOVER_GAP_MAX_MS);
}

void serviceImu() {
  uint32_t now = millis();
  if (now - lastImuPollMs > 1000) imuQuietRefMs = now;   // loop was busy elsewhere: not the sensor's fault
  lastImuPollMs = now;
  if (reqI2cReset) {                           // dashboard button
    reqI2cReset = false;
    Log.println("IMU: I2C bus reset requested from the dashboard");
    imuRecoverI2C(now);
    return;
  }
  if (!imuOK) {                                // never started: keep trying to start it
    if (now - lastImuRetryMs > 5000) {
      lastImuRetryMs = now;
      crumb(ST_IMU_BEGIN);
      if (imuAcks()) imuOK = imuBegin();
      else if (now - lastImuRecoverMs > imuRecoverGapMs) imuRecoverI2C(now);
      if (imuOK) { imuQuietRefMs = millis(); Log.println("IMU: started"); }
    }
    return;
  }
  crumb(ST_IMU_RESET);
  if (bno08x.wasReset()) {                     // BNO085 rebooted itself: reports must be re-enabled
    Log.println("IMU: sensor reset");
    if (imuAcks()) { imuEnableReports(); imuNeedReports = false; }
  }
  if (imuNeedReports && now - lastImuRecoverMs > 700 && imuAcks()) {
    imuEnableReports();
    imuNeedReports = false;
    Log.println("IMU: reports re-enabled after power-cycle");
  }
  crumb(ST_IMU_READ);

  imuGotOrient = false;
  for (int i = 0; i < 12; i++) {               // read until a transfer brings nothing new
    int before = imuEvents;
    sh2_service();
    if (imuEvents == before) break;
  }
  if (imuGotOrient) {
    quatToEuler(imuQr, imuQi, imuQj, imuQk);
    onOrientation();
    imuSoftTried = false;                      // data is flowing again
    imuRecoverGapMs = IMU_RECOVER_GAP_MS;
  } else if (now - imuQuietRefMs > IMU_SILENT_MS) {
    imuSilent(now);
  }
}

// ============================================================================
//                                  TIME
// ============================================================================
// Keeps the ESP32 system clock (UTC) in step, so files on the SD card get
// correct modified times and the .zip download carries them.
void setSystemTime(uint32_t unixT) {
  struct timeval tv;
  tv.tv_sec = (time_t)unixT;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

void setClock(const DateTime& g) {
  if (rtcOK) {
    rtc.adjust(g);
    rtc.start();
    rtcValid = true;
    resyncPending = true;
    lastRtcSec = 255;
  }
  baseUnix = g.unixtime();
  baseMillis = millis();
  clockSynced = true;
  clockSrc = 'R';
  setSystemTime(baseUnix);
}

// Locks millis() to the RTC second edge (+/-20 ms) at boot and every 60 s
void serviceRtc() {
  if (!rtcOK || !rtcValid) return;
  uint32_t now = millis();
  if (!resyncPending && now - lastResyncMs > 60000UL) { resyncPending = true; lastRtcSec = 255; }
  if (resyncPending && now - lastRtcPollMs >= 20) {
    lastRtcPollMs = now;
    DateTime t = rtc.now();
    if (lastRtcSec == 255) lastRtcSec = t.second();
    else if (t.second() != lastRtcSec) {
      baseUnix = t.unixtime();
      baseMillis = now;
      clockSynced = true;
      resyncPending = false;
      lastResyncMs = now;
      setSystemTime(baseUnix);
    }
  }
}

void onRmc() {
  if (!GPS.fix) return;
  if (GPS.year < 24 || GPS.month < 1 || GPS.month > 12 || GPS.day < 1 || GPS.day > 31) return;
  DateTime g(2000 + GPS.year, GPS.month, GPS.day, GPS.hour, GPS.minute, GPS.seconds);
  if (!gpsTimeSeen) {                                  // GPS now owns the clock (phone time no longer used)
    gpsTimeSeen = true;
    if (clockSynced && labs((long)g.unixtime() - (long)nowUnix()) <= RTC_MAX_DRIFT_S) clockSrc = 'R';
  }

  if (!rtcValid) {
    setClock(g);
    if (rtcOK) Log.println("RTC: set from GPS (RTC had no valid time)");
    return;
  }
#if RTC_MAX_DRIFT_S > 0
  if (clockSynced && (!driftChecked || millis() - lastDriftCheckMs > 600000UL)) {
    driftChecked = true;
    lastDriftCheckMs = millis();
    long diff = (long)g.unixtime() - (long)nowUnix();
    if (labs(diff) > RTC_MAX_DRIFT_S) {
      Log.printf("RTC: off by %ld s vs GPS, correcting\n", diff);
      setClock(g);
    }
  }
#endif
}

void updateGpsData(bool isRMC, bool isGGA) {
  if (isGGA) {
    gps.gpsSats = GPS.satellites;
    gps.gpsHdop = GPS.HDOP;
    gps.gpsFixQ = GPS.fixquality;
    if (GPS.fixquality > 0) gps.gpsAlt = GPS.altitude;
  }
  if (isRMC) {
    gps.gpsFix = GPS.fix;
    gpsNewForLog = true;
    gpsRmcMs = millis();
    if (GPS.fix) {
      gps.gpsLat       = GPS.latitudeDegrees;
      gps.gpsLon       = GPS.longitudeDegrees;
      gps.gpsSpeed     = GPS.speed * KNOTS_TO_MS;
      gps.gpsAngle     = GPS.angle;
      gps.gpsLastFixMs = millis();
    }
  }
}

// Split an NMEA sentence into fields (in place); returns the field count.
// f[0] is the talker + type ("GPGSV"); the checksum is cut off.
int nmeaFields(char* buf, char** f, int maxF) {
  char* star = strchr(buf, '*');
  if (star) *star = 0;
  int n = 0;
  char* p = buf + (buf[0] == '$' ? 1 : 0);
  while (n < maxF) {
    f[n++] = p;
    char* c = strchr(p, ',');
    if (!c) break;
    *c = 0;
    p = c + 1;
  }
  return n;
}

// GSV: satellites in view, each with elevation, azimuth and signal (not
// parsed by the Adafruit library). $xxGSV,msgs,msg,inView,{prn,elev,az,snr} x 4
void parseGsv(const char* s) {
  char buf[100], *f[24];
  strncpy(buf, s, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
  int n = nmeaFields(buf, f, 24);
  int sys = (f[0][0] == 'G' && f[0][1] == 'L') ? 1 : 0;   // GLGSV = GLONASS, else GPS
  if (n > 3 && *f[3]) gpsInView[sys] = (uint8_t)atoi(f[3]);
  uint32_t now = millis();
  for (int k = 4; k + 3 < n; k += 4) {                       // a trailing signal ID (NMEA 4.1) is skipped
    if (!*f[k]) continue;
    int prn = atoi(f[k]), snr = *f[k + 3] ? atoi(f[k + 3]) : 0;
    if (snr > gpsSnrAcc) gpsSnrAcc = (uint8_t)snr;
    if (prn <= 0 || prn > 127 || !*f[k + 1] || !*f[k + 2]) continue;   // no position: not plotted
    int slot = -1, oldest = 0;
    for (int i = 0; i < SKY_MAX; i++) {
      if (sky[i].prn == prn) { slot = i; break; }
      if (sky[i].ms < sky[oldest].ms) oldest = i;
    }
    if (slot < 0) slot = oldest;
    sky[slot] = {(uint8_t)prn, (uint8_t)constrain(atoi(f[k + 1]), 0, 90), (uint16_t)(atoi(f[k + 2]) % 360), (uint8_t)snr, now};
  }
  if (now - gpsSnrMs >= 5000) { gpsBestSnr = gpsSnrAcc; gpsSnrAcc = 0; gpsSnrMs = now; }
}

// GSA: PRNs used in the fix (fields 3-14). A fix gives one GSA per system,
// so a GSA more than 0.5 s after the last one starts a new set.
void parseGsa(const char* s) {
  char buf[100], *f[20];
  strncpy(buf, s, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
  int n = nmeaFields(buf, f, 20);
  uint32_t now = millis();
  if (now - skyUsedMs > 500) skyUsed[0] = skyUsed[1] = skyUsed[2] = skyUsed[3] = 0;
  skyUsedMs = now;
  for (int k = 3; k <= 14 && k < n; k++) {
    int prn = *f[k] ? atoi(f[k]) : 0;
    if (prn > 0 && prn < 128) skyUsed[prn >> 5] |= 1UL << (prn & 31);
  }
}

bool skyIsUsed(uint8_t prn) {
  return millis() - skyUsedMs < SKY_STALE_MS && (skyUsed[prn >> 5] >> (prn & 31) & 1);
}

void serviceGps() {
  int guard = 0;
  while (Serial1.available() && guard++ < 2048) {
    GPS.read();
    if (GPS.newNMEAreceived()) {
      char* s = GPS.lastNMEA();
      gpsLastNmeaMs = millis();
      gpsNmeaCount++;
      if (strstr(s, "GSV")) { parseGsv(s); continue; }
      if (strstr(s, "GSA")) { parseGsa(s); continue; }
      bool isRMC = strstr(s, "RMC") != nullptr;
      bool isGGA = strstr(s, "GGA") != nullptr;
      if (GPS.parse(s)) {
        updateGpsData(isRMC, isGGA);
        if (isRMC) onRmc();
      }
    }
  }
}

// Dashboard GPS diagnostics buttons (loop task).
// Restart: wake it (in case it is in standby) and send the whole set-up again.
// Cold start (PMTK104): the module forgets its saved satellite data, time and
// position, and its settings return to factory, so they are sent again after.
// The first fix after a cold start can take several minutes.
void gpsRestart(bool cold) {
  gpsRestarts++;
  Log.printf("GPS: %s requested from the dashboard\n", cold ? "COLD START" : "restart");
  crumb(ST_GPS);
  gpsSend("PMTK000");                              // any byte wakes it from standby
  if (cold) {
    gpsSend("PMTK104");
    for (int i = 0; i < 15; i++) { delay(100); esp_task_wdt_reset(); }   // let it reboot
  }
  gpsConfigure();
  gpsInView[0] = gpsInView[1] = 0;
  gpsBestSnr = gpsSnrAcc = 0;
}

// Send a PMTK sentence; body is the text between '$' and '*'
void gpsSend(const char* body) {
  uint8_t cs = 0;
  for (const char* c = body; *c; c++) cs ^= (uint8_t)*c;
  Serial1.printf("$%s*%02X\r\n", body, cs);
  Serial1.flush();
  delay(50);
}

void gpsConfigure() {
  const uint32_t baud = (GPS_RATE_HZ > 1) ? 115200UL : 9600UL;
  char cmd[24];
  snprintf(cmd, sizeof(cmd), "PMTK251,%lu", (unsigned long)baud);

  Serial1.setRxBufferSize(2048);
  // The backup cell can keep the module at its last baud, so send the baud
  // command at both speeds, then settle on the target.
  Serial1.begin(9600, SERIAL_8N1, RX, TX);
  delay(100);
  gpsSend(cmd);
  Serial1.updateBaudRate(115200);
  delay(100);
  gpsSend(cmd);
  Serial1.updateBaudRate(baud);
  delay(200);
  while (Serial1.available()) Serial1.read();

  gpsSend("PMTK314,0,1,0,1,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0");     // RMC + GGA every fix, GSA + GSV every 5th
  gpsSend("PGCMD,33,1");                                         // antenna status reports (older modules)
  gpsSend("CDCMD,33,1");                                         //   and newer CDTop modules
  if (GPS_RATE_HZ >= 10)     gpsSend("PMTK220,100");
  else if (GPS_RATE_HZ >= 5) gpsSend("PMTK220,200");
  else                       gpsSend("PMTK220,1000");
  if (GPS_RATE_HZ <= 5) {                                        // SBAS only valid <= 5 Hz
    gpsSend("PMTK313,1");                                        // enable SBAS search
    gpsSend("PMTK301,2");                                        // DGPS source = SBAS
  } else {
    gpsSend("PMTK313,0");
  }
  Log.printf("GPS: %d Hz, %lu baud, SBAS %s\n", GPS_RATE_HZ, (unsigned long)baud,
                GPS_RATE_HZ <= 5 ? "on" : "off");
}

// ============================================================================
//                                    SD
// ============================================================================
bool sdLock(uint32_t ms) { return xSemaphoreTake(sdMutex, pdMS_TO_TICKS(ms)) == pdTRUE; }
void sdUnlock() { xSemaphoreGive(sdMutex); }

bool sdMount() {
  if (SD.begin(SD_CS, SPI, SD_SPI_HZ)) return true;
  SD.end();
  if (SD.begin(SD_CS, SPI, 4000000)) {
    Log.println("SD: fast clock failed, mounted at 4 MHz");
    return true;
  }
  SD.end();
  return false;
}

void sdFail() {
  if (logFile) logFile.close();
  logName[0] = 0;
  SD.end();
  sdOK = false;
  Log.println("SD: write failed, will retry");
}

// New, unused file name for a recording. Call with sdMutex held.
void makeLogName(char* out, size_t n) {
  if (logStartUnix) {
    DateTime t(logStartUnix);
    char base[48];
    snprintf(base, sizeof(base), "/%04u%02u%02u_%02u%02u%02uZ%s%s", (unsigned)t.year(), (unsigned)t.month(),
             (unsigned)t.day(), (unsigned)t.hour(), (unsigned)t.minute(), (unsigned)t.second(),
             P.logTag[0] ? "_" : "", P.logTag);
    snprintf(out, n, "%s.csv", base);
    for (int k = 2; SD.exists(out) && k < 100; k++) snprintf(out, n, "%s_%d.csv", base, k);
  } else {
    for (int k = 1; k < 1000; k++) {
      snprintf(out, n, "/NOCLK_%03d%s%s.csv", k, P.logTag[0] ? "_" : "", P.logTag);
      if (!SD.exists(out)) break;
    }
  }
}

// Metadata lines (start with #) and the column names, at the top of a new file.
// pandas: pd.read_csv(path, comment='#')
void writeLogHeader() {
  char b[200];
  auto kv = [&](const char* k, const char* v) { snprintf(b, sizeof(b), "# %s,%s", k, v); logFile.println(b); };
  logFile.println("# Vessel Motion Recorder (VMR)");
  kv("firmware_build", __DATE__ " " __TIME__);
  kv("credits", "Written by Sophie Scopazzi (Aspect Wave LLC) and Claude (Anthropic) 2026");
  char id[16];
  uint64_t mac = ESP.getEfuseMac();
  snprintf(id, sizeof(id), "%02X%02X%02X", (unsigned)(mac >> 24) & 0xFF, (unsigned)(mac >> 32) & 0xFF, (unsigned)(mac >> 40) & 0xFF);
  kv("device_id", id);
  kv("file", recFile + 1);
  kv("name", P.logTag);
  kv("condition", LOAD_COND[P.logCond < LOAD_COND_N ? P.logCond : 0]);
  char t[72] = "unknown";
  if (logStartUnix) {
    DateTime d(logStartUnix);
    snprintf(t, sizeof(t), "%04u-%02u-%02uT%02u:%02u:%02uZ", (unsigned)d.year(), (unsigned)d.month(),
             (unsigned)d.day(), (unsigned)d.hour(), (unsigned)d.minute(), (unsigned)d.second());
  }
  kv("start_utc", t);
  kv("time_source", gpsTimeSeen ? "GPS" : clockSrc == 'P' ? "WiFi device (phone / laptop clock)" : clockSynced ? "RTC" : "none (no clock: utc_time blank)");
  snprintf(t, sizeof(t), "%+d (UTC = LT + ZD)", (int)P.zdHours);
  kv("zone_description", t);
  snprintf(t, sizeof(t), "%lu", 1000UL / LOG_INTERVAL_MS);
  kv("log_rate_hz", t);
  snprintf(t, sizeof(t), "%d", GPS_RATE_HZ);
  kv("gps_rate_hz", t);
  snprintf(t, sizeof(t), "BNO085 %s %lu Hz over I2C",
           USE_GAME_ROTATION_VECTOR ? "game rotation vector (no magnetometer)" : "rotation vector",
           1000000UL / IMU_REPORT_US);
  kv("imu", t);
  kv("axes", "X to bow / roll + = starboard down / pitch + = bow up / yaw relative (+ = clockwise) / yaw_rate + = turning to starboard");
#if ROLL_FLIP || PITCH_FLIP || YAW_FLIP
  snprintf(t, sizeof(t), "%s%s%s", ROLL_FLIP ? "roll " : "", PITCH_FLIP ? "pitch " : "", YAW_FLIP ? "yaw" : "");
  kv("sign_flips_applied", t);
#endif
  if (P.zeroApplied) snprintf(b, sizeof(b), "# level_zero,on (roll offset %.2f deg / pitch offset %.2f deg removed)", P.rollOffset, P.pitchOffset);
  else               snprintf(b, sizeof(b), "# level_zero,off (raw sensor angles)");
  logFile.println(b);
  if (P.shipBeamCm && P.shipDraftCm && P.shipLengthDm) {
    snprintf(b, sizeof(b), "# ship,beam %.2f m / draft %.2f m / LOA %.1f m (waterline length for gm_m = LOA - %.0f m)",
             P.shipBeamCm / 100.0f, P.shipDraftCm / 100.0f, P.shipLengthDm / 10.0f, LOA_TO_LWL_M);
    logFile.println(b);
  } else {
    kv("ship", "not entered (gm_m blank)");
  }
  kv("roll_period_s", "median of the last 9 full rolls about the 60 s mean; blank until 3 rolls");
  kv("gm_m", "estimate from roll period: GM = (2 C B / T)^2 / C = 0.373 + 0.023 B/d - 0.043 L/100 / L = LOA - 5 m (IMO IS Code 2008 A 2.3)");
  kv("event", "event number from the dashboard marker / event_note = its note");
  kv("gps_columns", "filled on the first row after each new fix only");
  kv("acc_columns", "acc_fwd_g / acc_stbd_g / acc_up_g: BNO085 accelerometer (gravity included = specific force) in ship axes, mean over each row; level and still = 0 / 0 / +1; heeled to starboard acc_stbd_g is negative; level-zero tilt removed when level_zero is on");
  logFile.println("utc_time,millis,roll_deg,pitch_deg,yaw_deg,yaw_rate_dpm,"
                  "gps_lat,gps_lon,gps_alt_m,gps_speed_ms,gps_speed_kn,gps_angle_deg,"
                  "gps_fix,gps_sats,gps_hdop,gps_fix_quality,gps_age_ms,"
                  "batt_v,batt_pct,zero_applied,roll_period_s,gm_m,event,event_note,"
                  "acc_fwd_g,acc_stbd_g,acc_up_g");
}

// Called with sdMutex held (loop task, while logging). Mounts the card if
// needed, (re)opens this recording's file for appending, and writes
// everything waiting in pend[].
void sdWritePending(uint32_t now) {
  if (!sdOK) {
    if (now - lastSdRetryMs < 10000) return;
    lastSdRetryMs = now;
    sdOK = sdMount();
    if (!sdOK) return;
    Log.println("SD: mounted");
  }

  if (!logFile) {
    if (!recFile[0]) makeLogName(recFile, sizeof(recFile));     // first write since Start
    logFile = SD.open(recFile, FILE_APPEND);                     // reopen after an SD error appends
    if (!logFile) { sdFail(); return; }
    strncpy(logName, recFile, sizeof(logName) - 1);
    logName[sizeof(logName) - 1] = 0;
    if (logFile.size() == 0) writeLogHeader();   // new file
    Log.printf("SD: recording to %s\n", logName);
  }

  if (pendLen) {
    if (logFile.write((const uint8_t*)pend, pendLen) != pendLen) { sdFail(); return; }
    logBytes += pendLen;
    pendLen = 0;
  }
  if (now - lastFlushMs > 2000) { logFile.flush(); lastFlushMs = now; }
}

// Applies Start/Stop requested from the dashboard (loop task)
void applyLogRequest(uint32_t now) {
  if (logWanted == logging) return;
  if (logWanted) {
    logging = true;                    // a new file opens on the first write
    logStartMs = now;
    logStartUnix = clockSynced ? nowUnix() : 0;
    recFile[0] = 0;                    // every Start gets its own file
    strncpy(P.logTag, pendTag, LOG_TAG_MAX);
    P.logTag[LOG_TAG_MAX] = 0;
    if (pendCond < LOAD_COND_N) P.logCond = pendCond;
    pendCond = 255;
    logInterrupted = false;
    evCount = 0;
    reqEvent = false;
    sysEvent = nullptr; pauseRowDue = false;
    logRows = 0; logBytes = 0; pendDropped = 0; pendLen = 0;
    gpsNewForLog = false;
    P.logActive = 1;
    Log.println("Logging: START");
  } else {
    if (sdLock(3000)) {
      if (sdOK && pendLen) sdWritePending(now);   // write what is buffered
      if (logFile) { logFile.flush(); logFile.close(); }
      logName[0] = 0;
      sdUnlock();
    }
    pendLen = 0;
    logging = false;
    sysEvent = nullptr; pauseRowDue = false;
    P.logActive = 0;
    Log.printf("Logging: STOP (%lu rows)\n", (unsigned long)logRows);
  }
  markPersistDirty(true);
}

void logSample() {
  uint32_t now = millis();
  applyLogRequest(now);
  if (!logging || (paused && !pauseRowDue)) {        // not recording, or paused: no rows
    gpsNewForLog = false;
    accSumF = accSumS = accSumU = 0; accSumN = 0;
    return;
  }
  logRows++;
  char ts[32] = "";
  if (clockSynced) {
    uint32_t el = now - baseMillis;
    DateTime t(baseUnix + el / 1000);
    snprintf(ts, sizeof(ts), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
             (unsigned)t.year(), (unsigned)t.month(), (unsigned)t.day(),
             (unsigned)t.hour(), (unsigned)t.minute(), (unsigned)t.second(),
             (unsigned)(el % 1000));
  }
  bool f = imuFresh();
  // GPS columns only on the first row after a new RMC; blank otherwise
  char gpsCols[160];
  if (gpsNewForLog) {
    gpsNewForLog = false;
    long age = (long)(now - gpsRmcMs);
    bool ok = gps.gpsFix;
    if (ok) {
      snprintf(gpsCols, sizeof(gpsCols), "%.6f,%.6f,%.1f,%.2f,%.2f,%.1f,1,%d,%.2f,%d,%ld",
               gps.gpsLat, gps.gpsLon, gps.gpsAlt, gps.gpsSpeed, gps.gpsSpeed / KNOTS_TO_MS,
               gps.gpsAngle, (int)gps.gpsSats, gps.gpsHdop, (int)gps.gpsFixQ, age);
    } else {
      snprintf(gpsCols, sizeof(gpsCols), ",,,,,,0,%d,,0,%ld", (int)gps.gpsSats, age);
    }
  } else {
    snprintf(gpsCols, sizeof(gpsCols), ",,,,,,,,,,");
  }

  // roll period, GM estimate, event marker (blank when none)
  char extra[80], rp[10] = "", gm[10] = "";
  if (f && !isnan(rollPeriod)) snprintf(rp, sizeof(rp), "%.2f", rollPeriod);
  if (f && !isnan(gmEst)) snprintf(gm, sizeof(gm), "%.2f", gmEst);
  // a marked event from the dashboard first; a PAUSED / RESUMED marker takes the next free row
  const char* evNote = reqEvent ? (const char*)reqEventNote : sysEvent;
  if (evNote) {
    LogEvent e;
    e.n = ++evCount;
    e.unix = clockSynced ? nowUnix() : 0;
    strncpy(e.note, evNote, EVENT_NOTE_MAX);
    e.note[EVENT_NOTE_MAX] = 0;
    for (int i = 4; i > 0; i--) evList[i] = evList[i - 1];
    evList[0] = e;
    if (reqEvent) reqEvent = false;
    else { sysEvent = nullptr; pauseRowDue = false; }   // marker written (rows stop after PAUSED)
    snprintf(extra, sizeof(extra), "%s,%s,%u,%s", rp, gm, (unsigned)e.n, e.note);
    Log.printf("Event %u: %s\n", (unsigned)e.n, e.note);
  } else {
    snprintf(extra, sizeof(extra), "%s,%s,,", rp, gm);
  }

  char acc[40] = ",,";                                 // mean acceleration over this row, g
  if (accSumN && accFresh())
    snprintf(acc, sizeof(acc), "%.4f,%.4f,%.4f", accSumF / accSumN, accSumS / accSumN, accSumU / accSumN);
  accSumF = accSumS = accSumU = 0; accSumN = 0;

  char line[400];
  snprintf(line, sizeof(line),
           "%s,%lu,%.2f,%.2f,%.2f,%.2f,%s,%.3f,%.1f,%d,%s,%s\n",
           ts, (unsigned long)now,
           f ? roll : NAN, f ? pitch : NAN, f ? yawDeg : NAN, f ? yawRate * 60.0f : NAN,
           gpsCols, battV, battPct, (int)P.zeroApplied, extra, acc);

  size_t L = strlen(line);
  if (pendLen + L <= pendCap) { memcpy(pend + pendLen, line, L); pendLen += L; }
  else if (sdOK) pendDropped++;

  if (!sdLock(0)) return;          // card briefly busy (web file listing): keep buffering
  sdWritePending(now);
  if (!sdOK) pendLen = 0;          // no card mounted: nothing to keep
  sdUnlock();
}

// SOG / COG moving averages: 1 Hz history, over the last P.sogAvgS s.
// SOG is the mean speed. COG is the direction of the mean velocity vector
// (east/north components), so headings wrap through 000/360 correctly and
// near-zero speeds (random GPS COG) carry almost no weight.
void serviceSog(uint32_t now) {
  if (now - lastSogTickMs < 1000) return;
  lastSogTickMs = (now - lastSogTickMs > 3000) ? now : lastSogTickMs + 1000;
  bool fx = gpsFixValid();
  float kn = fx ? gps.gpsSpeed / KNOTS_TO_MS : NAN;
  float a = gps.gpsAngle * (float)DEG_TO_RAD;
  sogHist[sogIdx] = kn;
  velE[sogIdx] = fx ? kn * sinf(a) : NAN;
  velN[sogIdx] = fx ? kn * cosf(a) : NAN;
  sogIdx = (sogIdx + 1) % 60;
  float sum = 0, se = 0, sn = 0;
  int cnt = 0;
  for (int i = 1; i <= P.sogAvgS && i <= 60; i++) {
    int k = (sogIdx + 60 - i) % 60;
    if (isnan(sogHist[k])) continue;
    sum += sogHist[k]; se += velE[k]; sn += velN[k]; cnt++;
  }
  sogAvgKn = cnt ? sum / cnt : NAN;
  if (cnt && hypotf(se, sn) / cnt >= COG_AVG_MIN_KN) {
    float c = atan2f(se, sn) * (float)RAD_TO_DEG;
    cogAvgDeg = c < 0 ? c + 360.0f : c;
  } else {
    cogAvgDeg = NAN;
  }
}

// ============================================================================
//                             BATTERY / POT
// ============================================================================
// The MAX17048 runs from the battery: no battery usually means begin() fails
// or the voltage reads NaN. Both show as "No battery" (battV = NaN).
void serviceBattery() {
  uint32_t now = millis();
  if (lastBattMs != 0 && now - lastBattMs < 5000) return;
  lastBattMs = now;
  cpuTempC = temperatureRead();                      // on-chip sensor, every 5 s with the battery
  if (!battOK) battOK = maxlipo.begin(&Wire);       // battery plugged in later
  if (!battOK) { battV = battPct = battRate = NAN; return; }
  float v = maxlipo.cellVoltage();
  if (isnan(v) || v < 2.5f) { battV = battPct = battRate = NAN; return; }
  battV = v;
  battPct = constrain(maxlipo.cellPercent(), 0.0f, 100.0f);
  battRate = maxlipo.chargeRate();
}

void setDisplayOn(bool on) {
  if (on == displayOn) return;
  displayOn = on;
  if (displayOK) display.oled_command(displayOn || oledForced ? SH110X_DISPLAYON : SH110X_DISPLAYOFF);
  lastDrawMs = 0;
}

// 180-degree turn of the SH1107 frame buffer (page format): reversing the byte
// order and the bits in each byte maps pixel (x,y) to (W-1-x, H-1-y).
void flipBuffer180() {
  uint8_t* b = display.getBuffer();
  if (!b) return;
  size_t n = (size_t)display.width() * display.height() / 8;
  for (size_t i = 0, j = n - 1; i < j; i++, j--) { uint8_t t = b[i]; b[i] = b[j]; b[j] = t; }
  for (size_t i = 0; i < n; i++) {
    uint8_t v = b[i];
    v = (uint8_t)((v & 0xF0) >> 4 | (v & 0x0F) << 4);
    v = (uint8_t)((v & 0xCC) >> 2 | (v & 0x33) << 2);
    v = (uint8_t)((v & 0xAA) >> 1 | (v & 0x55) << 1);
    b[i] = v;
  }
}

uint8_t oledRotation() { return P.oledFlip ? 3 : 1; }

void setContrastLevel(int c) {
  c = constrain(c, 1, 255);
  contrast = c;
  if (displayOK) display.setContrast((uint8_t)contrast);
}

// Physical controls. Last change wins between these and the web page:
// the switch acts only on a flip, the pot only when turned past POT_TAKEOVER_COUNTS.
void servicePot() {
  uint32_t now = millis();
  if (now - lastPotMs < 50) return;
  lastPotMs = now;

#if USE_POT_SWITCH
  bool sw = digitalRead(POT_SW_PIN) == LOW;
  if (lastSwState < 0) lastSwState = sw;
  else if ((int8_t)sw != lastSwState) { lastSwState = sw; setDisplayOn(sw); }
#endif

  int raw = analogRead(POT_PIN);
  potEma = (potEma < 0) ? raw : potEma + 0.3f * (raw - potEma);
  if (!potControl && fabsf(potEma - potAtOverride) > POT_TAKEOVER_COUNTS) potControl = true;
  if (potControl) {
    int c = constrain(map((int)potEma, 0, 4095, 1, 255), 1, 255);
    if (abs(c - contrast) >= 3) setContrastLevel(c);
  }
}

// Charge mode: everything off except Wi-Fi, the web page and the battery gauge.
// BNO085 commands are only sent when it answers (its driver can hang otherwise).
void setChargeMode(bool on) {
  if (on == chargeMode) return;
  if (on) {
    logWanted = false;                           // stopped by logSample() on its next pass
    dispBeforeCharge = displayOn;
    setDisplayOn(false);
    gpsSend("PMTK161,0");                        // GPS standby; any byte wakes it
    if (imuStarted && imuAcks()) {               // interval 0 = report off: sensors power down
      bno08x.enableReport(SH2_GAME_ROTATION_VECTOR, 0);
      bno08x.enableReport(SH2_ROTATION_VECTOR, 0);
      bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED, 0);
    }
    setCpuFrequencyMhz(80);                      // lowest speed Wi-Fi allows
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    chargeMode = true;
    Log.println("Charge mode ON");
  } else {
    setCpuFrequencyMhz(240);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    gpsSend("PMTK000");                          // wakes the GPS from standby
    gpsSend("PMTK000");
    gpsLastNmeaMs = millis();                    // standby was on purpose: don't count it as silence
    if (imuStarted) imuNeedReports = true;       // re-enabled once it answers
    imuQuietRefMs = millis();                    // don't count charge mode as IMU silence
    lastImuRecoverMs = millis();
    setDisplayOn(dispBeforeCharge);
    chargeMode = false;
    Log.println("Charge mode OFF");
  }
}

// Pause/resume the web task around Wi-Fi changes
void pauseWeb(bool p) {
  webPaused = p;
  if (!p) return;
  uint32_t t0 = millis();
  while (!webIdle && millis() - t0 < 3000) vTaskDelay(10);
}

// Datalogger mode: while recording (started with Start), switch off Wi-Fi and
// the OLED and slow the CPU. It never starts a recording itself.
// Button B or the screen button brings Wi-Fi and the OLED back.
void setLoggerMode(bool on) {
  if (on == loggerMode) return;
  if (on) {
    if (chargeMode) setChargeMode(false);
    dispBeforeLogger = displayOn;
    setDisplayOn(false);
    pauseWeb(true);
    server.close();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    setCpuFrequencyMhz(80);
    pinMode(LED_BUILTIN, OUTPUT);
    loggerMode = true;
    Log.println("Datalogger mode ON (Wi-Fi off). Press B or the screen button to wake.");
  } else {
    setCpuFrequencyMhz(240);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    server.begin();
    pauseWeb(false);
    digitalWrite(LED_BUILTIN, LOW);
    setDisplayOn(dispBeforeLogger);
    loggerMode = false;
    lastDrawMs = 0;
    Log.println("Datalogger mode OFF (Wi-Fi on)");
  }
}

// Clock from the dashboard phone, used only until GPS time has been seen this boot
void applyPhoneTime() {
  if (!reqPhone) return;
  reqPhone = false;
  if (gpsTimeSeen) return;
  uint32_t u = reqPhoneUnix + (millis() - reqPhoneRxMs) / 1000;
  setClock(DateTime(u));
  clockSrc = 'P';
  if (!phoneZdDone) {                                  // ZD from the phone once per boot
    phoneZdDone = true;
    P.zdHours = reqPhoneZd;
    markPersistDirty(true);
  }
  Log.printf("Clock set from phone (ZD %+d)\n", (int)P.zdHours);
}

// Pause / resume (dashboard). While logging, a PAUSED row is written before
// the rows stop and a RESUMED row when they start again (event column).
void setPaused(bool on) {
  if (on == paused) return;
  paused = on;
  if (on) {
    pausedAtMs = millis();
    rsCur = {0, 0, 0};                                 // drop the part-filled 10 s roll block
    if (logging) { sysEvent = "PAUSED"; pauseRowDue = true; }
  } else if (pauseRowDue) {
    sysEvent = nullptr; pauseRowDue = false;           // resumed before the marker went out
  } else if (logging) {
    sysEvent = "RESUMED";
  }
  lastDrawMs = 0;
  Log.printf("VMR %s\n", on ? "PAUSED" : "RESUMED");
}

// The pause, update and serial screens show even with the OLED switched off;
// during an update the contrast is also set to full.
void serviceOledForce() {
  bool ota = otaState != OTA_IDLE, want = paused || ota || serOledOn;
  if (want != oledForced) {
    oledForced = want;
    if (displayOK && !displayOn) display.oled_command(want ? SH110X_DISPLAYON : SH110X_DISPLAYOFF);
    lastDrawMs = 0;
  }
  if (ota && contrastBeforeOta < 0) { contrastBeforeOta = contrast; setContrastLevel(255); }
  else if (!ota && contrastBeforeOta >= 0) { setContrastLevel(contrastBeforeOta); contrastBeforeOta = -1; }
  if (otaState == OTA_FAIL && millis() - otaEndMs > 15000) otaState = OTA_IDLE;   // error shown 15 s
}

// Restart button (card 15) and a verified firmware update (card 16): stop
// logging (closes the file; after an update it was stopped when the upload
// began), save settings, restart. Logging does not resume after this.
void doRestart(const char* why) {
  Log.println(why);
  if (logging || logWanted) { logWanted = false; applyLogRequest(millis()); }
  if (eepromOK) savePersist();
  delay(300);
  esp_restart();
}

// Settings and actions requested from the dashboard. The web task only sets
// req* variables; they are applied here, in the loop task, so the I2C bus,
// the OLED and the EEPROM image P are only ever touched from one task.
void applyWebRequests() {
  int16_t s = reqScreen;
  if (s >= 0) { reqScreen = -1; if (s < NUM_SCREENS) { screen = (uint8_t)s; lastDrawMs = 0; } }
  int16_t c = reqContrast;
  if (c >= 0) { reqContrast = -1; potControl = false; potAtOverride = potEma; setContrastLevel(c); }
  int8_t o = reqDisplayOn;
  if (o >= 0) { reqDisplayOn = -1; setDisplayOn(o == 1); }
  int8_t z = reqZero;
  if (z >= 0) { setZero(z == 1); reqZero = -1; }
  int8_t gx = reqGraphMax, gn = reqGraphMin;
  if (gx > 0) { reqGraphMax = 0; P.graphMax = gx; markPersistDirty(true); lastDrawMs = 0; }
  if (gn < 0) { reqGraphMin = 0; P.graphMin = gn; markPersistDirty(true); lastDrawMs = 0; }
  int8_t px = reqPitchMax, pn = reqPitchMin;
  if (px > 0) { reqPitchMax = 0; P.pitchGraphMax = px; markPersistDirty(true); }
  if (pn < 0) { reqPitchMin = 0; P.pitchGraphMin = pn; markPersistDirty(true); }
  if (reqResetMax) { reqResetMax = 0; resetAllTime(); }
  if (reqGmReset) { reqGmReset = 0; gmWindowReset(); }
  if (reqClinoReset) { reqClinoReset = 0; clinoMaxS = clinoMaxP = 0; }
  if (reqGReset) { reqGReset = 0; gPeaksReset(); }
  int8_t tc = reqTrimCal;
  if (tc) {
    if (tc > 0) trimCalibrate(reqCalF, reqCalM, reqCalA);
    else { P.trimCal = 0; markPersistDirty(true); Log.println("Trim calibration cleared"); }
    reqTrimCal = 0;
  }
  int8_t cm = reqCharge;
  if (cm >= 0) { reqCharge = -1; setChargeMode(cm == 1); }
  if (reqLogger == 1 && millis() - reqLoggerAtMs > 600) { reqLogger = -1; setLoggerMode(true); }   // reply sent first
  if (reqBeamCm >= 0 || reqDraftCm >= 0 || reqLengthDm >= 0 || reqLbpDm >= 0 || reqRollThr > 0) {
    if (reqBeamCm >= 0)   { P.shipBeamCm = (uint16_t)reqBeamCm;     reqBeamCm = -1; }
    if (reqDraftCm >= 0)  { P.shipDraftCm = (uint16_t)reqDraftCm;   reqDraftCm = -1; }
    if (reqLengthDm >= 0) { P.shipLengthDm = (uint16_t)reqLengthDm; reqLengthDm = -1; }
    if (reqLbpDm >= 0)    { P.shipLbpDm = (uint16_t)reqLbpDm;       reqLbpDm = -1; }
    if (reqRollThr > 0)   { P.rollThrDeg = (uint8_t)reqRollThr;     reqRollThr = -1; }
    markPersistDirty(true);
    lastStatsMs = 0;                                  // recompute GM / statistics now
    gmWindowReset();                          // old samples used other particulars
  }
  int8_t fl = reqFlip;
  if (fl >= 0) {
    reqFlip = -1;
    P.oledFlip = fl;
    if (displayOK) display.setRotation(oledRotation());
    markPersistDirty(true);
    lastDrawMs = 0;
  }
  int8_t db = reqRotDb;
  if (db >= 0) { reqRotDb = -1; P.rotDeadband = db; markPersistDirty(true); }
  applyPhoneTime();
  int8_t rg = reqGps;
  if (rg) { reqGps = 0; gpsRestart(rg == 2); }
  if (reqRestart) doRestart("Restart requested from the dashboard");
  int8_t pz = reqPause;
  if (pz >= 0) { reqPause = -1; setPaused(pz == 1); }
  if (reqOtaReboot && millis() - otaEndMs > 1000) doRestart("Firmware update installed: restarting");   // reply sent first
  int8_t gi = reqGraphIdx;
  if (gi >= 0) { reqGraphIdx = -1; P.graphSecIdx = gi; markPersistDirty(true); lastDrawMs = 0; }
  int8_t sa = reqSogAvg;
  if (sa > 0) { reqSogAvg = 0; P.sogAvgS = sa; markPersistDirty(true); }
  if (reqZdSet) { P.zdHours = reqZd; reqZdSet = 0; markPersistDirty(true); lastDrawMs = 0; }
  int16_t nr = reqNudgeRoll, np = reqNudgePitch;
  if (nr || np) {                          // shift the READING: offset moves the other way
    P.rollOffset  = roundf((P.rollOffset  - nr * 0.1f) * 100.0f) / 100.0f;
    P.pitchOffset = roundf((P.pitchOffset - np * 0.1f) * 100.0f) / 100.0f;
    P.zeroApplied = 1;
    reqNudgeRoll = 0; reqNudgePitch = 0;
    markPersistDirty(true);
  }
}

// ============================================================================
//                                 BUTTONS
// ============================================================================
// Set uses the 5 s running mean of raw roll/pitch, not a single sample
bool setZero(bool on) {
  if (on && !haveImu) { showMessage("No IMU data"); return false; }
  if (!on) {
    P.zeroApplied = 0; P.rollOffset = 0; P.pitchOffset = 0;
    showMessage("Level zero CLEARED");
  } else {
    P.rollOffset = tareRollAvg; P.pitchOffset = tarePitchAvg; P.zeroApplied = 1;
    showMessage("Level zero SET");
  }
  Log.printf("Zero: %s  roll_off=%.2f pitch_off=%.2f\n",
                P.zeroApplied ? "on" : "off", P.rollOffset, P.pitchOffset);
  markPersistDirty(true);
  return true;
}

void toggleZero() {
  setZero(!P.zeroApplied);
}

void resetAllTime() {
  P.rollMaxPos = P.rollMaxNeg = P.pitchMaxPos = P.pitchMaxNeg = P.yawRateMax = 0;
  P.tRollMaxPos = P.tRollMaxNeg = P.tPitchMaxPos = P.tPitchMaxNeg = P.tYawRateMax = 0;
  imuStartMs = millis();                         // restart warm-up so the press itself is not recorded
  showMessage("All-time max RESET");
  Log.println("All-time maxima reset");
  markPersistDirty(true);
}

void serviceButtons() {
  btnA.update(); btnB.update(); btnC.update(); btnScr.update();
  if (loggerMode) {                                    // only B / screen button: wake up
    if (btnB.pressed || btnScr.pressed) setLoggerMode(false);
    return;
  }

  if (paused && btnB.pressed) {                        // B resumes after a pause (the screen stays)
    setPaused(false);
  } else if (btnB.pressed || btnScr.pressed) {
    screen = (screen + 1) % NUM_SCREENS;
    lastDrawMs = 0;
  }

  if (screen == SCREEN_GRAPH) {
    if (btnA.pressed && !btnC.isDown()) {          // A: top (positive) limit up
      P.graphMax += GRAPH_STEP_DEG;
      if (P.graphMax > GRAPH_LIMIT_DEG) P.graphMax = GRAPH_STEP_DEG;
      markPersistDirty(true); lastDrawMs = 0;
    }
    if (btnC.pressed && !btnA.isDown()) {          // C: bottom (negative) limit down
      P.graphMin -= GRAPH_STEP_DEG;
      if (P.graphMin < -GRAPH_LIMIT_DEG) P.graphMin = -GRAPH_STEP_DEG;
      markPersistDirty(true); lastDrawMs = 0;
    }
  }

  if (btnA.isDown() && btnC.isDown()) {
    uint32_t held = min(btnA.downFor(), btnC.downFor());
    if (!comboFired && held >= HOLD_ACTION_MS) {
      comboFired = true;
      if (screen == SCREEN_POSITION)     toggleZero();
      else if (screen == SCREEN_ALLTIME) resetAllTime();
      else if (screen == SCREEN_CLINO)   clinoMaxS = clinoMaxP = 0;
      else if (screen == SCREEN_GMETER)  gPeaksReset();
    }
  } else {
    comboFired = false;
  }
}

// ============================================================================
//                                  OLED
// ============================================================================
void printAt(int x, int y, const char* s) { display.setCursor(x, y); display.print(s); }

void fmtVal(char* out, size_t n, float v, int w, bool sign, int dp = 1) {
  if (isnan(v)) snprintf(out, n, "%*s", w, "--");
  else if (sign) snprintf(out, n, "%+*.*f", w, dp, v);
  else snprintf(out, n, "%*.*f", w, dp, v);
}

// Roll as side + size, 6 chars wide: "S 12.3" = starboard down, "P  4.0" = port down
void fmtRoll(char* out, size_t n, float v) {
  if (isnan(v)) { snprintf(out, n, "%6s", "--"); return; }
  float a = fabsf(v);
  if (a < 0.05f) snprintf(out, n, "%6s", "0.0");
  else           snprintf(out, n, "%c%5.1f", v > 0 ? 'S' : 'P', a);
}

void drawHeaderTime() {
  char src, z[8], b[24];
  uint32_t u = utcNow(src);
  zdText(z, sizeof(z));
  if (u) { DateTime t(ltOf(u)); snprintf(b, sizeof(b), "%02u:%02u:%02u ZD%s", (unsigned)t.hour(),
                                         (unsigned)t.minute(), (unsigned)t.second(), z); }
  else   snprintf(b, sizeof(b), "--:--:-- ZD%s", z);
  printAt(0, 0, b);
  if (battOK && !isnan(battPct)) {
    snprintf(b, sizeof(b), "%3d%%", (int)lroundf(battPct));
    printAt(128 - 6 * 4, 0, b);
  }
}

void drawLive() {
  drawHeaderTime();
  display.drawFastHLine(0, 9, 128, SH110X_WHITE);
  Win w = window1m();
  bool f = imuFresh();
  char line[32], a[10], b[10], c[10];

  snprintf(line, sizeof(line), "%-3s%6s%12s", "", "Live", "1-min max");
  printAt(0, 12, line);

  fmtRoll(a, sizeof(a), f ? roll : NAN);
  fmtRoll(b, sizeof(b), w.rMax);                 // furthest to starboard
  fmtRoll(c, sizeof(c), w.rMin);                 // furthest to port
  snprintf(line, sizeof(line), "Rol%s%s%s", a, b, c);
  printAt(0, 21, line);

  fmtVal(a, sizeof(a), f ? pitch : NAN, 6, true);
  fmtVal(b, sizeof(b), w.pMax, 6, true);
  fmtVal(c, sizeof(c), w.pMin, 6, true);
  snprintf(line, sizeof(line), "Pit%s%s%s", a, b, c);
  printAt(0, 30, line);

  fmtVal(a, sizeof(a), f ? yawDeg : NAN, 6, false);
  snprintf(line, sizeof(line), "Yaw%s  (rel)", a);
  printAt(0, 39, line);

  fmtVal(a, sizeof(a), f ? rotShown() : NAN, 6, true, 0);        // rate of turn, deg/min
  fmtVal(b, sizeof(b), w.yrMax * 60.0f, 6, false, 0);
  snprintf(line, sizeof(line), "ROT%s%s %c/min", a, b, (char)248);
  printAt(0, 48, line);

  char gpsS[8];
  if (gpsFixValid()) snprintf(gpsS, sizeof(gpsS), "%d", (int)gps.gpsSats);
  else               snprintf(gpsS, sizeof(gpsS), "--");
  snprintf(line, sizeof(line), "%s GPS %s Wi %d",
           logging ? (sdOK ? "REC " : "REC!") : (sdOK ? "stop" : "noSD"),
           gpsS, (int)WiFi.softAPgetStationNum());
  printAt(0, 56, line);
}

void drawGraph() {
  char line[32], a[10];
  fmtRoll(a, sizeof(a), imuFresh() ? roll : NAN);
  uint16_t secs = graphSecs();
  char per[12], len[8];
  if (isnan(rollPeriod)) snprintf(per, sizeof(per), "T --s");
  else                   snprintf(per, sizeof(per), "T%4.1fs", rollPeriod);
  if (secs < 60) snprintf(len, sizeof(len), "%us", (unsigned)secs);
  else           snprintf(len, sizeof(len), "%um", (unsigned)(secs / 60));
  snprintf(line, sizeof(line), "R%s %s %s", a, per, len);
  printAt(0, 0, line);

  const int gx0 = 19, gx1 = 127, gy0 = 10, gy1 = 63;
  float lo = P.graphMin, hi = P.graphMax;

  snprintf(line, sizeof(line), "S%d", (int)P.graphMax);         // top = starboard
  printAt(0, gy0, line);
  snprintf(line, sizeof(line), "P%d", -(int)P.graphMin);        // bottom = port
  printAt(0, gy1 - 7, line);
  display.drawFastVLine(gx0 - 1, gy0, gy1 - gy0 + 1, SH110X_WHITE);

  auto mapY = [&](float v) -> int {
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return gy1 - (int)lroundf((v - lo) * (gy1 - gy0) / (hi - lo));
  };

  int zy = mapY(0);
  for (int x = gx0; x <= gx1; x += 3) display.drawPixel(x, zy, SH110X_WHITE);
  if (zy > gy0 + 8 && zy < gy1 - 10) printAt(6, zy - 3, "0");
  if (!gRoll) return;

  // Each pixel column covers nS/cols samples; draw that column's min-max span,
  // joined to the previous column, so peaks survive on the 2 min view.
  const int cols = gx1 - gx0 + 1;
  const int nS = (int)((uint32_t)secs * 1000UL / GRAPH_SAMPLE_MS);

  if (secs >= GRAPH_ENVELOPE_S) {
    // 5 / 10 min: amplitude envelope. Highest and lowest roll within a sliding
    // window of about one roll period, drawn as two lines (peak to starboard,
    // peak to port), so the individual swings don't turn into a solid block.
    static int8_t cLo[128], cHi[128];
    static bool cOk[128];
    for (int c = 0; c < cols; c++) {
      int s0 = (int)((long)c * nS / cols), s1 = (int)((long)(c + 1) * nS / cols);
      float vlo = INFINITY, vhi = -INFINITY;
      for (int si = s0; si < s1; si++) {
        int age = nS - 1 - si;
        if (age >= gCount) continue;
        float v = gRoll[(gHead + GRAPH_POINTS - 1 - age) % GRAPH_POINTS];
        if (isnan(v)) continue;
        vlo = fminf(vlo, v); vhi = fmaxf(vhi, v);
      }
      cOk[c] = vlo <= vhi;
      if (cOk[c]) { cLo[c] = (int8_t)mapY(vlo); cHi[c] = (int8_t)mapY(vhi); }
    }
    float win = isnan(rollPeriod) ? 15.0f : constrain(1.25f * rollPeriod, 8.0f, 30.0f);
    int h = (int)ceilf(win * cols / secs) / 2;      // window = 2h+1 columns, at least one roll
    if (h < 1) h = 1;
    int pu = -1, pd = -1;
    for (int c = 0; c < cols; c++) {
      if (!cOk[c]) { pu = pd = -1; continue; }
      int up = gy1, dn = gy0;                       // screen y: smaller = higher
      for (int j = max(0, c - h); j <= min(cols - 1, c + h); j++)
        if (cOk[j]) { up = min(up, (int)cHi[j]); dn = max(dn, (int)cLo[j]); }
      int x = gx0 + c;
      if (pu >= 0) { display.drawLine(x - 1, pu, x, up, SH110X_WHITE); display.drawLine(x - 1, pd, x, dn, SH110X_WHITE); }
      else { display.drawPixel(x, up, SH110X_WHITE); display.drawPixel(x, dn, SH110X_WHITE); }
      pu = up; pd = dn;
    }
    return;
  }

  int prevY = -1;
  for (int c = 0; c < cols; c++) {
    int s0 = (int)((long)c * nS / cols), s1 = (int)((long)(c + 1) * nS / cols);
    if (s1 <= s0) s1 = s0 + 1;
    float vlo = INFINITY, vhi = -INFINITY, vfirst = NAN, vlast = NAN;
    for (int si = s0; si < s1; si++) {
      int age = nS - 1 - si;                       // 0 = newest sample
      if (age >= gCount) continue;
      float v = gRoll[(gHead + GRAPH_POINTS - 1 - age) % GRAPH_POINTS];
      if (isnan(v)) continue;
      if (isnan(vfirst)) vfirst = v;
      vlast = v;
      vlo = fminf(vlo, v);
      vhi = fmaxf(vhi, v);
    }
    if (isnan(vfirst)) { prevY = -1; continue; }
    int x = gx0 + c;
    if (prevY >= 0) display.drawLine(x - 1, prevY, x, mapY(vfirst), SH110X_WHITE);
    display.drawFastVLine(x, mapY(vhi), mapY(vlo) - mapY(vhi) + 1, SH110X_WHITE);
    prevY = mapY(vlast);
  }
}

void drawPosition() {
  char src, line[32], v[16];
  uint32_t u = utcNow(src);
  if (u) {
    DateTime lt(ltOf(u)), ut(u);
    snprintf(line, sizeof(line), "%02u:%02u:%02u LT  %02u:%02uZ", (unsigned)lt.hour(), (unsigned)lt.minute(),
             (unsigned)lt.second(), (unsigned)ut.hour(), (unsigned)ut.minute());
  } else {
    snprintf(line, sizeof(line), "No time yet");
  }
  printAt(0, 0, line);

  if (gpsFixValid()) {
    fmtDM(v, sizeof(v), gps.gpsLat, 'N', 'S', false);
    snprintf(line, sizeof(line), "Lat  %s", v);
    printAt(0, 8, line);
    fmtDM(v, sizeof(v), gps.gpsLon, 'E', 'W', true);
    snprintf(line, sizeof(line), "Lon %s", v);
    printAt(0, 16, line);
    char sg[8], cg[6];                                  // averaged SOG / COG (card 14 setting)
    if (isnan(sogAvgKn)) snprintf(sg, sizeof(sg), " --.-"); else snprintf(sg, sizeof(sg), "%4.1f", sogAvgKn);
    if (isnan(cogAvgDeg)) snprintf(cg, sizeof(cg), "---"); else snprintf(cg, sizeof(cg), "%03d", (int)lroundf(cogAvgDeg) % 360);
    snprintf(line, sizeof(line), "SOG %skn  COG %sT", sg, cg);
    printAt(0, 24, line);
  } else {
    printAt(0, 8, "Lat  --");
    printAt(0, 16, "Lon  --");
    printAt(0, 24, "No GPS fix");
  }
  snprintf(line, sizeof(line), "GPS %2d/%-2d sat %2ddB", (int)gps.gpsSats, gpsInView[0] + gpsInView[1], (int)gpsBestSnr);
  printAt(0, 32, line);

  // system health (roll and pitch are on the other screens)
  if (isnan(battV)) snprintf(line, sizeof(line), "Batt none (USB)");
  else snprintf(line, sizeof(line), "Batt %.2fV %3.0f%%%s", battV, battPct,
                !isnan(battRate) && battRate > 0.5f ? " chg" : "");
  printAt(0, 40, line);
  static uint32_t staMs = 0;                     // phones on the hotspot, refreshed every 2 s
  static int staN = 0, staWorst = 0;
  if (!staMs || millis() - staMs > 2000) {
    staMs = millis();
    wifi_sta_list_t sl;
    staN = 0; staWorst = 0;
    if (esp_wifi_ap_get_sta_list(&sl) == ESP_OK) {
      staN = sl.num;
      for (int i = 0; i < sl.num; i++) if (!i || sl.sta[i].rssi < staWorst) staWorst = sl.sta[i].rssi;
    }
  }
  if (staN) snprintf(line, sizeof(line), "WiFi %d phone%s %ddBm", staN, staN == 1 ? "" : "s", staWorst);
  else snprintf(line, sizeof(line), "WiFi no phones");
  printAt(0, 48, line);
  snprintf(line, sizeof(line), "CPU %2.0fC SD %s IMU %s", cpuTempC, sdOK ? "ok" : "--", imuFresh() ? "ok" : "--");
  printAt(0, 56, line);
  if (P.zeroApplied) printAt(104, 8, "ZERO");                // level zero on (A+C here toggles it)
}

void fmtDateShort(char* out, size_t n, uint32_t t) {
  if (t == 0) { snprintf(out, n, "-------"); return; }
  DateTime d(t);
  snprintf(out, n, "%02u%s%02u", (unsigned)d.day(), MONTHS[(d.month() + 11) % 12],
           (unsigned)(d.year() % 100));
}

void drawAllTime() {
  printAt(0, 0, "ALL-TIME MAXIMA");
  if (P.zeroApplied) printAt(104, 0, "ZERO");
  display.drawFastHLine(0, 9, 128, SH110X_WHITE);

  // sizes only: the label says which way. ROT in deg/min.
  const char* labels[5] = {"Stbd", "Port", "BowUp", "BowDn", "ROT"};
  float vals[5]   = {P.rollMaxPos, -P.rollMaxNeg, P.pitchMaxPos, -P.pitchMaxNeg, P.yawRateMax * 60.0f};
  int dps[5]      = {1, 1, 1, 1, 0};
  uint32_t ts[5]  = {P.tRollMaxPos, P.tRollMaxNeg, P.tPitchMaxPos, P.tPitchMaxNeg, P.tYawRateMax};
  char line[32], v[10], d[12];
  for (int i = 0; i < 5; i++) {
    if (vals[i] == 0 && ts[i] == 0) snprintf(v, sizeof(v), "%6s", "--");
    else fmtVal(v, sizeof(v), vals[i], 6, false, dps[i]);
    fmtDateShort(d, sizeof(d), ts[i]);
    snprintf(line, sizeof(line), "%-5s%s %s", labels[i], v, d);
    printAt(0, 12 + i * 9, line);
  }
  printAt(0, 56, "Hold A+C 3s: reset");
}

void drawGps() {
  char line[32];
  bool fx = gpsFixValid();
  const char* q = !fx ? "NO FIX" : (gps.gpsFixQ == 2 ? "DGPS" : "GPS");
  snprintf(line, sizeof(line), "GPS %-6s %2dHz", q, GPS_RATE_HZ);
  printAt(0, 0, line);
  display.drawFastHLine(0, 9, 128, SH110X_WHITE);

  int inView = gpsInView[0] + gpsInView[1];          // used / in view
  if (isnan(gps.gpsHdop)) snprintf(line, sizeof(line), "Sats %2d/%-2d HDOP --", (int)gps.gpsSats, inView);
  else snprintf(line, sizeof(line), "Sats %2d/%-2d HDOP %.2f", (int)gps.gpsSats, inView, gps.gpsHdop);
  printAt(0, 11, line);

  if (fx) {
    snprintf(line, sizeof(line), "Lat %11.6f %c", fabsf(gps.gpsLat), gps.gpsLat < 0 ? 'S' : 'N');
    printAt(0, 20, line);
    snprintf(line, sizeof(line), "Lon %11.6f %c", fabsf(gps.gpsLon), gps.gpsLon < 0 ? 'W' : 'E');
    printAt(0, 28, line);
    if (isnan(gps.gpsAlt)) snprintf(line, sizeof(line), "Alt     -- m");
    else snprintf(line, sizeof(line), "Alt %7.1f m", gps.gpsAlt);
    printAt(0, 36, line);
    snprintf(line, sizeof(line), "SOG %4.1fkn %5.2fm/s", gps.gpsSpeed / KNOTS_TO_MS, gps.gpsSpeed);
    printAt(0, 44, line);
    snprintf(line, sizeof(line), "COG %5.1f%cT", gps.gpsAngle, (char)248);
    printAt(0, 52, line);
  } else {
    printAt(0, 20, "Lat  --");
    printAt(0, 28, "Lon  --");
    printAt(0, 36, "Alt  --");
    printAt(0, 44, "SOG  --");
    printAt(0, 52, "COG  --");
  }
  if (gps.gpsLastFixMs) {                       // fix age, right of COG line
    float age = (millis() - gps.gpsLastFixMs) / 1000.0f;
    if (age < 100) snprintf(line, sizeof(line), "%4.1fs", age);
    else snprintf(line, sizeof(line), " >99s");
  } else {
    snprintf(line, sizeof(line), "  --s");
  }
  printAt(128 - 6 * 5, 52, line);
}

void drawOverlay(const char* t) {
  display.fillRect(4, 20, 120, 24, SH110X_BLACK);
  display.drawRect(4, 20, 120, 24, SH110X_WHITE);
  int x = (128 - (int)strlen(t) * 6) / 2;
  if (x < 6) x = 6;
  printAt(x, 28, t);
}

void printCentered(int y, const char* s) {
  int x = (128 - (int)strlen(s) * 6) / 2;
  printAt(x < 0 ? 0 : x, y, s);
}

// Full-screen PAUSED notice (replaces every screen while paused)
void drawPaused() {
  display.fillRect(0, 0, 128, 30, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setTextSize(3);
  printAt(11, 4, "PAUSED");
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  uint32_t s = (millis() - pausedAtMs) / 1000;
  char t[24];
  printCentered(34, logging ? "Logging on hold" : "Nothing counted");
  if (s < 3600) snprintf(t, sizeof(t), "for %lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
  else snprintf(t, sizeof(t), "for %luh %02lum", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
  printCentered(45, t);
  printCentered(56, "Press B to resume");
}

// Firmware update: header with %, the Aspect Wave logo while the image
// arrives (result lines once it is done), and a progress bar along the bottom.
void drawOta() {
  uint8_t st = otaState;
  uint32_t d = otaDone, tot = otaTotal;
  int pct = st == OTA_OK ? 100 : tot ? (int)(100ULL * d / tot) : 0;
  if (pct > 100) pct = 100;
  char t[24];
  display.fillRect(0, 0, 128, 9, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  printAt(2, 1, st == OTA_FAIL ? "UPDATE FAILED" : "FIRMWARE UPDATE");
  if (st != OTA_FAIL) { snprintf(t, sizeof(t), "%3d%%", pct); printAt(128 - 4 * 6 - 2, 1, t); }
  display.setTextColor(SH110X_WHITE);
  if (st == OTA_RX) {                                         // the logo while it arrives
    display.drawBitmap((128 - AWLOGO_SMALL_W) / 2, 10, awLogoSmall, AWLOGO_SMALL_W, AWLOGO_SMALL_H, SH110X_WHITE);
  } else if (st == OTA_OK) {
    printAt(0, 11, "Image verified    OK");
    printAt(0, 19, "Boot slot set     OK");
    printAt(0, 27, "Logging stopped");
    printAt(0, 35, "Restarting...");
  } else {
    char e[22];                                                // the error, over two lines
    strncpy(e, otaErr, 21); e[21] = 0;
    printAt(0, 15, e);
    if (strlen(otaErr) > 21) { strncpy(e, otaErr + 21, 21); e[21] = 0; printAt(0, 23, e); }
    printAt(0, 35, "Old firmware kept");
    printAt(0, 43, "Logging is stopped");
  }
  if (st != OTA_FAIL) {
    display.drawRect(0, 53, 128, 11, SH110X_WHITE);
    display.fillRect(2, 55, 124 * pct / 100, 7, SH110X_WHITE);
  }
}

// ---- Screen 6: clinometer. A needle hanging from the top centre swings over
// a +/-40 deg scale (starboard down = needle to the right, as seen looking
// forward); the two small triangles are drag pointers that stay at the
// largest roll each way until A+C is held (or Reset on the dashboard).
#define CLINO_CX     64
#define CLINO_CY      2
#define CLINO_R      58
#define CLINO_MAXDEG 40
void clinoPt(float deg, float r, int& x, int& y) {
  float a = deg * DEG_TO_RAD;
  x = CLINO_CX + (int)lroundf(sinf(a) * r);
  y = CLINO_CY + (int)lroundf(cosf(a) * r);
}

void clinoPointer(float deg) {
  if (deg == 0) return;
  deg = constrain(deg, -(float)CLINO_MAXDEG - 2, (float)CLINO_MAXDEG + 2);
  int x0, y0, x1, y1, x2, y2;
  clinoPt(deg, CLINO_R - 7, x0, y0);                          // tip, just inside the scale
  clinoPt(deg - 2.5f, CLINO_R - 13, x1, y1);
  clinoPt(deg + 2.5f, CLINO_R - 13, x2, y2);
  display.fillTriangle(x0, y0, x1, y1, x2, y2, SH110X_WHITE);
}

void drawClino() {
  int x0, y0, x1, y1;
  for (int a = -CLINO_MAXDEG; a <= CLINO_MAXDEG; a += 2) {     // the arc
    clinoPt(a, CLINO_R, x0, y0);
    display.drawPixel(x0, y0, SH110X_WHITE);
  }
  for (int a = -CLINO_MAXDEG; a <= CLINO_MAXDEG; a += 5) {     // ticks: long every 10 deg
    clinoPt(a, CLINO_R, x0, y0);
    clinoPt(a, CLINO_R - (a % 10 ? 3 : 6), x1, y1);
    display.drawLine(x0, y0, x1, y1, SH110X_WHITE);
  }
  printAt(4, 46, "P40");
  printAt(106, 46, "S40");
  char t[16];
  bool f = imuFresh();
  if (f) {
    float r = constrain(roll, -(float)CLINO_MAXDEG - 3, (float)CLINO_MAXDEG + 3);
    clinoPt(r, CLINO_R - 4, x1, y1);
    display.drawLine(CLINO_CX, CLINO_CY, x1, y1, SH110X_WHITE);
    display.drawLine(CLINO_CX + 1, CLINO_CY, x1 + 1, y1, SH110X_WHITE);   // 2 px needle
    display.fillCircle(CLINO_CX, CLINO_CY, 3, SH110X_WHITE);
  }
  clinoPointer(clinoMaxS);
  clinoPointer(clinoMaxP);
  display.setTextSize(2);                                      // live roll, top left
  if (f) snprintf(t, sizeof(t), "%.1f", fabsf(roll)); else strcpy(t, "--");
  printAt(0, 0, t);
  display.setTextSize(1);
  printAt(0, 17, !f ? "" : fabsf(roll) < 0.05f ? "level" : roll > 0 ? "STBD" : "PORT");
  snprintf(t, sizeof(t), "P%5.1f", -clinoMaxP);                // drag pointer values, top right
  printAt(128 - 6 * 6, 0, t);
  snprintf(t, sizeof(t), "S%5.1f", clinoMaxS);
  printAt(128 - 6 * 6, 9, t);
}

// ---- Screen 7: stern view. The hull, seen from astern (starboard on the
// right), tilts with the real roll over a fixed sea line; pitch is the bar
// on the right (bow up = marker up).
void drawStern() {
  const int cx = 54, cy = 42;                                  // waterline centre of the hull
  bool f = imuFresh();
  float th = f ? roll * DEG_TO_RAD : 0, c = cosf(th), sn = sinf(th);
  // sea: a line with a few wavelets under it
  display.drawLine(0, cy, 104, cy, SH110X_WHITE);
  for (int x = 2; x < 104; x += 8) { display.drawPixel(x, cy + 3, SH110X_WHITE); display.drawPixel(x + 4, cy + 6, SH110X_WHITE); }
  // hull section (tanker-like) and deckhouse, x to starboard, y down, origin on the waterline
  static const int8_t HULL[][2] = {{-30, -9}, {30, -9}, {30, 7}, {26, 12}, {-26, 12}, {-30, 7}, {-30, -9}};
  static const int8_t HOUSE[][2] = {{-9, -9}, {-9, -20}, {9, -20}, {9, -9}};
  auto px = [&](int x, int y, int& ox, int& oy) {
    ox = cx + (int)lroundf(x * c - y * sn);
    oy = cy + (int)lroundf(x * sn + y * c);
  };
  int ax, ay, bx, by;
  for (int i = 0; i < 6; i++) {
    px(HULL[i][0], HULL[i][1], ax, ay); px(HULL[i + 1][0], HULL[i + 1][1], bx, by);
    display.drawLine(ax, ay, bx, by, SH110X_WHITE);
  }
  int hx[4], hy[4];
  for (int i = 0; i < 4; i++) px(HOUSE[i][0], HOUSE[i][1], hx[i], hy[i]);
  display.fillTriangle(hx[0], hy[0], hx[1], hy[1], hx[2], hy[2], SH110X_WHITE);
  display.fillTriangle(hx[0], hy[0], hx[2], hy[2], hx[3], hy[3], SH110X_WHITE);
  px(0, -20, ax, ay); px(0, -27, bx, by);                      // mast
  display.drawLine(ax, ay, bx, by, SH110X_WHITE);
  // pitch bar: +/-6 deg, ticks every 2 deg
  const int bxl = 116, top = 4, bot = 56, mid = (top + bot) / 2;
  display.drawLine(bxl, top, bxl, bot, SH110X_WHITE);
  for (int d = -6; d <= 6; d += 2) {
    int y = mid - d * (bot - top) / 12;
    display.drawLine(bxl - (d ? 2 : 4), y, bxl, y, SH110X_WHITE);
  }
  if (f) {
    float pc = constrain(pitch, -6.5f, 6.5f);
    int y = mid - (int)lroundf(pc * (bot - top) / 12.0f);
    display.fillTriangle(bxl + 2, y, bxl + 7, y - 3, bxl + 7, y + 3, SH110X_WHITE);
  }
  char t[16];
  if (f) snprintf(t, sizeof(t), "%c %.1f", fabsf(roll) < 0.05f ? ' ' : roll > 0 ? 'S' : 'P', fabsf(roll)); else strcpy(t, "--");
  printAt(0, 0, t);
  fmtVal(t, sizeof(t), f ? pitch : NAN, 4, true);            // pitch, top right beside its bar
  printAt(bxl - 4 - 6 * (int)strlen(t), 0, t);
}

// ---- Screen 8: sky plot. Horizon at the edge, overhead in the middle, north
// up. Filled = used in the fix, open = seen; bigger = stronger signal.
void drawSky() {
  const int cx = 31, cy = 32, R = 30;
  display.drawCircle(cx, cy, R, SH110X_WHITE);
  for (int a = 0; a < 360; a += 15) {                          // 45 deg elevation ring, dotted
    float r = a * DEG_TO_RAD;
    display.drawPixel(cx + (int)lroundf(sinf(r) * R / 2), cy - (int)lroundf(cosf(r) * R / 2), SH110X_WHITE);
  }
  display.drawPixel(cx, cy, SH110X_WHITE);
  display.fillRect(cx - 3, 0, 7, 9, SH110X_BLACK);             // "N" on the circle
  printAt(cx - 2, 1, "N");
  uint32_t now = millis();
  int nSeen = 0, nUsed = 0;
  for (int i = 0; i < SKY_MAX; i++) {
    const SkySat& v = sky[i];
    if (!v.prn || now - v.ms > SKY_STALE_MS) continue;
    nSeen++;
    float rr = R * (90 - v.el) / 90.0f, a = v.az * DEG_TO_RAD;
    int x = cx + (int)lroundf(sinf(a) * rr), y = cy - (int)lroundf(cosf(a) * rr);
    int rad = v.snr >= 35 ? 3 : v.snr >= 20 ? 2 : 1;
    if (skyIsUsed(v.prn)) { display.fillCircle(x, y, rad, SH110X_WHITE); nUsed++; }
    else if (v.snr) display.drawCircle(x, y, rad, SH110X_WHITE);
    else display.drawPixel(x, y, SH110X_WHITE);
  }
  char t[24];
  printAt(68, 0, "SKY");
  snprintf(t, sizeof(t), "seen %3d", nSeen);       printAt(68, 12, t);
  snprintf(t, sizeof(t), "used %3d", nUsed);       printAt(68, 21, t);
  snprintf(t, sizeof(t), "best %2ddB", (int)gpsBestSnr); printAt(68, 30, t);
  display.fillCircle(70, 46, 2, SH110X_WHITE); printAt(76, 43, "used");
  display.drawCircle(70, 56, 2, SH110X_WHITE); printAt(76, 53, "seen");
}

// ---- Screen 9: at the dock. Trim in feet-inches (60 s mean; by the stern / head)
// and list to 0.01 deg. "UNCAL" = trim measured from level zero only.
void drawDock() {
  char t[24];
  float tm = trimMetres(dockRawPitch), ft = tm / 0.3048f;
  display.fillRect(0, 0, 128, 9, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  printAt(2, 1, "AT THE DOCK");
  snprintf(t, sizeof(t), "%s", P.trimCal ? "cal" : "UNCAL");
  printAt(126 - 6 * (int)strlen(t), 1, t);
  display.setTextColor(SH110X_WHITE);
  printAt(0, 13, "TRIM");
  display.setTextSize(2);                                      // feet-inches, like the draft marks
  if (isnan(ft)) strcpy(t, "--");
  else { int in = (int)lroundf(fabsf(ft) * 12.0f); snprintf(t, sizeof(t), "%d-%02d", in / 12, in % 12); }
  printAt(28, 12, t);
  display.setTextSize(1);
  int x = 28 + 12 * (int)strlen(t) + 4;
  printAt(x, 12, isnan(ft) ? "" : fabsf(ft) < 0.05f ? "even" : ft > 0 ? "stern" : "head");
  if (!isnan(ft)) { snprintf(t, sizeof(t), "%.1fft", fabsf(ft)); printAt(x, 21, t); }
  display.drawLine(0, 32, 127, 32, SH110X_WHITE);
  printAt(0, 40, "LIST");
  display.setTextSize(2);
  if (isnan(dockRoll)) strcpy(t, "--");
  else snprintf(t, sizeof(t), "%c%.2f", fabsf(dockRoll) < 0.005f ? ' ' : dockRoll > 0 ? 'S' : 'P', fabsf(dockRoll));
  printAt(28, 39, t);
  display.setTextSize(1);
  printAt(28 + 12 * (int)strlen(t) + 3, 39, "deg");
  snprintf(t, sizeof(t), "%2us mean", (unsigned)dockN);
  printAt(0, 56, t);
}

// ---- Screen 10: G-meter. The dot is the felt push in the deck plane (up =
// forward, right = starboard): heeled to starboard it sits to starboard, like
// sliding down the deck. Rings every 0.1 g (0.2 g once a peak passes 0.3 g),
// a short trail, peak-hold ticks on each axis; vertical g on the bar.
void drawGMeter() {
  const int cx = 31, cy = 32, R = 30;
  float pkMax = fmaxf(fmaxf(gPkF, gPkA), fmaxf(gPkS, gPkP));
  float ring = pkMax > 0.3f ? 0.2f : 0.1f, full = 3 * ring;            // outer ring = 3 rings
  display.drawCircle(cx, cy, R, SH110X_WHITE);
  for (int k = 1; k <= 2; k++)
    for (int a = 0; a < 360; a += 20) {
      float r = a * DEG_TO_RAD;
      display.drawPixel(cx + (int)lroundf(sinf(r) * R * k / 3), cy - (int)lroundf(cosf(r) * R * k / 3), SH110X_WHITE);
    }
  display.drawLine(cx - 2, cy, cx + 2, cy, SH110X_WHITE);
  display.drawLine(cx, cy - 2, cx, cy + 2, SH110X_WHITE);
  auto lab = [&](int x, int y, const char* t) {                       // letters sit on the outer ring
    display.fillRect(x - 1, y - 1, 7, 9, SH110X_BLACK);
    printAt(x, y, t);
  };
  lab(cx - 2, cy - R - 2, "F");
  lab(cx - 2, cy + R - 5, "A");
  lab(cx - R - 1, cy - 3, "P");
  lab(cx + R - 3, cy - 3, "S");
  auto sc = [&](float g) { return (int)lroundf(constrain(g / full, -1.05f, 1.05f) * R); };
  // peak hold: short ticks across each axis
  int t;
  if (gPkS > 0.005f) { t = sc(gPkS);  display.drawLine(cx + t, cy - 2, cx + t, cy + 2, SH110X_WHITE); }
  if (gPkP > 0.005f) { t = sc(gPkP);  display.drawLine(cx - t, cy - 2, cx - t, cy + 2, SH110X_WHITE); }
  if (gPkF > 0.005f) { t = sc(gPkF);  display.drawLine(cx - 2, cy - t, cx + 2, cy - t, SH110X_WHITE); }
  if (gPkA > 0.005f) { t = sc(gPkA);  display.drawLine(cx - 2, cy + t, cx + 2, cy + t, SH110X_WHITE); }
  char b[24];
  bool fr = accFresh();
  if (fr) {
    int x = cx + sc(gFeltS), y = cy - sc(gFeltF);
    gTrailX[gTrailHead] = (int8_t)(x - cx); gTrailY[gTrailHead] = (int8_t)(y - cy);   // trail, one point a frame
    gTrailHead = (gTrailHead + 1) % G_TRAIL;
    if (gTrailN < G_TRAIL) gTrailN++;
    for (int i = 0; i < gTrailN; i++) display.drawPixel(cx + gTrailX[i], cy + gTrailY[i], SH110X_WHITE);
    display.fillCircle(x, y, 3, SH110X_WHITE);
  }
  // numbers on the right
  printAt(68, 0, "G-METER");
  if (fr) snprintf(b, sizeof(b), "%c %.2f", gFeltS >= 0 ? 'S' : 'P', fabsf(gFeltS)); else strcpy(b, "--");
  printAt(68, 11, b);
  if (fr) snprintf(b, sizeof(b), "%c %.2f", gFeltF >= 0 ? 'F' : 'A', fabsf(gFeltF)); else strcpy(b, "--");
  printAt(68, 20, b);
  if (fr) snprintf(b, sizeof(b), "V %.2f", gVert); else strcpy(b, "--");
  printAt(68, 29, b);
  snprintf(b, sizeof(b), "ring %.1fg", ring);
  printAt(68, 40, b);
  // vertical g bar: 0.5 ... 1.5 g, tick at 1 g, peak min / max ticks
  const int bx0 = 68, bx1 = 126, by = 54;
  display.drawRect(bx0, by, bx1 - bx0 + 1, 7, SH110X_WHITE);
  auto vx = [&](float g) { return bx0 + (int)lroundf(constrain((g - 0.5f), 0.0f, 1.0f) * (bx1 - bx0)); };
  display.drawLine(vx(1.0f), by - 2, vx(1.0f), by + 8, SH110X_WHITE);
  display.drawLine(vx(gPkLo), by + 1, vx(gPkLo), by + 5, SH110X_WHITE);
  display.drawLine(vx(gPkHi), by + 1, vx(gPkHi), by + 5, SH110X_WHITE);
  if (fr) display.fillRect(min(vx(1.0f), vx(gVert)), by + 2, abs(vx(gVert) - vx(1.0f)) + 1, 3, SH110X_WHITE);
}

// Serial Monitor copy on the OLED (card 13 "Show on OLED", latching): the
// latest lines in the 4 x 6 px TomThumb font, wrapped at 32 characters,
// 10 lines, newest at the bottom.
void drawSerial() {
  const int COLS = 32, ROWS = 10;
  display.setFont(&TomThumb);
  int row = ROWS - 1;
  uint32_t seq = Log.seq();
  char ln[SLOG_LEN + 1], seg[COLS + 1];
  for (uint32_t k = seq; k > 0 && row >= 0; k--) {
    if (!Log.get(k - 1, ln)) break;
    int L = strlen(ln), nr = L ? (L + COLS - 1) / COLS : 1;
    for (int r = nr - 1; r >= 0 && row >= 0; r--, row--) {
      strncpy(seg, ln + r * COLS, COLS); seg[COLS] = 0;
      printAt(0, row * 6 + 5, seg);                              // custom fonts: y is the baseline
    }
  }
  display.setFont();                                             // back to the built-in 6 x 8 font
}

void drawScreen() {
  bool ota = otaState != OTA_IDLE;
  if (!displayOK || (!displayOn && !oledForced)) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  if (ota || serOledOn || paused) {                 // priority: update, serial (latched), pause
    if (ota) drawOta(); else if (serOledOn) drawSerial(); else drawPaused();
    crumb(ST_OLED_SEND);
    display.display();
    return;
  }
  switch (screen) {
    case SCREEN_LIVE:     drawLive();     break;
    case SCREEN_GRAPH:    drawGraph();    break;
    case SCREEN_POSITION: drawPosition(); break;
    case SCREEN_ALLTIME:  drawAllTime();  break;
    case SCREEN_GPS:      drawGps();      break;
    case SCREEN_CLINO:    drawClino();    break;
    case SCREEN_STERN:    drawStern();    break;
    case SCREEN_SKY:      drawSky();      break;
    case SCREEN_DOCK:     drawDock();     break;
    case SCREEN_GMETER:   drawGMeter();   break;
  }
  // Progress while A+C held on a screen that uses it
  if ((screen == SCREEN_POSITION || screen == SCREEN_ALLTIME || screen == SCREEN_CLINO || screen == SCREEN_GMETER) &&
      btnA.isDown() && btnC.isDown() && !comboFired) {
    uint32_t held = min(btnA.downFor(), btnC.downFor());
    if (held > 300) {
      char t[24];
      int left = (int)((HOLD_ACTION_MS - (held < HOLD_ACTION_MS ? held : HOLD_ACTION_MS)) / 1000) + 1;
      snprintf(t, sizeof(t), "%s in %ds", screen == SCREEN_POSITION ? "Zero" : screen == SCREEN_CLINO ? "Pointers" :
               screen == SCREEN_GMETER ? "Peaks" : "Reset", left);
      drawOverlay(t);
    }
  } else if ((int32_t)(msgUntil - millis()) > 0) {
    drawOverlay(msgText);
  }
  crumb(ST_OLED_SEND);
  display.display();
}

// ============================================================================
//                              WEB DASHBOARD
// ============================================================================
// The page itself is at the very end of this file: the Arduino IDE's prototype
// generator cannot read past a raw string this large, so all code comes first.
extern const char INDEX_HTML[];

void jsonNum(String& s, float v, int dp) {
  if (isnan(v) || isinf(v)) s += "null";
  else s += String(v, dp);
}

void jsonKV(String& s, const char* k, float v, int dp, bool comma = true) {
  s += '"'; s += k; s += "\":";
  jsonNum(s, v, dp);
  if (comma) s += ',';
}

void jsonTime(String& s, const char* k, uint32_t t, bool comma = true) {
  s += '"'; s += k; s += "\":\"";
  if (t) {
    DateTime d(t);
    char b[32];
    snprintf(b, sizeof(b), "%04u-%02u-%02u %02u:%02u:%02u",
             (unsigned)d.year(), (unsigned)d.month(), (unsigned)d.day(),
             (unsigned)d.hour(), (unsigned)d.minute(), (unsigned)d.second());
    s += b;
  }
  s += '"';
  if (comma) s += ',';
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");      // always the page of the running firmware
  server.send(200, "text/html", INDEX_HTML);
}

void handleData() {
  String j;
  j.reserve(8192);

  char src;
  uint32_t u = utcNow(src);
  j += "{\"build\":\"" __DATE__ " " __TIME__ "\",\"unix\":"; j += (unsigned long)u;
  j += ",\"src\":\""; j += (src == 'G') ? "GPS" : (src == 'R') ? "RTC" : (src == 'P') ? "phone" : "none";
  j += "\",\"gpsTime\":"; j += gpsTimeSeen ? "true" : "false";
  j += ",\"paused\":"; j += paused ? "true" : "false";
  j += ",\"pausedS\":"; j += paused ? (unsigned long)((millis() - pausedAtMs) / 1000) : 0UL;
  j += ",\"otaMax\":"; j += (unsigned long)otaMax;
  j += ",\"fwSize\":"; j += (unsigned long)fwSize;
  j += ",\"serOled\":"; j += serOledOn ? "true" : "false";
  j += ',';
  jsonKV(j, "battV", battV, 2);
  jsonKV(j, "battPct", battPct, 0);
  jsonKV(j, "battRate", battRate, 1);
  jsonKV(j, "cpuT", cpuTempC, 0);
  // Wi-Fi: phones on the hotspot and their signal strength as heard by the VMR;
  // "me" = the phone making this request (matched through its DHCP lease)
  {
    wifi_sta_list_t sl;
    int me = 0;
    j += "\"wifi\":{\"rssi\":[";
    if (esp_wifi_ap_get_sta_list(&sl) == ESP_OK) {
      esp_netif_pair_mac_ip_t pairs[ESP_WIFI_MAX_CONN_NUM];
      for (int i = 0; i < sl.num; i++) { memcpy(pairs[i].mac, sl.sta[i].mac, 6); pairs[i].ip.addr = 0; }
      esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
      if (ap && sl.num) esp_netif_dhcps_get_clients_by_mac(ap, sl.num, pairs);
      uint32_t rip = (uint32_t)server.client().remoteIP();
      for (int i = 0; i < sl.num; i++) {
        if (i) j += ',';
        j += (int)sl.sta[i].rssi;
        if (pairs[i].ip.addr && pairs[i].ip.addr == rip) me = sl.sta[i].rssi;
      }
    }
    j += "],\"me\":"; if (me) j += me; else j += "null";
    j += "},";
  }
  j += "\"charge\":"; j += chargeMode ? "true" : "false"; j += ',';

  bool fx = gpsFixValid();
  j += "\"fix\":"; j += fx ? "true" : "false";
  j += ",\"sats\":"; j += (int)gps.gpsSats;
  j += ",\"fixQ\":"; j += (int)gps.gpsFixQ;
  j += ",\"gpsHz\":"; j += GPS_RATE_HZ;
  j += ",\"inView\":"; j += (int)(gpsInView[0] + gpsInView[1]);
  j += ",\"snr\":"; j += (int)gpsBestSnr;
  j += ",\"gpsAge\":"; if (gpsLastNmeaMs) j += (unsigned long)((millis() - gpsLastNmeaMs) / 1000); else j += "null";
  j += ",\"gpsDiag\":{\"gp\":"; j += (int)gpsInView[0];
  j += ",\"gl\":"; j += (int)gpsInView[1];
  j += ",\"nmea\":"; j += (unsigned long)gpsNmeaCount;
  j += ",\"ant\":"; j += (int)GPS.antenna;                // 0 not reported, 1 problem, 2 internal, 3 external
  j += ",\"fixAge\":"; if (gps.gpsLastFixMs) j += (unsigned long)((millis() - gps.gpsLastFixMs) / 1000); else j += "null";
  j += ",\"restarts\":"; j += (unsigned long)gpsRestarts;
  j += "},";
  jsonKV(j, "lat", fx ? gps.gpsLat : NAN, 6);
  jsonKV(j, "lon", fx ? gps.gpsLon : NAN, 6);
  jsonKV(j, "alt", fx ? gps.gpsAlt : NAN, 1);
  jsonKV(j, "sog", fx ? gps.gpsSpeed / KNOTS_TO_MS : NAN, 1);
  jsonKV(j, "sogMs", fx ? gps.gpsSpeed : NAN, 2);
  jsonKV(j, "cog", fx ? gps.gpsAngle : NAN, 0);
  jsonKV(j, "sogAvg", sogAvgKn, 1);
  jsonKV(j, "cogAvg", cogAvgDeg, 1);
  jsonKV(j, "hdop", gps.gpsHdop, 2);

  bool f = imuFresh();
  jsonKV(j, "roll",    f ? roll    : NAN, 1);
  jsonKV(j, "pitch",   f ? pitch   : NAN, 1);
  jsonKV(j, "yaw",     f ? yawDeg  : NAN, 1);
  jsonKV(j, "rot",     f ? rotShown() : NAN, 0);         // deg/min, smoothed + deadband
  jsonKV(j, "rollT",   f ? rollPeriod : NAN, 1);           // s
  j += "\"rollTn\":"; j += (int)rpCount; j += ',';
  jsonKV(j, "gm", f ? gmEst : NAN, 2);
  jsonKV(j, "gmC", gmC, 3);
  j += "\"clino\":["; jsonNum(j, clinoMaxP, 1); j += ','; jsonNum(j, clinoMaxS, 1); j += "],";
  {                                                  // G-meter: felt push (fwd, stbd), vertical g, peaks
    bool fa = accFresh();
    j += "\"gmet\":{";
    jsonKV(j, "f", fa ? gFeltF : NAN, 3); jsonKV(j, "s", fa ? gFeltS : NAN, 3); jsonKV(j, "v", fa ? gVert : NAN, 3);
    j += "\"pk\":[";
    jsonNum(j, gPkF, 3); j += ','; jsonNum(j, gPkA, 3); j += ','; jsonNum(j, gPkS, 3); j += ',';
    jsonNum(j, gPkP, 3); j += ','; jsonNum(j, gPkHi, 3); j += ','; jsonNum(j, gPkLo, 3);
    j += "]},";
  }
  {                                                  // at the dock: 60 s means and live values
    float L = trimLengthM();
    j += "\"dock\":{";
    jsonKV(j, "trim", trimMetres(dockRawPitch), 3);      // m, + = by the stern
    jsonKV(j, "trimNow", f ? trimMetres(rawPitch) : NAN, 3);
    jsonKV(j, "ang", isnan(dockRawPitch) ? NAN : trimAngle(dockRawPitch), 3);
    jsonKV(j, "list", dockRoll, 3);
    jsonKV(j, "listNow", f ? roll : NAN, 2);
    jsonKV(j, "L", L, 1);
    j += "\"n\":"; j += (int)dockN;
    j += ",\"cal\":"; j += (int)P.trimCal;
    j += ",\"cf\":"; j += (int)P.calFwdIn; j += ",\"cm\":"; j += (int)P.calMidIn; j += ",\"ca\":"; j += (int)P.calAftIn;
    j += ',';
    jsonTime(j, "ct", P.trimCalUnix, false);
    j += "},";
  }
  if (server.hasArg("sky")) {                       // sky plot view: [prn, elev, az, snr, used]
    j += "\"sky\":[";
    uint32_t now = millis();
    bool first = true;
    for (int i = 0; i < SKY_MAX; i++) {
      const SkySat& v = sky[i];
      if (!v.prn || now - v.ms > SKY_STALE_MS) continue;
      if (!first) j += ',';
      first = false;
      char b[40];
      snprintf(b, sizeof(b), "[%u,%u,%u,%u,%d]", v.prn, v.el, v.az, v.snr, skyIsUsed(v.prn) ? 1 : 0);
      j += b;
    }
    j += "],";
  }
  jsonKV(j, "gmAvg", gmAvg, 2);                      // 10-min dashboard mean
  {                                                // seconds until the window is full (stops while paused)
    int32_t left = 0;
    if (gmWinCount < GM_SLOTS) {
      uint32_t since = min<uint32_t>((millis() - gmWinSampleMs) / 1000, GM_SAMPLE_MS / 1000);
      left = (int32_t)(GM_SLOTS - gmWinCount) * (GM_SAMPLE_MS / 1000) - (int32_t)since;
      if (left < 0) left = 0;
    }
    j += "\"gmLeft\":"; j += (int)left; j += ',';
  }
  j += "\"rs\":[";                                 // roll statistics, 10 and 30 min
  for (int w = 0; w < 2; w++) {
    const RollStats& st = rStat[w];
    if (w) j += ',';
    j += "{\"min\":"; j += (int)RS_WIN_MIN[w]; j += ",\"n\":"; j += (int)st.n; j += ',';
    jsonKV(j, "sig", st.sig, 1); jsonKV(j, "max", st.maxAmp, 1);
    jsonKV(j, "mean", st.mean, 1); jsonKV(j, "rms", st.rms, 1);
    j += "\"ovS\":"; j += (int)st.overS; j += ",\"ovP\":"; j += (int)st.overP; j += '}';
  }
  j += "],";

  Win w = window1m();
  j += "\"m1\":{";
  jsonKV(j, "rMax", w.rMax, 1); jsonKV(j, "rMin", w.rMin, 1);
  jsonKV(j, "pMax", w.pMax, 1); jsonKV(j, "pMin", w.pMin, 1);
  jsonKV(j, "yr", w.yrMax * 60.0f, 0, false);
  j += "},\"all\":{";
  jsonKV(j, "rMax", P.rollMaxPos, 1);  jsonTime(j, "trMax", P.tRollMaxPos);
  jsonKV(j, "rMin", P.rollMaxNeg, 1);  jsonTime(j, "trMin", P.tRollMaxNeg);
  jsonKV(j, "pMax", P.pitchMaxPos, 1); jsonTime(j, "tpMax", P.tPitchMaxPos);
  jsonKV(j, "pMin", P.pitchMaxNeg, 1); jsonTime(j, "tpMin", P.tPitchMaxNeg);
  jsonKV(j, "yr", P.yawRateMax * 60.0f, 0); jsonTime(j, "tyr", P.tYawRateMax, false);
  j += "},";

  j += "\"zero\":"; j += P.zeroApplied ? "true" : "false"; j += ',';
  jsonKV(j, "zr", P.rollOffset, 2);
  jsonKV(j, "zp", P.pitchOffset, 2);

  char ln[64] = "";
  if (sdLock(50)) { strncpy(ln, logName[0] ? logName + 1 : "", sizeof(ln) - 1); sdUnlock(); }
  j += "\"sd\":"; j += sdOK ? "true" : "false";
  j += ",\"log\":{\"on\":"; j += logging ? "true" : "false";
  j += ",\"intr\":"; j += logInterrupted ? "true" : "false";
  j += ",\"file\":\""; j += ln; j += "\"";
  j += ",\"sec\":"; j += logging ? (unsigned long)((millis() - logStartMs) / 1000) : 0UL;
  j += ",\"rows\":"; j += (unsigned long)logRows;
  j += ",\"bytes\":"; j += String((double)logBytes, 0);
  j += ",\"drop\":"; j += (unsigned long)pendDropped;
  j += ",\"cond\":\""; j += LOAD_COND[P.logCond < LOAD_COND_N ? P.logCond : 0];
  j += "\",\"ev\":[";
  for (int i = 0; i < 5 && i < evCount; i++) {
    if (i) j += ',';
    j += "{\"n\":"; j += (unsigned)evList[i].n; j += ',';
    jsonTime(j, "t", evList[i].unix);
    j += "\"note\":\""; j += evList[i].note; j += "\"}";
  }
  j += "]},\"disp\":{\"ok\":"; j += displayOK ? "true" : "false";
  j += ",\"on\":"; j += displayOn ? "true" : "false";
  j += ",\"scr\":"; j += (int)screen;
  j += ",\"pct\":"; j += (int)((contrast * 100 + 127) / 255);
  j += ",\"src\":\""; j += potControl ? "pot" : "web";
  j += "\",\"flip\":"; j += (int)P.oledFlip;
  j += "},\"set\":{\"gmax\":"; j += (int)P.graphMax;
  j += ",\"rotdb\":"; j += (int)P.rotDeadband;
  j += ",\"gmin\":"; j += (int)P.graphMin;
  j += ",\"pmax\":"; j += (int)P.pitchGraphMax;
  j += ",\"pmin\":"; j += (int)P.pitchGraphMin;
  j += ",\"beam\":"; j += String(P.shipBeamCm / 100.0f, 2);
  j += ",\"draft\":"; j += String(P.shipDraftCm / 100.0f, 2);
  j += ",\"len\":"; j += String(P.shipLengthDm / 10.0f, 1);
  j += ",\"lbp\":"; j += String(P.shipLbpDm / 10.0f, 1);
  j += ",\"rthr\":"; j += (int)P.rollThrDeg;
  j += ",\"cond\":"; j += (int)P.logCond;
  j += ",\"gsecs\":"; j += (int)graphSecs();
  j += ",\"sogavg\":"; j += (int)P.sogAvgS;
  j += ",\"zd\":"; j += (int)P.zdHours;
  j += "},\"reset\":\""; j += resetReason;
  j += "\",\"crash\":\"";
  { String c = crashReport; c.replace("\\", "\\\\"); c.replace("\"", "\\\""); c.replace("\n", "\\n"); j += c; }
  j += "\",\"i2cMiss\":\""; j += i2cMissing;
  j += "\",\"i2cNote\":\""; j += i2cBusNote;
  j += "\",\"imuRec\":"; j += (unsigned long)imuRecoveries;
  j += ",\"imuSoft\":"; j += (unsigned long)imuSoftRecoveries;
  j += ",\"imuOk\":"; j += imuFresh() ? "true" : "false";

  // Graph: the chosen length, at most GRAPH_WEB_BUCKETS columns. When k samples
  // share a column, send that column's min and max (keeps the roll peaks).
  uint16_t secs = graphSecs();
  int nS = (int)((uint32_t)secs * 1000UL / GRAPH_SAMPLE_MS);
  int k = (nS + GRAPH_WEB_BUCKETS - 1) / GRAPH_WEB_BUCKETS;
  int avail = (int)gCount < nS ? (int)gCount : nS;
  int nB = (avail + k - 1) / k;
  j += ",\"gSecs\":"; j += (int)secs;
  j += ",\"gK\":"; j += k;
  j += ",\"gB\":"; j += nS / k;
  j += ",\"gDt\":"; j += String(k * GRAPH_SAMPLE_MS / 1000.0f, 2);
  const float* series[2] = {gRoll, gPitch};
  const char* names[2] = {",\"gRoll\":[", "],\"gPitch\":["};
  for (int sr = 0; sr < 2; sr++) {
    j += names[sr];
    if (!series[sr]) continue;
    for (int bk = 0; bk < nB; bk++) {
      int ageHi = (nB - bk) * k - 1, ageLo = (nB - 1 - bk) * k;   // bucket covers these ages
      float lo = INFINITY, hi = -INFINITY;
      for (int age = ageHi; age >= ageLo; age--) {
        if (age >= gCount) continue;
        float v = series[sr][(gHead + GRAPH_POINTS - 1 - age) % GRAPH_POINTS];
        if (isnan(v)) continue;
        lo = fminf(lo, v); hi = fmaxf(hi, v);
      }
      if (bk) j += ',';
      if (lo > hi) { j += (k == 1) ? "null" : "null,null"; continue; }
      jsonNum(j, lo, 2);
      if (k > 1) { j += ','; jsonNum(j, hi, 2); }
    }
  }
  j += "]}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

// ---- ZIPCORE BEGIN (store-only streamed ZIP; also compiled by host tests) ----
static uint32_t zipCrcTable[256];
static bool zipCrcReady = false;

void zipCrcInit() {
  if (zipCrcReady) return;
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
    zipCrcTable[i] = c;
  }
  zipCrcReady = true;
}

// Running CRC-32: start at 0xFFFFFFFF, invert at the end
uint32_t zipCrcUpdate(uint32_t c, const uint8_t* p, size_t n) {
  while (n--) c = zipCrcTable[(c ^ *p++) & 0xFF] ^ (c >> 8);
  return c;
}

void zipPut16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void zipPut32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

void zipDosTime(uint32_t unixT, uint16_t* t, uint16_t* d) {
  if (unixT < 315532800UL) { *t = 0; *d = (1 << 5) | 1; return; }   // before 1980: 1980-01-01
  time_t tt = (time_t)unixT;
  struct tm tmv;
  gmtime_r(&tt, &tmv);
  *t = (uint16_t)((tmv.tm_hour << 11) | (tmv.tm_min << 5) | (tmv.tm_sec / 2));
  *d = (uint16_t)(((tmv.tm_year - 80) << 9) | ((tmv.tm_mon + 1) << 5) | tmv.tm_mday);
}

// Non-empty entries use flag bit 3 (CRC follows the data in a data descriptor).
// Empty entries have a known CRC (0), so they get a plain header and no descriptor.
uint16_t zipFlags(const ZipEntry* e) { return e->size ? 0x0008 : 0x0000; }

size_t zipLocalHeader(uint8_t* o, const ZipEntry* e) {
  size_t n = strlen(e->name);
  zipPut32(o, 0x04034b50UL); zipPut16(o + 4, 20); zipPut16(o + 6, zipFlags(e)); zipPut16(o + 8, 0);
  zipPut16(o + 10, e->dosTime); zipPut16(o + 12, e->dosDate);
  // CRC is not known yet (it follows in the data descriptor), but the sizes
  // are, so write them: lets streaming unzippers handle uncompressed entries.
  zipPut32(o + 14, 0); zipPut32(o + 18, e->size); zipPut32(o + 22, e->size);
  zipPut16(o + 26, (uint16_t)n); zipPut16(o + 28, 0);
  memcpy(o + 30, e->name, n);
  return 30 + n;
}

size_t zipDescriptor(uint8_t* o, const ZipEntry* e) {
  if (!e->size) return 0;
  zipPut32(o, 0x08074b50UL); zipPut32(o + 4, e->crc); zipPut32(o + 8, e->size); zipPut32(o + 12, e->size);
  return 16;
}

size_t zipCentral(uint8_t* o, const ZipEntry* e) {
  size_t n = strlen(e->name);
  zipPut32(o, 0x02014b50UL); zipPut16(o + 4, 20); zipPut16(o + 6, 20); zipPut16(o + 8, zipFlags(e));
  zipPut16(o + 10, 0); zipPut16(o + 12, e->dosTime); zipPut16(o + 14, e->dosDate);
  zipPut32(o + 16, e->crc); zipPut32(o + 20, e->size); zipPut32(o + 24, e->size);
  zipPut16(o + 28, (uint16_t)n); zipPut16(o + 30, 0); zipPut16(o + 32, 0);
  zipPut16(o + 34, 0); zipPut16(o + 36, 0); zipPut32(o + 38, 0); zipPut32(o + 42, e->offset);
  memcpy(o + 46, e->name, n);
  return 46 + n;
}

size_t zipEnd(uint8_t* o, uint16_t count, uint32_t cdSize, uint32_t cdOffset) {
  zipPut32(o, 0x06054b50UL); zipPut16(o + 4, 0); zipPut16(o + 6, 0);
  zipPut16(o + 8, count); zipPut16(o + 10, count);
  zipPut32(o + 12, cdSize); zipPut32(o + 16, cdOffset); zipPut16(o + 20, 0);
  return 22;
}

uint64_t zipTotalSize(const ZipEntry* e, size_t count) {
  uint64_t t = 22;
  for (size_t i = 0; i < count; i++) {
    uint64_t n = strlen(e[i].name);
    t += (30 + n) + e[i].size + (e[i].size ? 16 : 0) + (46 + n);
  }
  return t;
}

// Streams a complete ZIP. Reads each entry's bytes via rd() (exactly e[i].size
// bytes), writes everything via wr(). Fills crc/offset as it goes.
bool zipStream(ZipEntry* e, size_t count, ZipReadFn rd, ZipSinkFn wr, void* ctx,
               uint8_t* buf, size_t bufLen) {
  zipCrcInit();
  uint8_t hdr[46 + ZIP_NAME_MAX];
  uint32_t off = 0;
  for (size_t i = 0; i < count; i++) {
    e[i].offset = off;
    size_t h = zipLocalHeader(hdr, &e[i]);
    if (!wr(ctx, hdr, h)) return false;
    off += h;
    uint32_t left = e[i].size, c = 0xFFFFFFFFUL;
    while (left) {
      size_t want = left < bufLen ? left : bufLen;
      int got = rd(ctx, i, buf, want);
      if (got <= 0) return false;
      c = zipCrcUpdate(c, buf, (size_t)got);
      if (!wr(ctx, buf, (size_t)got)) return false;
      left -= (uint32_t)got;
    }
    e[i].crc = ~c;
    off += e[i].size;
    h = zipDescriptor(hdr, &e[i]);
    if (h && !wr(ctx, hdr, h)) return false;
    off += h;
  }
  uint32_t cdStart = off;
  for (size_t i = 0; i < count; i++) {
    size_t h = zipCentral(hdr, &e[i]);
    if (!wr(ctx, hdr, h)) return false;
    off += h;
  }
  size_t h = zipEnd(hdr, (uint16_t)count, off - cdStart, cdStart);
  return wr(ctx, hdr, h);
}
// ---- ZIPCORE END ----

// ---------------------------- microSD file manager ---------------------------
bool validName(const String& n) {
  if (n.length() == 0 || n.length() >= ZIP_NAME_MAX) return false;
  if (n[0] == '.') return false;
  for (size_t i = 0; i < n.length(); i++) {
    char c = n[i];
    if (c == '/' || c == '\\' || c == '"' || (uint8_t)c < 0x20) return false;
  }
  return true;
}

// Root-level files only (skips folders, dot-files and odd names). Call with sdMutex held.
void listRootFiles(std::vector<String>& names, std::vector<uint32_t>* sizes) {
  File root = SD.open("/");
  if (!root) return;
  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String nm = f.name();
      if (validName(nm)) {
        names.push_back(nm);
        if (sizes) sizes->push_back((uint32_t)f.size());
      }
    }
    f.close();
    f = root.openNextFile();
  }
  root.close();
}

bool clientWriteAll(NetworkClient& c, const uint8_t* p, size_t n) {
  while (n) {
    size_t w = c.write(p, n);
    if (w == 0) return false;
    p += w; n -= w;
  }
  return true;
}

bool loggingActive() { return logging || logWanted; }

bool rejectIfLogging() {
  if (!loggingActive()) return false;
  server.send(409, "text/plain", "Stop logging first");
  return true;
}

void handleFiles() {
  if (!sdLock(3000)) { server.send(503, "application/json", "{\"sd\":false,\"busy\":true,\"files\":[]}"); return; }
  if (!sdOK && !loggingActive()) sdOK = sdMount();      // card may have been swapped while stopped
  if (!sdOK) { sdUnlock(); server.send(200, "application/json", "{\"sd\":false,\"files\":[]}"); return; }
  std::vector<String> names;
  std::vector<uint32_t> sizes;
  listRootFiles(names, &sizes);
  unsigned long long total = SD.totalBytes(), used = SD.usedBytes();
  String active = logName[0] ? String(logName + 1) : String("");
  sdUnlock();

  String j;
  j.reserve(64 + names.size() * 48);
  char b[96];
  snprintf(b, sizeof(b), "{\"sd\":true,\"total\":%llu,\"used\":%llu,\"files\":[", total, used);
  j += b;
  for (size_t i = 0; i < names.size(); i++) {
    if (i) j += ',';
    j += "{\"n\":\""; j += names[i]; j += "\",\"s\":"; j += sizes[i];
    j += ",\"a\":"; j += (names[i] == active) ? "true" : "false"; j += '}';
  }
  j += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

void handleDownload() {
  if (rejectIfLogging()) return;
  if (!webBuf) { server.send(500, "text/plain", "Out of memory"); return; }
  String n = server.arg("f");
  if (!validName(n)) { server.send(400, "text/plain", "Bad file name"); return; }
  if (!sdLock(3000)) { server.send(503, "text/plain", "SD card busy, try again"); return; }
  File f = sdOK ? SD.open(("/" + n).c_str(), FILE_READ) : File();
  bool ok = f && !f.isDirectory();
  uint32_t size = ok ? (uint32_t)f.size() : 0;
  if (!ok && f) f.close();
  sdUnlock();
  if (!ok) { server.send(404, "text/plain", "File not found"); return; }

  bool csv = n.endsWith(".csv") || n.endsWith(".CSV");
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + n + "\"");
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(size);
  server.send(200, csv ? "text/csv" : "application/octet-stream", "");

  NetworkClient& client = server.client();
  uint32_t left = size;                  // send exactly the size announced in Content-Length
  while (left) {
    size_t want = left < WEB_BUF_BYTES ? left : WEB_BUF_BYTES;
    if (!sdLock(3000)) break;
    int got = f.read(webBuf, want);
    sdUnlock();
    if (got <= 0) break;
    if (!clientWriteAll(client, webBuf, (size_t)got)) break;
    left -= (uint32_t)got;
  }
  if (sdLock(3000)) { f.close(); sdUnlock(); } else f.close();
  if (left) { client.stop(); Log.printf("Web: download of %s aborted\n", n.c_str()); }
}

int webZipRead(void* vctx, size_t idx, uint8_t* buf, size_t want) {
  ZipCtx* z = (ZipCtx*)vctx;
  if (!sdLock(3000)) return -1;
  if (z->cur != (int)idx) {
    if (z->f) z->f.close();
    char path[ZIP_NAME_MAX + 2];
    snprintf(path, sizeof(path), "/%s", z->e[idx].name);
    z->f = sdOK ? SD.open(path, FILE_READ) : File();
    z->cur = (int)idx;
  }
  int got = z->f ? z->f.read(buf, want) : -1;
  sdUnlock();
  return got;
}

bool webZipSink(void* vctx, const uint8_t* buf, size_t n) {
  return clientWriteAll(*((ZipCtx*)vctx)->client, buf, n);
}

void handleZip() {
  if (rejectIfLogging()) return;
  if (!webBuf) { server.send(500, "text/plain", "Out of memory"); return; }
  std::vector<String> names;
  bool all = server.arg("all") == "1";
  if (!all) {
    for (int i = 0; i < server.args(); i++) if (server.argName(i) == "f") names.push_back(server.arg(i));
  }
  if (!sdLock(3000)) { server.send(503, "text/plain", "SD card busy, try again"); return; }
  if (!sdOK) { sdUnlock(); server.send(503, "text/plain", "No microSD card"); return; }
  if (all) listRootFiles(names, nullptr);
  size_t count = names.size();
  if (count == 0 || count > ZIP_MAX_FILES) {
    sdUnlock();
    server.send(400, "text/plain", count ? "Too many files for one zip; select fewer" : "No files selected");
    return;
  }
  ZipEntry* e = new (std::nothrow) ZipEntry[count];
  if (!e) { sdUnlock(); server.send(500, "text/plain", "Out of memory"); return; }
  String bad;
  for (size_t i = 0; i < count && !bad.length(); i++) {
    memset(&e[i], 0, sizeof(ZipEntry));
    if (!validName(names[i])) { bad = "bad name"; break; }
    File f = SD.open(("/" + names[i]).c_str(), FILE_READ);
    if (!f || f.isDirectory()) { bad = names[i]; if (f) f.close(); break; }
    strncpy(e[i].name, names[i].c_str(), ZIP_NAME_MAX - 1);
    e[i].size = (uint32_t)f.size();
    zipDosTime((uint32_t)f.getLastWrite(), &e[i].dosTime, &e[i].dosDate);
    f.close();
  }
  sdUnlock();
  if (bad.length()) { delete[] e; server.send(404, "text/plain", "Not found: " + bad); return; }

  uint64_t total = zipTotalSize(e, count);
  if (total > 0xFFFFFFF0ULL) {
    delete[] e;
    server.send(413, "text/plain", "Selection is over 4 GB; select fewer files or pull the card");
    return;
  }

  char zname[40];
  if (clockSynced) {
    DateTime t(nowUnix());
    snprintf(zname, sizeof(zname), "VMR_%04u%02u%02u_%02u%02u.zip", (unsigned)t.year(),
             (unsigned)t.month(), (unsigned)t.day(), (unsigned)t.hour(), (unsigned)t.minute());
  } else {
    snprintf(zname, sizeof(zname), "VMR_logs.zip");
  }
  server.sendHeader("Content-Disposition", String("attachment; filename=\"") + zname + "\"");
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength((size_t)total);
  server.send(200, "application/zip", "");

  ZipCtx z;
  z.e = e;
  z.cur = -1;
  z.client = &server.client();
  bool ok = zipStream(e, count, webZipRead, webZipSink, &z, webBuf, WEB_BUF_BYTES);
  if (z.f) { if (sdLock(3000)) { z.f.close(); sdUnlock(); } else z.f.close(); }
  delete[] e;
  if (!ok) { server.client().stop(); Log.println("Web: zip download aborted"); }
  else Log.printf("Web: sent %s (%u files, %llu bytes)\n", zname, (unsigned)count, (unsigned long long)total);
}

void handleDelete() {
  if (loggingActive()) {
    server.send(409, "application/json", "{\"deleted\":0,\"failed\":[\"stop logging first\"]}");
    return;
  }
  if (plotState == 1) {
    server.send(409, "application/json", "{\"deleted\":0,\"failed\":[\"wait for the plot to finish\"]}");
    return;
  }
  std::vector<String> names;
  bool all = server.arg("all") == "1";
  if (all) {
    if (!sdLock(3000)) { server.send(503, "application/json", "{\"deleted\":0,\"failed\":[\"SD busy\"]}"); return; }
    if (sdOK) listRootFiles(names, nullptr);
    sdUnlock();
  } else {
    for (int i = 0; i < server.args(); i++) if (server.argName(i) == "f") names.push_back(server.arg(i));
  }

  int deleted = 0, nFail = 0;
  String failed;
  for (size_t i = 0; i < names.size(); i++) {
    const String& n = names[i];
    bool valid = validName(n), ok = false;
    if (valid && sdLock(5000)) {
      if (sdOK) ok = SD.remove(("/" + n).c_str());
      sdUnlock();
    }
    if (ok) deleted++;
    else {
      if (nFail++) failed += ',';
      failed += '"'; failed += valid ? n : String("(invalid name)"); failed += '"';
    }
    vTaskDelay(1);
  }
  Log.printf("Web: deleted %d file(s), %d failed\n", deleted, nFail);
  String j = "{\"deleted\":"; j += deleted; j += ",\"failed\":["; j += failed; j += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

void sendLogState() {
  char ln[64] = "";
  if (sdLock(50)) { strncpy(ln, logName[0] ? logName + 1 : "", sizeof(ln) - 1); sdUnlock(); }
  String j = "{\"on\":"; j += logging ? "true" : "false";
  j += ",\"file\":\""; j += ln; j += "\",\"sd\":"; j += sdOK ? "true" : "false"; j += "}";
  server.send(200, "application/json", j);
}

// POST /log  state=start[&tag=suffix][&cond=0-5] | state=stop
void handleLog() {
  String st = server.arg("state");
  if (st == "start") {
    if (chargeMode) { server.send(409, "application/json", "{\"error\":\"Leave charge mode first\"}"); return; }
    if (!loggingActive()) {
      if (sdLock(3000)) { if (!sdOK) sdOK = sdMount(); sdUnlock(); }
      if (!sdOK) { server.send(409, "application/json", "{\"error\":\"No microSD card\"}"); return; }
      // File-name suffix: letters, digits, - and _ only (spaces become -)
      String tg = server.arg("tag");
      size_t k = 0;
      for (size_t i = 0; i < tg.length() && k < LOG_TAG_MAX; i++) {
        char c = tg[i];
        if (c == ' ') c = '-';
        if (isalnum((unsigned char)c) || c == '-' || c == '_') pendTag[k++] = c;
      }
      pendTag[k] = 0;
      int cd = server.hasArg("cond") ? server.arg("cond").toInt() : -1;
      pendCond = (cd >= 0 && cd < LOAD_COND_N) ? (uint8_t)cd : 255;
      logWanted = true;
    }
  } else if (st == "stop") {
    logWanted = false;
  } else {
    server.send(400, "application/json", "{\"error\":\"state must be start or stop\"}");
    return;
  }
  uint32_t t0 = millis();                         // wait for loop() to apply it
  while (logging != logWanted && millis() - t0 < 4000) vTaskDelay(10);
  sendLogState();
}

// POST /zero  action=set|clear  |  action=nudge&axis=roll|pitch&d=1|-1 (shift reading 0.1 deg)
void handleZero() {
  String a = server.arg("action");
  uint32_t t0 = millis();
  if (a == "nudge") {
    int d = constrain((int)server.arg("d").toInt(), -10, 10);
    if (server.arg("axis") == "roll") reqNudgeRoll = d; else reqNudgePitch = d;
    while ((reqNudgeRoll || reqNudgePitch) && millis() - t0 < 2000) vTaskDelay(10);
  } else if (a == "set" || a == "clear") {
    if (a == "set" && !imuFresh()) { server.send(409, "application/json", "{\"error\":\"No IMU data\"}"); return; }
    reqZero = (a == "set") ? 1 : 0;
    while (reqZero >= 0 && millis() - t0 < 2000) vTaskDelay(10);
  } else {
    server.send(400, "application/json", "{\"error\":\"action must be set, clear or nudge\"}");
    return;
  }
  String j = "{\"zero\":"; j += P.zeroApplied ? "true" : "false";
  j += ",\"zr\":"; j += String(P.rollOffset, 2);
  j += ",\"zp\":"; j += String(P.pitchOffset, 2); j += "}";
  server.send(200, "application/json", j);
}

// POST /set  any of: screen=0..9  pct=1..100 (brightness)  on=0/1 (OLED)  flip=0/1
//   gmax=1..70 gmin=-70..-1 (roll graph)  pmax / pmin (pitch graph)  gsecs=30|60|120|300|600
//   sogavg=1..60  rotdb=0..60  zd=-12..12  rthr=1..45  beam / draft / len / lbp (m, 0 clears)
//   charge=0/1  resetmax=1  gmreset=1  clinoreset=1  greset=1  serol=0/1 (serial monitor on the OLED)
// Values are checked here and applied by loop() (applyWebRequests).
void handleSet() {
  if (server.hasArg("screen")) {
    int v = server.arg("screen").toInt();
    if (v >= 0 && v < NUM_SCREENS) reqScreen = v;
  }
  if (server.hasArg("pct")) {
    int v = constrain((int)server.arg("pct").toInt(), 1, 100);
    reqContrast = (int16_t)constrain((v * 255 + 50) / 100, 1, 255);
  }
  if (server.hasArg("on")) reqDisplayOn = server.arg("on") == "1" ? 1 : 0;
  if (server.hasArg("gmax")) {
    int v = server.arg("gmax").toInt();
    if (v >= 1 && v <= GRAPH_LIMIT_DEG) reqGraphMax = v;
  }
  if (server.hasArg("gmin")) {
    int v = server.arg("gmin").toInt();
    if (v <= -1 && v >= -GRAPH_LIMIT_DEG) reqGraphMin = v;
  }
  if (server.hasArg("pmax")) {
    int v = server.arg("pmax").toInt();
    if (v >= 1 && v <= GRAPH_LIMIT_DEG) reqPitchMax = v;
  }
  if (server.hasArg("pmin")) {
    int v = server.arg("pmin").toInt();
    if (v <= -1 && v >= -GRAPH_LIMIT_DEG) reqPitchMin = v;
  }
  if (server.arg("resetmax") == "1") reqResetMax = 1;
  if (server.arg("gmreset") == "1") reqGmReset = 1;
  if (server.arg("clinoreset") == "1") reqClinoReset = 1;
  if (server.arg("greset") == "1") reqGReset = 1;
  if (server.hasArg("serol")) serOledOn = server.arg("serol") == "1";   // serial monitor on the OLED
  if (server.hasArg("charge")) reqCharge = server.arg("charge") == "1" ? 1 : 0;
  if (server.hasArg("flip")) reqFlip = server.arg("flip") == "1" ? 1 : 0;
  // ship particulars in metres (0 clears); roll statistics threshold in degrees
  auto meters = [](const String& a, float lo, float hi, float scale) -> int32_t {
    float v = a.toFloat();
    if (v == 0) return 0;
    return (v >= lo && v <= hi) ? (int32_t)lroundf(v * scale) : -1;
  };
  if (server.hasArg("beam"))  reqBeamCm   = meters(server.arg("beam"), 1, 100, 100);
  if (server.hasArg("draft")) reqDraftCm  = meters(server.arg("draft"), 0.3f, 40, 100);
  if (server.hasArg("len"))   reqLengthDm = meters(server.arg("len"), 5, 500, 10);
  if (server.hasArg("lbp"))   reqLbpDm    = meters(server.arg("lbp"), 5, 500, 10);
  if (server.hasArg("rthr")) {
    int v = server.arg("rthr").toInt();
    if (v >= 1 && v <= 45) reqRollThr = v;
  }
  if (server.hasArg("rotdb")) {
    int v = server.arg("rotdb").toInt();
    if (v >= 0 && v <= 60) reqRotDb = v;
  }
  if (server.hasArg("gsecs")) {
    int v = server.arg("gsecs").toInt();
    for (int i = 0; i < GRAPH_LENGTHS; i++) if (GRAPH_SECS[i] == v) reqGraphIdx = i;
  }
  if (server.hasArg("sogavg")) {
    int v = server.arg("sogavg").toInt();
    if (v >= 1 && v <= 60) reqSogAvg = v;
  }
  if (server.hasArg("zd")) {
    int v = server.arg("zd").toInt();
    if (v >= -12 && v <= 12) { reqZd = v; reqZdSet = 1; }
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /logger  -> datalogger mode (Wi-Fi goes off ~0.6 s after this reply)
void handleLogger() {
  if (!sdOK) { server.send(409, "application/json", "{\"error\":\"No microSD card\"}"); return; }
  if (!logging) { server.send(409, "application/json", "{\"error\":\"Not recording: press START LOGGING first\"}"); return; }
  reqLoggerAtMs = millis();
  reqLogger = 1;
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /restart
void handleRestart() {
  server.send(200, "application/json", "{\"ok\":true}");
  reqRestart = 1;
}

// POST /trimcal  fwd=, aft= (and mid=, optional) drafts in inches -> calibrate
//                clear=1 -> forget the calibration (trim then follows level zero)
void handleTrimCal() {
  uint32_t t0 = millis();
  if (server.arg("clear") == "1") {
    reqTrimCal = -1;
  } else {
    long f = server.arg("fwd").toInt(), m = server.arg("mid").toInt(), a = server.arg("aft").toInt();
    const char* err = nullptr;
    if (f < 12 || a < 12 || f > 1200 || a > 1200 || m < 0 || m > 1200) err = "Enter the forward and aft drafts";
    else if (isnan(trimLengthM())) err = "Enter the LOA (or LBP) in card 8 first";
    else if (!imuFresh() || dockN < DOCK_CAL_MIN) err = "Need 30 s of steady IMU data first";
    if (err) {
      String j = "{\"error\":\""; j += err; j += "\"}";
      server.send(409, "application/json", j);
      return;
    }
    reqCalF = (uint16_t)f; reqCalM = (uint16_t)m; reqCalA = (uint16_t)a;
    reqTrimCal = 1;
  }
  while (reqTrimCal && millis() - t0 < 2000) vTaskDelay(10);   // applied by the loop
  server.send(200, "application/json", P.trimCal ? "{\"cal\":true}" : "{\"cal\":false}");
}

// GET /serial?since=N  Serial Monitor lines after line N (developer card)
void handleSerial() {
  uint32_t since = server.hasArg("since") ? (uint32_t)server.arg("since").toInt() : 0, seq = Log.seq();
  uint32_t from = seq > SLOG_LINES ? seq - SLOG_LINES : 0;
  if (since > from && since <= seq) from = since;
  String j;
  j.reserve(160 + (seq - from) * 100);
  j = "{\"seq\":"; j += (unsigned long)seq; j += ",\"lines\":[";
  char ln[SLOG_LEN + 1];
  bool first = true;
  for (uint32_t i = from; i < seq; i++) {
    if (!Log.get(i, ln)) continue;
    if (!first) j += ',';
    first = false;
    j += '"';
    for (const char* c = ln; *c; c++) { if (*c == '"' || *c == '\\') j += '\\'; j += *c; }
    j += '"';
  }
  j += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

// POST /pause  on=1|0
void handlePause() {
  reqPause = server.arg("on") == "1" ? 1 : 0;
  uint32_t t0 = millis();
  while (reqPause >= 0 && millis() - t0 < 1000) vTaskDelay(5);   // applied by the loop
  server.send(200, "application/json", paused ? "{\"paused\":true}" : "{\"paused\":false}");
}

// POST /ota?size=<bytes>  multipart file = the compiled app image (.bin).
// handleOtaUpload runs for each chunk; handleOtaDone answers at the end.
void otaFail(const char* why) {
  if (Update.isRunning()) Update.abort();
  strncpy(otaErr, why, sizeof(otaErr) - 1);
  otaErr[sizeof(otaErr) - 1] = 0;
  otaEndMs = millis();
  otaState = OTA_FAIL;
  Log.printf("Firmware update failed: %s\n", why);
}

void handleOtaUpload() {
  HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    if (otaState == OTA_OK) return;                 // already installed, restarting
    otaErr[0] = 0;
    otaDone = 0;
    otaTotal = server.hasArg("size") ? (uint32_t)server.arg("size").toInt() : 0;
    if (!otaMax) otaFail("No OTA partition");
    else if (chargeMode) otaFail("Exit charge mode first");
    else if (otaTotal > otaMax) otaFail("File too large");
    else if (otaTotal < 300000) otaFail("Too small: not the VMR app");   // e.g. the .bootloader.bin
    else {
      otaState = OTA_RX;
      logWanted = false;                            // stop recording; the new firmware starts fresh
      Log.printf("Firmware update: receiving %s (%lu bytes), logging stopped\n", u.filename.c_str(), (unsigned long)otaTotal);
    }
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (otaState != OTA_RX) return;
    if (otaDone == 0) {                             // first chunk: must be an ESP32-S2 app image
      if (u.currentSize < 16 || u.buf[0] != 0xE9 ||
          (uint16_t)(u.buf[12] | (u.buf[13] << 8)) != CONFIG_IDF_FIRMWARE_CHIP_ID) {
        otaFail("Not an ESP32-S2 app .bin"); return;
      }
      if (!Update.begin(otaTotal)) { otaFail(Update.errorString()); return; }
    }
    if (Update.write(u.buf, u.currentSize) != u.currentSize) { otaFail(Update.errorString()); return; }
    otaDone += u.currentSize;
  } else if (u.status == UPLOAD_FILE_END) {
    if (otaState != OTA_RX) return;
    if (otaDone != otaTotal) otaFail("Upload incomplete");
    else if (!Update.end()) otaFail(Update.errorString());       // checks the image
    else { otaEndMs = millis(); otaState = OTA_OK; Log.printf("Firmware update: %lu bytes OK\n", (unsigned long)otaDone); }
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (otaState == OTA_RX) otaFail("Upload interrupted");
  }
}

void handleOtaDone() {
  if (otaState == OTA_OK) {
    server.send(200, "application/json", "{\"ok\":true}");
    otaEndMs = millis();
    reqOtaReboot = 1;
    return;
  }
  if (otaState == OTA_RX) otaFail("Upload incomplete");
  String j = "{\"error\":\"";
  j += otaErr[0] ? otaErr : "Update failed";
  j += "\"}";
  server.send(400, "application/json", j);
}

// POST /event  note=<text>  mark the next logged row (only while recording)
void handleEvent() {
  if (!logging) { server.send(409, "application/json", "{\"error\":\"Not recording\"}"); return; }
  if (paused) { server.send(409, "application/json", "{\"error\":\"Paused: resume first\"}"); return; }
  uint32_t t0 = millis();
  while (reqEvent && millis() - t0 < 1000) vTaskDelay(5);    // previous mark not written yet
  String in = server.arg("note");
  size_t k = 0;
  for (size_t i = 0; i < in.length() && k < EVENT_NOTE_MAX; i++) {
    char c = in[i];
    if (c < 0x20 || c > 0x7E) continue;                      // plain ASCII only
    if (c == ',' || c == '"' || c == '#' || c == '\\') c = ' ';   // keep the CSV and JSON simple
    reqEventNote[k++] = c;
  }
  reqEventNote[k] = 0;
  reqEvent = true;
  t0 = millis();
  while (reqEvent && millis() - t0 < 1000) vTaskDelay(5);    // written within one log row (0.1 s)
  String j = "{\"n\":"; j += (unsigned)evCount; j += "}";
  server.send(200, "application/json", j);
}

// ============================================================================
//                     PLOT FROM FILE (dashboard card 11)
// ============================================================================
// On request, a low-priority task reads one CSV (all of it, or its last N
// hours by the millis column) and boils it down to PLOT_BINS columns: min /
// max / mean of roll and pitch, means of battery, SOG, roll period and GM,
// plus any event markers. The phone draws the image. The task runs at idle
// priority and takes the SD card in short turns, so sampling and logging
// carry on while it works. Only one plot at a time (plotState).
// Old files work too: missing columns just come back empty.
#define PLOT_BINS    600
#define PLOT_CHUNK   8192
#define PLOT_EVENTS  40

struct PlotBin { float rMin, rMax, rSum, pMin, pMax, pSum; uint32_t n;
                 float xSum[4]; uint32_t xN[4]; };     // means of batt_v, gps_speed_kn, roll_period_s, gm_m
static const uint8_t PLOT_XCOL[4] = {17, 10, 20, 21}; // their CSV column numbers (0 = utc_time)

// millis (2nd column) of the first data line at or after byte 'off'; -1 if none
static int64_t plotMillisAt(File& f, uint32_t off, uint32_t endOff, uint32_t* lineOff) {
  char b[640];
  for (int tries = 0; tries < 4 && off < endOff; tries++) {
    if (!sdLock(2000)) return -1;
    f.seek(off);
    int n = f.read((uint8_t*)b, sizeof(b) - 1);
    sdUnlock();
    if (n <= 0) return -1;
    b[n] = 0;
    char* p = b;
    if (off != 0) { char* nl = strchr(p, '\n'); if (!nl) return -1; p = nl + 1; }   // skip the partial line
    while (*p) {
      char* nl = strchr(p, '\n');
      if (!nl) break;
      if (*p != '#' && *p != 'u') {
        char* c = strchr(p, ',');
        if (c && c < nl) {
          char* e;
          unsigned long ms = strtoul(c + 1, &e, 10);
          if (e != c + 1 && *e == ',') { if (lineOff) *lineOff = off + (uint32_t)(p - b); return (int64_t)ms; }
        }
      }
      p = nl + 1;
    }
    off += (uint32_t)(p - b);
    if (p == b) break;
  }
  return -1;
}

static void plotFail(const char* why) {
  strncpy(plotErr, why, sizeof(plotErr) - 1);
  plotErr[sizeof(plotErr) - 1] = 0;
  plotState = 3;
}

void plotTask(void*) {
  PlotBin* bins = (PlotBin*)malloc(PLOT_BINS * sizeof(PlotBin));
  char* buf = (char*)malloc(PLOT_CHUNK + 512);
  File f;
  do {
    if (!bins || !buf) { plotFail("not enough memory"); break; }
    char path[100];
    snprintf(path, sizeof(path), "/%s", plotFile);
    if (!sdLock(3000)) { plotFail("microSD busy"); break; }
    f = SD.open(path, FILE_READ);
    uint32_t size = f ? (uint32_t)f.size() : 0;
    sdUnlock();
    if (!f) { plotFail("cannot open the file"); break; }

    // Header: metadata lines (# key,value) and the column-name line
    char meta[4][48] = {"", "", "", ""};        // condition, name, zone_description, start_utc
    static const char* const KEYS[4] = {"# condition,", "# name,", "# zone_description,", "# start_utc,"};
    if (!sdLock(2000)) { plotFail("microSD busy"); break; }
    f.seek(0);
    int hn = f.read((uint8_t*)buf, 4096);
    sdUnlock();
    if (hn <= 0) { plotFail("empty file"); break; }
    buf[hn] = 0;
    for (char* p = buf; *p == '#' || *p == 'u';) {
      char* nl = strchr(p, '\n');
      if (!nl) break;
      *nl = 0;
      for (int k = 0; k < 4; k++) {
        size_t kl = strlen(KEYS[k]);
        if (!strncmp(p, KEYS[k], kl)) {
          strncpy(meta[k], p + kl, sizeof(meta[k]) - 1);
          meta[k][sizeof(meta[k]) - 1] = 0;
          char* cr = strchr(meta[k], '\r'); if (cr) *cr = 0;
        }
      }
      p = nl + 1;
    }

    // Time span: first and last data lines; then the start of the last N hours
    uint32_t first = 0, start = 0, tail = size > 640 ? size - 640 : 0, lastLine = 0;
    int64_t firstMs = plotMillisAt(f, 0, size, &first);
    int64_t lastMs = -1;
    for (uint32_t o = tail; o < size; ) {              // walk to the last complete line
      uint32_t lo = 0;
      int64_t m = plotMillisAt(f, o, size, &lo);
      if (m < 0) break;
      if (lastMs >= 0 && lo <= lastLine) break;
      lastMs = m; lastLine = lo; o = lo + 1;
    }
    if (firstMs < 0 || lastMs < 0) { plotFail("no data rows in the file"); break; }
    uint32_t spanMs = (uint32_t)lastMs - (uint32_t)firstMs;  // unsigned: survives one millis wrap
    start = first;
    if (plotHours && spanMs > plotHours * 3600000UL) {
      uint32_t want = spanMs - plotHours * 3600000UL;     // offset (ms from the first row) to start at
      uint32_t lo = first, hi = lastLine;
      while (hi - lo > 1024) {                            // rows are written in time order
        uint32_t mid = lo + (hi - lo) / 2, at = 0;
        int64_t m = plotMillisAt(f, mid, size, &at);
        if (m < 0) break;
        if ((uint32_t)m - (uint32_t)firstMs < want) lo = mid; else hi = mid;
      }
      start = lo;
    }

    // Pass over the rows
    for (int i = 0; i < PLOT_BINS; i++) bins[i] = {INFINITY, -INFINITY, 0, INFINITY, -INFINITY, 0, 0, {0, 0, 0, 0}, {0, 0, 0, 0}};
    uint32_t t0ms = 0, rows = 0, pos = start, endOff = size;
    double   totSum[6] = {0, 0, 0, 0, 0, 0};          // whole-span means: roll, pitch, batt, sog, rp, gm
    uint32_t totN[6]   = {0, 0, 0, 0, 0, 0};
    bool haveT0 = false;
    char t0utc[32] = "";
    struct { uint32_t ms; uint16_t n; char note[EVENT_NOTE_MAX + 1]; } ev[PLOT_EVENTS];
    int nev = 0;
    uint32_t spanSel = plotHours && spanMs > plotHours * 3600000UL ? plotHours * 3600000UL : spanMs;
    if (spanSel == 0) spanSel = 1;
    size_t carry = 0;
    bool skipFirst = (start != first);                    // binary search lands mid-line
    while (pos < endOff) {
      size_t want = min((uint32_t)PLOT_CHUNK, endOff - pos);
      if (!sdLock(2000)) { vTaskDelay(10); continue; }
      f.seek(pos);
      int n = f.read((uint8_t*)buf + carry, want);
      sdUnlock();
      if (n <= 0) break;
      pos += n;
      size_t len = carry + n;
      buf[len] = 0;
      char* p = buf;
      if (skipFirst) { char* nl = strchr(p, '\n'); if (!nl) { carry = 0; continue; } p = nl + 1; skipFirst = false; }
      while (true) {
        char* nl = (char*)memchr(p, '\n', len - (p - buf));
        if (!nl) break;
        *nl = 0;
        if (*p != '#' && *p != 'u') {
          char* c1 = strchr(p, ',');
          if (c1) {
            char* e;
            uint32_t ms = strtoul(c1 + 1, &e, 10);
            if (*e == ',') {
              if (!haveT0) {                              // first row of the plot: its UTC time
                haveT0 = true;
                size_t tl = (size_t)(c1 - p);
                if (tl < sizeof(t0utc)) { memcpy(t0utc, p, tl); t0utc[tl] = 0; }
                t0ms = ms;
                spanSel = (uint32_t)lastMs - t0ms;
                if (spanSel == 0) spanSel = 1;
              }
              float r = strtof(e + 1, &e);
              float pt = (*e == ',') ? strtof(e + 1, &e) : NAN;
              uint32_t rel = ms - t0ms;
              int bi = (int)((uint64_t)rel * PLOT_BINS / ((uint64_t)spanSel + 1));
              // start of every later field (field 3 = pitch ends at e)
              const char* fld[24];
              int nf = 4;
              for (const char* q = e; nf < 24 && (q = strchr(q, ',')) != nullptr; q++) fld[nf++] = q + 1;
              if (bi >= 0 && bi < PLOT_BINS) {
                PlotBin& B = bins[bi];
                if (!isnan(r))  { B.rMin = fminf(B.rMin, r);  B.rMax = fmaxf(B.rMax, r);  B.rSum += r;  totSum[0] += r;  totN[0]++; }
                if (!isnan(pt)) { B.pMin = fminf(B.pMin, pt); B.pMax = fmaxf(B.pMax, pt); B.pSum += pt; totSum[1] += pt; totN[1]++; }
                if (!isnan(r) && !isnan(pt)) B.n++;
                for (int k = 0; k < 4; k++) {                // blank fields (no fix, no period) are skipped
                  int c = PLOT_XCOL[k];
                  if (c >= nf || *fld[c] == ',' || *fld[c] == 0 || *fld[c] == '\r') continue;
                  char* ee;
                  float v = strtof(fld[c], &ee);
                  if (ee != fld[c] && !isnan(v)) { B.xSum[k] += v; B.xN[k]++; totSum[2 + k] += v; totN[2 + k]++; }
                }
                rows++;
              }
              // event column (field 22) on the rows that carry a marker
              if (nev < PLOT_EVENTS && nf > 22 && *fld[22] && *fld[22] != ',') {
                ev[nev].ms = rel;
                ev[nev].n = (uint16_t)atoi(fld[22]);
                ev[nev].note[0] = 0;
                if (nf > 23) {
                  strncpy(ev[nev].note, fld[23], EVENT_NOTE_MAX); ev[nev].note[EVENT_NOTE_MAX] = 0;
                  char* cr = strchr(ev[nev].note, '\r'); if (cr) *cr = 0;
                  char* cm = strchr(ev[nev].note, ','); if (cm) *cm = 0;   // acc_* columns follow the note
                }
                nev++;
              }
            }
          }
        }
        p = nl + 1;
      }
      carry = len - (p - buf);                            // partial last line
      if (carry > 400) carry = 0;                         // malformed: drop it
      memmove(buf, p, carry);
      plotPct = (uint8_t)min(99UL, (unsigned long)(100ULL * (pos - start) / (endOff - start + 1)));
      vTaskDelay(1);
    }

    // JSON for the page
    String j;
    j.reserve(36000);
    j += "{\"file\":\""; j += plotFile;
    j += "\",\"cond\":\""; j += meta[0];
    j += "\",\"name\":\""; j += meta[1];
    j += "\",\"zd\":\""; j += meta[2];
    j += "\",\"t0\":\""; j += t0utc;
    j += "\",\"dur\":"; j += String(spanSel / 1000.0, 1);
    j += ",\"rows\":"; j += (unsigned long)rows;
    j += ",\"part\":"; j += (start != first) ? "true" : "false";
    const char* nm[2] = {",\"roll\":[", "],\"pitch\":["};
    for (int s2 = 0; s2 < 2; s2++) {
      j += nm[s2];
      for (int i = 0; i < PLOT_BINS; i++) {
        const PlotBin& B = bins[i];
        float lo = s2 ? B.pMin : B.rMin, hi = s2 ? B.pMax : B.rMax, sm = s2 ? B.pSum : B.rSum;
        if (i) j += ',';
        if (lo > hi || !B.n) { j += "null,null,null"; continue; }
        j += String(lo, 1); j += ','; j += String(hi, 1); j += ','; j += String(sm / B.n, 1);
      }
    }
    static const char* const XN[4] = {"],\"batt\":[", "],\"sog\":[", "],\"rp\":[", "],\"gm\":["};
    static const uint8_t XD[4] = {2, 1, 1, 2};          // decimals
    for (int k = 0; k < 4; k++) {
      j += XN[k];
      for (int i = 0; i < PLOT_BINS; i++) {
        if (i) j += ',';
        if (!bins[i].xN[k]) j += "null";
        else j += String(bins[i].xSum[k] / bins[i].xN[k], (unsigned int)XD[k]);
      }
    }
    // means over the whole span, and how many values each is from
    j += "],\"mean\":[";
    for (int k = 0; k < 6; k++) {
      if (k) j += ',';
      if (!totN[k]) j += "null"; else j += String(totSum[k] / totN[k], k == 2 || k == 5 ? 3 : 2);
    }
    j += "],\"meanN\":[";
    for (int k = 0; k < 6; k++) { if (k) j += ','; j += (unsigned long)totN[k]; }
    j += "],\"ev\":[";
    for (int i = 0; i < nev; i++) {
      if (i) j += ',';
      j += '['; j += String(ev[i].ms / 1000.0, 1); j += ','; j += ev[i].n; j += ",\""; j += ev[i].note; j += "\"]";
    }
    j += "]}";
    plotJson = j;
    plotPct = 100;
    plotState = 2;
  } while (false);
  if (f) { if (sdLock(3000)) { f.close(); sdUnlock(); } }
  free(bins);
  free(buf);
  vTaskDelete(nullptr);
}

// POST /plot  file=<name>&hours=<0 = whole file>
void handlePlot() {
  if (plotState == 1) { server.send(409, "application/json", "{\"error\":\"A plot is already being made\"}"); return; }
  String n = server.arg("file");
  if (!validName(n)) { server.send(400, "application/json", "{\"error\":\"Bad file name\"}"); return; }
  if (!sdOK) { server.send(409, "application/json", "{\"error\":\"No microSD card\"}"); return; }
  strncpy(plotFile, n.c_str(), sizeof(plotFile) - 1);
  plotFile[sizeof(plotFile) - 1] = 0;
  long h = server.arg("hours").toInt();
  plotHours = (h > 0 && h <= 24 * 60) ? (uint32_t)h : 0;
  plotJson = String();
  plotErr[0] = 0;
  plotPct = 0;
  plotState = 1;
  if (xTaskCreate(plotTask, "plot", 6144, nullptr, tskIDLE_PRIORITY, nullptr) != pdPASS) {
    plotState = 3;
    server.send(500, "application/json", "{\"error\":\"Could not start\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// GET /plot  -> {"state":..,"pct":..,"err":".."} or, when done, the plot data
void handlePlotGet() {
  server.sendHeader("Cache-Control", "no-store");
  if (plotState == 2) { server.send(200, "application/json", plotJson); return; }
  String j = "{\"state\":"; j += (int)plotState; j += ",\"pct\":"; j += (int)plotPct;
  j += ",\"err\":\""; j += plotErr; j += "\"}";
  server.send(200, "application/json", j);
}

// POST /gps  action=restart|cold   (GPS diagnostics card)
void handleGps() {
  String a = server.arg("action");
  if (chargeMode) { server.send(409, "application/json", "{\"error\":\"Leave charge mode first\"}"); return; }
  if (a == "restart") reqGps = 1;
  else if (a == "cold") reqGps = 2;
  else { server.send(400, "application/json", "{\"error\":\"action must be restart or cold\"}"); return; }
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /i2creset  power-cycle the STEMMA QT rail (BNO085, EEPROM, I2C pull-ups)
// and clear the bus, as the automatic recovery does
void handleI2cReset() {
  if (chargeMode) { server.send(409, "application/json", "{\"error\":\"Leave charge mode first\"}"); return; }
  reqI2cReset = true;
  uint32_t t0 = millis();                         // wait for loop() to do it (about 0.6 s)
  while (reqI2cReset && millis() - t0 < 3000) vTaskDelay(10);
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /phonetime  unix=<s> zd=<hours, nautical sign>   (ignored once GPS time is known)
void handlePhoneTime() {
  uint32_t u = (uint32_t)strtoul(server.arg("unix").c_str(), nullptr, 10);
  int zd = server.arg("zd").toInt();
  bool use = !gpsTimeSeen && u > 1700000000UL && zd >= -12 && zd <= 12;
  if (use) { reqPhoneUnix = u; reqPhoneZd = zd; reqPhoneRxMs = millis(); reqPhone = true; }
  server.send(200, "application/json", use ? "{\"used\":true}" : "{\"used\":false}");
}

// Web server runs in its own task so a long download never stalls the
// sensors and screen in loop(). Same priority as loop(): time-sliced.
void webTask(void* arg) {
  (void)arg;
  for (;;) {
    if (webPaused) { webIdle = true; vTaskDelay(50); continue; }
    webIdle = false;
    server.handleClient();
    vTaskDelay(1);
  }
}

void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// ============================================================================
//                                  SETUP
// ============================================================================
bool bootAnyFail = false;

// Frees an I2C bus left stuck by a device that was reset mid-transfer (SDA held
// low): clock SCL until SDA is released, then send a STOP. Run before Wire.begin().
void i2cBusClear() {
  pinMode(SDA, INPUT_PULLUP);
  pinMode(SCL, INPUT_PULLUP);
  delayMicroseconds(20);
  bool sda = digitalRead(SDA), scl = digitalRead(SCL);
  if (sda && scl) return;
  char b[64];
  snprintf(b, sizeof(b), "bus was stuck (SDA=%d SCL=%d)", sda, scl);
  i2cBusNote = b;
  pinMode(SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(SCL, HIGH);
  for (int i = 0; i < 18 && digitalRead(SDA) == LOW; i++) {
    digitalWrite(SCL, LOW);  delayMicroseconds(6);
    digitalWrite(SCL, HIGH); delayMicroseconds(6);
  }
  pinMode(SDA, OUTPUT_OPEN_DRAIN);                    // STOP: SDA rises while SCL high
  digitalWrite(SDA, LOW);  delayMicroseconds(6);
  digitalWrite(SCL, HIGH); delayMicroseconds(6);
  digitalWrite(SDA, HIGH); delayMicroseconds(6);
  pinMode(SDA, INPUT_PULLUP);
  pinMode(SCL, INPUT_PULLUP);
  delayMicroseconds(20);
  i2cBusNote += digitalRead(SDA) ? ", cleared" : ", STILL STUCK (a device is holding SDA low)";
}

// Lists what answers on the bus and which expected parts are missing
// (the battery gauge 0x36 is left out: it is absent whenever no battery is fitted)
void i2cScan() {
  static const uint8_t addr[] = {0x3C, 0x4A, 0x50, 0x68};
  static const char* const nm[] = {"OLED 0x3C", "IMU 0x4A", "EEPROM 0x50", "RTC 0x68"};
  String i2cFound;
  i2cMissing = "";
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { char b[8]; snprintf(b, sizeof(b), "0x%02X ", a); i2cFound += b; }
  }
  for (int i = 0; i < 4; i++) {
    Wire.beginTransmission(addr[i]);
    if (Wire.endTransmission() != 0) { if (i2cMissing.length()) i2cMissing += ", "; i2cMissing += nm[i]; }
  }
  Log.printf("I2C scan: %s\n", i2cFound.length() ? i2cFound.c_str() : "(nothing)");
  if (i2cMissing.length()) Log.printf("I2C missing: %s\n", i2cMissing.c_str());
  if (i2cBusNote.length()) Log.printf("I2C: %s\n", i2cBusNote.c_str());
}

// OLED not found at boot: keep looking, so a loose wing or slow power-up recovers
void serviceOledRetry() {
  if (displayOK || millis() - lastOledRetryMs < 5000) return;
  lastOledRetryMs = millis();
  if (!display.begin(OLED_ADDR, true)) return;
  display.oled_command(SH110X_DISPLAYOFF);            // begin() lit it at a fixed contrast
  displayOK = true;
  display.setRotation(oledRotation());
  display.cp437(true);
  display.setContrast((uint8_t)contrast);
  display.clearDisplay();
  display.display();
  if (displayOn) display.oled_command(SH110X_DISPLAYON);
  lastDrawMs = 0;
  Log.println("OLED: found");
}

const char* excCauseName(uint32_t c) {
  switch (c) {
    case 0:  return "IllegalInstruction";
    case 2:  return "InstructionFetchError";
    case 3:  return "LoadStoreError";
    case 6:  return "IntegerDivideByZero";
    case 9:  return "LoadStoreAlignment";
    case 20: return "InstFetchProhibited";
    case 28: return "LoadProhibited (bad pointer read)";
    case 29: return "StoreProhibited (bad pointer write)";
    default: return "";
  }
}

// Why did we restart? If it was a crash, pull the summary out of the core
// dump the ESP32 wrote to flash, then erase it so it is reported only once.
void readResetInfo() {
  esp_reset_reason_t r = esp_reset_reason();
  switch (r) {
    case ESP_RST_POWERON:  resetReason = "power-on"; break;
    case ESP_RST_SW:       resetReason = "software restart"; break;
    case ESP_RST_PANIC:    resetReason = "CRASH (panic)"; break;
    case ESP_RST_INT_WDT:  resetReason = "CRASH (interrupt watchdog)"; break;
    case ESP_RST_TASK_WDT: resetReason = "CRASH (task watchdog)"; break;
    case ESP_RST_WDT:      resetReason = "CRASH (watchdog)"; break;
    case ESP_RST_BROWNOUT: resetReason = "BROWNOUT (supply voltage dipped)"; break;
    case ESP_RST_DEEPSLEEP:resetReason = "deep sleep wake"; break;
    case ESP_RST_EXT:      resetReason = "reset pin"; break;
    case ESP_RST_USB:      resetReason = "USB reset"; break;
    default:               resetReason = String("other (") + (int)r + ")"; break;
  }
  bool crashed = (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT);
  if (crashed && crumbMagic == CRUMB_MAGIC && crumbStage < ST_COUNT) {
    static const char* const SN[] = {"Live", "Graph", "Position", "Maxima", "GPS"};
    crashReport = "Main loop was in: ";
    crashReport += STAGE_NAMES[crumbStage];
    crashReport += "  (screen ";
    crashReport += crumbScreen < 5 ? SN[crumbScreen] : "?";
    crashReport += ", up "; crashReport += crumbUptime; crashReport += " s)";
    if (r == ESP_RST_TASK_WDT || r == ESP_RST_PANIC)
      crashReport += "\nIf the cause is a task watchdog, the loop was stuck > " + String(LOOP_WDT_MS / 1000) + " s there.";
  }
  crumbMagic = CRUMB_MAGIC; crumbStage = ST_NONE; crumbUptime = 0;
  if (esp_core_dump_image_check() != ESP_OK) return;   // no (valid) dump stored
  esp_core_dump_summary_t* sm = (esp_core_dump_summary_t*)malloc(sizeof(esp_core_dump_summary_t));
  if (sm && esp_core_dump_get_summary(sm) == ESP_OK) {
    char b[48];
    String t = crashReport.length() ? crashReport + "\n" : String("");
    t += "Core dump: task '";
    t += sm->exc_task; t += "'";
    snprintf(b, sizeof(b), "  PC 0x%08lx", (unsigned long)sm->exc_pc); t += b;
    snprintf(b, sizeof(b), "  cause %lu ", (unsigned long)sm->ex_info.exc_cause); t += b;
    t += excCauseName(sm->ex_info.exc_cause);
    snprintf(b, sizeof(b), "  addr 0x%08lx", (unsigned long)sm->ex_info.exc_vaddr); t += b;
    t += "\nBacktrace:";
    for (uint32_t i = 0; i < sm->exc_bt_info.depth && i < 16; i++) {
      snprintf(b, sizeof(b), " 0x%08lx", (unsigned long)sm->exc_bt_info.bt[i]); t += b;
    }
    if (sm->exc_bt_info.corrupted) t += " (corrupted)";
    t += "\nBuild ";
    char sha[9]; memcpy(sha, sm->app_elf_sha256, 8); sha[8] = 0; t += sha;
    crashReport = t;
  }
  free(sm);
  esp_core_dump_image_erase();
}

// Read the pot and switch immediately (no smoothing) and set contrast, so the
// panel is never lit at a brightness the pot doesn't call for.
void applyPotNow() {
#if USE_POT_SWITCH
  displayOn = digitalRead(POT_SW_PIN) == LOW;
  lastSwState = displayOn;
#else
  displayOn = true;
#endif
  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(POT_PIN);
  potEma = sum / 8.0f;
  contrast = constrain(map((int)potEma, 0, 4095, 1, 255), 1, 255);
  if (displayOK) display.setContrast((uint8_t)contrast);
  lastPotMs = millis();
}

// Wait during boot while still following the pot and switch
void bootWait(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    servicePot();
    esp_task_wdt_reset();
    delay(10);
  }
}

// One line of the boot list (OLED + serial). isFault holds the list longer.
void bootText(const char* label, const char* state, bool isFault) {
  if (isFault) bootAnyFail = true;
  Log.printf("%-8s %s\n", label, state);
  if (displayOK) {
    display.print(label);
    display.print(' ');
    display.println(state);
    display.display();
  }
}

void bootLine(const char* label, bool ok) { bootText(label, ok ? "OK" : "FAIL", !ok); }

void setup() {
  Log.begin(115200);
  delay(300);
  Log.println("\nVessel Motion Recorder (VMR) starting");
  readResetInfo();
  Log.printf("Last reset: %s\n", resetReason.c_str());
  if (crashReport.length()) Log.println(crashReport);

  // Watchdog on the loop task (setup runs on it too): if it stops making
  // progress for LOOP_WDT_MS, the ESP32 restarts and the next boot reports
  // the breadcrumb of where it was stuck. bootWait() and loop() feed it.
  esp_task_wdt_config_t wdt = { LOOP_WDT_MS, 0, true };
  esp_task_wdt_reconfigure(&wdt);
  enableLoopWDT();
  crumb(ST_BOOT);

  // ESP32-S2 Feather: STEMMA QT power + I2C pull-ups are switched by PIN_I2C_POWER.
  // Its rest level is OFF. Hold it OFF first so the BNO085 and EEPROM get a real
  // power cycle on every boot (incl. watchdog restarts), then switch it ON.
#if defined(PIN_I2C_POWER)
  pinMode(PIN_I2C_POWER, INPUT);
  delay(1);
  i2cPowerOff = digitalRead(PIN_I2C_POWER);
  pinMode(PIN_I2C_POWER, OUTPUT);
#endif
  stemmaPower(false);
  delay(I2C_POWER_OFF_MS);
  stemmaPower(true);
  delay(I2C_POWER_SETTLE_MS);
  i2cBusClear();

  sdMutex = xSemaphoreCreateMutex();
  pendCap = SD_PEND_BYTES;
  pend = (char*)malloc(pendCap);
  if (!pend) { pendCap = 2048; pend = (char*)malloc(pendCap); }
  if (!pend) pendCap = 0;
  webBuf = (uint8_t*)malloc(WEB_BUF_BYTES);
  if (!pend || !webBuf) Log.println("WARNING: buffer allocation failed");

  Wire.begin();
  Wire.setClock(400000);
  i2cScan();

  btnA.begin(BUTTON_A);
  btnB.begin(BUTTON_B);
  btnC.begin(BUTTON_C);
  btnScr.begin(SCREEN_BTN_PIN);
  pinMode(POT_SW_PIN, INPUT_PULLUP);
  analogReadResolution(12);

  // ---- EEPROM (settings first: the OLED needs the flip setting) ----
  Wire.beginTransmission(EEPROM_ADDR);
  eepromOK = (Wire.endTransmission() == 0);
  if (eepromOK) loadPersist(); else persistDefaults();

  // ---- OLED ----
  displayOK = display.begin(OLED_ADDR, true);
  if (displayOK) {
    // begin() ends by switching the panel ON at the library's fixed contrast.
    // Blank it straight away, load the Adafruit splash (begin() pre-loads it
    // into the buffer) while dark, then show it on EVERY boot (power-on,
    // after a firmware update, any restart) for SPLASH_MS at the lowest
    // brightness, whatever the pot and its switch say, followed by the
    // Aspect Wave logo for SPLASH_MS. The pot and switch take over after.
    display.oled_command(SH110X_DISPLAYOFF);
    display.setContrast(1);
    if (P.oledFlip) flipBuffer180();               // splash the right way up when flipped
    display.display();
    display.oled_command(SH110X_DISPLAYON);
    for (uint32_t t0 = millis(); millis() - t0 < SPLASH_MS; delay(10)) esp_task_wdt_reset();
    display.setRotation(oledRotation());           // then the Aspect Wave logo, same brightness and time
    display.clearDisplay();
    display.drawBitmap((128 - AWLOGO_W) / 2, (64 - AWLOGO_H) / 2, awLogo, AWLOGO_W, AWLOGO_H, SH110X_WHITE);
    display.display();
    for (uint32_t t0 = millis(); millis() - t0 < SPLASH_MS; delay(10)) esp_task_wdt_reset();
    applyPotNow();                                 // pot brightness + on/off switch from here on
    if (!displayOn) display.oled_command(SH110X_DISPLAYOFF);
    display.setRotation(oledRotation());
    display.cp437(true);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("VMR  VESSEL MOTION");
    if (crashReport.length() || resetReason.startsWith("CRASH") || resetReason.startsWith("BROWN")) {
      display.println(resetReason.startsWith("BROWN") ? "Last reset: BROWNOUT" : "Last reset: CRASH");
      bootAnyFail = true;                   // hold the boot list longer
    }
    display.display();
  }
  Log.printf("OLED     %s\n", displayOK ? "OK" : "FAIL");

  bootLine("EEPROM", eepromOK);

  // ---- Battery ----
  battOK = maxlipo.begin(&Wire);
  bootText("Battery", battOK ? "OK" : "none", false);    // running on USB alone is fine

  // ---- RTC ----
  rtcOK = rtc.begin(&Wire);
  if (rtcOK) {
    bool valid = rtc.initialized() && !rtc.lostPower();
    if (valid) {
      DateTime t = rtc.now();
      valid = t.year() >= 2024 && t.year() < 2100;
    }
    rtcValid = valid;
    if (rtcValid) {
      rtc.start();
      DateTime t = rtc.now();
      baseUnix = t.unixtime();
      baseMillis = millis();
      clockSynced = true;
      resyncPending = true;
      lastRtcSec = 255;
      setSystemTime(baseUnix);
      clockSrc = 'R';
      Log.printf("RTC: valid, %04u-%02u-%02u %02u:%02u:%02u UTC\n",
                    (unsigned)t.year(), (unsigned)t.month(), (unsigned)t.day(),
                    (unsigned)t.hour(), (unsigned)t.minute(), (unsigned)t.second());
    } else {
      Log.println("RTC: no valid time, waiting for GPS fix");
    }
  }
  bootLine(rtcValid ? "RTC" : "RTC(nt)", rtcOK);

  // ---- GPS ----
  gpsConfigure();
  bootLine("GPS", true);

  // ---- IMU ----
  crumb(ST_IMU_BEGIN);
  imuOK = imuBegin();
  bootLine("IMU", imuOK);
  crumb(ST_BOOT);

  // ---- SD ----
  sdOK = sdMount();
  lastSdRetryMs = millis();
  bootLine("SD", sdOK);
  if (P.logActive) {                      // was recording at the last restart / power loss:
    logInterrupted = true;                // NOT resumed - only the Start button starts a recording
    P.logActive = 0;
    markPersistDirty(true);
    Log.println("Logging: was recording before this restart - stopped (press START to record)");
    if (displayOK) { display.println("Log: stopped by restart"); display.display(); }
  }

  // ---- Wi-Fi AP + web ----
  WiFi.mode(WIFI_AP);
  bool apOK = WiFi.softAP(AP_SSID, AP_PASS);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/files", HTTP_GET, handleFiles);
  server.on("/dl", HTTP_GET, handleDownload);
  server.on("/zip", HTTP_GET, handleZip);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/log", HTTP_POST, handleLog);
  server.on("/set", HTTP_POST, handleSet);
  server.on("/zero", HTTP_POST, handleZero);
  server.on("/logger", HTTP_POST, handleLogger);
  server.on("/restart", HTTP_POST, handleRestart);
  server.on("/i2creset", HTTP_POST, handleI2cReset);
  server.on("/gps", HTTP_POST, handleGps);
  server.on("/event", HTTP_POST, handleEvent);
  server.on("/pause", HTTP_POST, handlePause);
  server.on("/serial", HTTP_GET, handleSerial);
  server.on("/trimcal", HTTP_POST, handleTrimCal);
  server.on("/ota", HTTP_POST, handleOtaDone, handleOtaUpload);
  server.on("/plot", HTTP_POST, handlePlot);
  server.on("/plot", HTTP_GET, handlePlotGet);
  server.on("/phonetime", HTTP_POST, handlePhoneTime);
  server.onNotFound(handleNotFound);
  {
    // The spare slot must be a different partition from the one running: with a
    // single OTA slot (TinyUF2 No OTA) IDF hands back the running one itself.
    const esp_partition_t* np = esp_ota_get_next_update_partition(nullptr);
    otaMax = (np && np != esp_ota_get_running_partition()) ? np->size : 0;   // 0: no spare slot
    fwSize = ESP.getSketchSize();
  }
  server.begin();
  xTaskCreate(webTask, "web", 10240, nullptr, 1, nullptr);
  Log.printf("WiFi AP  %s  SSID=%s  http://%s\n", apOK ? "OK" : "FAIL",
                AP_SSID, WiFi.softAPIP().toString().c_str());
  bootLine("WiFi AP", apOK);

  // ---- buffers ----
  for (int i = 0; i < 60; i++) resetBin(bins[i]);
  binStartMs = millis();
  gRoll  = (float*)malloc(GRAPH_POINTS * sizeof(float));
  gPitch = (float*)malloc(GRAPH_POINTS * sizeof(float));
  rsCyc  = (RollCycle*)malloc(RS_CYCLES * sizeof(RollCycle));
  rsSort = (float*)malloc(RS_CYCLES * sizeof(float));
  if (!rsCyc || !rsSort) { free(rsCyc); free(rsSort); rsCyc = nullptr; rsSort = nullptr; Log.println("WARNING: roll statistics off (no memory)"); }
  if (!gRoll || !gPitch) {                             // graphs off rather than crash
    Log.println("WARNING: graph buffers not allocated");
    free(gRoll); free(gPitch);
    gRoll = gPitch = nullptr;
  } else {
    for (int i = 0; i < GRAPH_POINTS; i++) gRoll[i] = gPitch[i] = NAN;
  }
  for (int i = 0; i < 60; i++) sogHist[i] = velE[i] = velN[i] = NAN;
  lastGraphMs = millis();
  lastPersistSaveMs = millis();

  if (displayOK) {
    bootWait(bootAnyFail ? BOOTLIST_FAIL_MS : BOOTLIST_MS);
  }
  imuQuietRefMs = millis();                    // the IMU is not read during the boot screens
}

// ============================================================================
//                                   LOOP
// ============================================================================
void loop() {
  crumbScreen = screen;
  crumbUptime = millis() / 1000;
  if (chargeMode) {                              // only the gauge, web requests and switches
    crumb(ST_WEBREQ);  applyWebRequests();
    crumb(ST_POT);     servicePot();
    crumb(ST_BATT);    serviceBattery();
    uint32_t now = millis();
    if (now - lastLogMs >= LOG_INTERVAL_MS) { lastLogMs = now; crumb(ST_LOG); logSample(); }  // applies a pending Stop
    crumb(ST_PERSIST); servicePersist();
    if (now - lastBeatMs >= HEARTBEAT_MS) {
      lastBeatMs = now;
      Log.printf("[charge %lus] battery %.2f V %.0f%% %+.1f%%/h\n", (unsigned long)(now / 1000),
                    battV, battPct, battRate);
    }
    crumb(ST_IDLE);
    vTaskDelay(pdMS_TO_TICKS(20));               // let the CPU idle between passes
    return;
  }
  crumb(ST_GPS);     serviceGps();
  serviceImu();      // sets its own IMU stages
  crumb(ST_RTC);     serviceRtc();
  crumb(ST_BINS);    serviceBins();
  crumb(ST_BUTTONS); serviceButtons();
  if (!loggerMode) { crumb(ST_POT); servicePot(); }   // OLED stays off in datalogger mode
  crumb(ST_WEBREQ);  applyWebRequests();
  crumb(ST_BATT);    serviceBattery();
  if (!loggerMode) { crumb(ST_OLED_RETRY); serviceOledRetry(); serviceOledForce(); }

  uint32_t now = millis();
  crumb(ST_GRAPH);
  updateRollStats(now);
  if (!paused) gmWindowTick(now);
  serviceDock(now);
  serviceSog(now);
  if (now - lastGraphMs >= GRAPH_SAMPLE_MS) {
    lastGraphMs += GRAPH_SAMPLE_MS;
    if (now - lastGraphMs > GRAPH_SAMPLE_MS * 4) lastGraphMs = now;
    if (gRoll) {
      bool f = imuFresh();
      gRoll[gHead]  = f ? roll    : NAN;
      gPitch[gHead] = f ? pitch   : NAN;
      gHead = (gHead + 1) % GRAPH_POINTS;
      if (gCount < GRAPH_POINTS) gCount++;
    }
  }

  if (now - lastLogMs >= LOG_INTERVAL_MS) {
    lastLogMs = now;
    crumb(ST_LOG);
    logSample();
  }

  crumb(ST_PERSIST);
  servicePersist();

  if (now - lastBeatMs >= HEARTBEAT_MS) {          // proof of life on the Serial Monitor
    lastBeatMs = now;
    Log.printf("[alive %lus] IMU %s (re-requests %lu, power-cycles %lu) | GPS %s, %d used / %d in view, best %d dB-Hz, %lu NMEA%s | log %s | %s | last reset: %s\n",
                  (unsigned long)(now / 1000), imuFresh() ? "ok" : "NO DATA",
                  (unsigned long)imuSoftRecoveries, (unsigned long)imuRecoveries,
                  gpsFixValid() ? "fix" : "no fix", (int)gps.gpsSats, gpsInView[0] + gpsInView[1], (int)gpsBestSnr,
                  (unsigned long)gpsNmeaCount, gpsLastNmeaMs && now - gpsLastNmeaMs < 3000 ? "" : " (SILENT)",
                  logging ? "REC" : "off", loggerMode ? "DATALOGGER (Wi-Fi off)" : "Wi-Fi on",
                  resetReason.c_str());
    if (crashReport.length() && beatCount < 3) Log.println(crashReport);
    beatCount++;
  }

  if (loggerMode) {                              // no screen; brief red LED blip = alive
    digitalWrite(LED_BUILTIN, (now - lastBlinkMs) < 30 ? HIGH : LOW);
    if (now - lastBlinkMs >= LOGGER_BLINK_MS) lastBlinkMs = now;
  } else if (now - lastDrawMs >= DISPLAY_INTERVAL_MS) {
    lastDrawMs = now;
    crumb(ST_DRAW);
    drawScreen();
  }
  crumb(ST_IDLE);
  vTaskDelay(1);                   // let the web task and Wi-Fi run
}

// ============================================================================
//                     DASHBOARD PAGE (served at http://192.168.4.1)
// ============================================================================
const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Vessel Motion Recorder</title>
<style>
:root{--bg:#0b1220;--card:#131c2e;--fg:#e6edf7;--mut:#8a97ad;--roll:#4fc3f7;--pitch:#ffb74d;--grid:#26324a;--go:#2e9e4f;--halt:#d93a3a;--warn:#ff6b6b}
body.day{--bg:#eef1f5;--card:#ffffff;--fg:#111827;--mut:#566173;--roll:#0277bd;--pitch:#d96c00;--grid:#d6dce5;--go:#2e7d32;--halt:#c62828;--warn:#c62828}
body.red{--bg:#000;--card:#0d0000;--fg:#e0281e;--mut:#8c1a14;--roll:#e0281e;--pitch:#8c1a14;--grid:#2a0505;--halt:#e0281e;--warn:#e0281e}
*{box-sizing:border-box}
.sysb{width:130px;flex:none}
.banner{margin:0 12px 10px;padding:10px 40px 10px 12px;border:2px solid var(--warn);border-radius:12px;background:var(--card);color:var(--fg);font-size:.85rem;line-height:1.4;position:relative}
.banner b{color:var(--warn)}
#otabanx{position:absolute;top:4px;right:4px;min-height:32px;width:32px;padding:0;border:none;font-size:1.3rem;color:var(--mut)}
.credit{text-align:center;color:var(--mut);font-size:.75rem;line-height:1.5;padding:4px 16px 20px}
.credit b{color:var(--fg);font-size:.85rem}
[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:var(--fg);font-family:-apple-system,system-ui,"Segoe UI",Roboto,sans-serif}
/* header: text on the left; Pause (2 tall) with ZD under it, then Logger / Charge / Night; the
   battery line runs under the text and the free half of the Pause column */
header{display:grid;grid-template-columns:minmax(0,1fr) 38px 38px 76px;gap:6px;padding:14px 16px 8px;align-items:stretch}
.htxt{grid-area:1/1/3/2;min-width:0}
#batt{grid-area:3/1/4/3;align-self:start;margin-top:-6px;min-width:0}   /* straight under the UTC line */
#pb{grid-area:1/2/3/4}#zd{grid-area:3/3}#lb{grid-area:1/4}#cb{grid-area:2/4}#nb{grid-area:3/4}
header button,header select{min-height:36px;width:100%;margin:0}
main{padding:0 12px 16px;display:grid;gap:10px;max-width:720px;margin:0 auto}
main.stale{opacity:.45}
.card{background:var(--card);border-radius:12px;padding:12px 14px}
.row{display:flex;justify-content:space-between;align-items:center;gap:10px}
.t{font-size:clamp(1.5rem,8vw,2rem);white-space:nowrap;font-weight:700;font-variant-numeric:tabular-nums;line-height:1.1}
.t small{font-size:.9rem;color:var(--mut);font-weight:500;margin-left:4px}
.sub{color:var(--mut);font-size:.8rem;margin-top:2px;font-variant-numeric:tabular-nums}
.l{color:var(--mut);font-size:.7rem;text-transform:uppercase;letter-spacing:.06em}
.live{display:grid;grid-template-columns:1fr 1fr;gap:10px 8px}
.v{font-size:2.1rem;font-weight:700;font-variant-numeric:tabular-nums;line-height:1.1}
.v small{font-size:.9rem;color:var(--mut);font-weight:500}
.gpos{font-size:clamp(.95rem,4.7vw,1.35rem);font-weight:700;font-variant-numeric:tabular-nums;white-space:pre;margin:6px 0 10px}
.lbl{font-size:.95rem;color:var(--fg)}
.gax{display:flex;justify-content:space-between;align-items:center;margin-top:8px}
.gax .step button{min-width:38px;min-height:38px;padding:2px 6px}
.gax .step span{min-width:56px;font-size:.9rem}
.gax .roll span{color:var(--roll)}.gax .pitch span{color:var(--pitch)}
.glg{display:flex;justify-content:space-between;font-size:.8rem;color:var(--mut);margin-top:6px}
.glg b{display:inline-block;width:10px;height:10px;border-radius:2px;margin:0 6px;vertical-align:-1px}
.dlg{position:fixed;inset:0;background:rgba(0,0,0,.55);display:flex;align-items:center;justify-content:center;padding:16px;z-index:10}
.dlg[hidden]{display:none}
.dlgbox{background:var(--card);border:1px solid var(--grid);border-radius:14px;padding:16px;width:100%;max-width:380px}
.dlgbox input{width:100%;box-sizing:border-box;margin:10px 0 6px;padding:10px;font-size:1rem;border-radius:8px;border:1px solid var(--grid);background:var(--bg);color:var(--fg)}
.dlgbox .btns button{min-height:50px;font-weight:700}
.dlgbox .go{background:var(--go);border-color:var(--go);color:#fff}
#tagprev{color:var(--fg);word-break:break-all}
button{background:transparent;color:var(--fg);border:1px solid var(--grid);border-radius:8px;padding:6px 10px;font-size:.85rem;min-height:44px}
button:disabled{opacity:.35}
button.warn{color:var(--warn);border-color:var(--warn)}
#pb,#nb,#cb,#lb{color:var(--mut)}
#zd{font-size:.68rem;color:var(--mut);background:transparent;-webkit-appearance:none;appearance:none;padding:0;text-align:center;text-align-last:center}
#pb.on{background:var(--warn);border-color:var(--warn);color:var(--bg);font-weight:700}
.pausecard{border:2px solid var(--warn);text-align:center}
.pbig{font-size:2.6rem;font-weight:800;letter-spacing:.12em;color:var(--warn);line-height:1.1}
main.paused .card:not(.pausecard){opacity:.55}
.devbtn{width:100%;color:var(--mut)}
.devsec{display:grid;gap:10px}
.devsec[hidden]{display:none}
.ser{font:9px/1.35 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;white-space:pre-wrap;word-break:break-all;background:var(--bg);border-radius:8px;padding:8px;margin:8px 0;min-height:calc(10 * 1.35 * 9px + 16px)}
#seroled{min-height:36px}
.bar{height:8px;background:var(--grid);border-radius:4px;margin-top:8px;overflow:hidden}
.bar>div{height:100%;width:0;background:var(--roll)}
#lb{font-size:.8rem}
.lrow{display:flex;justify-content:space-between;align-items:center;margin-bottom:6px}
.cn{display:inline-block;min-width:1.5em;padding:1px 5px;margin-right:7px;border-radius:5px;background:var(--grid);color:var(--fg);text-align:center;font-weight:700;letter-spacing:0}
body.noyaw .yawonly{display:none!important}
.one{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
header .one{font-size:min(.8rem,3vw)}   /* title and battery line stay on one line on narrow phones */
#cb.on{color:var(--go);border-color:var(--go);font-weight:700}
.chg{border:2px solid var(--go)}
.chg .big{font-size:1.6rem;font-weight:700;font-variant-numeric:tabular-nums;margin-top:4px}
main.charging>.card:not(.chg):not(.keep){display:none}
select{background:var(--bg);color:var(--fg);border:1px solid var(--grid);border-radius:8px;min-height:32px;padding:2px 6px;font-size:.8rem}
table{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums;font-size:.95rem}
th,td{padding:5px 2px;text-align:right}
th{color:var(--mut);font-weight:500;font-size:.7rem;text-transform:uppercase;letter-spacing:.05em}
th:first-child,td:first-child{text-align:left}
td.w{color:var(--mut);font-size:.75rem}
canvas{width:100%;height:240px;display:block;margin-top:8px}
#g.tall{height:290px}
.gctl{display:flex;align-items:center;gap:8px}
.step{display:inline-flex;align-items:center;gap:4px}
.step button{min-width:44px;font-size:1.1rem;padding:4px 8px}
.step span{min-width:64px;text-align:center;font-variant-numeric:tabular-nums;font-weight:600}
.ctl{display:grid;grid-template-columns:auto 1fr;gap:8px 10px;align-items:center;margin-top:10px}
.ctl .sub{margin:0}
.ctl .step{justify-self:end}
.rtp{font-size:1.25rem;font-variant-numeric:tabular-nums}
.recst{font-size:1.35rem;font-weight:700;margin-top:4px;font-variant-numeric:tabular-nums}
.recst.on{color:var(--halt)}
.tabs{display:inline-flex;gap:4px}
.tabs button{min-height:34px;padding:2px 10px;font-size:.8rem}
.tabs button.sel{background:var(--roll);border-color:var(--roll);color:var(--bg);font-weight:700}
.st td:last-child{font-weight:600}
.gmrow{display:flex;align-items:center;gap:8px;margin:6px 0 4px}
.gmrow .gpos{margin:0;flex:1}
.gmt{color:var(--mut);font-size:.8rem;font-variant-numeric:tabular-nums;white-space:nowrap}
.ship{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-top:10px}
.ship.drafts{grid-template-columns:repeat(3,1fr)}
.dockgrid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:6px}
.dockgrid .gpos{margin:2px 0}
.ship label{font-size:.75rem;color:var(--mut)}
.ship input,.evrow input{width:100%;box-sizing:border-box;padding:8px;font-size:1rem;border-radius:8px;border:1px solid var(--grid);background:var(--bg);color:var(--fg);margin-top:3px}
.evrow{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:end;margin-top:12px}
.evrow button{min-height:42px;font-weight:700}
#evlist{margin-top:6px}
.conds{display:grid;gap:6px;margin:10px 0 4px}
.tog{border-radius:8px;padding:3px 10px;min-height:32px;font-size:.8rem;color:var(--mut)}
.tog.on{background:var(--roll);border-color:var(--roll);color:var(--bg);font-weight:700}
.togs{display:flex;flex-wrap:wrap;gap:5px}
.pgrid{display:grid;grid-template-columns:auto 1fr;gap:8px 10px;align-items:center;margin-top:10px}
.pgrid select{width:100%;min-height:40px;font-size:.95rem;border-radius:8px;border:1px solid var(--grid);background:var(--bg);color:var(--fg);padding:4px 6px}
#pimg{width:100%;margin-top:10px;border-radius:6px;display:block}
#pimg[hidden],#psave[hidden]{display:none}
.btnlink{display:block;text-align:center;margin-top:8px;padding:12px;border:1px solid var(--grid);border-radius:8px;color:var(--fg);text-decoration:none;font-weight:700}
.conds label{display:flex;gap:10px;align-items:center;padding:8px 10px;border:1px solid var(--grid);border-radius:8px;font-size:.95rem}
.conds input{width:20px;height:20px;margin:0;accent-color:var(--roll)}
.dot{display:inline-block;width:12px;height:12px;border-radius:50%;background:var(--halt);margin-right:8px;vertical-align:1px;animation:bl 1s steps(2) infinite}
@keyframes bl{50%{opacity:.15}}
.recbtns{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:12px}
.recbtns button{min-height:60px;font-size:1.05rem;font-weight:800;letter-spacing:.04em;border:0;border-radius:12px;color:#fff}
.recbtns .go{background:var(--go)}
.recbtns .halt{background:var(--halt)}
.recbtns button:disabled{opacity:.22}
body.red .recbtns .go{background:transparent;border:2px solid var(--fg);color:var(--fg)}
body.red .recbtns .halt{color:#000}
.seg{display:grid;grid-template-columns:repeat(5,1fr);gap:6px;margin-top:10px}
.seg button{padding:4px 2px;font-size:.78rem}
.seg button.sel{background:var(--roll);border-color:var(--roll);color:var(--bg);font-weight:700}
input[type=range]{width:100%;height:36px;accent-color:var(--roll);margin:4px 0 0}
.zb{display:flex;gap:8px}
.zb button{padding:6px 14px;white-space:nowrap}
#flist{max-height:340px;overflow-y:auto;margin-top:6px}
.f{display:grid;grid-template-columns:auto 1fr auto auto;gap:10px;align-items:center;padding:9px 0;border-top:1px solid var(--grid);font-variant-numeric:tabular-nums}
.f input,.chk input{width:20px;height:20px;margin:0;accent-color:var(--roll)}
.f a{color:var(--roll);text-decoration:none;font-size:1.15rem;padding:2px 8px}
.f em{font-style:normal;font-size:.65rem;font-weight:700;color:var(--bg);background:var(--pitch);border-radius:4px;padding:1px 4px;margin-left:6px;vertical-align:1px}
.fs{color:var(--mut);font-size:.85rem}
.chk{display:flex;gap:10px;align-items:center;margin:10px 0 2px;font-size:.85rem;color:var(--mut)}
.btns{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:10px}
.lock{color:var(--pitch)}
.crash{border:2px solid var(--halt)}
.crash .l{color:var(--halt)}
#probtxt{white-space:pre-wrap;word-break:break-all;font-size:.75rem;margin:8px 0;user-select:all}
</style></head><body>
<header><div class="htxt"><div class="sub one">VESSEL MOTION RECORDER</div>
<div class="t"><span id="lt">--:--:--</span><small>LT</small></div>
<div class="sub" id="ltd">waiting for data</div>
<div class="sub" id="utc"></div></div>
<div class="sub one" id="batt"></div>
<button id="pb">Pause</button><button id="lb">Logger</button><button id="cb">Charge</button><select id="zd" aria-label="Zone description"></select><button id="nb">Night</button></header>
<div class="banner" id="otaban" hidden><button id="otabanx" aria-label="Close">×</button><b>Wi-Fi firmware updates are off.</b> This firmware was flashed with a partition scheme that has no spare slot. Flash it once over USB with Tools → Partition Scheme → "Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)"; after that, updates work from card 16.</div>
<main id="main">
<div class="card pausecard" id="pausecard" hidden>
 <div class="pbig">PAUSED</div>
 <div class="sub" id="pauseinfo" style="margin:6px 0 10px"></div>
 <button id="pres" class="warn" style="width:100%">Resume</button>
</div>
<div class="card chg" id="chgcard" hidden>
 <div class="l">Charge mode</div>
 <div class="big" id="chgbatt">--</div>
 <div class="sub">OLED, logging, GPS and IMU sensors are off; CPU slowed; Wi-Fi on low power. Tap Exit charge (top right) to leave.</div>
</div>
<div class="card">
 <div class="l"><b class="cn">1</b>GPS</div>
 <div class="gpos" id="pos">--</div>
 <div class="live">
  <div><div class="v" id="gsog">--</div></div>
  <div><div class="v" id="gcog">--</div></div>
 </div>
 <div class="sub" id="nav" style="margin-top:8px"></div>
</div>
<div class="card">
 <div class="row"><div class="l"><b class="cn">2</b><span id="gttl">Graph</span></div><div class="gctl"><button class="tog" id="gviewsb">Views</button><div class="step" id="gsecsc"><button data-s="gsecs" data-d="-1" aria-label="Shorter">‹</button><span id="gsecsv">--</span><button data-s="gsecs" data-d="1" aria-label="Longer">›</button></div></div></div>
 <div class="togs" id="gviews" style="margin-top:8px" hidden><button class="tog" data-v="graph">Graph</button><button class="tog" data-v="clino">Clinometer</button><button class="tog" data-v="stern">Stern view</button><button class="tog" data-v="sky">Sky plot</button><button class="tog" data-v="gmeter">G-meter</button></div>
 <div class="row" style="margin-top:8px"><span class="lbl">Roll period</span><span><b id="rtp" class="rtp">--</b> <span class="sub" id="rtpn"></span></span></div>
 <div class="glg" id="glg"><span><b style="background:var(--roll);margin-left:0"></b>Roll · left axis</span><span>Pitch · right axis<b style="background:var(--pitch);margin-right:0"></b></span></div>
 <div class="gax" id="gaxt">
  <div class="step roll"><button data-s="gmax" data-d="-1" aria-label="Roll top smaller">‹</button><span id="gmaxv">--</span><button data-s="gmax" data-d="1" aria-label="Roll top larger">›</button></div>
  <div class="step pitch"><button data-s="pmax" data-d="-1" aria-label="Pitch top smaller">‹</button><span id="pmaxv">--</span><button data-s="pmax" data-d="1" aria-label="Pitch top larger">›</button></div>
 </div>
 <canvas id="g"></canvas>
 <button id="vreset" style="width:100%;margin-top:6px" hidden>Reset</button>
 <div class="gax" id="gaxb" style="margin-top:2px">
  <div class="step roll"><button data-s="gmin" data-d="-1" aria-label="Roll bottom smaller">‹</button><span id="gminv">--</span><button data-s="gmin" data-d="1" aria-label="Roll bottom larger">›</button></div>
  <div class="step pitch"><button data-s="pmin" data-d="-1" aria-label="Pitch bottom smaller">‹</button><span id="pminv">--</span><button data-s="pmin" data-d="1" aria-label="Pitch bottom larger">›</button></div>
 </div>
 <div class="sub" id="gnote" style="margin-top:8px"></div>
</div>
<div class="card">
 <div class="lrow"><div class="l"><b class="cn">3</b>Motion</div><button class="tog" id="showyaw" aria-pressed="true">Yaw</button></div>
 <div class="live">
  <div><div class="l">Roll</div><div class="v" id="roll">--</div></div>
  <div><div class="l">Pitch</div><div class="v" id="pitch">--</div></div>
  <div class="yawonly"><div class="l">Yaw (relative)</div><div class="v" id="yaw">--</div></div>
  <div class="yawonly"><div class="l">Rate of turn</div><div class="v" id="rot">--</div></div>
 </div>
 <div class="ctl yawonly"><span class="lbl">ROT deadband</span><div class="step"><button data-s="rotdb" data-d="-1" aria-label="Smaller deadband">‹</button><span id="rotdbv">--</span><button data-s="rotdb" data-d="1" aria-label="Larger deadband">›</button></div></div>
</div>
<div class="card" id="dispcard">
 <div class="row"><div class="l"><b class="cn">4</b>OLED display</div><div class="togs"><button class="tog" id="dflip">Flip</button><button class="tog" id="don">On</button></div></div>
 <div class="seg" id="scr"><button data-n="0">Live</button><button data-n="1">Graph</button><button data-n="2">Position</button><button data-n="3">Maxima</button><button data-n="4">GPS</button><button data-n="5">Clino</button><button data-n="6">Stern</button><button data-n="7">Sky</button><button data-n="8">Dock</button><button data-n="9">G</button></div>
 <div class="row" style="margin-top:12px"><span class="sub">Brightness</span><span class="sub" id="bpct">--</span></div>
 <input type="range" id="bri" min="1" max="100" value="50">
 <div class="sub" id="dsrc"></div>
</div>
<div class="card">
 <div class="l" style="margin-bottom:6px"><b class="cn">5</b>Maxima</div>
 <table><thead><tr><th></th><th>Last 1 min</th><th>All-time</th><th>When (UTC)</th></tr></thead><tbody id="mx"></tbody></table>
</div>
<div class="card">
 <div class="row"><div class="l"><b class="cn">6</b>Roll statistics</div><div class="tabs" id="rswin"><button data-w="0">10 min</button><button data-w="1">30 min</button></div></div>
 <table class="st" style="margin-top:6px"><tbody id="rstab"></tbody></table>
 <div class="ctl"><span class="lbl">Count rolls beyond</span><div class="step"><button data-s="rthr" data-d="-1">‹</button><span id="rthrv">--</span><button data-s="rthr" data-d="1">›</button></div></div>
 <div class="sub" style="margin-top:8px">Each full roll the roll-period detector accepts counts once. Amplitude = half its peak-to-peak swing about the running mean; "beyond" uses the peak angle including list.</div>
</div>
<div class="card">
 <div class="row"><div><div class="l"><b class="cn">7</b>Level zero</div><div class="sub" id="zst">--</div></div>
  <div class="zb"><button id="zset">Set zero</button><button id="zclr">Clear</button></div></div>
 <div class="ctl">
  <span class="sub">Roll</span><div class="step"><button data-z="roll" data-d="-1" aria-label="Roll reading toward port">‹ P</button><span id="zrv">--</span><button data-z="roll" data-d="1" aria-label="Roll reading toward starboard">S ›</button></div>
  <span class="sub">Pitch</span><div class="step"><button data-z="pitch" data-d="-1" aria-label="Pitch reading bow down">‹ dn</button><span id="zpv">--</span><button data-z="pitch" data-d="1" aria-label="Pitch reading bow up">up ›</button></div>
 </div>
 <div class="sub" style="margin-top:6px">Each tap shifts that reading 0.1°.</div>
</div>
<div class="card">
 <div class="lrow"><div class="l"><b class="cn">8</b>GM estimate</div><button class="tog" id="dockb">At the dock</button></div>
 <div class="gmrow"><span class="gpos" id="gmv">--</span><span class="gmt" id="gmt"></span><button class="tog" id="gmrst" hidden>Restart</button></div>
 <div class="sub" id="gminfo"></div>
 <div class="ship">
  <div><label for="sbeam">Beam (m)</label><input id="sbeam" type="number" inputmode="decimal" step="0.01" min="0"></div>
  <div><label for="sdraft">Draft (m)</label><input id="sdraft" type="number" inputmode="decimal" step="0.01" min="0"></div>
  <div><label for="slen">LOA (m)</label><input id="slen" type="number" inputmode="decimal" step="0.1" min="0"></div>
  <div><label for="slbp">LBP (m, for trim; optional)</label><input id="slbp" type="number" inputmode="decimal" step="0.1" min="0"></div>
 </div>
 <button id="ssave" style="width:100%;margin-top:8px" disabled>Save ship particulars</button>
 <div class="sub" style="margin-top:8px">From the measured roll period with the IMO rolling-period formula (IS Code 2008, A 2.3): GM = (2·C·B / T)², C = 0.373 + 0.023·B/d − 0.043·L/100.</div>
</div>
<div class="card" id="dockcard" hidden>
 <div class="l"><b class="cn">8b</b>At the dock</div>
 <div class="dockgrid">
  <div><div class="lbl">Trim</div><div class="gpos" id="dtrim">--</div><div class="sub" id="dtrim2"></div></div>
  <div><div class="lbl">List</div><div class="gpos" id="dlist">--</div><div class="sub" id="dlist2"></div></div>
 </div>
 <div class="sub" id="dcal" style="margin-top:10px"></div>
 <div class="lbl" style="margin-top:12px">Calibrate from the draft marks</div>
 <div class="ship drafts">
  <div><label for="dfwd">Forward</label><input id="dfwd" placeholder="33-04" autocomplete="off" autocapitalize="off" spellcheck="false"></div>
  <div><label for="dmid">Midships (opt.)</label><input id="dmid" placeholder="40-01" autocomplete="off" autocapitalize="off" spellcheck="false"></div>
  <div><label for="daft">Aft</label><input id="daft" placeholder="47-03" autocomplete="off" autocapitalize="off" spellcheck="false"></div>
 </div>
 <div class="btns"><button id="dcalgo" disabled>Calibrate</button><button id="dcalclr">Clear calibration</button></div>
 <div class="sub" id="dmsg" style="margin-top:6px"></div>
 <div class="sub" style="margin-top:8px">Drafts in feet-inches (33-04, 33 04 or 33'04"). Trim = L × tan(pitch), 60 s means; list to 0.01°. Calibrating stores the pitch offset that makes the reading match the marks right now. The VMR measures the deck slope where it is mounted, and hull bending (hog / sag) changes that slope near the ends, so recalibrate when the loading changes much. The midships draft shows the hog or sag at the time of calibration.</div>
</div>
<div class="card">
 <div class="l"><b class="cn">9</b>Recording to microSD</div>
 <div class="recst" id="recst">--</div>
 <div class="sub" id="recinfo"></div>
 <div class="recbtns"><button id="bstart" class="go">START LOGGING</button><button id="bstop" class="halt">STOP LOGGING</button></div>
 <div class="evrow"><div><label class="sub" for="evnote">Event note (optional)</label><input id="evnote" maxlength="40" autocomplete="off" placeholder="e.g. course change"></div><button id="evbtn" disabled>Mark event</button></div>
 <div class="sub" id="evlist"></div>
</div>
<div class="card keep">
 <div class="row"><div class="l"><b class="cn">10</b>microSD files</div><button id="sdr">Refresh</button></div>
 <div class="sub" id="sdinfo">loading…</div>
 <div class="sub lock" id="sdlock" hidden>Stop logging to download or delete files.</div>
 <label class="chk"><input type="checkbox" id="sall">Select all</label>
 <div id="flist"></div>
 <div class="sub" id="selinfo"></div>
 <div class="btns">
  <button id="bdl" disabled>Download selected</button><button id="bdel" class="warn" disabled>Delete selected</button>
  <button id="bdla">Download all</button><button id="bdela" class="warn">Delete all</button>
 </div>
 <div class="sub" id="sdmsg"></div>
</div>
<div class="card keep">
 <div class="l"><b class="cn">11</b>Plot from file</div>
 <div class="pgrid">
  <label class="sub" for="pfile">File</label><select id="pfile"></select>
  <label class="sub" for="prange">Span</label><select id="prange"><option value="1">Last 1 h</option><option value="3">Last 3 h</option><option value="6">Last 6 h</option><option value="12">Last 12 h</option><option value="24" selected>Last 24 h</option><option value="48">Last 48 h</option><option value="0">Whole file</option></select>
 </div>
 <div class="togs" id="psel" style="margin-top:10px"><button class="tog" data-k="roll">Roll</button><button class="tog" data-k="pitch">Pitch</button><button class="tog" data-k="batt">Battery V</button><button class="tog" data-k="sog">SOG</button><button class="tog" data-k="rp">Roll period</button><button class="tog" data-k="gm">GM</button><button class="tog" data-k="mean">5-min mean</button></div>
 <button id="pgo" style="width:100%;margin-top:10px">Make plot</button>
 <div class="sub" id="pmsg"></div>
 <img id="pimg" alt="Roll and pitch plot" hidden>
 <a id="psave" class="btnlink" hidden>Save image</a>
</div>
<button id="devbtn" class="devbtn">Enable Developer Settings</button>
<div class="devsec" id="dev" hidden>
<div class="card keep" id="probcard">
 <div class="l"><b class="cn">12</b><span id="probttl">Hardware check</span></div>
 <pre id="probtxt"></pre>
 <div class="sub">Copy this text when reporting a problem. Crash details clear at the next restart.</div>
</div>
 <div class="card">
 <div class="row"><div class="l"><b class="cn">13</b>Serial monitor</div><button class="tog" id="seroled">Show on OLED</button></div>
 <pre class="ser" id="serout">--</pre>
 <div class="sub">The last 10 lines the VMR printed (the same as the USB Serial Monitor), newest at the bottom. Show on OLED puts them on the OLED (even if it is switched off) until you tap it again; it then goes back to its screen.</div>
</div>
<div class="card">
 <div class="l"><b class="cn">14</b>GPS diagnostics</div>
 <table class="st" style="margin-top:6px"><tbody id="gdtab"></tbody></table>
 <div class="btns"><button id="grst">Restart GPS</button><button id="gcold" class="warn">Cold start</button></div>
 <div class="ctl"><span class="lbl">SOG / COG average</span><div class="step"><button data-s="sogavg" data-d="-1" aria-label="Shorter average">‹</button><span id="sogavgv">--</span><button data-s="sogavg" data-d="1" aria-label="Longer average">›</button></div></div>
 <div class="sub">Averaging for the SOG and COG shown in card 1 and on the OLED (default 10 s).</div>
 <div class="sub" style="margin-top:8px">Signal: 30+ dB-Hz is good, under 25 is weak. Antenna "external" needs an active antenna (it draws current); a passive one, or a bad cable, shows as internal. The GPS wing's red LED blinks every second while searching and every 15 s with a fix.</div>
 <div class="sub" id="gdmsg"></div>
</div>
<div class="card keep">
 <div class="row"><div><div class="l"><b class="cn">15</b>System</div><div class="sub" id="sysinfo"></div></div><button id="rst" class="warn sysb">Restart VMR</button></div>
 <div style="margin-top:10px"><div class="lbl">Wi-Fi</div><div class="sub" id="wifiinfo">--</div></div>
 <div class="row" style="margin-top:10px"><div class="sub">Power-cycles the IMU and EEPROM and clears the I2C bus. Use if roll and pitch freeze.</div><button id="i2cr" class="warn sysb">Reset I2C bus</button></div>
 <div class="row" style="margin-top:10px"><div class="sub">Erases the all-time maxima (card 5) and their dates. Also: hold A+C on the OLED maxima screen.</div><button id="mreset" class="warn sysb">Reset maxima</button></div>
</div>
<div class="card">
 <div class="l"><b class="cn">16</b>Firmware update</div>
 <div class="sub" id="fwinfo">--</div>
 <div id="fwform" hidden>
  <input type="file" id="fwfile" style="margin-top:8px;width:100%;color:var(--fg)">
  <button id="fwgo" class="warn" style="width:100%;margin-top:8px" disabled>Install firmware</button>
  <div class="bar" id="fwbar" hidden><div id="fwbarv"></div></div>
  <div class="sub" id="fwmsg" style="margin-top:6px"></div>
  <div class="sub" style="margin-top:8px">Arduino IDE: Sketch → Export Compiled Binary, then pick <b>VMR.ino.bin</b> from the sketch's build folder (not the .merged, .bootloader or .partitions file). A .ino or .txt can't be installed: the VMR can't compile code. It checks the file, installs it and restarts (about 15 s). Logging stops when the upload starts and the new firmware starts fresh, not recording; settings, level zero and maxima are kept. If anything fails, the old firmware stays.</div>
 </div>
</div>
</div>
</main>
<footer class="credit"><b>Vessel Motion Recorder</b><br>Written by Sophie Scopazzi (Aspect Wave LLC) and Claude (Anthropic), 2026</footer>
<div class="dlg" id="tagdlg" hidden><div class="dlgbox" role="dialog" aria-labelledby="tagttl">
 <div class="l" id="tagttl">Start logging</div>
 <div class="sub" style="margin-top:6px">Name to add to the end of the file name (optional; letters, numbers, - and _):</div>
 <input id="tagin" maxlength="23" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false" placeholder="e.g. transit_SF">
 <div class="sub">File: <b id="tagprev"></b></div>
 <div class="sub">The date and time (UTC) in the name are set when you tap Start.</div>
 <div class="sub" style="margin-top:10px">Loading condition (written in the file header):</div>
 <div class="conds" id="conds">
  <label><input type="radio" name="cond" value="1">Docked</label>
  <label><input type="radio" name="cond" value="2">Ballast (95%)</label>
  <label><input type="radio" name="cond" value="3">Ballast (98%)</label>
  <label><input type="radio" name="cond" value="4">Loaded (CP)</label>
  <label><input type="radio" name="cond" value="5">Loaded (LB)</label>
 </div>
 <div class="btns"><button id="tagcancel">Cancel</button><button id="taggo" class="go">START</button></div>
</div></div>
<script>
const $=id=>document.getElementById(id);
const f=(v,d=1,s=true)=>v==null?'--':((s&&v>0?'+':'')+v.toFixed(d));
const fmt=b=>b<1024?b+' B':b<1048576?(b/1024).toFixed(1)+' KB':b<1073741824?(b/1048576).toFixed(1)+' MB':(b/1073741824).toFixed(2)+' GB';
const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const hms=s=>[Math.floor(s/3600),Math.floor(s%3600/60),s%60].map(n=>String(n).padStart(2,'0')).join(':');
const dm=(v,p,n)=>{if(v==null)return'--';const h=v<0?n:p;v=Math.abs(v);let d=Math.floor(v),m=(v-d)*60;if(m>=59.9995){d++;m=0;}return d+'° '+m.toFixed(3).padStart(6,'0')+"' "+h;}
const post=(url,body)=>fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
const store=(k,v)=>{try{localStorage.setItem(k,v)}catch(e){}};
const recall=k=>{try{return localStorage.getItem(k)}catch(e){return null}};
// roll: + = starboard side down, shown as side + size. pitch: + = bow up.
const side=(v,d=1)=>v==null?'--':Math.abs(v)<0.5*Math.pow(10,-d)?(0).toFixed(d):(v>0?'S ':'P ')+Math.abs(v).toFixed(d);
const zdTxt=h=>(h>0?'+':h<0?'−':'')+Math.abs(h);
const DAYS=['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
let last=null,isLogging=null,files=[],briBusy=0,briTimer=null,build=null;
let sysMsg=null,down=false,lastRx=0;   // sysMsg holds a Restart/Logger notice until the VMR drops off and comes back

// ---- settings the user changes here: show the new value at once, then
//      ignore the device's (older) value for a moment so taps don't bounce
const S={gsecs:60,gmax:5,gmin:-5,pmax:5,pmin:-5,sogavg:10,zd:0,rotdb:3,rthr:10};
const busy={};
// settings that step through a list of choices with the < > arrows
const LIST={gsecs:[30,60,120,300,600],sogavg:[1,2,3,5,10,15,20,30,45,60],rotdb:[0,1,2,3,5,10,15,20,30]};
const label={gsecs:v=>v<60?v+' s':(v/60)+' min',sogavg:v=>v+' s',rotdb:v=>v?v+'°/min':'off',
 gmax:v=>'S '+v+'°',gmin:v=>'P '+(-v)+'°',pmax:v=>'up '+v+'°',pmin:v=>'dn '+(-v)+'°',rthr:v=>v+'°'};
// show every setting next to its arrows
const showSet=()=>{for(const k of ['gsecs','gmax','gmin','pmax','pmin','sogavg','rotdb','rthr'])$(k+'v').textContent=label[k](S[k]);
 if(document.activeElement!==$('zd'))$('zd').value=S.zd;}
const setCmd=(k,v)=>{S[k]=v;busy[k]=Date.now();showSet();draw(last);post('/set',k+'='+v).catch(()=>{});}
// < > arrows: graph length, SOG/COG average and ROT deadband step through their lists; roll statistics
// threshold and graph limits move 1 deg (graph limits 1-70 deg)
const stepSet=(k,d)=>{
 let v;
 if(LIST[k]){const L=LIST[k];let i=L.indexOf(S[k]);if(i<0)i=L.findIndex(x=>x>=S[k]);if(i<0)i=L.length-1;v=L[Math.max(0,Math.min(L.length-1,i+d))];}
 else if(k==='rthr')v=Math.max(1,Math.min(45,S.rthr+d));
 else if(k==='gmax'||k==='pmax')v=Math.max(1,Math.min(70,S[k]+d));
 else v=Math.max(-70,Math.min(-1,S[k]-d));   // gmin / pmin shown as 'P n' / 'dn n': right arrow = bigger n
 if(v!==S[k])setCmd(k,v);
}
document.querySelectorAll('[data-s]').forEach(b=>b.onclick=()=>stepSet(b.dataset.s,+b.dataset.d));
for(let h=-12;h<=12;h++)$('zd').add(new Option('ZD'+zdTxt(h),h));   // compact: the box is half a button wide
$('zd').onchange=()=>setCmd('zd',+$('zd').value);
// take the VMR's settings, except any this page changed in the last 2.5 s
const takeSet=(D)=>{for(const k in S)if(!(Date.now()-(busy[k]||0)<2500))S[k]=D[k];showSet();}

// ---- theme: Day (light) -> Night (dark) -> Red
const THEMES=['day','night','red'],TNAME={day:'Day',night:'Night',red:'Red'};
let theme=recall('theme')||(recall('night')==='1'?'red':'night');
const applyTheme=()=>{document.body.classList.toggle('day',theme==='day');document.body.classList.toggle('red',theme==='red');
 $('nb').textContent=TNAME[theme];draw(last);}
$('nb').onclick=()=>{theme=THEMES[(THEMES.indexOf(theme)+1)%3];store('theme',theme);applyTheme();drawPlot();};   // plot follows the theme
// ---- show / hide yaw (this phone only)
let showYaw=recall('showyaw')!=='0';
const setTog=(el,on)=>{el.classList.toggle('on',!!on);el.setAttribute('aria-pressed',on?'true':'false');};
const applyYaw=()=>{setTog($('showyaw'),showYaw);document.body.classList.toggle('noyaw',!showYaw);if(last)render(last);}
$('showyaw').onclick=()=>{showYaw=!showYaw;store('showyaw',showYaw?'1':'0');applyYaw();};
$('cb').onclick=()=>{
 const on=!(last&&last.charge);
 if(on&&!confirm('Enter charge mode?\n\nTurns off the OLED, logging'+(isLogging?' (logging will STOP)':'')+', GPS and IMU sensors, slows the CPU and lowers Wi-Fi power. This page keeps showing the battery.'))return;
 post('/set','charge='+(on?1:0)).catch(()=>{});};

// ---- live graph (card 2)
const tlab=s=>s===0?'now':s<60?'-'+s+'s':'-'+(s/60)+'m';
// Long views (5 and 10 min) are drawn as an amplitude envelope: the highest and lowest
// value within a sliding window of at least one roll (1.25 x the roll period, 8-30 s),
// so each roll's peak survives but the individual swings are not drawn.
const ENV_SECS=300;
const envWin=d=>d.rollT?Math.min(30,Math.max(8,1.25*d.rollT)):15;
const envelope=(arr,h)=>{const n=arr.length/2,up=new Array(n).fill(null),dn=new Array(n).fill(null);
 for(let i=0;i<n;i++){let a=Infinity,b=-Infinity;
  for(let j=Math.max(0,i-h);j<=Math.min(n-1,i+h);j++){const l=arr[2*j],u=arr[2*j+1];if(l!=null&&l<a)a=l;if(u!=null&&u>b)b=u;}
  if(arr[2*i]!=null&&b>=a){up[i]=b;dn[i]=a;}}
 return {up,dn,n};};

const draw=(d)=>{
 if(!d)return;const c=$('g'),r=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;
 if(c.width!==Math.round(w*r)||c.height!==Math.round(h*r)){c.width=Math.round(w*r);c.height=Math.round(h*r);}
 const x=c.getContext('2d');x.setTransform(r,0,0,r,0,0);x.clearRect(0,0,w,h);
 const cs=getComputedStyle(document.body),col=n=>cs.getPropertyValue(n).trim();
 if(gView!=='graph'){drawView(x,w,h,d,col);return;}
 const L=40,Rm=40,T=8,B=20,W=w-L-Rm,H=h-T-B,k=d.gK,nB=d.gB,dt=d.gDt;
 const scale=(hi,lo)=>v=>T+H*(hi-Math.max(lo,Math.min(hi,v)))/(hi-lo);
 const Yr=scale(S.gmax,S.gmin),Yp=scale(S.pmax,S.pmin),X=b=>L+W*b/Math.max(1,nB-1);
 x.font='11px system-ui,sans-serif';x.lineWidth=1;x.textBaseline='middle';
 const half=v=>Math.sign(v)*Math.round(Math.abs(v)/2);             // mid grid lines on whole degrees, same both sides
 for(const g of [S.gmax,half(S.gmax),0,half(S.gmin),S.gmin]){     // grid follows the roll (left) axis
  x.strokeStyle=col('--grid');x.beginPath();x.moveTo(L,Yr(g));x.lineTo(L+W,Yr(g));x.stroke();
  x.fillStyle=col('--roll');x.textAlign='right';x.fillText((g===0?'':g>0?'S ':'P ')+Math.abs(Math.round(g*10)/10)+'°',L-5,Yr(g));}
 x.fillStyle=col('--pitch');x.textAlign='left';x.strokeStyle=col('--pitch');
 for(const g of [S.pmax,half(S.pmax),0,half(S.pmin),S.pmin]){    // pitch (right) axis: ticks and labels
  x.beginPath();x.moveTo(L+W,Yp(g));x.lineTo(L+W+4,Yp(g));x.stroke();
  x.fillText((g>0?'+':g<0?'−':'')+Math.abs(Math.round(g*10)/10)+'°',L+W+6,Yp(g));}
 if(Math.abs(Yp(0)-Yr(0))>1){x.setLineDash([2,4]);x.beginPath();x.moveTo(L,Yp(0));x.lineTo(L+W,Yp(0));x.stroke();x.setLineDash([]);}
 x.fillStyle=col('--mut');
 const span=(nB-1)*dt,stp=span<=60?15:span<=120?30:span<=300?60:120;x.textBaseline='top';
 for(let s=0;s<=span+0.01;s+=stp){x.textAlign=s?'center':'right';x.fillText(tlab(s),X((nB-1)-s/dt),T+H+5);}
 const line=(arr,cname,Yf)=>{const nA=k===1?arr.length:arr.length/2,off=nB-nA;x.strokeStyle=col(cname);x.lineWidth=k===1?2:1.5;x.lineJoin='round';x.beginPath();let pen=false;
  arr.forEach((v,i)=>{if(v==null){pen=false;return;}const px=X(off+(k===1?i:Math.floor(i/2))),py=Yf(v);if(pen)x.lineTo(px,py);else{x.moveTo(px,py);pen=true;}});x.stroke();};
 const long=k>1&&d.gSecs>=ENV_SECS,hB=Math.max(1,Math.floor(Math.ceil(envWin(d)/dt)/2));   // window = 2*hB+1 columns, at least one roll
 const band=(arr,cname,Yf)=>{const e=envelope(arr,hB),off=nB-e.n,c=col(cname);
  for(let i=0;i<e.n;){if(e.up[i]==null){i++;continue;}let j=i;while(j+1<e.n&&e.up[j+1]!=null)j++;
   x.beginPath();for(let q=i;q<=j;q++)x.lineTo(X(off+q),Yf(e.up[q]));for(let q=j;q>=i;q--)x.lineTo(X(off+q),Yf(e.dn[q]));x.closePath();
   x.globalAlpha=0.22;x.fillStyle=c;x.fill();x.globalAlpha=1;
   x.strokeStyle=c;x.lineWidth=1.6;x.lineJoin='round';
   for(const side of [e.up,e.dn]){x.beginPath();for(let q=i;q<=j;q++)x.lineTo(X(off+q),Yf(side[q]));x.stroke();}
   x.setLineDash([4,4]);x.lineWidth=1;x.beginPath();   // middle of the band, smoothed over ~3 rolls (mean list / trim)
   for(let q=i;q<=j;q++){let sm=0,nm=0;for(let r=Math.max(i,q-3*hB);r<=Math.min(j,q+3*hB);r++){sm+=e.up[r]+e.dn[r];nm+=2;}x.lineTo(X(off+q),Yf(sm/nm));}
   x.stroke();x.setLineDash([]);
   i=j+1;}};
 if(long){band(d.gPitch,'--pitch',Yp);band(d.gRoll,'--roll',Yr);}
 else{line(d.gPitch,'--pitch',Yp);line(d.gRoll,'--roll',Yr);}
 const nt=['Roll (left axis): starboard plots up. Pitch (right axis): bow up plots up.'];
 if(long)nt.push('Long view: each band is the range over a sliding '+Math.round((2*hB+1)*dt)+' s window (at least one roll), so its edges are the peak angles; the dashed line is its middle (list / trim).');
 else if(k>1)nt.push('Each column shows the min-max over '+dt+' s.');
 $('gnote').textContent=nt.join(' ');
}
window.addEventListener('resize',()=>draw(last));

// ---- card 2 views: the live graph, or (behind the Views button) the OLED's clinometer,
//      stern view and sky plot, drawn here from the same live data
let gViews=recall('gviews')==='1',gView=recall('gview')||'graph';
const GV={graph:'Graph',clino:'Clinometer',stern:'Stern view',sky:'Sky plot',gmeter:'G-meter'};
const applyGView=()=>{if(!gViews||!GV[gView])gView='graph';$('gviews').hidden=!gViews;setTog($('gviewsb'),gViews);
 document.querySelectorAll('#gviews .tog').forEach(b=>setTog(b,b.dataset.v===gView));
 const g=gView==='graph';$('gttl').textContent=GV[gView];['gsecsc','glg','gaxt','gaxb'].forEach(i=>$(i).hidden=!g);
 $('vreset').hidden=gView!=='clino'&&gView!=='gmeter';$('vreset').textContent=gView==='clino'?'Reset drag pointers':'Reset peaks';
 gTrail.length=0;$('g').classList.toggle('tall',!g);draw(last);};
$('gviewsb').onclick=()=>{gViews=!gViews;store('gviews',gViews?'1':'0');applyGView();};
document.querySelectorAll('#gviews .tog').forEach(b=>b.onclick=()=>{gView=b.dataset.v;store('gview',gView);applyGView();});
$('vreset').onclick=()=>post('/set',gView==='clino'?'clinoreset=1':'greset=1').catch(()=>{});
const gTrail=[];   // G-meter: the dot's last positions on this page (one per update, ~5 s)
const DEG=Math.PI/180;
const drawView=(x,w,h,d,col)=>{
 const fg=col('--fg'),mut=col('--mut'),grid=col('--grid'),rc=col('--roll'),pc=col('--pitch');
 const txt=(t,X,Y,font,c,al,bl)=>{x.font=font;x.fillStyle=c;x.textAlign=al||'left';x.textBaseline=bl||'top';x.fillText(t,X,Y);};
 const rollTxt=v=>v==null?'--':Math.abs(v)<0.05?'0.0°':(v>0?'S ':'P ')+Math.abs(v).toFixed(1)+'°';
 if(gView==='clino'){
  // needle hangs from the top centre; starboard down swings it right (looking forward)
  const cx=w/2,cy=16,Rr=Math.min(h-38,(w/2-34)/Math.sin(40*DEG)),pt=(a,r)=>[cx+Math.sin(a*DEG)*r,cy+Math.cos(a*DEG)*r];
  x.strokeStyle=mut;x.lineWidth=1.5;x.beginPath();x.arc(cx,cy,Rr,50*DEG,130*DEG);x.stroke();
  for(let a=-40;a<=40;a+=5){const [x0,y0]=pt(a,Rr),[x1,y1]=pt(a,Rr-(a%10?6:12));x.beginPath();x.moveTo(x0,y0);x.lineTo(x1,y1);x.stroke();
   if(a%10===0){const [tx,ty]=pt(a,Rr+12);txt(a?(a>0?'S':'P')+Math.abs(a):'0',tx,ty,'11px system-ui,sans-serif',mut,'center','middle');}}
  const ptr=(a,c)=>{if(!a)return;a=Math.max(-42,Math.min(42,a));const [x0,y0]=pt(a,Rr-14),[x1,y1]=pt(a-2.5,Rr-26),[x2,y2]=pt(a+2.5,Rr-26);
   x.fillStyle=c;x.beginPath();x.moveTo(x0,y0);x.lineTo(x1,y1);x.lineTo(x2,y2);x.closePath();x.fill();};
  const cp=d.clino||[0,0];ptr(cp[0],pc);ptr(cp[1],pc);
  if(d.roll!=null){const [nx,ny]=pt(Math.max(-43,Math.min(43,d.roll)),Rr-6);x.strokeStyle=rc;x.lineWidth=3;x.lineCap='round';
   x.beginPath();x.moveTo(cx,cy);x.lineTo(nx,ny);x.stroke();x.lineCap='butt';}
  x.fillStyle=fg;x.beginPath();x.arc(cx,cy,5,0,2*Math.PI);x.fill();
  txt(rollTxt(d.roll),4,2,'bold 26px system-ui,sans-serif',rc);
  txt('drag pointers',w-4,4,'11px system-ui,sans-serif',mut,'right');
  txt('P '+Math.abs(cp[0]||0).toFixed(1)+'° · S '+(cp[1]||0).toFixed(1)+'°',w-4,18,'bold 14px system-ui,sans-serif',pc,'right');
  $('gnote').textContent='Like the bridge clinometer: the needle is the live roll; the triangles are drag pointers that stay at the largest roll each way since they were reset (here, or by holding A+C on the OLED clinometer screen). Scale ±40°.';
 }else if(gView==='stern'){
  // the hull seen from astern (starboard on the right) tilts with the live roll over a fixed sea line
  const wl=h*0.6,cx=(w-56)/2,k=Math.min((w-80)/80,h/48),th=(d.roll||0)*DEG,c=Math.cos(th),sn=Math.sin(th);
  const P=(px,py)=>[cx+(px*c-py*sn)*k,wl+(px*sn+py*c)*k];
  x.fillStyle=rc;x.globalAlpha=0.08;x.fillRect(0,wl,w-56,h-wl);x.globalAlpha=1;
  const poly=(pts,fill,stroke,alpha)=>{x.beginPath();pts.forEach(([a,b],i)=>{const [X,Y]=P(a,b);if(i)x.lineTo(X,Y);else x.moveTo(X,Y);});x.closePath();
   if(fill){x.globalAlpha=alpha||1;x.fillStyle=fill;x.fill();x.globalAlpha=1;}if(stroke){x.strokeStyle=stroke;x.lineWidth=2;x.stroke();}};
  poly([[-30,-9],[30,-9],[30,7],[26,12],[-26,12],[-30,7]],rc,rc,0.3);
  poly([[-9,-9],[-9,-20],[9,-20],[9,-9]],mut,fg,0.9);
  const [m0x,m0y]=P(0,-20),[m1x,m1y]=P(0,-27);x.strokeStyle=fg;x.lineWidth=2;x.beginPath();x.moveTo(m0x,m0y);x.lineTo(m1x,m1y);x.stroke();
  x.strokeStyle=mut;x.lineWidth=1.5;x.beginPath();x.moveTo(0,wl);x.lineTo(w-56,wl);x.stroke();
  // pitch bar: +/-6 deg
  const bx=w-30,t0=24,b0=h-16,mid=(t0+b0)/2,Yp=v=>mid-v*(b0-t0)/12;
  x.strokeStyle=mut;x.lineWidth=1;x.beginPath();x.moveTo(bx,t0);x.lineTo(bx,b0);x.stroke();
  for(let v=-6;v<=6;v+=2){x.beginPath();x.moveTo(bx-(v?5:9),Yp(v));x.lineTo(bx,Yp(v));x.stroke();txt((v>0?'+':v<0?'−':'')+Math.abs(v),bx+14,Yp(v),'10px system-ui,sans-serif',mut,'left','middle');}
  if(d.pitch!=null){const y=Yp(Math.max(-6.5,Math.min(6.5,d.pitch)));x.fillStyle=pc;x.beginPath();x.moveTo(bx+2,y);x.lineTo(bx+12,y-6);x.lineTo(bx+12,y+6);x.closePath();x.fill();}
  txt(rollTxt(d.roll),4,2,'bold 26px system-ui,sans-serif',rc);
  txt('pitch '+(d.pitch==null?'--':f(d.pitch)+'°'),w-44,4,'bold 14px system-ui,sans-serif',pc,'right');
  $('gnote').textContent='Seen from astern (starboard on the right): the hull tilts with the live roll at true scale. Right: pitch, ±6° (bow up = marker up).';
 }else if(gView==='gmeter'){
  // felt push in the deck plane (up = forward, right = starboard); vertical g on the bar at the right
  const g=d.gmet||{},pk=g.pk||[0,0,0,0,1,1],pmax=Math.max(pk[0],pk[1],pk[2],pk[3]),ring=pmax>0.3?0.2:0.1,full=3*ring;
  const R=Math.min(h/2-18,(w-130)/2),cx=R+16,cy=h/2+4,sc=v=>Math.max(-1.05,Math.min(1.05,v/full))*R;
  x.strokeStyle=grid;x.lineWidth=1;
  for(let k=1;k<=3;k++){x.beginPath();x.arc(cx,cy,R*k/3,0,2*Math.PI);x.stroke();
   txt((ring*k).toFixed(1)+' g',cx+R*k/3*0.71+3,cy-R*k/3*0.71-3,'10px system-ui,sans-serif',mut,'left','bottom');}
  x.beginPath();x.moveTo(cx-R,cy);x.lineTo(cx+R,cy);x.moveTo(cx,cy-R);x.lineTo(cx,cy+R);x.stroke();
  [['FWD',0,-1],['AFT',0,1],['P',-1,0],['S',1,0]].forEach(([t,a,b])=>txt(t,cx+a*(R+10),cy+b*(R+10),'bold 11px system-ui,sans-serif',mut,'center','middle'));
  // peak hold: a dashed diamond through the four peaks
  x.strokeStyle=pc;x.lineWidth=1.5;x.setLineDash([4,4]);x.beginPath();
  x.moveTo(cx,cy-sc(pk[0]));x.lineTo(cx+sc(pk[2]),cy);x.lineTo(cx,cy+sc(pk[1]));x.lineTo(cx-sc(pk[3]),cy);x.closePath();x.stroke();x.setLineDash([]);
  if(g.f!=null){const X=cx+sc(g.s),Y=cy-sc(g.f);
   if(!gTrail.length||gTrail[gTrail.length-1][2]!==d.unix+'/'+g.f){gTrail.push([X,Y,d.unix+'/'+g.f]);if(gTrail.length>10)gTrail.shift();}
   gTrail.forEach(([a,b],i)=>{x.globalAlpha=0.15+0.5*i/gTrail.length;x.fillStyle=rc;x.beginPath();x.arc(a,b,3,0,2*Math.PI);x.fill();});x.globalAlpha=1;
   x.fillStyle=rc;x.beginPath();x.arc(X,Y,7,0,2*Math.PI);x.fill();x.strokeStyle=fg;x.lineWidth=1.5;x.stroke();}
  // vertical g: 0.5 ... 1.5 g bar
  const bx=w-34,t0=22,b0=h-14,Yv=v=>b0-(Math.max(0.5,Math.min(1.5,v))-0.5)*(b0-t0);
  x.strokeStyle=mut;x.lineWidth=1;x.strokeRect(bx-6,t0,12,b0-t0);
  for(const v of [0.5,1,1.5]){x.beginPath();x.moveTo(bx-10,Yv(v));x.lineTo(bx+10,Yv(v));x.stroke();txt(v.toFixed(1),bx+13,Yv(v),'10px system-ui,sans-serif',mut,'left','middle');}
  x.fillStyle=pc;[pk[4],pk[5]].forEach(v=>x.fillRect(bx-8,Yv(v)-1,16,2));
  if(g.v!=null){x.fillStyle=rc;const y1=Yv(1),y2=Yv(g.v);x.fillRect(bx-5,Math.min(y1,y2),10,Math.abs(y2-y1)+1);}
  txt('vertical',bx,6,'10px system-ui,sans-serif',mut,'center');
  const lat=g.s==null?'--':(g.s>=0?'S ':'P ')+Math.abs(g.s).toFixed(2),lng=g.f==null?'--':(g.f>=0?'F ':'A ')+Math.abs(g.f).toFixed(2);
  txt(lat+' · '+lng+' g',4,2,'bold 18px system-ui,sans-serif',rc);
  txt('vertical '+(g.v==null?'--':g.v.toFixed(2)+' g'),4,24,'13px system-ui,sans-serif',fg);
  txt('peaks P '+pk[3].toFixed(2)+' S '+pk[2].toFixed(2)+' F '+pk[0].toFixed(2)+' A '+pk[1].toFixed(2)+' · V '+pk[5].toFixed(2)+'–'+pk[4].toFixed(2),4,h-14,'11px system-ui,sans-serif',mut);
  $('gnote').textContent='The push you feel, in the deck plane: up = forward, right = starboard. Heeled to starboard the dot sits to starboard (as if sliding down the deck); motion adds to it. Includes gravity, so it is what a person or loose gear feels. Dashed diamond / bar ticks: peaks since reset. Smoothed over 0.3 s.';
 }else{
  // sky plot: horizon at the edge, overhead in the middle, north up
  const R=Math.min(h/2-16,(w-160)/2),cx=R+14,cy=h/2+2;   // legend needs ~120 px on the right
  x.strokeStyle=grid;x.lineWidth=1;
  for(const el of [0,30,60]){x.beginPath();x.arc(cx,cy,R*(90-el)/90,0,2*Math.PI);x.stroke();}
  x.beginPath();x.moveTo(cx-R,cy);x.lineTo(cx+R,cy);x.moveTo(cx,cy-R);x.lineTo(cx,cy+R);x.stroke();
  [['N',0,-1],['E',1,0],['S',0,1],['W',-1,0]].forEach(([t,a,b])=>txt(t,cx+a*(R+9),cy+b*(R+9),'bold 11px system-ui,sans-serif',mut,'center','middle'));
  const sats=d.sky||[];let used=0;
  for(const [prn,el,az,snr,u] of sats){const rr=R*(90-el)/90,X=cx+Math.sin(az*DEG)*rr,Y=cy-Math.cos(az*DEG)*rr;
   const c=prn<=32?rc:prn>=65?pc:mut,rad=snr>=35?7:snr>=20?5:3;if(u)used++;
   x.beginPath();x.arc(X,Y,rad,0,2*Math.PI);if(u){x.fillStyle=c;x.fill();}else{x.strokeStyle=c;x.lineWidth=1.5;x.stroke();}
   txt(String(prn),X+rad+2,Y,'10px system-ui,sans-serif',mut,'left','middle');}
  const lx=cx+R+26;let ly=8;const line=(t,c,b)=>{txt(t,lx,ly,(b?'bold ':'')+'13px system-ui,sans-serif',c||fg);ly+=20;};
  if(!d.sky)line('waiting for satellite data',mut);
  line(sats.length+' seen',fg,1);line(used+' used',fg,1);line('best '+(d.snr||0)+' dB-Hz',mut);ly+=6;
  const key=(t,c,fill)=>{x.beginPath();x.arc(lx+6,ly+8,5,0,2*Math.PI);if(fill){x.fillStyle=c;x.fill();}else{x.strokeStyle=c;x.lineWidth=1.5;x.stroke();}txt(t,lx+16,ly+1,'12px system-ui,sans-serif',fg);ly+=18;};
  key('GPS',rc,1);key('GLONASS',pc,1);key('SBAS',mut,1);key('open = not used',mut,0);
  $('gnote').textContent='Every satellite the GPS reports, where it is in the sky: horizon at the edge, overhead in the middle, north up. Filled = used in the fix; bigger = stronger signal (20 and 35 dB-Hz steps). Updated every 5 s.';
 }};

// ---- main render: called for every /data reply (twice a second)
const render=(d)=>{
 takeSet(d.set);
 if(d.unix){const lt=new Date((d.unix-S.zd*3600)*1000),ut=new Date(d.unix*1000),iso=t=>t.toISOString();
  $('lt').textContent=iso(lt).slice(11,19);$('ltd').textContent=DAYS[lt.getUTCDay()]+' '+iso(lt).slice(0,10);
  $('utc').textContent='UTC '+iso(ut).slice(11,19)+' · '+iso(ut).slice(0,10)+' · time from '+(d.src==='phone'?'WiFi':d.src);}   // a phone or laptop on the hotspot
 else{$('lt').textContent='--:--:--';$('ltd').textContent='no time yet';$('utc').textContent='Waiting for GPS or RTC time';}
 const rate=d.battRate==null||Math.abs(d.battRate)<0.5?'':' '+(d.battRate>0?'+':'−')+Math.abs(d.battRate).toFixed(1)+'%/h';   // + = charging
 const bt=d.battV==null?'No battery':'Batt '+d.battV.toFixed(2)+'V '+Math.round(d.battPct)+'%'+rate;
 const ct=d.cpuT==null?'':' CPU '+Math.round(d.cpuT)+'°C';
 $('batt').textContent=bt+ct;
 $('chgbatt').textContent=d.battV==null?'No battery detected':d.battV.toFixed(2)+' V · '+Math.round(d.battPct)+'%'+(rate?' ·'+rate:'');
 $('chgcard').hidden=!d.charge;$('main').classList.toggle('charging',d.charge);
 $('cb').textContent=d.charge?'Exit charge':'Charge';$('cb').classList.toggle('on',d.charge);
 $('roll').innerHTML=d.roll==null?'--':Math.abs(d.roll)<0.05?'0.0<small>°</small>':Math.abs(d.roll).toFixed(1)+'<small>° '+(d.roll>0?'STBD':'PORT')+'</small>';
 $('pitch').innerHTML=d.pitch==null?'--':f(d.pitch)+'<small>° '+(d.pitch>0.05?'bow up':d.pitch<-0.05?'bow dn':'')+'</small>';
 $('yaw').innerHTML=d.yaw==null?'--':d.yaw.toFixed(1)+'<small>°</small>';
 $('rot').innerHTML=d.rot==null?'--':f(d.rot,0)+'<small>°/min</small>';
 $('zst').textContent=d.zero?('ON · level at roll '+side(d.zr,2)+'°, pitch '+f(d.zp,2)+'°'):'Off · raw sensor angles';
 $('zclr').disabled=!d.zero;
 $('zrv').textContent=side(d.roll)+(d.roll==null?'':'°');$('zpv').textContent=f(d.pitch)+(d.pitch==null?'':'°');
 $('rtp').textContent=d.rollT==null?'--':d.rollT.toFixed(1)+' s';
 $('rtpn').textContent=d.rollT==null?(d.roll==null?'':'need 3 full rolls'):'median of last '+d.rollTn+' rolls';
 draw(d);
 const m=d.m1,a=d.all,fv=(v,kk)=>kk==='r'?side(v):f(v,kk==='o'?0:1,kk!=='o');
 const rows=[['Roll to stbd',m.rMax,a.rMax,a.trMax,'r'],['Roll to port',m.rMin,a.rMin,a.trMin,'r'],
  ['Pitch + bow up',m.pMax,a.pMax,a.tpMax,'p'],['Pitch − bow dn',m.pMin,a.pMin,a.tpMin,'p'],
  ...(showYaw?[['|ROT| °/min',m.yr,a.yr,a.tyr,'o']]:[])];
 $('mx').innerHTML=rows.map(r=>{const none=(r[2]===0&&!r[3]);
  return '<tr><td>'+r[0]+'</td><td>'+fv(r[1],r[4])+'</td><td>'+(none?'--':fv(r[2],r[4]))+
  '</td><td class="w">'+(r[3]?r[3].slice(0,16):(none?'':'no clock'))+'</td></tr>';}).join('');
 $('pos').textContent=d.fix?dm(d.lat,'N','S')+'  '+dm(d.lon,'E','W'):'No GPS fix';
 $('gsog').innerHTML=d.sogAvg==null?'--':d.sogAvg.toFixed(1)+'<small> kn</small>';
 $('gcog').innerHTML=d.cogAvg==null?'--':((Math.round(d.cogAvg*10)/10)%360).toFixed(1).padStart(5,'0')+'<small>°T</small>';
 $('nav').textContent=d.fix?('Now: SOG '+d.sog.toFixed(1)+' kn ('+d.sogMs.toFixed(2)+' m/s) · COG '+String(Math.round(d.cog)%360).padStart(3,'0')+'°T · Alt '+(d.alt==null?'--':d.alt.toFixed(1)+' m')+' · '+d.sats+'/'+d.inView+' sats · HDOP '+(d.hdop==null?'--':d.hdop.toFixed(2))+' · '+(d.fixQ==2?'DGPS':'GPS')+' '+d.gpsHz+' Hz'):gpsWhy(d);
 renderStats(d);
 renderGDiag(d);
 renderGm(d);
 renderDock(d);
 renderDisp(d.disp);
 renderLog(d);
 renderPause(d);     // after renderLog: uses isLogging
 renderFw(d);
 if(Date.now()-serBusy>2000)setTog($('seroled'),!!d.serOled);
 renderProblems(d);
 sendPhoneTime(d);
 $('sysinfo').textContent=sysMsg||('Last restart: '+d.reset+' · build '+d.build);
}

// Why there is no fix: the module silent, or talking but not seeing enough sky
const gpsWhy=d=>{
 if(d.gpsAge==null||d.gpsAge>5)return 'No data from the GPS module'+(d.gpsAge==null?'':' for '+d.gpsAge+' s')+' — see GPS diagnostics (Developer Settings).';
 const sky=!d.inView?'no satellites in view yet':d.inView+' in view, strongest '+d.snr+' dB-Hz';
 return 'No fix · GPS is talking · '+sky+(d.inView&&d.snr<25?' (weak: needs open sky)':d.inView?'':' (indoors or antenna blocked?)')+' · '+d.sats+' used';};
// ---- developer settings (cards 12-16, below card 11; remembered per phone)
let devOn=recall('dev')==='1',probN=0;
const devBtn=()=>{const w=!devOn&&probN>0;$('devbtn').classList.toggle('warn',w);
 $('devbtn').textContent=(devOn?'Hide':'Enable')+' Developer Settings'+(w?' · ⚠ '+probN+' issue'+(probN>1?'s':''):'');};
const applyDev=()=>{$('dev').hidden=!devOn;devBtn();if(devOn){serPoll();if(last)renderGDiag(last);}};
$('devbtn').onclick=()=>{devOn=!devOn;store('dev',devOn?'1':'0');applyDev();};
// ---- serial monitor (card 13): polled once a second, only while shown
let serSeq=0,serLines=[],serTimer=null,serBusy=0;
const serPoll=async()=>{clearTimeout(serTimer);serTimer=null;if(!devOn)return;
 if(!document.hidden){try{const d=await (await fetch('/serial?since='+serSeq,{cache:'no-store'})).json();
  if(d.seq<serSeq)serLines=[];                                     // the VMR restarted
  serLines=serLines.concat(d.lines).slice(-10);serSeq=d.seq;
  $('serout').textContent=serLines.length?serLines.join('\n'):'(nothing printed yet)';}catch(e){}}
 serTimer=setTimeout(serPoll,1000);};
$('seroled').onclick=async()=>{const on=!$('seroled').classList.contains('on');serBusy=Date.now();setTog($('seroled'),on);   // latching
 try{await post('/set','serol='+(on?1:0));}catch(e){setTog($('seroled'),!on);alert('No response from the VMR');}};
// ---- GPS diagnostics (card 14)
const renderGDiag=d=>{if(!devOn||!d.gpsDiag)return;const g=d.gpsDiag;
 const talk=d.gpsAge==null?'SILENT since boot':d.gpsAge>5?'SILENT for '+d.gpsAge+' s':'talking ('+g.nmea.toLocaleString()+' sentences)';
 const fix=d.fix?(d.fixQ==2?'DGPS (SBAS)':'GPS')+' · '+d.sats+' satellites used':'none'+(g.fixAge!=null?' · last fix '+g.fixAge+' s ago':' since boot');
 const ant=['not reported','PROBLEM (short?)','internal','external (active)'][g.ant]||'?';
 const rows=[['Module',talk],['Fix',fix],['In view','GPS '+g.gp+' · GLONASS '+g.gl],['Strongest signal',(d.snr?d.snr+' dB-Hz'+(d.snr>=30?' (good)':d.snr>=25?' (fair)':' (weak)'):'none')],
  ['Antenna',ant],['HDOP',d.hdop==null?'--':d.hdop.toFixed(2)],['Rate',d.gpsHz+' Hz'],['Restarts (this boot)',g.restarts]];
 $('gdtab').innerHTML=rows.map(x=>'<tr><td>'+x[0]+'</td><td>'+esc(x[1])+'</td></tr>').join('');};
const gpsCmd=async(a,msg)=>{try{const d=await (await post('/gps','action='+a)).json();$('gdmsg').textContent=d.error||msg;}catch(e){$('gdmsg').textContent='No response from the VMR';}};
$('grst').onclick=()=>gpsCmd('restart','GPS set-up sent again. It keeps its saved satellite data, so a fix should come back quickly if the sky is clear.');
$('gcold').onclick=()=>{if(confirm('Cold start the GPS?\n\nIt forgets its saved satellites, time and position and searches from scratch. The first fix can take 5-15 minutes under open sky.'))
 gpsCmd('cold','Cold start sent. Leave it under open sky; the first fix can take 5-15 minutes.');};
// ---- level zero (card 7) and all-time maxima reset (card 15, developer settings)
const zeroCmd=async(body)=>{
 try{const d=await (await post('/zero',body)).json();if(d.error)alert(d.error);}
 catch(e){alert('No response from the VMR');}
}
$('zset').onclick=()=>{if(confirm('Set level zero now?\n\nThe roll and pitch averaged over the last 5 s become 0°. Do this only when the vessel is upright and steady: any list or trim right now is removed from all readings.'))zeroCmd('action=set');};
$('zclr').onclick=()=>{if(confirm('Clear level zero? Readings go back to raw sensor angles.'))zeroCmd('action=clear');};
document.querySelectorAll('[data-z]').forEach(b=>b.onclick=()=>zeroCmd('action=nudge&axis='+b.dataset.z+'&d='+b.dataset.d));
$('mreset').onclick=()=>{if(confirm('Reset ALL-TIME maxima? The stored record values and their dates are erased.'))post('/set','resetmax=1').catch(()=>{});};

// ---- OLED display (card 4) and the header / system buttons
const renderDisp=(D)=>{
 document.querySelectorAll('#scr button').forEach(b=>b.classList.toggle('sel',+b.dataset.n===D.scr));
 if(Date.now()-onBusy>2500){setTog($('don'),D.on);$('don').textContent=D.on?'On':'Off';}
 if(Date.now()-flipBusy>2500)setTog($('dflip'),D.flip);
 if(Date.now()-briBusy>2000){$('bri').value=D.pct;$('bpct').textContent=D.pct+'%';}
 $('dsrc').textContent=D.ok?('Brightness set by '+(D.src==='pot'?'the knob':'this page (turn the knob to take it back)')):'OLED not detected (I2C 0x3C). These controls have no effect until it is found.';
 $('dispcard').style.opacity=D.ok?'':'.6';
}
document.querySelectorAll('#scr button').forEach(b=>b.onclick=()=>{
 document.querySelectorAll('#scr button').forEach(x=>x.classList.toggle('sel',x===b));post('/set','screen='+b.dataset.n).catch(()=>{});});
let flipBusy=0,onBusy=0;
$('don').onclick=()=>{const on=!$('don').classList.contains('on');onBusy=Date.now();setTog($('don'),on);$('don').textContent=on?'On':'Off';post('/set','on='+(on?1:0)).catch(()=>{});};
$('dflip').onclick=()=>{const on=!$('dflip').classList.contains('on');flipBusy=Date.now();setTog($('dflip'),on);post('/set','flip='+(on?1:0)).catch(()=>{});};
$('lb').onclick=async()=>{
 if(!isLogging){alert('Datalogger mode only runs while recording: press START LOGGING first.');return;}
 if(!confirm('Datalogger mode?\n\nRecording continues. Wi-Fi and the OLED switch OFF to save battery, so this page will disconnect.\n\nTo get Wi-Fi and the screen back: press button B on the OLED, or the screen button.'))return;
 try{const d=await (await post('/logger','go=1')).json();if(d.error){alert(d.error);return;}
  $('main').classList.add('stale');sysMsg='Datalogger mode: Wi-Fi off. Press B or the screen button on the VMR, then rejoin the Wi-Fi "Vessel Motion Recorder".';$('sysinfo').textContent=sysMsg;}
 catch(e){alert('No response from the VMR');}
};
// ---- pause (top right): for moving the VMR
const setPause=async on=>{try{await post('/pause','on='+(on?1:0));}catch(e){alert('No response from the VMR');}};
$('pb').onclick=()=>{const on=!(last&&last.paused);
 if(on&&!confirm('Pause the VMR?\n\nFor moving it. Live values keep updating, but '+(isLogging?'no rows are logged (PAUSED and RESUMED are marked in the file), and ':'')+'maxima, roll period, roll statistics and the GM average stop counting.\n\nTap Resume (or press B on the VMR) once it is back in place.'))return;
 setPause(on);};
$('pres').onclick=()=>setPause(false);
const renderPause=d=>{const pz=!!d.paused;$('pausecard').hidden=!pz;$('main').classList.toggle('paused',pz);
 $('pb').textContent=pz?'Resume':'Pause';$('pb').classList.toggle('on',pz);if(!pz)return;
 const s=d.pausedS||0,t=s<3600?Math.floor(s/60)+':'+String(s%60).padStart(2,'0'):Math.floor(s/3600)+' h '+String(Math.floor(s/60)%60).padStart(2,'0')+' min';
 $('pauseinfo').textContent='For '+t+'. '+(isLogging?'No rows are being logged':'Nothing is being counted')+' until you resume (here, or button B on the VMR). If it is mounted differently now, set Level zero again first.';};

// ---- firmware update (card 16); the banner at the top when this build has no spare slot
$('otabanx').onclick=()=>{$('otaban').hidden=true;if(last)store('otaban',last.build);};
let fwBusy=false;
const renderFw=d=>{if(fwBusy)return;const ok=d.otaMax>0;$('fwform').hidden=!ok;
 $('otaban').hidden=ok||recall('otaban')===d.build;   // closed for this build
 $('fwinfo').textContent=ok?'Running build '+d.build+' · '+fmt(d.fwSize)+' (room for '+fmt(d.otaMax)+')'
  :'This firmware was flashed with a partition scheme that has no spare slot for updates. Flash it once over USB with Tools → Partition Scheme → "Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)"; after that, updates work from here.';};
$('fwfile').onchange=()=>{const f=$('fwfile').files[0],m=t=>{$('fwmsg').textContent=t;};$('fwgo').disabled=true;m('');if(!f)return;
 const n=f.name.toLowerCase();
 if(/\.(ino|txt|cpp|c|h)$/.test(n))return m('That is source code, and the VMR can\'t compile it. In the Arduino IDE use Sketch → Export Compiled Binary and pick VMR.ino.bin.');
 if(!n.endsWith('.bin'))return m('Pick the compiled .bin file (VMR.ino.bin).');
 if(/merged|bootloader|partitions|boot_app0/.test(n))return m('Wrong .bin: use VMR.ino.bin, the app image, not this one.');
 if(last&&last.otaMax&&f.size>last.otaMax)return m('Too large: '+fmt(f.size)+' (room for '+fmt(last.otaMax)+').');
 m(f.name+' · '+fmt(f.size));$('fwgo').disabled=false;};
$('fwgo').onclick=()=>{const f=$('fwfile').files[0];if(!f)return;
 if(!confirm('Install '+f.name+' ('+fmt(f.size)+')?\n\nThe VMR restarts when it is done (about 15 s). '+(isLogging?'Logging stops now and the new firmware starts not recording. ':'')+'Keep it powered and stay on the VMR Wi-Fi until then.'))return;
 const wasLog=isLogging;fwBusy=true;$('fwgo').disabled=true;$('fwfile').disabled=true;$('fwbar').hidden=false;$('fwbarv').style.width='0';
 const done=(ok,t)=>{$('fwmsg').textContent=t;if(!ok){fwBusy=false;$('fwfile').disabled=false;$('fwgo').disabled=false;$('fwbar').hidden=true;}};
 const fd=new FormData();fd.append('fw',f,f.name);const x=new XMLHttpRequest();
 x.open('POST','/ota?size='+f.size);x.timeout=300000;
 x.upload.onprogress=e=>{if(!e.lengthComputable)return;const p=Math.round(100*e.loaded/e.total);$('fwbarv').style.width=p+'%';
  $('fwmsg').textContent=p<100?'Uploading and writing… '+p+'%':'Checking the new firmware…';};
 x.onload=()=>{let r={};try{r=JSON.parse(x.responseText);}catch(e){}
  if(x.status===200&&r.ok)done(true,'Installed. The VMR is restarting; this page reloads by itself when it is back.');
  else done(false,'Update failed: '+(r.error||'HTTP '+x.status)+'. The old firmware is still running.'+(wasLog?' Logging was stopped: start it again in card 9.':''));};
 x.onerror=x.ontimeout=()=>done(false,'Upload failed (Wi-Fi dropped?). If the VMR does not restart, the old firmware is still running.');
 x.send(fd);};

$('i2cr').onclick=async()=>{
 if(!confirm('Reset the I2C bus?\n\nThe IMU and EEPROM are switched off and on. Roll and pitch drop out for about 2 s'+(isLogging?' (logging continues; those rows have no IMU data)':'')+'.'))return;
 $('i2cr').disabled=true;
 try{const d=await (await post('/i2creset','go=1')).json();if(d.error)alert(d.error);}catch(e){alert('No response from the VMR');}
 setTimeout(()=>{$('i2cr').disabled=false;},2000);
};
$('rst').onclick=async()=>{
 if(!confirm('Restart the VMR?\n\n'+(isLogging?'Logging stops and the file is closed first. ':'')+'Settings are saved. Wi-Fi drops for about 15 s; this page reconnects by itself.'))return;
 try{await post('/restart','go=1');sysMsg='Restarting… this page reconnects by itself.';$('sysinfo').textContent=sysMsg;}catch(e){}
};
let phoneSent=false;
const sendPhoneTime=(d)=>{
 if(phoneSent||d.gpsTime)return;phoneSent=true;
 const zd=Math.round(new Date().getTimezoneOffset()/60);   // JS offset: +min west of UTC = nautical ZD sign
 post('/phonetime','unix='+Math.floor(Date.now()/1000)+'&zd='+Math.max(-12,Math.min(12,zd))).catch(()=>{phoneSent=false;});
};
$('bri').oninput=()=>{briBusy=Date.now();$('bpct').textContent=$('bri').value+'%';
 clearTimeout(briTimer);briTimer=setTimeout(()=>post('/set','pct='+$('bri').value).catch(()=>{}),120);};

// ---- roll statistics (card 6)
let rsW=+(recall('rswin')||0);
const rsTabs=()=>document.querySelectorAll('#rswin button').forEach(b=>b.classList.toggle('sel',+b.dataset.w===rsW));
document.querySelectorAll('#rswin button').forEach(b=>b.onclick=()=>{rsW=+b.dataset.w;store('rswin',rsW);rsTabs();if(last)renderStats(last);});
rsTabs();
const renderStats=(d)=>{const r=d.rs&&d.rs[rsW];if(!r)return;const few=r.n<3;
 const rows=[['Significant roll (highest ⅓)',few?'--':'±'+r.sig.toFixed(1)+'°'],['Largest roll',few?'--':'±'+r.max.toFixed(1)+'°'],
  ['RMS roll (about the mean)',r.rms==null?'--':r.rms.toFixed(1)+'°'],['Mean list',r.mean==null?'--':side(r.mean)+'°'],
  ['Rolls counted',r.n+(few?' (need 3)':'')],['Beyond '+S.rthr+'°','S '+r.ovS+' · P '+r.ovP]];
 $('rstab').innerHTML=rows.map(x=>'<tr><td>'+x[0]+'</td><td>'+x[1]+'</td></tr>').join('');}

// ---- GM estimate (card 8)
let shipDirty=false;
const SHIP={beam:'sbeam',draft:'sdraft',len:'slen',lbp:'slbp'};
for(const k in SHIP)$(SHIP[k]).oninput=()=>{shipDirty=true;$('ssave').disabled=false;};
$('ssave').onclick=async()=>{const b=Object.keys(SHIP).map(k=>k+'='+(parseFloat($(SHIP[k]).value)||0)).join('&');
 $('ssave').disabled=true;try{await post('/set',b);shipDirty=false;}catch(e){$('ssave').disabled=false;alert('No response from the VMR');}};
const renderGm=(d)=>{const st=d.set,have=st.beam>0&&st.draft>0&&st.len>0;
 if(!shipDirty)for(const k in SHIP)if(document.activeElement!==$(SHIP[k]))$(SHIP[k]).value=st[k]>0?st[k]:'';
 const g=d.gmAvg!=null?d.gmAvg:d.gm;   // 10-min mean once it has a sample
 $('gmv').textContent=!have?'Enter ship particulars':g==null?'Waiting for roll period':'GM ≈ '+g.toFixed(2)+' m';
 $('gmt').textContent=!have?'':d.paused?'paused':d.gmLeft>0?'⏱ '+Math.floor(d.gmLeft/60)+':'+String(d.gmLeft%60).padStart(2,'0'):'10-min mean';
 $('gmrst').hidden=!have;
 $('gminfo').textContent=have?('Now '+(d.gm==null?'--':d.gm.toFixed(2)+' m')+' · T '+(d.rollT==null?'--':d.rollT.toFixed(1)+' s')+' · C '+(d.gmC==null?'--':d.gmC.toFixed(3))):'Beam, draft and length overall, in metres.';}
$('gmrst').onclick=async()=>{try{await post('/set','gmreset=1');}catch(e){alert('No response from the VMR');}};

// ---- at the dock (card 8b, behind the "At the dock" button; remembered per phone)
let dockOn=recall('dock')==='1';
const applyDock=()=>{setTog($('dockb'),dockOn);$('dockcard').hidden=!dockOn;};
$('dockb').onclick=()=>{dockOn=!dockOn;store('dock',dockOn?'1':'0');applyDock();if(last)renderDock(last);};
// feet-inches: "33-04", "33 04", "33'04\"", "33" -> inches; null if not a draft
const parseFtIn=t=>{const m=String(t).trim().match(/^(\d{1,2})(?:\s*['\-\s.]\s*(\d{1,2})\s*"?)?$/);if(!m)return null;
 const ft=+m[1],inch=m[2]==null?0:+m[2];return inch<12&&ft>0?ft*12+inch:null;};
const ftIn=i=>{const a=Math.round(Math.abs(i));return Math.floor(a/12)+'-'+String(a%12).padStart(2,'0');};
const M2IN=39.3701;
const renderDock=d=>{if(!dockOn||!d.dock)return;const k=d.dock,st=d.set;
 const tr=k.trim,ft=tr==null?null:tr*3.28084,side=v=>Math.abs(v)<0.05?'even keel':v>0?'by the stern':'by the head';
 $('dtrim').textContent=ft==null?'--':Math.abs(ft).toFixed(1)+' ft';
 $('dtrim2').textContent=ft==null?(k.L==null?'Enter the LOA or LBP in card 8':'Waiting for IMU data'):
  side(ft)+' · '+ftIn(tr*M2IN)+' · '+(k.ang>=0?'+':'')+k.ang.toFixed(2)+'°'+(k.trimNow!=null?' · now '+(k.trimNow*3.28084).toFixed(1)+' ft':'');
 $('dlist').textContent=k.list==null?'--':side2(k.list);
 const across=k.list!=null&&st.beam>0?st.beam*Math.tan(Math.abs(k.list)*Math.PI/180):null;
 $('dlist2').textContent=k.list==null?'':(across!=null?'≈ '+ftIn(across*M2IN)+' draft difference across the beam':'')+(k.listNow!=null?' · now '+side2(k.listNow):'');
 const Ltxt='L '+(k.L==null?'--':k.L.toFixed(1)+' m'+(st.lbp>0?' (LBP)':' (LOA − 5 m; enter the LBP for better)'));
 let c;
 if(k.cal){const defl=k.cm?((k.cf+k.ca)/2-k.cm):null;
  c='Calibrated'+(k.ct?' '+k.ct.slice(0,16)+' UTC':'')+' from F '+ftIn(k.cf)+(k.cm?' · M '+ftIn(k.cm):'')+' · A '+ftIn(k.ca)+
   ' (trim '+ftIn(k.ca-k.cf)+' '+(k.ca>=k.cf?'by the stern':'by the head')+
   (defl!=null?'; '+(Math.abs(defl)<0.25?'no hog or sag':(defl>0?'hog ':'sag ')+Math.abs(defl).toFixed(1)+' in')+'; quarter mean '+ftIn((k.cf+6*k.cm+k.ca)/8):'')+'). '+Ltxt+'.';}
 else c='Not calibrated: the trim follows level zero (card 7), so it is only as good as that. Read the marks and calibrate below for real feet. '+Ltxt+'.';
 $('dcal').textContent=c;
 const ready=k.n>=30&&k.L!=null;$('dcalgo').disabled=!ready;$('dcalgo').textContent=ready?'Calibrate':'Calibrate (needs '+(k.L==null?'the ship length':Math.max(0,30-k.n)+' s more data')+')';
 $('dcalclr').disabled=!k.cal;};
const side2=v=>Math.abs(v)<0.005?'0.00°':(v>0?'S ':'P ')+Math.abs(v).toFixed(2)+'°';
$('dcalgo').onclick=async()=>{const F=parseFtIn($('dfwd').value),A=parseFtIn($('daft').value),mt=$('dmid').value.trim(),M=mt?parseFtIn(mt):0;
 if(F==null||A==null||M==null){$('dmsg').textContent='Drafts as feet-inches, e.g. 33-04 (inches 0-11). Forward and aft are needed; midships is optional.';return;}
 if(!confirm('Calibrate the trim from these drafts?\n\nF '+ftIn(F)+(M?' · M '+ftIn(M):'')+' · A '+ftIn(A)+'\nTrim '+ftIn(A-F)+' '+(A>=F?'by the stern':'by the head')+'\n\nThe last minute of pitch becomes this trim. Read the marks with the ship steady.'))return;
 try{const r=await (await post('/trimcal','fwd='+F+'&mid='+M+'&aft='+A)).json();$('dmsg').textContent=r.error||'Calibrated.';}catch(e){$('dmsg').textContent='No response from the VMR';}};
$('dcalclr').onclick=async()=>{if(!confirm('Clear the trim calibration? The trim then follows level zero again.'))return;
 try{await post('/trimcal','clear=1');$('dmsg').textContent='Calibration cleared.';}catch(e){$('dmsg').textContent='No response from the VMR';}};

// ---- recording and event marker (card 9)
$('evbtn').onclick=async()=>{$('evbtn').disabled=true;
 try{const d=await (await post('/event','note='+encodeURIComponent($('evnote').value))).json();if(d.error)alert(d.error);else $('evnote').value='';}
 catch(e){alert('No response from the VMR');}};
$('evnote').onkeydown=e=>{if(e.key==='Enter'&&!$('evbtn').disabled)$('evbtn').click();};
const renderLog=(d)=>{
 const L=d.log,on=L.on;
 $('recst').className='recst'+(on?' on':'');
 $('recst').innerHTML=on?'<span class="dot"></span>RECORDING '+hms(L.sec):'Not recording';
 $('recinfo').textContent=on?(L.file?L.file+' · ':'')+L.rows+' rows · '+fmt(L.bytes)+(L.drop?' · '+L.drop+' rows dropped':'')+(d.sd?'':' · microSD NOT FOUND'):(d.sd?(L.intr?'The last recording was cut off by a restart or power loss. Nothing is being saved: press START to record again.':'Screens are live; nothing is being saved.'):'No microSD card');
 $('bstart').disabled=on;$('bstop').disabled=!on;$('evbtn').disabled=!on||d.paused;   // no event marks while paused
 if(on&&L.cond)$('recinfo').textContent+=' · '+L.cond;
 $('evlist').innerHTML=on&&L.ev&&L.ev.length?L.ev.map(e=>'Event '+e.n+(e.t?' · '+e.t.slice(11)+' UTC':'')+(e.note?' · '+esc(e.note):'')).join('<br>'):(on?'No events marked in this file yet.':'');
 if(isLogging!==on){isLogging=on;sdLockUi();loadFiles();}
}
const logCmd=async(st)=>{
 $('bstart').disabled=$('bstop').disabled=true;
 try{const d=await (await post('/log','state='+st)).json();if(d.error)alert(d.error);}
 catch(e){alert('No response from the VMR');}
}
// Start: ask for an optional file-name suffix. The time in the name is when START is tapped here.
const cleanTag=t=>t.replace(/ /g,'-').replace(/[^A-Za-z0-9_-]/g,'').slice(0,23);
let tagTimer=null;
const tagPreview=()=>{const t=cleanTag($('tagin').value),sfx=t?'_'+t:'';
 if(last&&last.unix){const u=new Date((last.unix+(Date.now()-lastRx)/1000)*1000).toISOString();
  $('tagprev').textContent=u.slice(0,4)+u.slice(5,7)+u.slice(8,10)+'_'+u.slice(11,13)+u.slice(14,16)+u.slice(17,19)+'Z'+sfx+'.csv';}
 else $('tagprev').textContent='NOCLK_nnn'+sfx+'.csv (no clock yet)';};
const tagClose=()=>{$('tagdlg').hidden=true;clearInterval(tagTimer);};
$('bstart').onclick=()=>{$('tagin').value='';const c=last&&last.set?last.set.cond:0;
 document.querySelectorAll('input[name=cond]').forEach(r=>r.checked=+r.value===c);$('conds').style.outline='';
 $('tagdlg').hidden=false;tagPreview();tagTimer=setInterval(tagPreview,250);$('tagin').focus();};
$('tagin').oninput=()=>{const c=cleanTag($('tagin').value);if(c!==$('tagin').value)$('tagin').value=c;tagPreview();};
$('tagin').onkeydown=e=>{if(e.key==='Enter')$('taggo').click();else if(e.key==='Escape')tagClose();};
$('tagcancel').onclick=tagClose;
$('taggo').onclick=()=>{const t=cleanTag($('tagin').value),c=document.querySelector('input[name=cond]:checked');
 if(!c){$('conds').style.outline='2px solid var(--warn)';return;}
 tagClose();logCmd('start&tag='+encodeURIComponent(t)+'&cond='+c.value);};
$('bstop').onclick=()=>{if(confirm('Stop logging? The file is closed. The next Start opens a new file.'))logCmd('stop');};

// ---- microSD files (card 10; download/delete only while stopped)
const sel=()=>[...document.querySelectorAll('#flist input:checked')].map(i=>i.dataset.n);
const selInfo=()=>{
 const s=sel(),tot=files.filter(x=>s.includes(x.n)).reduce((a,x)=>a+x.s,0);
 $('selinfo').textContent=files.length?(s.length?s.length+' selected · '+fmt(tot):'None selected'):'';
 $('bdl').disabled=$('bdel').disabled=!s.length||!!isLogging;
 $('bdla').disabled=$('bdela').disabled=!files.length||!!isLogging;
 $('sall').checked=files.length>0&&s.length===files.length;
}
const sdLockUi=()=>{$('sdlock').hidden=!isLogging;selInfo();
 document.querySelectorAll('#flist a').forEach(a=>a.style.visibility=isLogging?'hidden':'');}
// ---- plot from file (card 11): the VMR boils the file down to 600 columns of
// min / max / mean; the phone draws the image (long-press it, or Save image)
let plotData=null;
const PSEL=new Set((recall('plotsel')||'roll,pitch').split(',').filter(Boolean));
const PKEYS=['roll','pitch','batt','sog','rp','gm'];   // plottable series; also the order of d.mean / d.meanN
const anyPlot=()=>PKEYS.some(k=>PSEL.has(k));
document.querySelectorAll('#psel .tog').forEach(b=>{setTog(b,PSEL.has(b.dataset.k));b.onclick=()=>{const k=b.dataset.k;
 if(PSEL.has(k))PSEL.delete(k);else PSEL.add(k);setTog(b,PSEL.has(k));store('plotsel',[...PSEL].join(','));
 $('pgo').disabled=!files.length||!anyPlot();drawPlot();};});
const fillPlotFiles=()=>{const sel=$('pfile'),cur=sel.value;
 sel.innerHTML=files.map(x=>'<option value="'+esc(x.n)+'">'+esc(x.n)+(x.a?' (recording)':'')+' · '+fmt(x.s)+'</option>').join('');
 if(files.some(x=>x.n===cur))sel.value=cur;$('pgo').disabled=!files.length||!anyPlot();};
const niceStep=(span,u)=>{const raw=span/5;for(const m of [1,2,5,10,20,50,100,200,500])if(m*u>=raw)return m*u;return 1000*u;};
// what each panel shows: band = min..max shaded (+ optional mean); otherwise one mean line per column
// plot colours per theme (the image matches the page; Save image keeps them)
const PPAL={
 day:{bg:'#ffffff',fg:'#111827',mut:'#4b5563',grid:'#d1d5db',zero:'#9ca3af',frame:'#9ca3af',ev:'#6b7280',evt:'#374151',a:0.25,
  s:{roll:'#0277bd',pitch:'#d96c00',batt:'#15803d',sog:'#7c3aed',rp:'#0891b2',gm:'#b91c1c'}},
 night:{bg:'#131c2e',fg:'#e6edf7',mut:'#8a97ad',grid:'#26324a',zero:'#4a5a78',frame:'#3a4866',ev:'#8a97ad',evt:'#c9d3e3',a:0.35,
  s:{roll:'#4fc3f7',pitch:'#ffb74d',batt:'#4ade80',sog:'#b39dfa',rp:'#22d3ee',gm:'#f87171'}},
 red:{bg:'#0d0000',fg:'#e0281e',mut:'#8c1a14',grid:'#2a0505',zero:'#5a0f0b',frame:'#4a0c08',ev:'#8c1a14',evt:'#e0281e',a:0.35,
  s:{roll:'#e0281e',pitch:'#e0281e',batt:'#e0281e',sog:'#e0281e',rp:'#e0281e',gm:'#e0281e'}}};
const PSER=[
 {k:'roll',band:1,name:'Roll',u:1,lab:g=>g===0?'0°':(g>0?'S ':'P ')+Math.abs(g)+'°'},
 {k:'pitch',band:1,name:'Pitch',u:1,lab:g=>g===0?'0°':(g>0?'+':'−')+Math.abs(g)+'°'},
 {k:'batt',name:'Battery',unit:'V',u:0.1,d:2},
 {k:'sog',name:'SOG',unit:'kn',u:1,d:1,z:1},
 {k:'rp',name:'Roll period',unit:'s',u:1,d:1,z:1},
 {k:'gm',name:'GM estimate',unit:'m',u:0.1,d:2,z:1}];
const drawPlot=()=>{const d=plotData;if(!d||!anyPlot())return;
 const sel=PSER.filter(p=>PSEL.has(p.k)&&d[p.k]),mean=PSEL.has('mean');if(!sel.length)return;
 const nb=sel.filter(p=>p.band).length,BH=sel.length<=2?(sel.length===1?620:430):330,LH=240,GAP=36;
 const TT=34;   // title strip above each panel (the axes fit the data, so titles can't sit inside)
 const W=1600,top=130,H=top+sel.reduce((t,p)=>t+TT+(p.band?BH:LH),0)+GAP*(sel.length-1)+70;
 const c=document.createElement('canvas');c.width=W;c.height=H;const x=c.getContext('2d'),K=PPAL[theme]||PPAL.night;
 x.fillStyle=K.bg;x.fillRect(0,0,W,H);
 const zd=parseInt(d.zd);const ZD=isNaN(zd)?S.zd:zd;const t0=d.t0?Date.parse(d.t0):NaN,n=d.roll.length/3,dur=d.dur;
 const L=110,R=40,PW=W-L-R,X=sec=>L+PW*sec/Math.max(1,dur),XC=i=>X((i+0.5)*dur/n);
 const lt=sec=>new Date(t0+sec*1000-ZD*3600000).toISOString();
 x.fillStyle=K.fg;x.font='bold 32px system-ui,sans-serif';x.textBaseline='top';x.textAlign='left';
 x.fillText('VMR · '+d.file,L,24);
 x.font='24px system-ui,sans-serif';x.fillStyle=K.mut;
 const span=isNaN(t0)?'no clock: time from start of file':(lt(0).slice(0,16).replace('T',' ')+' – '+lt(dur).slice(0,16).replace('T',' ')+' LT (ZD '+(ZD>0?'+':'')+ZD+')');
 x.fillText([d.cond,d.name,span,d.rows.toLocaleString()+' rows'].filter(Boolean).join(' · '),L,68);
 const nl=sel.length-nb;   // panels drawn as lines (battery, SOG, roll period, GM)
 x.textAlign='right';
 x.fillText(mean?'bold line: 5-min mean · '+(nb?'shaded':'')+(nb&&nl?' / ':'')+(nl?'faint line':'')+': unsmoothed'
                :[nb?'shaded: full range':'',nl?'lines: column means':''].filter(Boolean).join(' · '),W-R,24);
 x.textAlign='left';
 const steps=[60,300,600,900,1800,3600,7200,10800,21600,43200,86400];const st=steps.find(s=>dur/s<=8)||86400;
 const lt0=(t0-ZD*3600000)/1000;
 let py=top;const pan=[];   // panel extents, for the event lines
 for(const sp of sel){py+=TT;pan.push([py,py+(sp.band?BH:LH)]);const PH=sp.band?BH:LH,a=d[sp.k];
  let mn=Infinity,mx=-Infinity;
  if(sp.band){for(let i=0;i<n;i++)if(a[3*i]!=null){mn=Math.min(mn,a[3*i]);mx=Math.max(mx,a[3*i+1]);}}
  else for(const v of a)if(v!=null){mn=Math.min(mn,v);mx=Math.max(mx,v);}
  const has=isFinite(mn);if(!has){mn=0;mx=sp.u;}
  // axis fits the data: max rounded up, min rounded down, to the series' unit (whole degrees etc.)
  let yTop=Math.round(Math.ceil(mx/sp.u-1e-9)*sp.u*100)/100;
  let yBot=Math.round(Math.floor(mn/sp.u+1e-9)*sp.u*100)/100;if(sp.z&&yBot<0)yBot=0;
  if(yTop<=yBot)yTop=Math.round((yBot+sp.u)*100)/100;               // flat data: one unit of room
  const Y=v=>py+PH*(yTop-v)/(yTop-yBot),gs=niceStep(yTop-yBot,sp.u),dp=sp.u<1?(gs<0.1?2:1):0;
  const lab=sp.lab||(g=>g.toFixed(dp));
  const C=K.s[sp.k];x.font='22px system-ui,sans-serif';x.fillStyle=K.mut;x.textAlign='right';x.textBaseline='middle';
  const ticks=new Set([yTop,yBot]);for(let g=Math.ceil(yBot/gs-1e-9)*gs;g<=yTop+1e-9;g+=gs)ticks.add(Math.round(g*100)/100);
  for(const g of [...ticks].sort((p,q)=>q-p)){
   if(g!==yTop&&g!==yBot&&(Math.abs(g-yTop)<gs*0.6||Math.abs(g-yBot)<gs*0.6))continue;   // keep labels apart
   x.strokeStyle=g===0&&sp.band?K.zero:K.grid;x.lineWidth=g===0&&sp.band?2:1;x.beginPath();x.moveTo(L,Y(g));x.lineTo(L+PW,Y(g));x.stroke();x.fillText(lab(g),L-12,Y(g));}
  x.strokeStyle=K.grid;x.lineWidth=1;
  if(!isNaN(t0))for(let k=Math.ceil(lt0/st)*st;k<=lt0+dur;k+=st){const xx=X(k-lt0);x.beginPath();x.moveTo(xx,py);x.lineTo(xx,py+PH);x.stroke();}
  if(sp.band){
   // band = min..max; columns shorter than ~15 s are widened to a 15 s window so it shows the peaks, not a comb
   const hb=Math.max(0,Math.ceil((15/(dur/n)-1)/2)),lo=[],hi=[];
   for(let i=0;i<n;i++){let l=Infinity,h=-Infinity;for(let q=Math.max(0,i-hb);q<=Math.min(n-1,i+hb);q++)if(a[3*q]!=null){l=Math.min(l,a[3*q]);h=Math.max(h,a[3*q+1]);}
    lo.push(a[3*i]==null?null:l);hi.push(a[3*i]==null?null:h);}
   x.fillStyle=C;x.globalAlpha=K.a;
   for(let i=0;i<n;){if(lo[i]==null){i++;continue;}let j=i;while(j+1<n&&lo[j+1]!=null)j++;
    x.beginPath();for(let q=i;q<=j;q++)x.lineTo(XC(q),Y(hi[q]));for(let q=j;q>=i;q--)x.lineTo(XC(q),Y(lo[q]));x.closePath();x.fill();i=j+1;}
   x.globalAlpha=1;
   if(mean){const hw=Math.round(150/(dur/n));x.strokeStyle=C;x.lineWidth=3;x.lineJoin='round';x.beginPath();let pen=false;
    for(let i=0;i<n;i++){if(a[3*i+2]==null){pen=false;continue;}let sm=0,k=0;
     for(let q=Math.max(0,i-hw);q<=Math.min(n-1,i+hw);q++)if(a[3*q+2]!=null){sm+=a[3*q+2];k++;}
     if(pen)x.lineTo(XC(i),Y(sm/k));else{x.moveTo(XC(i),Y(sm/k));pen=true;}}x.stroke();}
  }else{
   // column means; with "5-min mean" on they are drawn faint and the 5-min average bold on top
   const hw=Math.round(150/(dur/n)),seg=(val,wid,alpha)=>{x.strokeStyle=C;x.lineWidth=wid;x.globalAlpha=alpha;x.lineJoin='round';x.beginPath();let pen=false;
    for(let i=0;i<n;i++){const v=val(i);if(v==null){pen=false;continue;}if(pen)x.lineTo(XC(i),Y(v));else{x.moveTo(XC(i),Y(v));pen=true;}}x.stroke();x.globalAlpha=1;};
   seg(i=>a[i],mean?1.5:3,mean?0.35:1);
   if(mean)seg(i=>{if(a[i]==null)return null;let sm=0,k=0;for(let q=Math.max(0,i-hw);q<=Math.min(n-1,i+hw);q++)if(a[q]!=null){sm+=a[q];k++;}return sm/k;},3.5,1);}
  x.fillStyle=K.fg;x.font='bold 24px system-ui,sans-serif';x.textAlign='left';x.textBaseline='bottom';
  // mean over the whole span (every logged value, not the columns), shown once there is
  // at least a minute of it: 600 rows at 10 Hz, or 60 GPS fixes for SOG
  const mi=PKEYS.indexOf(sp.k),mv=d.mean?d.mean[mi]:null,mN=d.meanN?d.meanN[mi]:0,mOk=mv!=null&&mN>=(sp.k==='sog'?60:600);
  if(mOk&&(sp.k==='rp'||sp.k==='gm')){   // roll period and GM: the whole-span mean as a dashed line across the panel
   const ym=Y(mv),lab='mean '+mv.toFixed(sp.d)+' '+sp.unit;
   x.strokeStyle=K.fg;x.globalAlpha=0.8;x.setLineDash([14,8]);x.lineWidth=2;x.beginPath();x.moveTo(L,ym);x.lineTo(L+PW,ym);x.stroke();x.setLineDash([]);x.globalAlpha=1;
   x.font='bold 20px system-ui,sans-serif';const tw=x.measureText(lab).width,ty=ym-PH*0.08<py+24?ym+6:ym-30;
   x.fillStyle=K.bg;x.fillRect(L+PW-tw-16,ty,tw+12,24);x.fillStyle=K.fg;x.textAlign='right';x.textBaseline='top';x.fillText(lab,L+PW-10,ty+2);
   x.font='bold 24px system-ui,sans-serif';x.textAlign='left';x.textBaseline='bottom';}
  let ttl;
  if(!has)ttl=sp.name+' · no data in this file';
  else if(sp.k==='roll')ttl='Roll (°) · max S '+Math.max(0,mx).toFixed(1)+'° · P '+Math.max(0,-mn).toFixed(1)+'°'+(mOk?' · mean '+side(mv)+'° (list)':'');
  else if(sp.k==='pitch')ttl='Pitch (°) · max up '+Math.max(0,mx).toFixed(1)+'° · down '+Math.max(0,-mn).toFixed(1)+'°'+(mOk?' · mean '+f(mv)+'° (trim, + bow up)':'');
  else ttl=sp.name+' ('+sp.unit+') · '+mn.toFixed(sp.d)+' – '+mx.toFixed(sp.d)+(mOk?' · mean '+mv.toFixed(sp.d):'');
  x.fillText(ttl,L,py-6);
  x.strokeStyle=K.frame;x.lineWidth=1.5;x.strokeRect(L,py,PW,PH);
  py+=PH+GAP;}
 const bottom=py-GAP;x.fillStyle=K.mut;x.font='22px system-ui,sans-serif';x.textAlign='center';x.textBaseline='top';
 if(!isNaN(t0)){let lastDay='';
  for(let k=Math.ceil(lt0/st)*st;k<=lt0+dur;k+=st){const iso=new Date(k*1000).toISOString(),day=iso.slice(5,10);
   x.fillText(iso.slice(11,16),X(k-lt0),bottom+8);if(day!==lastDay){x.fillText(day,X(k-lt0),bottom+34);lastDay=day;}}}
 else{for(let k=0;k<=dur;k+=st)x.fillText('+'+(k/3600).toFixed(k%3600?1:0)+' h',X(k),bottom+8);}
 x.strokeStyle=K.ev;x.setLineDash([8,8]);x.lineWidth=2;x.fillStyle=K.evt;x.font='bold 20px system-ui,sans-serif';x.textBaseline='bottom';
 for(const e of d.ev){const xx=X(e[0]);for(const [y0,y1] of pan){x.beginPath();x.moveTo(xx,y0);x.lineTo(xx,y1);x.stroke();}x.fillText('E'+e[1],xx,top-2);}
 x.setLineDash([]);
 const url=c.toDataURL('image/png');$('pimg').src=url;$('pimg').hidden=false;
 $('psave').href=url;$('psave').download=d.file.replace(/\.csv$/i,'')+'_'+(d.part?Math.round(dur/3600)+'h':'all')+'.png';$('psave').hidden=false;
 $('pmsg').textContent=(d.ev.length?d.ev.map(e=>'E'+e[1]+': '+(e[2]||'(no note)')).join(' · ')+'. ':'')+'Long-press the image to save it, or tap Save image.';};
$('pgo').onclick=async()=>{const f=$('pfile').value;if(!f)return;$('pgo').disabled=true;$('pmsg').textContent='Starting…';
 try{const r=await (await post('/plot','file='+encodeURIComponent(f)+'&hours='+$('prange').value)).json();if(r.error){$('pmsg').textContent=r.error;$('pgo').disabled=false;return;}}
 catch(e){$('pmsg').textContent='No response from the VMR';$('pgo').disabled=false;return;}
 const t0=Date.now();
 const wait=async()=>{try{const d=await (await fetch('/plot',{cache:'no-store'})).json();
   if(d.roll){plotData=d;$('pgo').disabled=false;drawPlot();return;}
   if(d.state===3){$('pmsg').textContent='Plot failed: '+d.err;$('pgo').disabled=false;return;}
   if(d.state===0){$('pmsg').textContent='The plot was interrupted (VMR restarted?). Try again.';$('pgo').disabled=false;return;}
   $('pmsg').textContent='Reading the file… '+d.pct+'% ('+Math.round((Date.now()-t0)/1000)+' s). Logging and the live data carry on.';}catch(e){}
  setTimeout(wait,1000);};
 wait();};
const loadFiles=async()=>{
 try{
  const d=await (await fetch('/files',{cache:'no-store'})).json();
  if(!d.sd){files=[];$('sdinfo').textContent=d.busy?'Card busy, tap Refresh':'No microSD card';$('flist').innerHTML='';selInfo();return;}
  files=d.files.sort((a,b)=>a.n<b.n?1:-1);
  $('sdinfo').textContent=files.length+' file'+(files.length===1?'':'s')+' · '+fmt(d.used)+' used of '+fmt(d.total);
  $('flist').innerHTML=files.length?files.map(x=>'<label class="f"><input type="checkbox" data-n="'+esc(x.n)+'"><span>'+esc(x.n)+(x.a?'<em>REC</em>':'')+'</span><span class="fs">'+fmt(x.s)+'</span><a href="/dl?f='+encodeURIComponent(x.n)+'" download="'+esc(x.n)+'" aria-label="Download '+esc(x.n)+'">⬇</a></label>').join(''):'<div class="sub" style="padding:8px 0">No files</div>';
  sdLockUi();fillPlotFiles();
 }catch(e){$('sdinfo').textContent='Could not read the file list (download in progress?)';}
}
$('flist').addEventListener('change',selInfo);
$('sall').onchange=()=>{document.querySelectorAll('#flist input').forEach(i=>i.checked=$('sall').checked);selInfo();};
const go=url=>{if(isLogging)return;const a=document.createElement('a');a.href=url;a.download='';document.body.appendChild(a);a.click();a.remove();
 $('sdmsg').textContent='Download started. Live values pause until it finishes.';}
const one=n=>'/dl?f='+encodeURIComponent(n);
$('bdl').onclick=()=>{const s=sel();if(!s.length)return;go(s.length===1?one(s[0]):'/zip?'+s.map(n=>'f='+encodeURIComponent(n)).join('&'));};
$('bdla').onclick=()=>{if(!files.length)return;go(files.length===1?one(files[0].n):'/zip?all=1');};
const del=async(names,all)=>{
 $('sdmsg').textContent='Deleting…';$('bdel').disabled=$('bdela').disabled=true;
 try{
  const d=await (await post('/delete',all?'all=1':names.map(n=>'f='+encodeURIComponent(n)).join('&'))).json();
  $('sdmsg').textContent='Deleted '+d.deleted+' file'+(d.deleted===1?'':'s')+(d.failed.length?' · could not delete: '+d.failed.join(', '):'');
 }catch(e){$('sdmsg').textContent='Delete failed: no response from the VMR';}
 loadFiles();
}
$('bdel').onclick=()=>{
 const s=sel();if(!s.length)return;
 const t=files.filter(x=>s.includes(x.n)).reduce((a,x)=>a+x.s,0);
 if(confirm('Delete '+s.length+' file'+(s.length===1?'':'s')+' ('+fmt(t)+') from the microSD card?\nThis cannot be undone.'))del(s,false);
}
$('bdela').onclick=()=>{
 if(!files.length)return;
 const t=files.reduce((a,x)=>a+x.s,0);
 const ans=prompt('Delete ALL '+files.length+' files ('+fmt(t)+') from the microSD card?\nThis cannot be undone.\n\nType DELETE to confirm.');
 if(ans!==null){if(ans.trim().toUpperCase()==='DELETE')del([],true);else $('sdmsg').textContent='Not deleted (confirmation text did not match).';}
}
$('sdr').onclick=loadFiles;

// ---- hardware check (card 12, developer settings): crash restart, I2C faults, IMU recoveries
const renderProblems=(d)=>{
 const crashed=/^(CRASH|BROWN)/.test(d.reset),t=[];
 if(crashed){t.push('Last restart: '+d.reset);if(d.crash)t.push(d.crash);}
 if(d.i2cMiss)t.push('I2C devices not answering: '+d.i2cMiss);
 if(d.i2cNote)t.push('I2C '+d.i2cNote);
 if(!d.imuOk&&!d.charge)t.push('IMU: no data right now');   // not read in charge mode
 if(!d.charge&&(d.gpsAge==null||d.gpsAge>5))t.push('GPS module not sending data'+(d.gpsAge==null?' since boot':' for '+d.gpsAge+' s')+' (card 14: Restart GPS)');
 if(d.imuSoft)t.push('IMU went quiet and its reports were re-requested '+d.imuSoft+' time'+(d.imuSoft===1?'':'s')+' since boot');
 if(d.imuRec)t.push('I2C bus reset (IMU power-cycled) '+d.imuRec+' time'+(d.imuRec===1?'':'s')+' since boot, including any from the Reset I2C bus button');
 probN=t.length;devBtn();$('probcard').classList.toggle('crash',t.length>0);
 $('probttl').textContent=crashed?'The VMR restarted after a problem':'Hardware check';
 $('probtxt').textContent=t.length?t.join('\n'):'No problems found.';
}

// ---- polling
// Wi-Fi health (card 15): signal at the VMR, plus this page's own update timing over the last 30 s
const link=[];
const rssiTxt=v=>v+' dBm ('+(v>=-60?'good':v>=-70?'fair':v>=-80?'weak':'poor')+')';
const renderWifi=d=>{const w=d.wifi||{rssi:[]},ok=link.filter(x=>x.ok),miss=link.length-ok.length;
 const a=w.me!=null?'This phone '+rssiTxt(w.me):w.rssi.length?'Weakest phone '+rssiTxt(Math.min(...w.rssi)):'No phones listed';
 const n=w.rssi.length+' phone'+(w.rssi.length===1?'':'s')+' connected';
 let b='';if(ok.length){const ms=ok.reduce((s,x)=>s+x.ms,0)/ok.length,kb=ok.reduce((s,x)=>s+x.b,0)/ok.length/1024;
  b='Updates: '+Math.round(ms)+' ms · '+kb.toFixed(1)+' KB each · ≈'+Math.round(kb/(ms/1000))+' KB/s · '+Math.round(100*miss/link.length)+'% missed';}
 $('wifiinfo').innerHTML=esc(a)+' · '+esc(n)+(b?'<br>'+esc(b):'');};
const poll=async()=>{
 const t0=performance.now(),ac=new AbortController(),to=setTimeout(()=>ac.abort(),3000);
 try{const r=await fetch(gView==='sky'?'/data?sky=1':'/data',{cache:'no-store',signal:ac.signal}),txt=await r.text();clearTimeout(to);
  link.push({ok:true,ms:performance.now()-t0,b:txt.length});if(link.length>60)link.shift();
  const d=JSON.parse(txt);
  if(down){down=false;sysMsg=null;}
  if(build===null)build=d.build;else if(d.build!==build){location.reload();return;}   // firmware was updated
  last=d;lastRx=Date.now();render(d);renderWifi(d);$('main').classList.remove('stale');}
 catch(e){clearTimeout(to);link.push({ok:false});if(link.length>60)link.shift();down=true;$('main').classList.add('stale');}
 setTimeout(poll,500);
}
showSet();
applyTheme();
applyYaw();
applyDev();
applyGView();
applyDock();
poll();
</script></body></html>
)HTML";