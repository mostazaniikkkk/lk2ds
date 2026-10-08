"""Movies + their scripted sound: for every movie, find the GOD scene(s) that play it (GODRom_L.GOD + scene names
from main.bin), render the matching sound sequence SD_SEQ_Pt_<id> from sacinfo.csv (WAIT/PLAY/STOP/VOL/FADE, ms),
cut the window where the movie is on screen and mux: color + alpha (stream copy) + FLAC audio.
Output: release/movies_with_audio/mNNNN.mkv + index.csv"""
import os, sys, re, csv, wave, functools, subprocess, collections
import numpy as np
sys.path.insert(0, os.path.dirname(__file__)); import god
import imageio_ffmpeg
FF = imageio_ffmpeg.get_ffmpeg_exe()
from paths import DATA as ROOT
SR_DIR = os.path.join(ROOT, "cdn", "decrypted", "ProductImages", "KLU_SoundResources")
WAV_DIR = os.path.join(ROOT, "cdn", "extracted", "sound")
MOV_DIR = os.path.join(ROOT, "cdn", "extracted", "movies")
OUT = os.path.join(ROOT, "release", "movies_with_audio")
TMP = os.path.join(ROOT, "scratch", "mux")
RATE, FPS = 44100, 30

def rows(name):
    for l in open(os.path.join(SR_DIR, name), encoding="utf-8-sig"):
        l = l.strip()
        if l: yield l.split(",")
cache = {int(r[0]): r[1] for r in rows("SD_cache.txt")}
sac_by_name = {r[1]: int(r[0]) for r in rows("sac_map.csv")}
steps = collections.defaultdict(list)
for r in rows("sacinfo.csv"):
    if r[0].isdigit() and len(r) >= 4: steps[int(r[0])].append((int(r[1]), int(r[2]), int(r[3]), [int(x) for x in r[4:]]))

@functools.lru_cache(maxsize=256)
def load_wav(wid):
    w = wave.open(os.path.join(WAV_DIR, cache[wid]))
    a = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float32) / 32768
    a = a.reshape(-1, w.getnchannels())
    if a.shape[1] == 1: a = np.repeat(a, 2, axis=1)
    if w.getframerate() != RATE:
        n = int(round(len(a) * RATE / w.getframerate())); x = np.linspace(0, len(a) - 1, n)
        a = np.stack([np.interp(x, np.arange(len(a)), a[:, c]) for c in range(2)], axis=1).astype(np.float32)
    return a

def render_sac(sac, length_s):
    """Render one sound command list into a stereo float buffer of length_s seconds."""
    n = int(length_s * RATE) + 1; out = np.zeros((n, 2), np.float32)
    t = 0.0
    tracks = {}                    # track -> dict(start, wav, next, loop, stop, vol events)
    vol = collections.defaultdict(lambda: [(0.0, 1.0, 1.0)])   # track -> [(time, vol1, vol2)] step changes
    plays = []
    def stop(tr, at):
        if tr in tracks and tracks[tr]["stop"] is None: tracks[tr]["stop"] = at
    for _, tr, op, a in sorted(steps[sac]):
        if op == 4: t += a[0] / 1000
        elif op == 9:
            stop(tr, t); p = {"track": tr, "start": t, "wav": a[0], "next": a[1] if len(a) > 1 else 0,
                              "loop": len(a) > 2 and a[2] == 1, "stop": None}
            tracks[tr] = p; plays.append(p)
        elif op == 10: stop(tr, t)
        elif op in (5, 6):
            _, v1, v2 = vol[tr][-1]; v = a[0] / 255
            vol[tr].append((t, v, v2) if op == 5 else (t, v1, v))
        elif op == 8 and len(a) >= 3:                       # FADE(from, to, ms) on volume 1, as 32 small steps
            _, v1, v2 = vol[tr][-1]; d = a[2] / 1000
            for k in range(33): vol[tr].append((t + d * k / 32, (a[0] + (a[1] - a[0]) * k / 32) / 255, v2))
    for p in plays:
        s0 = int(p["start"] * RATE)
        if s0 >= n: continue
        end = n if p["stop"] is None else min(n, int(p["stop"] * RATE))
        # build source: wav, then optional chained wav, looping when flagged
        first = load_wav(p["wav"]); chunks = [first]; total = len(first)
        nxt = load_wav(p["next"]) if p["next"] else (first if p["loop"] else None)
        while nxt is not None and s0 + total < end:
            chunks.append(nxt); total += len(nxt)
            if not p["loop"]: break
        src = np.concatenate(chunks)[:max(0, end - s0)]
        if not len(src): continue
        tt = (s0 + np.arange(len(src))) / RATE
        g = np.ones(len(src), np.float32)
        ev = vol[p["track"]]
        ev = sorted(ev, key=lambda e: e[0]); times = np.array([e[0] for e in ev]); idx = np.searchsorted(times, tt, side="right") - 1
        g *= np.array([e[1] * e[2] for e in ev], np.float32)[idx]
        out[s0:s0 + len(src)] += src * g[:, None]
    return out

def main():
    os.makedirs(OUT, exist_ok=True); os.makedirs(TMP, exist_ok=True)
    g = god.God(os.path.join(ROOT, "cdn", "extracted", "rom", "GODRom_L.GOD"))
    names = god.scene_names(os.path.join(ROOT, "cdn", "extracted", "rom", "main.bin"))
    uses = collections.defaultdict(list)
    for k, n in enumerate(names):
        m = re.fullmatch(r"Sn_([A-Z][a-z]\d\d_\d{4})", n)
        sac = sac_by_name.get(f"SD_SEQ_Pt_{m.group(1)}") if m else None
        for u in god.scene_movies(g, k): uses[u["movie"]].append((n, u["start"], u["frames"], sac, u["cond"] is not None or u["variant_wrk"] is not None))
    only = {int(x) for x in sys.argv[1:]} or None
    index = []
    scene_audio = {}
    for mv in sorted(uses):
        if only and mv not in only: continue
        cands = [u for u in uses[mv] if u[3] is not None]
        if not cands: index.append({"movie": f"m{mv:04d}", "scene": uses[mv][0][0], "sound": "", "start_frame": uses[mv][0][1], "alternatives": len(uses[mv]) - 1, "status": "no scripted sound (background/BGM-driven)"}); continue
        cands.sort(key=lambda u: (u[4], u[1], u[0]))     # prefer unconditional, earliest in scene
        scene, start, frames, sac, _ = cands[0]
        need = (start + frames) / FPS + 0.5
        if sac not in scene_audio or scene_audio[sac].shape[0] < int(need * RATE):
            scene_audio[sac] = render_sac(sac, max(need, 1))
        seg = scene_audio[sac][int(start / FPS * RATE):int((start + frames) / FPS * RATE)]
        peak = float(np.abs(seg).max()) if len(seg) else 0.0
        if peak > 1: seg = seg / peak                    # avoid clipping when many layers sum up
        wav_p = os.path.join(TMP, f"m{mv:04d}.wav")
        with wave.open(wav_p, "wb") as w:
            w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE)
            w.writeframes((np.clip(seg, -1, 1) * 32767).astype("<i2").tobytes())
        v, a = os.path.join(MOV_DIR, f"m{mv:04d}.m1v"), os.path.join(MOV_DIR, f"m{mv:04d}_alpha.m1v")
        dst = os.path.join(OUT, f"m{mv:04d}.mkv")
        r = subprocess.run([FF, "-hide_banner", "-v", "error", "-y",
                            "-fflags", "+genpts", "-r", "30", "-f", "mpegvideo", "-i", v,
                            "-fflags", "+genpts", "-r", "30", "-f", "mpegvideo", "-i", a, "-i", wav_p,
                            "-map", "0:v", "-map", "1:v", "-map", "2:a", "-c:v", "copy", "-c:a", "flac",
                            "-metadata:s:v:0", "title=color", "-metadata:s:v:1", "title=alpha",
                            "-metadata:s:a:0", f"title={[k for k, s in sac_by_name.items() if s == sac][0]}",
                            "-disposition:v:1", "0", "-metadata", f"title=m{mv:04d} ({scene})", dst], capture_output=True, text=True)
        os.remove(wav_p)
        index.append({"movie": f"m{mv:04d}", "scene": scene, "sound": [k for k, s in sac_by_name.items() if s == sac][0],
                      "start_frame": start, "alternatives": len(uses[mv]) - 1,
                      "status": ("ok" if r.returncode == 0 else "mux error: " + r.stderr[:200]) + ("" if peak > 0.001 else " (sound silent: muted voice/placeholder)")})
        print(index[-1], flush=True)
    with open(os.path.join(OUT, "index.csv" if not only else "index_partial.csv"), "w", newline="", encoding="utf-8") as f:
        wr = csv.DictWriter(f, fieldnames=list(index[0])); wr.writeheader(); wr.writerows(index)

if __name__ == "__main__":
    main()
