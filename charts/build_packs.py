#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Aspect Wave LLC
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the VMR chart packs in regions.json from NOAA's Chart Display Service MBTiles.

    python3 build_packs.py --out packs/                      everything, downloading what's needed from NOAA
    python3 build_packs.py --out packs/ --only san-francisco-bay
    python3 build_packs.py --out packs/ --mbtiles ~/Downloads   use NOAA files already downloaded (no internet)
    python3 build_packs.py --out packs/ --previous last/packs.json   nothing built if nothing changed since then
    python3 build_packs.py --plan plan.json --previous last/packs.json    only work out what to build (the Action's first job)
    python3 build_packs.py --merge packs/ --plan plan.json                 the packs.json of each job's packs put together

NOAA publishes one MBTiles file per region of the coast (https://distribution.charts.noaa.gov/ncds/index.html,
updated weekly), and doesn't publish which file covers what. This script reads each file's coverage from its
first 64 KB (the MBTiles metadata sits at the front), downloads only the files that cover a pack, and builds
the pack from them. GitHub runs it each month (.github/workflows/charts.yml) and publishes the packs as a
release when something has changed; it runs the same way on a laptop.

Each region makes two packs: <name>.vmrchart, with the zooms regions.json gives each area (smaller), and
<name>-max.vmrchart, every tile NOAA has inside the region's box at every zoom (nothing left out; "max": false on a
region skips it). A max pack over 2 GiB, too big for a GitHub release, is left out and noted in packs.json.

Writes <out>/<name>.vmrchart and <name>-max.vmrchart for each region and <out>/packs.json: what was built, from which NOAA files, and a
fingerprint of what it was built from (each NOAA file's size and Last-Modified, regions.json and the pack
tools, Leaflet included). With --previous (the last release's packs.json), when the fingerprint is the same nothing is
downloaded or built: <out>/UNCHANGED is written instead and the workflow publishes nothing."""
import argparse, glob, hashlib, json, os, sqlite3, sys, time, urllib.request, email.utils, re, multiprocessing
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_vmrchart import build, EXT, LEAFLET, Tiles

GH_MAX = 2147483648 - 4 * 1048576                 # a GitHub release file must be under 2 GiB

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
                "maxz": int(m.get("maxzoom", 0)), "bytes": total, "modified": r.headers.get("Last-Modified") or ""}
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


def fingerprint(regions, use, files, tools):
    """What the packs are made from: the NOAA files each one uses (size, Last-Modified), the regions and the tools."""
    noaa = {}
    for name in sorted({os.path.basename(s) for u in use.values() for s in u}):
        s = next(x for u in use.values() for x in u if os.path.basename(x) == name)
        noaa[name] = {"bytes": files[s]["bytes"], "modified": files[s].get("modified", "")}
    return {"noaa": noaa, "packs": {r["name"]: [os.path.basename(s) for s in use[r["name"]]] for r in regions},
            "regions": hashlib.sha256(json.dumps(regions, sort_keys=True).encode()).hexdigest(),
            "tools": hashlib.sha256(b"".join(open(t, "rb").read() for t in tools)).hexdigest()}


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
    ap.add_argument("--previous", help="the last release's packs.json: build nothing if nothing has changed since")
    ap.add_argument("--force", action="store_true", help="build even if nothing has changed")
    ap.add_argument("--plan", help="write what to build to this file (JSON) and stop; with --merge, the plan to merge by")
    ap.add_argument("--merge", help="a folder of packs built in parts (each part's packs.json named <anything>.packs.json): write its packs.json")
    ap.add_argument("--no-max", action="store_true", help="the standard packs only")
    a = ap.parse_args()
    if a.merge:
        plan = json.load(open(a.plan))
        packs = [p for f in sorted(glob.glob(os.path.join(a.merge, "*.packs.json"))) for p in json.load(open(f))["packs"]]
        order = {n: i for i, n in enumerate(plan["regions"])}
        packs.sort(key=lambda p: (order.get(p["region"], 999), p["detail"] != "standard"))
        got = {p["region"] for p in packs}
        miss = [n for n in plan["regions"] if n not in got]
        json.dump({"built": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "extension": EXT, "packs": packs,
                   "skipped": [k for f in sorted(glob.glob(os.path.join(a.merge, "*.packs.json"))) for k in json.load(open(f)).get("skipped", [])],
                   "fingerprint": plan["fingerprint"]}, open(os.path.join(a.merge, "packs.json"), "w"), indent=1)
        if miss: sys.exit("no packs for " + ", ".join(miss))
        print("%d packs in %s/packs.json" % (len(packs), a.merge))
        return
    regions = [r for r in json.load(open(a.regions))["regions"] if not a.only or r["name"] in a.only]
    if not regions: sys.exit("no such pack in " + a.regions)
    os.makedirs(a.out, exist_ok=True); os.makedirs(a.work, exist_ok=True)

    files = {}                                   # source (URL or local path) -> coverage
    if a.mbtiles:
        for p in sorted(glob.glob(os.path.join(a.mbtiles, "*.mbtiles"))):
            m = read_meta(p)
            files[p] = {"bounds": [float(v) for v in m["bounds"].split(",")], "minz": int(m.get("minzoom", 0)),
                        "maxz": int(m.get("maxzoom", 0)), "bytes": os.path.getsize(p),
                        "modified": email.utils.formatdate(os.path.getmtime(p), usegmt=True)}
    else:
        cache = {"_dir": a.work}
        urls = noaa_files(); print("%d NOAA files listed; reading their coverage" % len(urls))
        for u in urls:
            info = coverage(u, cache)
            if info: files[u] = info
    for s, i in files.items():
        print("  %-16s %8.1f MB  zoom %2d-%-2d  %s" % (os.path.basename(s), i["bytes"] / 1e6, i["minz"], i["maxz"],
                                                     ", ".join("%.2f" % v for v in i["bounds"])))

    uses = {}
    for reg in regions:
        box = region_box(reg)
        use = [s for s, i in files.items() if overlaps(box, i["bounds"])]
        # the most detailed file first: where two files both have chart on a tile, the deeper-zoom, smaller file wins
        use.sort(key=lambda s: (-files[s]["maxz"], (files[s]["bounds"][2] - files[s]["bounds"][0]) * (files[s]["bounds"][3] - files[s]["bounds"][1])))
        uses[reg["name"]] = use
    fp = fingerprint(regions, uses, files, [os.path.join(here, "make_vmrchart.py"), __file__] + LEAFLET)
    if a.previous and not a.force and not a.only and os.path.exists(a.previous):
        try: prev = json.load(open(a.previous)).get("fingerprint")
        except Exception: prev = None
        if prev == fp:
            print("NOAA's files, the regions and the tools are the same as for %s: nothing to build" % a.previous)
            if a.plan: json.dump({"build": False, "regions": [], "fingerprint": fp}, open(a.plan, "w"), indent=1)
            else: open(os.path.join(a.out, "UNCHANGED"), "w").write("unchanged since the last release\n")
            return
        if prev:
            for n, v in fp["noaa"].items():
                if prev.get("noaa", {}).get(n) != v: print("  changed since the last release: %s (%s)" % (n, v["modified"] or "no date"))
            for k in ("packs", "regions", "tools"):
                if prev.get(k) != fp[k]: print("  changed since the last release: %s" % k)

    if a.plan:
        todo = [r["name"] for r in regions if uses[r["name"]]]
        for r in regions:
            if not uses[r["name"]]: print("%s: no NOAA file covers it" % r["name"])
        json.dump({"build": True, "regions": todo, "fingerprint": fp}, open(a.plan, "w"), indent=1)
        print("to build: " + " ".join(todo))
        if len(todo) < len(regions): sys.exit("%d of %d regions have no NOAA file" % (len(regions) - len(todo), len(regions)))
        return

    done, skipped, regs_done = [], [], 0
    with multiprocessing.Pool() as pool:                 # the tile images are looked at on every core
        for reg in regions:
            use = uses[reg["name"]]; box = region_box(reg)
            if not use:
                print("%s: no NOAA file covers it, skipped" % reg["name"]); continue
            print("%s: from %s" % (reg["name"], ", ".join(os.path.basename(s) for s in use)))
            paths = use if a.mbtiles else [download(u, a.work) for u in use]
            T = Tiles(True, pool)                         # shared by the region's two packs
            date = time.strftime("%Y-%m-%d", time.gmtime(max(os.path.getmtime(x) for x in paths)))
            kinds = [("standard", reg["name"], reg["title"], False)]
            if not a.no_max and reg.get("max", True): kinds.append(("max", reg["name"] + "-max", reg["title"] + " (max detail)", True))
            for detail, name, title, every in kinds:
                p = build(name, title, reg["areas"], paths, a.out, minz=reg.get("minz", 3), every=every, tiles=T,
                          extra={"region": reg["name"], "detail": detail})
                n = os.path.getsize(p)
                if n > GH_MAX:
                    os.remove(p); skipped.append({"name": name, "bytes": n, "why": "over 2 GiB, too big for a GitHub release"})
                    print("%s: %.2f GB, over GitHub's 2 GiB for a release file: left out" % (name, n / 1e9)); continue
                done.append({"name": name, "region": reg["name"], "detail": detail, "title": title, "file": os.path.basename(p),
                             "bytes": n, "sha256": sha256(p), "bounds": box, "noaa": [os.path.basename(s) for s in use], "chart_date": date})
            T = None
            regs_done += 1
    json.dump({"built": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "extension": EXT, "packs": done, "skipped": skipped,
               "fingerprint": fp}, open(os.path.join(a.out, "packs.json"), "w"), indent=1)
    if regs_done < len(regions): sys.exit("%d of %d regions built" % (regs_done, len(regions)))
    print("%d packs built in %s" % (len(done), a.out))


if __name__ == "__main__":
    main()
