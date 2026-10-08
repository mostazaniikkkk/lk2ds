"""rmv_c.asset (CPK) -> cdn/extracted/movies/mNNNN.m1v + mNNNN_alpha.m1v (decrypted MPEG-1 elementary streams)."""
import os, sys
sys.path.insert(0, os.path.dirname(__file__)); import cpk, usm
from paths import DATA as ROOT
OUT = os.path.join(ROOT, "cdn", "extracted", "movies"); os.makedirs(OUT, exist_ok=True)
f, h, toc, base = cpk.open_cpk(os.path.join(ROOT, "cdn", "decrypted", "ProductImages", "KLU_GraphicResources", "rmv_c.asset"))
for n, e in enumerate(toc):
    f.seek(base + e["FileOffset"]); data = f.read(e["FileSize"])
    s = usm.demux(data); stem = os.path.join(OUT, os.path.splitext(e["FileName"])[0])
    open(stem + ".m1v", "wb").write(s.get(b"@SFV", b""))
    if b"@ALP" in s: open(stem + "_alpha.m1v", "wb").write(s[b"@ALP"])
    if n % 50 == 0: print(n, e["FileName"], flush=True)
print("DONE", len(toc))
