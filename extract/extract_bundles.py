"""Export decrypted Unity bundles: rml_a -> extracted/images/*.png, snd_a -> extracted/sound/*.wav, pg_a -> extracted/rom/*.
usage: extract_bundles.py [images WORKER NWORKERS | sound | rom]"""
import UnityPy, os, sys
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paths import DATA
ROOT = os.path.join(DATA, "cdn")
DEC = os.path.join(ROOT, "decrypted", "ProductImages"); OUT = os.path.join(ROOT, "extracted")
mode = sys.argv[1]
if mode == "images":
    w, nw = int(sys.argv[2]), int(sys.argv[3]); out = os.path.join(OUT, "images"); os.makedirs(out, exist_ok=True)
    env = UnityPy.load(os.path.join(DEC, "KLU_GraphicResources", "rml_a.asset"))
    texs = [o for o in env.objects if o.type.name == "Texture2D"]
    for i, o in enumerate(texs[w::nw]):
        d = o.read(); dst = os.path.join(out, d.m_Name + ".png")
        if not os.path.exists(dst): d.image.save(dst)
        if i % 2000 == 0: print(w, i, len(texs) // nw, flush=True)
elif mode == "sound":
    out = os.path.join(OUT, "sound"); os.makedirs(out, exist_ok=True)
    env = UnityPy.load(os.path.join(DEC, "KLU_SoundResources", "snd_a.asset"))
    for o in env.objects:
        if o.type.name == "AudioClip":
            for name, data in o.read().samples.items(): open(os.path.join(out, name), "wb").write(data)
elif mode == "rom":
    out = os.path.join(OUT, "rom"); os.makedirs(out, exist_ok=True)
    env = UnityPy.load(os.path.join(DEC, "KLU_GraphicResources", "pg_a.asset"))
    for o in env.objects:
        if o.type.name == "TextAsset":
            d = o.read(); s = d.m_Script
            open(os.path.join(out, d.m_Name), "wb").write(s.encode("utf-8", "surrogateescape") if isinstance(s, str) else bytes(s))
print("DONE", mode, flush=True)
