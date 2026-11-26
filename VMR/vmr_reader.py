# SPDX-FileCopyrightText: 2026 Aspect Wave LLC
# SPDX-License-Identifier: MIT
"""
vmr_reader.py - read Vessel Measurement Recorder (VMR) CSV files into pandas.

    from vmr_reader import read_vmr, events, fixes, waves, slow, summary

    df = read_vmr("20261108_042800Z_Overnight.csv")
    df.attrs["meta"]            # the file's "# key,value" header lines, as a dict
    df["roll_deg"]              # 10 Hz roll, + = starboard down
    fixes(df)                   # only the rows with a new GPS fix
    events(df)                  # marks, casts, pauses, set-up changes ...
    waves(df)                   # the wave / GM estimates (every 30 s)
    slow(df)                    # one row a second: battery, weather, roll period, GM ...
    ctd(df)                     # SeaLabMOS: the WTB's 1-s CTD means (iwb_*), with the session state
    print(summary(df))

Command line:  python vmr_reader.py FILE.csv [FILE2.csv ...]   (prints a summary of each)

What it does for you
  - Skips the "#" metadata lines and keeps them in df.attrs["meta"].
  - Index: utc_time as a UTC timestamp. Adds local_time (LT = UTC - ZD, from the
    file's zone_description) when the file has one.
  - Slow columns (battery, level zero, roll period, GM, add-on weather, mag_cal,
    box_temp_c) are written once a second and blank in between, meaning
    "unchanged". With fill_slow=True (the default) each row gets the value in force.
    A blank on a row where they were written stays blank (no value).
  - mag_var_deg is written on the first row and when it changes; it is filled forward too.
  - GPS columns are filled only on the first row after each new fix and are left
    blank in between, so no fix is ever counted twice. Use fixes(df) for them.
  - SeaLabMOS: the iwb_* columns are the WTB's 1-s means, on the
    row after the second they cover and never repeated; iwb_state is on every row while
    SeaLabMOS is on. batt_a (12 V battery, INA260) is written with batt_v.

Needs pandas. Written for the VMR by Aspect Wave LLC and Claude (Anthropic), 2026.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

import pandas as pd

__all__ = ["read_vmr", "read_many", "read_marks", "read_meta",
           "events", "fixes", "waves", "slow", "ctd", "summary", "SLOW_COLUMNS", "IWB_COLUMNS", "UNITS"]

# Written together once a second and on change, blank between
SLOW_COLUMNS = ["batt_v", "batt_pct", "zero_applied", "roll_period_s", "gm_ft",
                "pressure_mbar", "air_temp_c", "rh_pct", "mag_cal", "box_temp_c", "batt_a", "batt_gauge_pct"]
# SeaLabMOS: the WTB's 1-s means, on the row after each second, never repeated (not filled)
IWB_COLUMNS = ["iwb_sal_psu", "iwb_temp_c", "iwb_pres_dbar", "iwb_cpu_c", "iwb_depth_m", "iwb_ec_uscm",
               "iwb_rho_kgm3", "iwb_int_t_c", "iwb_int_rh_pct", "iwb_dewpt_c", "iwb_v"]
# Filled only on the first row after each new GPS fix
GPS_COLUMNS = ["gps_lat", "gps_lon", "gps_alt_m", "gps_speed_ms", "gps_speed_kn",
               "gps_angle_deg", "gps_fix", "gps_sats", "gps_hdop", "gps_fix_quality", "gps_age_ms"]
WAVE_COLUMNS = ["wave_te_s", "wave_tr_s",
                "heave_hs_m", "heave_tp_s", "roll_tp_s", "heave_hs_mid_m",
                "baro_hs_m", "baro_hs_mid_m", "baro_tp_s",          # baro_: from the pressure
                "fused_hs_m", "fused_hs_mid_m", "fused_coh", "baro_gain"]   # fused_: the two together
WIND_COLUMNS = ["wind_app_deg", "wind_app_kn", "wind_true_deg", "wind_true_kn"]
TEXT_COLUMNS = ["event_note", "iwb_state"]

# Units by column (the CSV keeps these whatever the VMR's display units are set to)
UNITS = {
    "roll_deg": "deg (+ starboard down)", "pitch_deg": "deg (+ bow up)",
    "yaw_rate_dpm": "deg/min (+ to starboard)", "gps_lat": "deg", "gps_lon": "deg",
    "gps_alt_m": "m", "gps_speed_ms": "m/s", "gps_speed_kn": "kn", "gps_angle_deg": "deg T (COG)",
    "gps_age_ms": "ms", "batt_v": "V", "batt_pct": "%", "roll_period_s": "s", "gm_ft": "ft",
    "acc_fwd_g": "g", "acc_stbd_g": "g", "acc_up_g": "g", "pressure_mbar": "mbar (at the sensor)",
    "air_temp_c": "degC", "rh_pct": "%", "trim_ft": "ft (+ by the stern)", "list_deg": "deg (+ starboard)",
    "wave_te_s": "s", "wave_tr_s": "s", "heave_hs_m": "m", "heave_tp_s": "s", "roll_tp_s": "s",
    "heave_hs_mid_m": "m", "mag_heading_deg": "deg M", "mag_field_ut": "uT",
    "wind_app_deg": "deg from, off the bow", "wind_app_kn": "kn", "wind_true_deg": "deg T from",
    "wind_true_kn": "kn", "mag_var_deg": "deg (+ east)", "box_temp_c": "degC (inside the box)",
    "baro_hs_m": "m (heave Hs from the pressure)", "baro_hs_mid_m": "m (the same, at midships)", "baro_tp_s": "s",
    "fused_hs_m": "m (heave Hs, motion and pressure fused)", "fused_hs_mid_m": "m (the same, at midships)",
    "fused_coh": "0-1 (coherence of the two, motion-weighted)", "baro_gain": "x (the pressure's height over the motion's, 12 s and shorter)",
    "batt_a": "A (12 V battery: + charging, - drawing)",
    "batt_gauge_pct": "% (the LiPo fuel gauge's own; batt_pct is from the voltage)",
    "iwb_sal_psu": "PSS-78", "iwb_temp_c": "degC (water, TSYS01)", "iwb_pres_dbar": "dbar (sea pressure)",
    "iwb_cpu_c": "degC (IWB processor)", "iwb_depth_m": "m (TEOS-10)", "iwb_ec_uscm": "uS/cm (raw EZO-EC)",
    "iwb_rho_kgm3": "kg/m3 (in-situ density, TEOS-10)", "iwb_int_t_c": "degC (IWB tube air)", "iwb_int_rh_pct": "% (IWB tube air)",
    "iwb_dewpt_c": "degC (IWB tube air: the leak sign)", "iwb_v": "V (supply at the IWB)", "iwb_cast": "cast number (open cast)",
}



def read_meta(path) -> dict:
    """The '# key,value' lines at the top of a VMR file, as a dict (values are strings)."""
    meta = {}
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if not line.startswith("#"):
                break
            body = line[1:].strip()
            if not body:
                continue
            key, _, val = body.partition(",")
            if not _ and "title" not in meta:   # the first line has no comma: the system name
                meta["title"] = key
            else:
                meta[key.strip()] = val.strip()
    return meta


def _zd_hours(meta: dict):
    """Zone description in hours from '+8 (UTC = LT + ZD)', or None."""
    m = re.match(r"\s*([+-]?\d+)", meta.get("zone_description", ""))
    return int(m.group(1)) if m else None


def read_vmr(path, fill_slow: bool = True, local_time: bool = True) -> pd.DataFrame:
    """Read one VMR recording (CSV) into a DataFrame indexed by UTC time.

    fill_slow  - give every row the slow columns' value in force (see the module notes)
    local_time - add a local_time column from the file's ZD
    """
    path = Path(path)
    meta = read_meta(path)
    df = pd.read_csv(path, comment="#", low_memory=False,
                     dtype={c: "string" for c in TEXT_COLUMNS + ["event"]})

    for c in df.columns:                                     # numbers stay numbers
        if c not in TEXT_COLUMNS and c not in ("utc_time", "event"):
            df[c] = pd.to_numeric(df[c], errors="coerce")

    if fill_slow:
        present = [c for c in SLOW_COLUMNS if c in df.columns]
        if present:
            # the row where the slow columns were written: batt_v marks it; without a
            # battery gauge, any slow value at all
            if "batt_v" in df.columns and df["batt_v"].notna().any():
                written = df["batt_v"].notna()
            else:
                written = df[present].notna().any(axis=1)
            group = written.cumsum()
            for c in present:
                vals = df[c].where(written)                   # the value written on each marker row
                df[c] = vals.groupby(group).transform("first").where(group > 0)
        if "mag_var_deg" in df.columns:
            df["mag_var_deg"] = df["mag_var_deg"].ffill()

    t = pd.to_datetime(df["utc_time"], utc=True, errors="coerce", format="ISO8601")
    df = df.drop(columns=["utc_time"])
    df.index = pd.DatetimeIndex(t, name="utc_time")
    zd = _zd_hours(meta)
    if local_time and zd is not None:
        df["local_time"] = (df.index - pd.Timedelta(hours=zd)).tz_localize(None)

    df.attrs["meta"] = meta
    df.attrs["file"] = path.name
    df.attrs["zd_hours"] = zd
    return df


def read_many(paths, **kw) -> pd.DataFrame:
    """Read several files of one recording (a new file each day, or a Last 20 min file)
    and join them in time order. The metadata kept is the first file's."""
    frames = [read_vmr(p, **kw) for p in sorted(map(str, paths))]
    out = pd.concat(frames).sort_index(kind="stable")
    out.attrs = dict(frames[0].attrs, files=[f.attrs["file"] for f in frames])
    return out


def read_marks(path) -> pd.DataFrame:
    """Read a location marks file (/location_mark/..._location_marks.csv)."""
    df = pd.read_csv(path, comment="#", dtype={"note": "string", "type": "string"})
    for c in ("utc_time", "end_utc_time"):
        if c in df.columns:
            df[c] = pd.to_datetime(df[c], utc=True, errors="coerce", format="ISO8601")
    return df


def fixes(df: pd.DataFrame) -> pd.DataFrame:
    """Rows with a new GPS fix report (gps_fix = 0 is a lost fix: position blank)."""
    return df.loc[df["gps_fix"].notna(), [c for c in GPS_COLUMNS if c in df.columns]]


def events(df: pd.DataFrame) -> pd.DataFrame:
    """Rows carrying an event: Mark event, LOCATION MARK n, CAST n START / STOP, PAUSED,
    RESUMED, set-up changes, TRIM CALIBRATED, LEVEL ZERO SET, SD RECOVERED ..."""
    has = pd.Series(False, index=df.index)
    for c in ("event", "event_note"):
        if c in df.columns:
            has |= df[c].notna() & (df[c].astype("string").str.strip() != "")
    cols = [c for c in ("local_time", "event", "event_note") if c in df.columns]
    return df.loc[has, cols]


def waves(df: pd.DataFrame) -> pd.DataFrame:
    """The Waves estimates: one row each time a new one was made (every 30 s)."""
    cols = [c for c in WAVE_COLUMNS if c in df.columns]
    if not cols:
        return df.iloc[0:0]
    return df.loc[df[cols].notna().any(axis=1), cols]


def slow(df: pd.DataFrame, rule: str = "1s") -> pd.DataFrame:
    """The slow columns (and wind) one row per `rule` (default 1 s), from the filled frame."""
    cols = [c for c in SLOW_COLUMNS + WIND_COLUMNS if c in df.columns]
    return df[cols].resample(rule).last()


def ctd(df: pd.DataFrame) -> pd.DataFrame:
    """SeaLabMOS: the rows carrying the WTB's 1-s CTD means, with iwb_state and iwb_cast
    (the 5 Hz samples are on the WTB's card: /sess and /casts)."""
    cols = [c for c in IWB_COLUMNS if c in df.columns]
    if not cols:
        return df.iloc[0:0]
    keep = cols + [c for c in ("iwb_state", "iwb_cast", "local_time") if c in df.columns]
    return df.loc[df[cols].notna().any(axis=1), keep]


def summary(df: pd.DataFrame) -> str:
    """A few lines describing the recording."""
    m = df.attrs.get("meta", {})
    t0, t1 = df.index.min(), df.index.max()
    dur = (t1 - t0) if pd.notna(t0) and pd.notna(t1) else pd.Timedelta(0)
    rate = len(df) / dur.total_seconds() if dur.total_seconds() > 0 else float("nan")
    lines = [
        f"{df.attrs.get('file', '')}  (firmware {m.get('firmware_version', '?')}, device {m.get('device_id', '?')})",
        f"  {t0:%Y-%m-%d %H:%M:%S} to {t1:%Y-%m-%d %H:%M:%S} UTC, {dur}  ·  {len(df):,} rows ({rate:.2f} /s)",
    ]
    if m.get("condition"):
        lines.append(f"  condition: {m['condition']}")
    if "roll_deg" in df and df["roll_deg"].notna().any():
        r = df["roll_deg"]
        lines.append(f"  roll: max starboard {max(r.max(), 0):.1f}°, max port {max(-r.min(), 0):.1f}°, "
                     f"mean (list) {r.mean():+.2f}° (+ = starboard)")
    if "pitch_deg" in df and df["pitch_deg"].notna().any():
        p = df["pitch_deg"]
        lines.append(f"  pitch: {p.min():+.1f}° to {p.max():+.1f}°, mean {p.mean():+.2f}°")
    if "gm_ft" in df and df["gm_ft"].notna().any():
        gm = df["gm_ft"].dropna()
        lines.append(f"  GM: median {gm.median():.2f} ft ({gm.median() * 0.3048:.2f} m)")
    if "gps_fix" in df:
        f = fixes(df)
        lines.append(f"  GPS: {int((f['gps_fix'] > 0).sum()):,} fixes")
    w = waves(df)
    if "heave_hs_m" in w and w["heave_hs_m"].notna().any():
        line = f"  heave Hs (motion): median {w['heave_hs_m'].median():.2f} m"
        if "baro_hs_m" in w and w["baro_hs_m"].notna().any():
            line += f"  ·  from the pressure: median {w['baro_hs_m'].median():.2f} m"
        lines.append(line)
    c = ctd(df)
    if len(c) and "iwb_depth_m" in c and c["iwb_depth_m"].notna().any():
        casts = c["iwb_cast"].dropna().unique() if "iwb_cast" in c else []
        lines.append(f"  CTD (SeaLabMOS): {len(c):,} s of data, deepest {c['iwb_depth_m'].max():.1f} m, casts {len(casts)}")
    if "pressure_mbar" in df and df["pressure_mbar"].notna().any():
        lines.append(f"  pressure: {df['pressure_mbar'].min():.1f}-{df['pressure_mbar'].max():.1f} mbar")
    ev = events(df)
    lines.append(f"  events: {len(ev)}")
    gaps = df.index.to_series().diff().dt.total_seconds()
    step = float(m.get("log_rate_hz") and 1 / float(m["log_rate_hz"]) or gaps.median())
    big = gaps[gaps > max(2 * step, 0.2)]
    if len(big):
        lines.append(f"  gaps over {max(2 * step, 0.2):.1f} s: {len(big)}, longest {big.max():.1f} s")
    return "\n".join(lines)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    for p in sys.argv[1:]:
        print(summary(read_vmr(p)))
        print()
