"""Dump the LK2 asset CDN: downloads every chunk in manifest.0, verifies MD5,
then reassembles split files into cdn/files/<path>.
Raw chunks keep their CDN names in cdn/Android_data/ (mirror of the server layout)."""
import os, sys, hashlib, urllib.request, concurrent.futures as cf, time
PLATFORM = sys.argv[1] if len(sys.argv) > 1 else "Android"   # Android | iOS
BASE = f"https://d2ey4eaka9of9j.cloudfront.net/opsim/yTcv4gqc4PPRh2Lea9dBAhD351qPPrlb/0079C238B2/{PLATFORM}_data"
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paths import DATA
ROOT = os.path.join(DATA, "cdn")
RAW = os.path.join(ROOT, f"{PLATFORM}_data"); OUT = os.path.join(ROOT, "files" if PLATFORM == "Android" else f"files_{PLATFORM}")
os.makedirs(RAW, exist_ok=True)

def fetch(url, dst):
    with urllib.request.urlopen(url, timeout=60) as r, open(dst + ".part", "wb") as f:
        while (b := r.read(1 << 20)): f.write(b)
    os.replace(dst + ".part", dst)

SPAN = 1024
def md5(p):
    """AssetHash128: sampled MD5 (Installer AssetHash128.TryCalcHashAsync).
    size < 2*SPAN -> MD5(first SPAN bytes)
    else          -> MD5(head SPAN) ^ MD5(SPAN @ size/2) ^ MD5(tail SPAN)"""
    n = os.path.getsize(p)
    with open(p, "rb") as f:
        if n < 2 * SPAN: return hashlib.md5(f.read(SPAN)).hexdigest().upper()
        acc = 0
        for off in (0, n // 2, n - SPAN):
            f.seek(off); acc ^= int.from_bytes(hashlib.md5(f.read(SPAN)).digest(), "big")
    return f"{acc:032X}"

man = os.path.join(RAW, "manifest.0")
fetch(BASE + "/manifest.0", man)
items = []
for line in open(man, encoding="utf-8").read().splitlines():
    if not line.strip(): continue
    path, split, idx, h, start, size = line.split(",")
    items.append(dict(path=path, split=int(split), idx=int(idx), hash=h, start=int(start), size=int(size),
                      name=f"{idx}.{h}.{split}"))

def job(it):
    dst = os.path.join(RAW, it["name"])
    for attempt in range(5):
        if os.path.exists(dst) and os.path.getsize(dst) == it["size"] and md5(dst) == it["hash"]:
            return it["name"], "ok"
        try: fetch(f"{BASE}/{it['name']}", dst)
        except Exception as e:
            print("retry", it["name"], e, flush=True); time.sleep(3)
    return it["name"], "FAILED"

with cf.ThreadPoolExecutor(4) as ex:
    for name, st in ex.map(job, items):
        print(st, name, flush=True)

# reassemble
for path in dict.fromkeys(i["path"] for i in items):
    parts = sorted((i for i in items if i["path"] == path), key=lambda i: i["split"])
    dst = os.path.join(OUT, path); os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "wb") as f:
        for p in parts:
            assert f.tell() == p["start"], (path, p)
            with open(os.path.join(RAW, p["name"]), "rb") as src:
                while (b := src.read(1 << 22)): f.write(b)
    print("built", path, os.path.getsize(dst), flush=True)
# full-file checksums for preservation (the game's own hash only samples 3 KiB)
with open(os.path.join(ROOT, f"SHA256SUMS_{PLATFORM}.txt"), "w", encoding="utf-8") as out:
    for d in (RAW, OUT):
        for dp, _, fs in os.walk(d):
            for fn in sorted(fs):
                fp = os.path.join(dp, fn); h = hashlib.sha256()
                with open(fp, "rb") as f:
                    while (b := f.read(1 << 22)): h.update(b)
                out.write(f"{h.hexdigest()}  {os.path.relpath(fp, ROOT).replace(os.sep, '/')}\n")
print("DONE")
