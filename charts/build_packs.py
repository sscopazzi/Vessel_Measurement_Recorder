#!/usr/bin/env python3
"""Builds the VMR chart packs in regions.json from NOAA's Chart Display Service MBTiles.

    python3 build_packs.py --out packs/                      everything, downloading what's needed from NOAA
    python3 build_packs.py --out packs/ --only san-francisco-bay
    python3 build_packs.py --out packs/ --mbtiles ~/Downloads   use NOAA files already downloaded (no internet)

NOAA publishes one MBTiles file per region of the coast (https://distribution.charts.noaa.gov/ncds/index.html,
updated weekly), and doesn't publish which file covers what. This script reads each file's coverage from its
first 64 KB (the MBTiles metadata sits at the front), downloads only the files that cover a pack, and builds
the pack from them. GitHub runs it each month (.github/workflows/charts.yml) and publishes the packs as a
release; it runs the same way on a laptop.

Writes <out>/<name>.vmrchart for each pack and <out>/packs.json (what was built, from which NOAA files)."""
import argparse, glob, hashlib, json, os, sqlite3, sys, time, urllib.request, email.utils, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_vmrchart import build, EXT

CATALOG = "https://raw.githubusercontent.com/chartcatalogs/catalogs/master/NOAA_MBTiles_Catalog.xml"
BASE = "https://distribution.charts.noaa.gov/ncds/mbtiles/"
UA = {"User-Agent": "VMR chart pack builder (github.com/sscopazzi/Vessel_Measurement_Recorder)"}


def get(url, rng=None, tries=4):
    for k in range(tries):
        try:
            h = dict(UA)
            if rng: h["Range"] = "bytes=%d-%d" % rng
            return urllib.request.urlopen(urllib.request.Request(url, headers=h), timeout=120)
        except Exception as e:
            if k == tries - 1: raise
            print("  %s: %s, trying again" % (url, e)); time.sleep(10 * (k + 1))


def noaa_files():
    """The NOAA MBTiles file URLs, from the chartcatalogs list."""
    xml = get(CATALOG).read().decode()
    urls = sorted(set(re.findall(r"<zipfile_location>\s*(\S+?\.mbtiles)\s*</zipfile_location>", xml)))
    if not urls: sys.exit("no NOAA MBTiles files listed in " + CATALOG)
    return urls


def read_meta(path):
    db = sqlite3.connect("file:%s?mode=ro&immutable=1" % path, uri=True)
    m = dict(db.execute("SELECT name, value FROM metadata").fetchall()); db.close()
    return m


def coverage(url, cache):
    """A NOAA file's bounds and zooms, from its first 64 KB (padded out to the full length so SQLite can open it)."""
    if url in cache: return cache[url]
    r = get(url, (0, 65535)); head = r.read()
    total = int((r.headers.get("Content-Range") or "/%d" % len(head)).split("/")[-1])
    tmp = os.path.join(cache["_dir"], "head.mbtiles")
    with open(tmp, "wb") as f: f.write(head); f.truncate(total)
    try:
        m = read_meta(tmp)
        info = {"bounds": [float(v) for v in m["bounds"].split(",")], "minz": int(m.get("minzoom", 0)),
                "maxz": int(m.get("maxzoom", 0)), "bytes": total}
    except Exception as e:
        print("  %s: couldn't read its coverage (%s), skipping it" % (url, e)); info = None
    os.remove(tmp)
    cache[url] = info
    return info


def overlaps(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def region_box(reg):
    A = reg["areas"]
    return [min(a["bbox"][0] for a in A), min(a["bbox"][1] for a in A), max(a["bbox"][2] for a in A), max(a["bbox"][3] for a in A)]


def download(url, folder):
    """Downloads a NOAA file once (kept for the other packs in this run), dated with NOAA's Last-Modified."""
    path = os.path.join(folder, url.rsplit("/", 1)[-1])
    if os.path.exists(path): return path
    r = get(url); n = int(r.headers.get("Content-Length") or 0); got = 0; t0 = time.time(); last = 0
    with open(path + ".part", "wb") as f:
        while True:
            b = r.read(1 << 20)
            if not b: break
            f.write(b); got += len(b)
            if time.time() - last > 15:
                last = time.time(); print("  %s: %.0f / %.0f MB" % (os.path.basename(path), got / 1e6, n / 1e6))
    if n and got != n: sys.exit("%s: download stopped at %d of %d bytes" % (url, got, n))
    lm = r.headers.get("Last-Modified")
    os.replace(path + ".part", path)
    if lm:
        t = email.utils.parsedate_to_datetime(lm).timestamp(); os.utime(path, (t, t))
    print("  %s: %.0f MB in %.0f s" % (os.path.basename(path), got / 1e6, time.time() - t0))
    return path


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    return h.hexdigest()


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--regions", default=os.path.join(here, "regions.json"))
    ap.add_argument("--out", default="packs")
    ap.add_argument("--only", nargs="*", help="pack names to build (default: all)")
    ap.add_argument("--mbtiles", help="a folder of NOAA .mbtiles files already downloaded (nothing is downloaded)")
    ap.add_argument("--work", default="noaa", help="folder for NOAA downloads (default ./noaa)")
    a = ap.parse_args()
    regions = [r for r in json.load(open(a.regions))["regions"] if not a.only or r["name"] in a.only]
    if not regions: sys.exit("no such pack in " + a.regions)
    os.makedirs(a.out, exist_ok=True); os.makedirs(a.work, exist_ok=True)

    files = {}                                   # source (URL or local path) -> coverage
    if a.mbtiles:
        for p in sorted(glob.glob(os.path.join(a.mbtiles, "*.mbtiles"))):
            m = read_meta(p)
            files[p] = {"bounds": [float(v) for v in m["bounds"].split(",")], "minz": int(m.get("minzoom", 0)),
                        "maxz": int(m.get("maxzoom", 0)), "bytes": os.path.getsize(p)}
    else:
        cache = {"_dir": a.work}
        urls = noaa_files(); print("%d NOAA files listed; reading their coverage" % len(urls))
        for u in urls:
            info = coverage(u, cache)
            if info: files[u] = info
    for s, i in files.items():
        print("  %-16s %8.1f MB  zoom %2d-%-2d  %s" % (os.path.basename(s), i["bytes"] / 1e6, i["minz"], i["maxz"],
                                                     ", ".join("%.2f" % v for v in i["bounds"])))

    done = []
    for reg in regions:
        box = region_box(reg)
        use = [s for s, i in files.items() if overlaps(box, i["bounds"])]
        # the most detailed file first: where two files both have chart on a tile, the deeper-zoom, smaller file wins
        use.sort(key=lambda s: (-files[s]["maxz"], (files[s]["bounds"][2] - files[s]["bounds"][0]) * (files[s]["bounds"][3] - files[s]["bounds"][1])))
        if not use:
            print("%s: no NOAA file covers it, skipped" % reg["name"]); continue
        print("%s: from %s" % (reg["name"], ", ".join(os.path.basename(s) for s in use)))
        paths = use if a.mbtiles else [download(u, a.work) for u in use]
        p = build(reg["name"], reg["title"], reg["areas"], paths, a.out, minz=reg.get("minz", 3))
        done.append({"name": reg["name"], "title": reg["title"], "file": os.path.basename(p), "bytes": os.path.getsize(p),
                     "sha256": sha256(p), "bounds": box, "noaa": [os.path.basename(s) for s in use],
                     "chart_date": time.strftime("%Y-%m-%d", time.gmtime(max(os.path.getmtime(x) for x in paths)))})
    json.dump({"built": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "extension": EXT, "packs": done},
              open(os.path.join(a.out, "packs.json"), "w"), indent=1)
    if len(done) < len(regions): sys.exit("%d of %d packs built" % (len(done), len(regions)))
    print("%d packs built in %s" % (len(done), a.out))


if __name__ == "__main__":
    main()
