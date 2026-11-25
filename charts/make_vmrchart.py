#!/usr/bin/env python3
"""Makes a VMR chart pack (.vmrchart) from NOAA Chart Display Service MBTiles, for the VMR's Map card.

    python3 make_vmrchart.py --region regions.json:juan-de-fuca-puget --out packs/  ncds_20c.mbtiles
    python3 make_vmrchart.py --name test --bbox=-124.8,47.0,-122.1,48.8 --maxz 13 --out packs/  ncds_20c.mbtiles

NOAA's MBTiles come from https://distribution.charts.noaa.gov/ncds/index.html (one file per region,
updated weekly). A pack is a set of areas, each a box (west,south,east,north) with the zooms kept there:
the whole region light (zoom 12 or 13), the straits and sounds at 14, harbours at 16. A region that runs
across two NOAA files takes both; where both have a tile, the first file with chart on it wins. Tiles with
no chart on them (fully transparent) are left out, so the map shows nothing there rather than a blank.
Each tile is kept as the smaller of NOAA's own image and a lossless WebP of it; tiles with the same bytes
(open water, plain land) are stored once.

The .vmrchart file (little-endian):
  header, 64 bytes:
    0  "VMRC"           4  u16 version (1)   6  u16 header length (64)
    8  u32 tiles        12 u32 index offset   16 u32 metadata offset   20 u32 metadata length
    24 f32 west, south, east, north (degrees)
    40 u8 min zoom      41 u8 max zoom        42 u16 tile size (pixels)
    44 u32 built (UTC)  48 u32 chart date (UTC: the newest NOAA file used)
    52 u32 data offset  56 u32 file length    60 u32 0
  metadata: UTF-8 JSON (name, title, source, attribution, areas)
  index: one 16-byte entry per tile, sorted by key: u64 key = zoom << 48 | x << 24 | y (XYZ, y down from
         the north), u32 offset, u32 length
  data: the tile images (PNG, JPEG or WebP; the VMR tells them apart by their first bytes)
Not for navigation: NOAA ENC charts shown as pictures, for situational awareness."""
import argparse, json, math, os, sqlite3, struct, sys, time, io, hashlib

MAGIC, VERSION, HDR, EXT = b"VMRC", 1, 64, ".vmrchart"
HFMT = "<4sHHIIIIffffBBHIIIII"


def tile_range(w, s, e, n, z):
    """XYZ tiles (x0, x1, y0, y1 inclusive) covering a box at zoom z."""
    def tx(lon): return int(math.floor((lon + 180.0) / 360.0 * (1 << z)))
    def ty(lat):
        lat = max(-85.0511, min(85.0511, lat)); r = math.radians(lat)
        return int(math.floor((1.0 - math.log(math.tan(r) + 1.0 / math.cos(r)) / math.pi) / 2.0 * (1 << z)))
    m = (1 << z) - 1
    return max(0, tx(w)), min(m, tx(e)), max(0, ty(n)), min(m, ty(s))


class Tiles:
    """Looks at each different tile image once: is it blank, how big is it, what is its smallest form."""
    def __init__(self, webp):
        self.webp, self.info = webp, {}          # sha1 -> (blank, px, smallest bytes)

    def look(self, data):
        h = hashlib.sha1(data).digest()
        if h not in self.info:
            blank, px, best = False, 256, data
            try:
                from PIL import Image
                im = Image.open(io.BytesIO(data)); im.load(); px = im.size[0]
                if im.mode in ("RGBA", "LA", "P"):
                    a = im.convert("RGBA").getchannel("A").getextrema()
                    blank = a[1] == 0
                if self.webp and not blank:
                    if im.mode not in ("RGB", "RGBA"): im = im.convert("RGBA")
                    b = io.BytesIO(); im.save(b, "WEBP", lossless=True, quality=75, method=4)   # as small as method 6, 60x faster
                    if len(b.getvalue()) < len(data): best = b.getvalue()
            except ImportError:
                pass
            except Exception:
                pass
            self.info[h] = (blank, px, best)
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


def build(name, title, areas, sources, out_dir, webp=True, minz=3, quiet=False):
    """areas: [{"bbox": [w,s,e,n], "maxz": z, "minz": z (optional)}]; sources: .mbtiles paths, best first."""
    dbs = [sqlite3.connect("file:%s?mode=ro" % p, uri=True) for p in sources]
    want = wanted(areas, minz)
    if not quiet: print("%s: %d tiles wanted across %d area(s)" % (name, len(want), len(areas)))
    T = Tiles(webp)
    blobs, index, where = [], [], {}            # where: sha1 -> blob number
    raw = got = blanks = 0; px = 0
    t0 = time.time()
    for k, (z, x, y) in enumerate(sorted(want)):
        row = (1 << z) - 1 - y                  # MBTiles rows count up from the south (TMS)
        pick = None
        for db in dbs:
            r = db.execute("SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?", (z, x, row)).fetchone()
            if not r or not r[0]: continue
            data = bytes(r[0]); h, (blank, p, best) = T.look(data)
            if blank: continue
            pick = (data, h, p, best); break
        if pick is None:
            blanks += 1; continue
        data, h, p, best = pick
        if not px: px = p
        raw += len(data)
        if h not in where:
            where[h] = len(blobs); blobs.append(best)
        index.append(((z << 48) | (x << 24) | y, where[h]))
        got += 1
        if not quiet and k % 20000 == 0 and k: print("  %d / %d tiles looked up, %.0f s" % (k, len(want), time.time() - t0))
    for db in dbs: db.close()
    if not index: sys.exit("%s: no chart tiles in %s for these areas" % (name, ", ".join(sources)))
    index.sort()
    W = min(a["bbox"][0] for a in areas); S = min(a["bbox"][1] for a in areas)
    E = max(a["bbox"][2] for a in areas); N = max(a["bbox"][3] for a in areas)
    zs = [k >> 48 for k, _ in index]
    built, chart = int(time.time()), mbtiles_date(sources)
    meta = json.dumps({"name": name, "title": title,
                       "source": "NOAA Chart Display Service (ENC): " + ", ".join(os.path.basename(p) for p in sources),
                       "attribution": "NOAA ENC charts: not for navigation", "areas": areas}, separators=(",", ":")).encode()
    meta_off = HDR; idx_off = meta_off + len(meta); idx_off += (-idx_off) % 16
    data_off = idx_off + 16 * len(index)
    offs, o = [], data_off
    for b in blobs: offs.append(o); o += len(b)
    flen = o
    if flen >= 1 << 32: sys.exit("%s: over 4 GB, too big for the microSD card's FAT32: use fewer or smaller areas" % name)
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, name + EXT)
    with open(path + ".part", "wb") as f:
        f.write(struct.pack(HFMT, MAGIC, VERSION, HDR, len(index), idx_off, meta_off, len(meta),
                            W, S, E, N, min(zs), max(zs), px or 256, built, chart, data_off, flen, 0))
        f.write(meta); f.write(b"\0" * (idx_off - meta_off - len(meta)))
        for key, bn in index: f.write(struct.pack("<QII", key, offs[bn], len(blobs[bn])))
        for b in blobs: f.write(b)
    os.replace(path + ".part", path)
    if not quiet:
        print("%s: %d tiles (%d different, %d empty left out), zoom %d-%d, %.1f MB (NOAA's images were %.1f MB) -> %s"
              % (name, got, len(blobs), blanks, min(zs), max(zs), flen / 1e6, raw / 1e6, path))
    return path


def read_pack(path):
    """The header and metadata of a pack, and a lookup for one tile (used by the tests)."""
    f = open(path, "rb"); h = struct.unpack(HFMT, f.read(HDR))
    if h[0] != MAGIC: raise ValueError("not a VMR chart pack")
    n, idx, mo, ml = h[3], h[4], h[5], h[6]
    f.seek(mo); meta = json.loads(f.read(ml))
    def tile(z, x, y):
        key = (z << 48) | (x << 24) | y; lo, hi = 0, n - 1
        while lo <= hi:
            mid = (lo + hi) // 2; f.seek(idx + 16 * mid); k, off, ln = struct.unpack("<QII", f.read(16))
            if k == key: f.seek(off); return f.read(ln)
            if k < key: lo = mid + 1
            else: hi = mid - 1
        return None
    return {"tiles": n, "bounds": h[7:11], "minz": h[11], "maxz": h[12], "px": h[13], "built": h[14],
            "chart": h[15], "len": h[17], "meta": meta}, tile


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
    a = ap.parse_args()
    if a.region:
        fn, rn = a.region.rsplit(":", 1)
        reg = next(r for r in json.load(open(fn))["regions"] if r["name"] == rn)
        build(reg["name"], reg["title"], reg["areas"], a.mbtiles, a.out, webp=not a.keep_images, minz=reg.get("minz", 3))
    elif a.name and a.bbox:
        build(a.name, a.name, [{"bbox": [float(v) for v in a.bbox.split(",")], "maxz": a.maxz}], a.mbtiles, a.out,
              webp=not a.keep_images, minz=a.minz)
    else:
        ap.error("give --region, or --name and --bbox")
