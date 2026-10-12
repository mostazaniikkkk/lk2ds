"""Song list of the machine (SONIC MODE song select, Sn_Ha09: titles in image bank RSC 1944, songs SD_BGM_Ha09_NN):
every song complete (intro + one pass of its loop), as a FastVideoDS clip streamed from data/lk2ds/movies (too long
for RAM as a BGM): a still card with the title, 30 fps.

Writes game/data/song_NN.bin (+ gal_song_NN card previews 256x144 LZ for the gallery) and tools/song_list.txt.
usage: make_songs.py   (needs Docker for the encoder)
"""
import os, sys, wave, subprocess
from concurrent.futures import ThreadPoolExecutor
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageEnhance

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'extract'))
from paths import DATA as ROOT  # original game data (extract/paths.py)
NDS = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
import god_render as g
import movies_audio as MA
import lz77

OUT = os.path.join(NDS, 'game', 'data')
WORK = os.path.join(NDS, 'build', 'songs')
FPS = 30    # like every other clip: FastVideoDS carries the audio per frame (low fps overflows its queue)
FONT = 'C:/Windows/Fonts/arialbd.ttf' if os.name == 'nt' else '/usr/share/fonts/truetype/msttcorefonts/Arial_Bold.ttf'

# (title image index in RSC 1944 or None, sac, English name, price)
SONGS = [
    (0, 'Ha09_01', 'Resonation Wings', 3000), (1, 'Ha09_02', 'Kibou no Hikari', 3000),
    (2, 'Ha09_03', 'BORDER', 3000), (3, 'Ha09_04', 'fly in the sky', 2000),
    (4, 'Ha09_05', 'Beyond the limits (instrumental)', 2000), (5, 'Ha09_06', 'Senjou no Valkyrie', 2000),
    (6, 'Ha09_07', 'I Love Valkyria (instrumental)', 2500), (7, 'Ha09_08', 'Sonic Diver', 2500),
    (8, 'Ha09_09', 'Dive into the sky', 2500), (9, 'Ha09_10', 'Mizuumi Kogoe-bushi', 2000),
    (10, 'Ha09_11', 'Tsukame Glory Day', 2000), (11, 'Ha09_12', 'Kazeiro no Photograph', 2000),
    (12, 'Ha09_13', 'Ao no Kanata e', 2500), (13, 'Ha09_14', 'Oozora e no Yakusoku', 2000),
    (14, 'Ha09_15', 'Forget Me Not', 2500), (15, 'Ha09_16', 'Zero no Tsubasa', 2500),
    (16, 'Ha09_17', 'Tegami', 2500), (17, 'Ha09_18', 'Nami Daiko', 2500),
    (18, 'Ha09_19', 'Sugao no Kimi de', 2500),
    (22, 'Ha09_23', 'Absolute Airspace -Normal-', 1500), (23, 'Ha09_24', 'Absolute Airspace -High-', 1500),
    (24, 'Ha09_25', 'Absolute Airspace -Super High-', 2000), (25, 'Ha09_26', 'Ura Absolute Airspace -Stage 1-', 1500),
    (26, 'Ha09_27', 'Ura Absolute Airspace -Stage 2-', 1500), (27, 'Ha09_28', 'Ura Absolute Airspace -Stage 3-', 1500),
    (28, 'Ha09_29', 'Zettai Kuuiki (Lovekyure 1)', 2000), (29, 'Ha09_30', 'Seikuu Sentou (Lovekyure 1)', 2000),
    (30, 'Ha09_31', 'Hangeki Kaishi (Lovekyure 1)', 2000), (31, 'Ha09_32', 'Tsuki no Kioku', 3000),
    (32, 'Ha09_33', 'Tsukiuta -Gekka-', 3000),
    (None, 'Ha09_34', 'Bonus track A', 3000), (None, 'Ha09_35', 'Bonus track B', 3000),
]


JP_TITLES = {1, 4, 5, 6, 9, 10, 11, 12, 13, 15, 16, 17, 18} | set(range(22, 33))


def song_length(sac):
    plays = [a for _, _, op, a in sorted(MA.steps[sac]) if op == 9][0]
    wavs = plays[:2] if plays[1] else plays[:1]
    return sum(len(MA.load_wav(w)) for w in wavs) / MA.RATE


def title_images():
    ls, isz, l0 = g.T['RSC'][1944]
    return [g.T['IMG'][l0 + k][0] for k in range(ls * isz)]


def card(title_id, name, n):
    bg = Image.open(os.path.join(NDS, 'build', 'title_top.png')).convert('RGB').resize((1024, 576))
    bg = ImageEnhance.Brightness(bg).enhance(0.35).convert('RGBA')
    d = ImageDraw.Draw(bg)
    f = ImageFont.truetype(FONT, 40)
    fs = ImageFont.truetype(FONT, 30)
    d.text((512 - d.textlength('~ SONG %02d ~' % (n + 1), font=fs) / 2, 150), '~ SONG %02d ~' % (n + 1), font=fs,
           fill=(255, 180, 210))
    if title_id is not None:
        t = g.load_image(title_ids[title_id])
        t = t.resize((t.width * 2, t.height * 2), Image.LANCZOS)
        bg.alpha_composite(t, ((1024 - t.width) // 2, 230))
    if title_id is None or title_id in JP_TITLES:  # romanized name under a Japanese title
        d.text((512 - d.textlength(name, font=f) / 2, 340), name, font=f, fill=(255, 255, 255), stroke_width=3,
               stroke_fill=(120, 20, 70))
    return bg.convert('RGB')


def bgr555(img):
    a = np.array(img.convert('RGB')).astype(np.uint16) >> 3
    return (a[:, :, 0] | (a[:, :, 1] << 5) | (a[:, :, 2] << 10) | 0x8000).astype('<u2')


def render(n):
    title_id, sac_name, name, price = SONGS[n]
    sac = MA.sac_by_name['SD_BGM_' + sac_name]
    dur = song_length(sac)
    pcm = MA.render_sac(sac, dur + 1)[:int(dur * MA.RATE)]
    k = int(1.5 * MA.RATE)
    pcm[-k:] *= np.linspace(1, 0, k)[:, None]
    peak = float(np.abs(pcm).max())
    if peak > 0.99:
        pcm *= 0.99 / peak
    wav = os.path.join(WORK, f'song_{n:02d}.wav')
    with wave.open(wav, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(MA.RATE)
        w.writeframes((pcm * 32767).astype('<i2').tobytes())
    im = card(title_id, name, n)
    png = os.path.join(WORK, f'song_{n:02d}.png')
    im.resize((512, 288), Image.LANCZOS).save(png)
    with open(os.path.join(OUT, f'gal_song_{n:02d}_lz.bin'), 'wb') as f:
        f.write(lz77.compress(bgr555(im.resize((256, 144), Image.LANCZOS)).tobytes()))
    mp4 = os.path.join(WORK, f'song_{n:02d}.mp4')
    subprocess.run([MA.FF, '-v', 'error', '-y', '-loop', '1', '-framerate', str(FPS), '-i', png, '-i', wav,
                    '-c:v', 'libx264', '-tune', 'stillimage', '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-b:a', '192k',
                    '-shortest', mp4], check=True)
    os.remove(wav)
    subprocess.run(['docker', 'run', '--rm', '-t', '-e', 'LC_ALL=C.UTF-8', '-v', f'{NDS}:/work', '-w', '/work',
                    'lk2ds-dev', 'dotnet', 'build/fvenc/FastVideoDSEncoder.dll', '-j', '1',
                    f'build/songs/song_{n:02d}.mp4', f'game/data/song_{n:02d}.bin'],
                   check=True, stdout=subprocess.DEVNULL, env=dict(os.environ, MSYS_NO_PATHCONV='1'))
    return n, name, dur


title_ids = None


def main():
    global title_ids
    os.makedirs(WORK, exist_ok=True)
    title_ids = title_images()
    todo = [int(a) for a in sys.argv[1:]] or list(range(len(SONGS)))
    with ThreadPoolExecutor(4) as ex:
        for n, name, dur in ex.map(render, todo):
            print(f'song {n:02d} {name}: {dur:.0f}s', flush=True)
    with open(os.path.join(HERE, 'song_list.txt'), 'w', encoding='utf8') as f:
        for n, (t, sac, name, price) in enumerate(SONGS):
            f.write(f'{n}\t{name}\t{price}\n')


if __name__ == '__main__':
    main()
