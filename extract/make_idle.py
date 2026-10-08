"""Normal-mode idle loop (the screen seen most of the game) rendered from the GOD scene data.

Layers, back to front: background movie (Sn_Aa00_0001..0007, rotated with a crossfade), Ichigo idle
(Sn_Aa01_0000), cockpit frame parts (Sn_Aa02/03/04_0000), the chibis and the KLEIN plate (taken from the
on-screen state of the Sn_Aa06 transition, which shows them before sliding out).
2400 frames @30fps: a multiple of every layer loop (Ichigo 100, frame parts 12/50/120), so it loops seamlessly.

usage: make_idle.py <out.mp4> [frames]
"""
import os, sys, subprocess
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
import god_render as g

FPS = 30
TOTAL = 2400
FADE = 20
BGS = [f"Sn_Aa00_{k:04d}" for k in range(1, 8)]
FRONT = ["Sn_Aa01_0000", "Sn_Aa02_0000", "Sn_Aa03_0000", "Sn_Aa04_0000"]
# chibis (anchor 400,400 at y 2080) and the KLEIN plate: image id, LCD x, y
EXTRA = [(0xB2F2, (248 - 400) // 4, 420), (0xB277, (608 - 400) // 4, 420), (0xB2BF, (936 - 400) // 4, 420),
         (0xB2AB, (1288 - 400) // 4, 420), (0xB3DE, 1024 - 422, 576 - 126)]


class Movie:
    """sequential looped reader of a background movie at LCD size"""

    def __init__(self, scene, skip=0):
        if g.FF is None:
            import imageio_ffmpeg
            g.FF = imageio_ffmpeg.get_ffmpeg_exe()
        mid = g.layers(scene)[0][2][3]
        path = os.path.join(g.MOV_DIR, f"m{mid:04d}.m1v")
        self.p = subprocess.Popen([g.FF, "-v", "error", "-stream_loop", "-1", "-i", path, "-vf",
                                   "scale=1024:576:flags=lanczos", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                                  stdout=subprocess.PIPE)
        for _ in range(skip):
            self.next()

    def next(self):
        return Image.frombytes("RGB", (1024, 576), self.p.stdout.read(1024 * 576 * 3))

    def close(self):
        self.p.kill()


def main():
    out = sys.argv[1]
    total = int(sys.argv[2]) if len(sys.argv) > 2 else TOTAL
    n = len(BGS)
    seg = [TOTAL // n + (1 if k < TOTAL % n else 0) for k in range(n)]
    starts = [sum(seg[:k]) for k in range(n)]
    front = g.scene_layers(FRONT)
    extra = [(g.load_image(i), x, y) for i, x, y in EXTRA]

    enc = subprocess.Popen([g.FF or __import__("imageio_ffmpeg").get_ffmpeg_exe(), "-v", "error", "-y",
                            "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "1024x576", "-r", str(FPS), "-i", "-",
                            "-f", "lavfi", "-i", "anullsrc=r=32768:cl=mono",
                            "-vf", "scale=512:288:flags=lanczos", "-c:v", "libx264", "-crf", "14", "-pix_fmt",
                            "yuv420p", "-c:a", "aac", "-shortest", "-t", f"{total / FPS:.4f}", out],
                           stdin=subprocess.PIPE)
    cur = Movie(BGS[0])
    prev = Movie(BGS[-1], seg[-1])  # the wrap-around fade continues the last segment
    k = 0
    for f in range(total):
        if k + 1 < n and f == starts[k + 1]:
            if prev:
                prev.close()
            k += 1
            prev, cur = cur, Movie(BGS[k])
        bg = cur.next()
        t = f - starts[k]
        if prev is not None:
            if t < FADE:
                bg = Image.blend(prev.next(), bg, (t + 1) / (FADE + 1))
            else:
                prev.close()
                prev = None
        frame = g.compose(front, f, (0, 0, 0, 0))
        canvas = bg.convert("RGBA")
        canvas.alpha_composite(frame)
        for im, x, y in extra:
            canvas.alpha_composite(im, (max(x, 0), y), (max(-x, 0), 0))
        enc.stdin.write(canvas.convert("RGB").tobytes())
        if f % 100 == 0:
            print(f, flush=True)
    enc.stdin.close()
    enc.wait()
    cur.close()
    if prev:
        prev.close()


SPLIT = 600   # frames per background clip: a multiple of every layer loop (100, 12, 30, 120)


def main_split(out_dir):
    """one clip per background (the game chains them and knows which one is on screen): background k from
    its frame FADE, the front layers at a fixed phase, and a crossfade into background k+1's first FADE
    frames at the end, so clip k+1 (starting at FADE) continues it seamlessly."""
    os.makedirs(out_dir, exist_ok=True)
    n = len(BGS)
    front = g.scene_layers(FRONT)
    extra = [(g.load_image(i), x, y) for i, x, y in EXTRA]
    ff = g.FF or __import__("imageio_ffmpeg").get_ffmpeg_exe()
    for k in range(n):
        out = os.path.join(out_dir, f"idle_{k}.mp4")
        enc = subprocess.Popen([ff, "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "1024x576",
                                "-r", str(FPS), "-i", "-", "-f", "lavfi", "-i", "anullsrc=r=32768:cl=mono",
                                "-vf", "scale=512:288:flags=lanczos", "-c:v", "libx264", "-crf", "14",
                                "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", "-t", f"{SPLIT / FPS:.4f}", out],
                               stdin=subprocess.PIPE)
        cur = Movie(BGS[k], FADE)
        nxt = None
        for f in range(SPLIT):
            bg = cur.next()
            t = f - (SPLIT - FADE)
            if t >= 0:
                if nxt is None:
                    nxt = Movie(BGS[(k + 1) % n])
                bg = Image.blend(bg, nxt.next(), (t + 1) / (FADE + 1))
            # fixed phase past every pre part: all clips show the front layers identically
            frame = g.compose(front, f + SPLIT, (0, 0, 0, 0))
            canvas = bg.convert("RGBA")
            canvas.alpha_composite(frame)
            for im, x, y in extra:
                canvas.alpha_composite(im, (max(x, 0), y), (max(-x, 0), 0))
            enc.stdin.write(canvas.convert("RGB").tobytes())
        enc.stdin.close()
        enc.wait()
        cur.close()
        if nxt:
            nxt.close()
        print(out, flush=True)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--split":
        main_split(sys.argv[2])
    else:
        main()
