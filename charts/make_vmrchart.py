#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Aspect Wave LLC
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes a VMR chart pack (.vmrchart) from NOAA Chart Display Service MBTiles, for the VMR's Chart card.

    python3 make_vmrchart.py --region regions.json:juan-de-fuca-puget --out packs/  ncds_20c.mbtiles
    python3 make_vmrchart.py --name test --bbox=-124.8,47.0,-122.1,48.8 --maxz 13 --out packs/  ncds_20c.mbtiles
    python3 make_vmrchart.py --region regions.json:juan-de-fuca-puget --max --out packs/  ncds_20c.mbtiles

NOAA's MBTiles come from https://distribution.charts.noaa.gov/ncds/index.html (one file per region,
updated weekly). A pack is a set of areas, each a box (west,south,east,north) with the zooms kept there:
the whole region light (zoom 12 or 13), the straits and sounds at 14, harbours at 16. A region that runs
across two NOAA files takes both; where both have a tile, the first file with chart on it wins. A max-detail pack
(<name>-max.vmrchart, --max) leaves nothing out: every tile NOAA has inside the region's box, at every zoom; the VMR
keeps one of the two for a region (loading either replaces the other). Tiles with
no chart on them (fully transparent) are left out, so the map shows nothing there rather than a blank.
Each tile is kept as the smaller of NOAA's own image and a lossless WebP of it; tiles with the same bytes
(open water, plain land) are stored once. Each pack also carries Leaflet (leaflet/, the map code the Chart card
draws with), so a VMR with a pack on its card shows the chart without internet.

The .vmrchart file (little-endian):
  header, 64 bytes:
    0  "VMRC"           4  u16 version (1)   6  u16 header length (64)
    8  u32 tiles        12 u32 index offset   16 u32 metadata offset   20 u32 metadata length
    24 f32 west, south, east, north (degrees)
    40 u8 min zoom      41 u8 max zoom        42 u16 tile size (pixels)
    44 u32 built (UTC)  48 u32 chart date (UTC: the newest NOAA file used)
    52 u32 data offset  56 u32 file length    60 u32 library offset (0: none)
  metadata: UTF-8 JSON (name, title, source, attribution, areas)
  index: one 16-byte entry per tile, sorted by key: u64 key = zoom << 48 | x << 24 | y (XYZ, y down from
         the north), u32 offset, u32 length
  data: the tile images (PNG, JPEG or WebP; the VMR tells them apart by their first bytes)
  library: "VMRL", u32 Leaflet JS length, u32 Leaflet CSS length, u32 0, then the two, each gzipped
Not for navigation: NOAA ENC charts shown as pictures, for situational awareness."""
import argparse, json, math, os, sqlite3, struct, sys, time, io, hashlib, gzip, re

MAGIC, VERSION, HDR, EXT = b"VMRC", 1, 64, ".vmrchart"
HFMT = "<4sHHIIIIffffBBHIIIII"
LEAFLET = [os.path.join(os.path.dirname(os.path.abspath(__file__)), "leaflet", f) for f in ("leaflet.js", "leaflet.css")]


def library():
    """Leaflet's version and the library block: "VMRL", the JS and CSS lengths, 0, then each gzipped."""
    js, css = (open(p, "rb").read() for p in LEAFLET)
    v = re.search(rb"Leaflet (\d+\.\d+\.\d+)", js[:400])
    zj, zc = (gzip.compress(b, compresslevel=9, mtime=0) for b in (js, css))
    return (v.group(1).decode() if v else "?"), b"VMRL" + struct.pack("<III", len(zj), len(zc), 0) + zj + zc


def tile_range(w, s, e, n, z):
    """XYZ tiles (x0, x1, y0, y1 inclusive) covering a box at zoom z."""
    def tx(lon): return int(math.floor((lon + 180.0) / 360.0 * (1 << z)))
    def ty(lat):
        lat = max(-85.0511, min(85.0511, lat)); r = math.radians(lat)
        return int(math.floor((1.0 - math.log(math.tan(r) + 1.0 / math.cos(r)) / math.pi) / 2.0 * (1 << z)))
    m = (1 << z) - 1
    return max(0, tx(w)), min(m, tx(e)), max(0, ty(n)), min(m, ty(s))


def _look(arg):
    """One tile image: (blank, pixels, its smallest form). A function of its own so a pool of processes can run it."""
    data, webp = arg
    blank, px, best = False, 256, data
    try:
        from PIL import Image
        im = Image.open(io.BytesIO(data)); im.load(); px = im.size[0]
        if im.mode in ("RGBA", "LA", "P"):
            a = im.convert("RGBA").getchannel("A").getextrema()
            blank = a[1] == 0
        if webp and not blank:
            if im.mode not in ("RGB", "RGBA"): im = im.convert("RGBA")
            b = io.BytesIO(); im.save(b, "WEBP", lossless=True, quality=75, method=4)   # as small as method 6, 60x faster
            if len(b.getvalue()) < len(data): best = b.getvalue()
    except ImportError:
        pass
    except Exception:
        pass
    return blank, px, best


class Tiles:
    """Looks at each different tile image once: is it blank, how big is it, what is its smallest form. With a pool, the
    images of a batch are looked at in parallel (prepare). One Tiles can serve several packs (a region's two)."""
    def __init__(self, webp, pool=None):
        self.webp, self.pool, self.info = webp, pool, {}          # sha1 -> (blank, px, smallest bytes)

    def prepare(self, datas):
        new = {}
        for d in datas:
            h = hashlib.sha1(d).digest()
            if h not in self.info and h not in new: new[h] = d
        if not new: return
        args = [(d, self.webp) for d in new.values()]
        res = self.pool.map(_look, args, chunksize=16) if self.pool and len(args) > 16 else [_look(a) for a in args]
        for h, r in zip(new, res): self.info[h] = r

    def look(self, data):
        h = hashlib.sha1(data).digest()
        if h not in self.info: self.info[h] = _look((data, self.webp))
        return h, self.info[h]


def mbtiles_date(paths):
    """The newest of the files' times (the download script sets them to NOAA's Last-Modified)."""
    return max(int(os.path.getmtime(p)) for p in paths)


def wanted(areas, minz):
    want = set()
    for a in areas:
        w, s, e, n = a["bbox"]
        for z in range(a.get("minz", minz), a["maxz"] + 1):
            x0, x1, y0, y1 = tile_range(w, s, e, n, z)
            for x in range(x0, x1 + 1):
                for y in range(y0, y1 + 1):
                    want.add((z, x, y))
    return want


def existing(dbs, box, minz):
    """Max detail: every tile the files have inside box, at every zoom from minz to each file's deepest (XYZ)."""
    want = set()
    w, s, e, n = box
    for db in dbs:
        m = dict(db.execute("SELECT name, value FROM metadata").fetchall())
        try: top = int(m.get("maxzoom"))
        except (TypeError, ValueError): top = db.execute("SELECT MAX(zoom_level) FROM tiles").fetchone()[0] or 0
        for z in range(minz, top + 1):
            x0, x1, y0, y1 = tile_range(w, s, e, n, z)
            r0, r1 = (1 << z) - 1 - y1, (1 << z) - 1 - y0          # MBTiles rows count up from the south (TMS)
            for x, row in db.execute("SELECT tile_column, tile_row FROM tiles WHERE zoom_level=? AND tile_column BETWEEN ? AND ? "
                                     "AND tile_row BETWEEN ? AND ?", (z, x0, x1, r0, r1)):
                want.add((z, x, (1 << z) - 1 - row))
    return want


def build(name, title, areas, sources, out_dir, webp=True, minz=3, quiet=False, every=False, tiles=None, extra=None):
    """areas: [{"bbox": [w,s,e,n], "maxz": z, "minz": z (optional)}]; sources: .mbtiles paths, best first.
    every: max detail, every tile the files have inside the areas' box (the areas' zooms are not used). tiles: a Tiles
    to use (and add to), so a region's second pack looks again at none of the images its first one did. extra: more
    metadata."""
    dbs = [sqlite3.connect("file:%s?mode=ro" % p, uri=True) for p in sources]
    box = [min(a["bbox"][0] for a in areas), min(a["bbox"][1] for a in areas), max(a["bbox"][2] for a in areas), max(a["bbox"][3] for a in areas)]
    want = existing(dbs, box, minz) if every else wanted(areas, minz)
    if every: areas = [{"label": "Everything NOAA has in the region", "bbox": box, "maxz": max((z for z, _, _ in want), default=minz)}]
    if not quiet: print("%s: %d tiles %s across %d area(s)" % (name, len(want), "in NOAA's files" if every else "wanted", len(areas)))
    T = tiles or Tiles(webp)
    blobs, index, where = [], [], {}            # where: sha1 -> blob number
    raw = got = blanks = 0; px = 0
    t0 = time.time(); keys = sorted(want); CH = 4096
    for c0 in range(0, len(keys), CH):          # in batches: each file's tiles for the batch read, then their images looked at together
        chunk = keys[c0:c0 + CH]; pend = chunk; pick = {}
        for db in dbs:
            if not pend: break
            got_d = []
            for (z, x, y) in pend:
                r = db.execute("SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?", (z, x, (1 << z) - 1 - y)).fetchone()
                if r and r[0]: got_d.append(((z, x, y), bytes(r[0])))
            T.prepare([d for _, d in got_d])
            for key, data in got_d:
                h, (blank, p, best) = T.look(data)
                if not blank: pick[key] = (data, h, p, best)
            pend = [k for k in pend if k not in pick]          # blank or missing here: the next file may have it
        for key in chunk:
            if key not in pick:
                blanks += 1; continue
            z, x, y = key; data, h, p, best = pick[key]
            if not px: px = p
            raw += len(data)
            if h not in where:
                where[h] = len(blobs); blobs.append(best)
            index.append(((z << 48) | (x << 24) | y, where[h]))
            got += 1
        if not quiet and (c0 // CH) % 10 == 9: print("  %d / %d tiles looked up, %.0f s" % (c0 + len(chunk), len(want), time.time() - t0))
    for db in dbs: db.close()
    if not index: sys.exit("%s: no chart tiles in %s for these areas" % (name, ", ".join(sources)))
    index.sort()
    W = min(a["bbox"][0] for a in areas); S = min(a["bbox"][1] for a in areas)
    E = max(a["bbox"][2] for a in areas); N = max(a["bbox"][3] for a in areas)
    zs = [k >> 48 for k, _ in index]
    built, chart = int(time.time()), mbtiles_date(sources)
    lfv, lib = library()
    meta = json.dumps(dict({"name": name, "title": title, "leaflet": lfv,
                       "source": "NOAA Chart Display Service (ENC): " + ", ".join(os.path.basename(p) for p in sources),
                       "attribution": "NOAA ENC charts: not for navigation", "areas": areas}, **(extra or {})), separators=(",", ":")).encode()
    meta_off = HDR; idx_off = meta_off + len(meta); idx_off += (-idx_off) % 16
    data_off = idx_off + 16 * len(index)
    offs, o = [], data_off
    for b in blobs: offs.append(o); o += len(b)
    lib_off = o; flen = o + len(lib)
    if flen >= 1 << 32: sys.exit("%s: over 4 GB, too big for the microSD card's FAT32: use fewer or smaller areas" % name)
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, name + EXT)
    with open(path + ".part", "wb") as f:
        f.write(struct.pack(HFMT, MAGIC, VERSION, HDR, len(index), idx_off, meta_off, len(meta),
                            W, S, E, N, min(zs), max(zs), px or 256, built, chart, data_off, flen, lib_off))
        f.write(meta); f.write(b"\0" * (idx_off - meta_off - len(meta)))
        for key, bn in index: f.write(struct.pack("<QII", key, offs[bn], len(blobs[bn])))
        for b in blobs: f.write(b)
        f.write(lib)
    os.replace(path + ".part", path)
    if not quiet:
        print("%s: %d tiles (%d different, %d empty left out), zoom %d-%d, %.1f MB (NOAA's images were %.1f MB) -> %s"
              % (name, got, len(blobs), blanks, min(zs), max(zs), flen / 1e6, raw / 1e6, path))
    return path


def read_pack(path):
    """The header and metadata of a pack, and a lookup for one tile (used by the tests). "leaflet": its Leaflet JS and
    CSS (gzipped), or None."""
    f = open(path, "rb"); h = struct.unpack(HFMT, f.read(HDR))
    if h[0] != MAGIC: raise ValueError("not a VMR chart pack")
    n, idx, mo, ml = h[3], h[4], h[5], h[6]
    f.seek(mo); meta = json.loads(f.read(ml))
    lf = None
    if h[18]:
        f.seek(h[18]); m, nj, nc, _ = struct.unpack("<4sIII", f.read(16))
        if m == b"VMRL": lf = (f.read(nj), f.read(nc))
    def tile(z, x, y):
        key = (z << 48) | (x << 24) | y; lo, hi = 0, n - 1
        while lo <= hi:
            mid = (lo + hi) // 2; f.seek(idx + 16 * mid); k, off, ln = struct.unpack("<QII", f.read(16))
            if k == key: f.seek(off); return f.read(ln)
            if k < key: lo = mid + 1
            else: hi = mid - 1
        return None
    return {"tiles": n, "bounds": h[7:11], "minz": h[11], "maxz": h[12], "px": h[13], "built": h[14],
            "chart": h[15], "len": h[17], "meta": meta, "leaflet": lf}, tile


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("mbtiles", nargs="+", help="NOAA .mbtiles file(s) covering the region, best first")
    ap.add_argument("--region", help="regions.json:name, to take a region's areas from the file")
    ap.add_argument("--name", help="the pack's name (file name), with --bbox")
    ap.add_argument("--bbox", help="west,south,east,north in degrees, with --name (write it as --bbox=-124,47,-122,49)")
    ap.add_argument("--minz", type=int, default=3, help="shallowest zoom for --bbox (default 3)")
    ap.add_argument("--maxz", type=int, default=13, help="deepest zoom for --bbox (default 13)")
    ap.add_argument("--out", default=".", help="folder for the pack")
    ap.add_argument("--keep-images", action="store_true", help="keep NOAA's images as they are (no WebP)")
    ap.add_argument("--max", action="store_true", help="with --region: the max-detail pack too (<name>-max.vmrchart)")
    a = ap.parse_args()
    if a.region:
        import multiprocessing
        fn, rn = a.region.rsplit(":", 1)
        reg = next(r for r in json.load(open(fn))["regions"] if r["name"] == rn)
        with multiprocessing.Pool() as pool:
            T = Tiles(not a.keep_images, pool)
            build(reg["name"], reg["title"], reg["areas"], a.mbtiles, a.out, webp=not a.keep_images, minz=reg.get("minz", 3),
                  tiles=T, extra={"region": reg["name"], "detail": "standard"})
            if a.max:
                build(reg["name"] + "-max", reg["title"] + " (max detail)", reg["areas"], a.mbtiles, a.out, webp=not a.keep_images,
                      minz=reg.get("minz", 3), every=True, tiles=T, extra={"region": reg["name"], "detail": "max"})
    elif a.name and a.bbox:
        build(a.name, a.name, [{"bbox": [float(v) for v in a.bbox.split(",")], "maxz": a.maxz}], a.mbtiles, a.out,
              webp=not a.keep_images, minz=a.minz)
    else:
        ap.error("give --region, or --name and --bbox")
