"""Minimal GOD scene compositor: renders layers of one or more scenes the way the KLU GOD engine places them.

Coordinates are in screen units (SCR = 4096x2304 for a 1024x576 LCD, i.e. 1/4 pixel). Each role (INS) has an
anime group (ANG -> AGI -> AN2: image patterns or a movie) and an action group (ACG -> ACT -> PAR) whose
MAP coefficients give position / anchor / zoom. Only what the idle scenes need is implemented: the action's
initial MAP (no keyframe interpolation), image patterns at one pattern per frame, movies looped.

usage: god_render.py <out_dir> <frames> <scene> [<scene> ...]   (scenes listed bottom layer first)
"""
import os, sys, subprocess
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
import god

god.FMT.update({"ACG": "<III", "ACT": "<III", "PAR": "<II", "MAP": "<IIIfII", "POS": "<ii", "ANC": "<ii",
                "ZOM": "<ff", "SCR": "<II"})
from paths import DATA as ROOT
G = god.God(os.path.join(ROOT, "cdn/extracted/rom/GODRom_L.GOD"))
T = {n: G.table(n) for n in "SN2 TRG SQ2 INS ANG AGI AN2 PTNTBL PTN PRMTBL PRM RSC IMG IMA ACG ACT PAR MAP POS ANC "
                            "ZOM SCR".split()}
NAMES = god.scene_names(os.path.join(ROOT, "cdn/extracted/rom/main.bin"))
SW, SH = T["SCR"][0]
SCALE = SW // 1024  # screen units per LCD pixel
IMG_DIR = os.path.join(ROOT, "cdn/extracted/images")
MOV_DIR = os.path.join(ROOT, "cdn/extracted/movies")
FF = None


def map_coef(m):
    p, a, z, rot, _, _ = T["MAP"][m]
    pos = T["POS"][p] if p < len(T["POS"]) else (0, 0)
    anc = T["ANC"][a] if a < len(T["ANC"]) else (0, 0)
    zom = T["ZOM"][z] if z < len(T["ZOM"]) else (1.0, 1.0)
    return pos, anc, zom


def layers(scene):
    """[(trigger priority, role priority, anime, pos, anc, zom, start, dur, length, blend)] for every role;
    blend = INS field 3 (8: additive, used by light effects drawn on black)"""
    out = []
    k = NAMES.index(scene)
    ts, t0 = T["SN2"][k]
    for t in range(t0, t0 + ts):
        tr = T["TRG"][t]
        sq = T["SQ2"][tr[0]]
        for part, (n, s0) in (("pre", (sq[4], sq[8])), ("loop", (sq[5], sq[9]))):
            if s0 == 0xFFFFFFFF:
                continue
            for r in range(s0, s0 + n):
                time, dur, pri, blend, _, _, ang, _, acg, _ = T["INS"][r]
                pos, anc, zom = (SW // 2, SH // 2), (SW // 2, SH // 2), (1.0, 1.0)
                motion = []
                if acg < len(T["ACG"]):
                    _, asz, act0 = T["ACG"][acg]
                    if asz:
                        act = T["ACT"][act0]
                        pos, anc, zom = map_coef(act[0])
                        # keyframes: (frames, MAP) whose coefficients are per-frame deltas (slides, shakes, zooms)
                        motion = [(d, *map_coef(m)) for d, m in (T["PAR"][q] for q in range(act[2], act[2] + act[1]))]
                asz, a0 = T["ANG"][ang]
                an = T["AN2"][T["AGI"][a0][0]]  # first variant
                out.append((tr[11], pri, an, pos, anc, zom, time + (sq[2] if part == "loop" else 0), dur,
                            sq[2] + sq[3], blend, tuple(motion), sq[2]))
    return out


def pattern_images(an, f):
    """image ids drawn by an image anime at frame f"""
    psz, dur, ptype, pid = an
    p = T["PTN"][T["PTNTBL"][pid + (f % psz)][0]]
    ids = []
    for q in range(p[1], p[1] + p[0]):
        pr = T["PRM"][T["PRMTBL"][q][0]]
        ls, isz, l0 = T["RSC"][pr[0]]
        i = l0 + pr[1] * isz + pr[2]
        if i < len(T["IMG"]) and T["IMG"][i][0] != 0xFFFFFFFF:
            ids.append(T["IMG"][i][0])
    return ids


_img_cache = {}


def load_image(i):
    """image i at its real size: textures are stored shrunk to power-of-two sizes, IMA keeps the original"""
    if i not in _img_cache:
        path = os.path.join(IMG_DIR, f"i{i:08X}.png")
        w, h = T["IMA"][i][1:3] if i < len(T["IMA"]) else (1, 1)
        if not os.path.exists(path):  # not extracted (e.g. ids past the image table): draw nothing
            return Image.new("RGBA", (1, 1))
        im = Image.open(path).convert("RGBA")
        if im.size != (w, h):
            im = im.resize((w, h), Image.LANCZOS)
        if len(_img_cache) > 512:
            _img_cache.clear()
        _img_cache[i] = im
    return _img_cache[i]


_movie_cache = {}


def movie_frame(mid, f):
    """frame f (looped) of raw movie m<id> with its alpha, as RGBA"""
    if mid not in _movie_cache:
        global FF
        if FF is None:
            import imageio_ffmpeg
            FF = imageio_ffmpeg.get_ffmpeg_exe()
        frames = []
        for suffix in ("", "_alpha"):
            path = os.path.join(MOV_DIR, f"m{mid:04d}{suffix}.m1v")
            if not os.path.exists(path):
                frames.append(None)
                continue
            # the app's movies are stored at half size (512x288): scale to the 1024x576 LCD
            raw = subprocess.run([FF, "-v", "error", "-i", path, "-vf", "scale=1024:576:flags=lanczos",
                                  "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                                 capture_output=True).stdout
            n = len(raw) // (1024 * 576 * 3)
            frames.append([Image.frombytes("RGB", (1024, 576), raw[i * 1024 * 576 * 3:(i + 1) * 1024 * 576 * 3])
                           for i in range(n)])
        _movie_cache[mid] = frames
    rgb, alpha = _movie_cache[mid]
    im = rgb[f % len(rgb)].convert("RGBA")
    if alpha:
        im.putalpha(alpha[f % len(alpha)].convert("L"))
    return im


def scene_layers(scenes):
    ls = []
    for s in scenes:
        ls += layers(s)
    # lower priority numbers are in front: draw the highest numbers first
    ls.sort(key=lambda l: (-l[0], -l[1]))
    return ls


def render(scenes, frames, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    ls = scene_layers(scenes)
    for f in range(frames):
        compose(ls, f).convert("RGB").save(os.path.join(out_dir, f"f{f:05d}.png"))
    return frames


def compose(ls, f, bg=(0, 0, 0, 255), base=None):
    """frame f of the layers over a solid colour, or over `base` (1024x576 image: what the layers overlay)"""
    canvas = base.convert("RGBA").copy() if base is not None else Image.new("RGBA", (1024, 576), bg)
    if True:
        for trig_pri, pri, an, pos, anc, zom, start, dur, length, blend, *rest in ls:
            # a trigger plays its pre part once, then repeats only its loop part
            pre = rest[1] if len(rest) > 1 else 0
            if length and f >= length:
                f2 = pre + (f - pre) % (length - pre) if length > pre else f % length
            else:
                f2 = f
            lf = f2 - start
            if lf < 0 or (dur and lf >= dur):
                continue
            if rest and rest[0]:
                pos, anc, zom = moved(rest[0], pos, anc, zom, lf)
            if an[2] == 1:
                imgs = [movie_frame(an[3], lf)]
            else:
                imgs = [load_image(i) for i in pattern_images(an, lf)]
            for im in imgs:
                if zom != (1.0, 1.0) and zom[0] > 0:
                    im = im.resize((max(1, round(im.width * zom[0])), max(1, round(im.height * zom[1]))))
                x = round((pos[0] - anc[0] * zom[0]) / SCALE)
                y = round((pos[1] - anc[1] * zom[1]) / SCALE)
                if blend == BLEND_ADD:
                    add(canvas, im, x, y)
                elif blend == BLEND_MUL:
                    mul(canvas, im, x, y)
                else:
                    canvas.alpha_composite(im, (x, y)) if x >= 0 and y >= 0 else canvas.paste(im, (x, y), im)
    return canvas


BLEND_ADD = 8
BLEND_MUL = 2  # white vignettes and tints drawn over a scene


def mul(canvas, im, x, y):
    """multiply blend of im (weighted by its alpha) onto canvas at (x, y), clipped"""
    x0, y0 = max(x, 0), max(y, 0)
    x1, y1 = min(x + im.width, canvas.width), min(y + im.height, canvas.height)
    if x1 <= x0 or y1 <= y0:
        return
    src = np.asarray(im.crop((x0 - x, y0 - y, x1 - x, y1 - y)), dtype=np.uint32)
    box = (x0, y0, x1, y1)
    dst = np.asarray(canvas.crop(box), dtype=np.uint32)
    a = src[:, :, 3:4]
    rgb = (dst[:, :, :3] * (255 - a) + dst[:, :, :3] * src[:, :, :3] // 255 * a) // 255
    out = np.concatenate([rgb, dst[:, :, 3:4]], 2).astype(np.uint8)
    canvas.paste(Image.fromarray(out, "RGBA"), box)


def moved(motion, pos, anc, zom, lf):
    """apply the per-frame deltas of the action keyframes for the first lf frames"""
    px, py = pos
    ax, ay = anc
    zx, zy = zom
    left = lf
    for d, dp, da, dz in motion:
        n = min(d, left)
        if n <= 0:
            break
        px += dp[0] * n
        py += dp[1] * n
        ax += da[0] * n
        ay += da[1] * n
        zx += dz[0] * n
        zy += dz[1] * n
        left -= n
    return (px, py), (ax, ay), (zx, zy)


def add(canvas, im, x, y):
    """additive blend of im (weighted by its alpha) onto canvas at (x, y), clipped"""
    x0, y0 = max(x, 0), max(y, 0)
    x1, y1 = min(x + im.width, canvas.width), min(y + im.height, canvas.height)
    if x1 <= x0 or y1 <= y0:
        return
    src = np.asarray(im.crop((x0 - x, y0 - y, x1 - x, y1 - y)), dtype=np.uint16)
    box = (x0, y0, x1, y1)
    dst = np.asarray(canvas.crop(box), dtype=np.uint16)
    rgb = dst[:, :, :3] + (src[:, :, :3] * src[:, :, 3:4] >> 8)
    out = np.concatenate([np.minimum(rgb, 255), dst[:, :, 3:4]], 2).astype(np.uint8)
    canvas.paste(Image.fromarray(out, "RGBA"), box)


if __name__ == "__main__":
    out, n = sys.argv[1], int(sys.argv[2])
    render(sys.argv[3:], n, out)
    print("rendered", n, "frames")
