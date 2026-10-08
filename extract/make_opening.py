"""Opening movie for the DS boot, rendered from the GOD scene data: the BIG bonus clip played over
"Resonation Wings" (Machico), Sn_Dd01_0001.

Layers: movie m0268 (frames 0-323) then m0267 (324-2911), the song caption and the other image layers;
the scene goes on with an illustration slideshow after the animation, so it is cut at END (fade out).
Audio = SD_BGM_Dd01 rendered from sacinfo like tools/add_bgm.py, faded out with the picture.

usage: make_opening.py <out.mp4>
"""
import os, sys, wave, subprocess
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
import god_render as g
import movies_audio as M

SCENE = "Sn_Dd01_0001"
BGM = "SD_BGM_Dd01"
END = 2750  # the animation ends here; the rest is a slideshow
FPS = 30
FADE_OUT = 2.0
GAIN = 0.9


def movie_frames(mid):
    path = os.path.join(g.MOV_DIR, f"m{mid:04d}.m1v")
    p = subprocess.Popen([M.FF, "-v", "error", "-i", path, "-vf", "scale=1024:576:flags=lanczos", "-f", "rawvideo",
                          "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE)
    while True:
        raw = p.stdout.read(1024 * 576 * 3)
        if len(raw) < 1024 * 576 * 3:
            break
        yield Image.frombytes("RGB", (1024, 576), raw)


def main():
    out = sys.argv[1]
    ls = g.scene_layers([SCENE])
    movies = sorted((l for l in ls if l[2][2] == 1), key=lambda l: l[6])
    images = [l for l in ls if l[2][2] == 0]
    total = END

    # audio first: a wav the video encode muxes in
    pcm = M.render_sac(M.sac_by_name[BGM], total / FPS + 1)[:int(total / FPS * M.RATE)] * GAIN
    n = int(FADE_OUT * M.RATE)
    pcm[-n:] *= np.linspace(1, 0, n)[:, None]
    wav = out + ".wav"
    with wave.open(wav, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(M.RATE)
        w.writeframes((np.clip(pcm, -1, 1) * 32767).astype("<i2").tobytes())

    enc = subprocess.Popen([M.FF, "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "1024x576",
                            "-r", str(FPS), "-i", "-", "-i", wav, "-vf", "scale=512:288:flags=lanczos",
                            "-c:v", "libx264", "-crf", "14", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k",
                            "-shortest", out], stdin=subprocess.PIPE)
    f = 0
    for mv in movies:
        for k, frame in enumerate(movie_frames(mv[2][3])):
            if k >= mv[7] or f >= total:
                break
            canvas = frame.convert("RGBA")
            canvas.alpha_composite(g.compose(images, f, (0, 0, 0, 0)))
            left = total - f  # fade the picture with the music
            if left <= FADE_OUT * FPS:
                canvas = Image.blend(Image.new("RGBA", canvas.size, (0, 0, 0, 255)), canvas, left / (FADE_OUT * FPS))
            enc.stdin.write(canvas.convert("RGB").tobytes())
            f += 1
            if f % 200 == 0:
                print(f, flush=True)
    enc.stdin.close()
    enc.wait()
    os.remove(wav)
    print("frames", f, "of", total)


if __name__ == "__main__":
    main()
