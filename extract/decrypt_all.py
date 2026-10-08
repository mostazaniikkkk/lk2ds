"""Decrypt every EB01 file in cdn/files -> cdn/decrypted (same relative paths); copies plaintext files as-is."""
import os, sys, shutil, time
sys.path.insert(0, os.path.dirname(__file__)); import eb01
from paths import DATA
ROOT = os.path.join(DATA, "cdn")
SRC, DST = os.path.join(ROOT, "files"), os.path.join(ROOT, "decrypted")
for dp, _, fs in os.walk(SRC):
    for fn in fs:
        s = os.path.join(dp, fn); d = os.path.join(DST, os.path.relpath(s, SRC))
        os.makedirs(os.path.dirname(d), exist_ok=True)
        with open(s, "rb") as f: magic = f.read(4)
        t = time.time()
        if magic == b"EB01": eb01.decrypt_file(s, d); kind = "EB01"
        else: shutil.copyfile(s, d); kind = "copy"
        with open(d, "rb") as f: head = f.read(16)
        print(f"{kind} {os.path.relpath(s, SRC)} {time.time()-t:.1f}s -> {head[:12]!r}", flush=True)
print("DONE")
