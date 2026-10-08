"""Continuous effects of normal play (連続演出), one FastVideoDS clip per game, rendered from the GOD scenes with
every layer (movies + image layers: WIN logos, gauges, subtitles) and their scripted sound. The Japanese
subtitles (1024x56 images) are swapped for English ones from tools/date_text/effect_subs.tsv, drawn at the
same place and time. Each effect's BGM plays separately (tools/make_bgm.py).

  race  Sn_Ay04  chibi race for vacation days
  ecm   Sn_Ay01  ECM battle: Ai vs Fold
  rion  Sn_Ay02  Rion vs Koon
  tsug  Sn_Ay03  Tsugumi vs Short

Writes game/data/fx_<clip>.bin (moved into the pack by make_pak.py) and tools/effect_files.txt.
usage: make_effects.py [clip ...]   (needs Docker for the encoder)
"""
import os, sys, csv, wave, subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'extract'))
from paths import DATA as ROOT  # original game data (extract/paths.py)
NDS = os.path.normpath(os.path.join(HERE, '..'))
import god
import god_render as g
import movies_audio as MA

FPS = 30
WORK = os.path.join(NDS, 'build', 'fx')
FONT = 'C:/Windows/Fonts/arialbd.ttf' if os.name == 'nt' else '/usr/share/fonts/truetype/msttcorefonts/Arial_Bold.ttf'


def S(grp, *ids):
    return [f'Sn_{grp}_{i}' for i in ids]


# clip -> scenes played back to back during one game
CLIPS = {
    # chibi race
    'race_intro': S('Ay04', '0101', '0111'),
    'race_start': S('Ay04', '0201', '0211'),
    'race_start_hot': S('Ay04', '0202', '0211'),
    'race_net_lose': S('Ay04', '0231'),
    'race_rival_a': S('Ay04', '0232', '0301', '0311'),
    'race_rival_b': S('Ay04', '0232', '0301', '0312'),
    'race_a_win': S('Ay04', '0331', '0333', '0401'),
    'race_a_lose': S('Ay04', '0331', '0332'),
    'race_b_win': S('Ay04', '0334', '0336', '0401'),
    'race_b_lose': S('Ay04', '0334', '0335'),
    # ECM battle
    'ecm_1': S('Ay01', '0101', '0131'), 'ecm_1_hot': S('Ay01', '0101', '0132'),
    'ecm_2': S('Ay01', '0201', '0211', '0231'), 'ecm_2_hot': S('Ay01', '0202', '0212', '0232'),
    'ecm_3_win': S('Ay01', '0301', '0332'), 'ecm_3_lose': S('Ay01', '0301', '0331'),
    'ecm_3_back': S('Ay01', '0301', '0331', '0401'),
    'ecm_3_hot_win': S('Ay01', '0302', '0332'), 'ecm_3_hot_lose': S('Ay01', '0302', '0331'),
    'ecm_3_hot_back': S('Ay01', '0302', '0331', '0401'),
    # Rion vs Koon
    'rion_1': S('Ay02', '0101', '0111', '0131'), 'rion_1_hot': S('Ay02', '0101', '0111', '0132'),
    'rion_2': S('Ay02', '0201', '0211'), 'rion_2_hot': S('Ay02', '0202', '0232', '0231'),
    'rion_3_win': S('Ay02', '0301', '0311', '0332', '0333'), 'rion_3_lose': S('Ay02', '0301', '0311', '0331'),
    'rion_3_back': S('Ay02', '0301', '0311', '0331', '0401', '0431'),
    'rion_3_hot_win': S('Ay02', '0302', '0311', '0332', '0333'),
    'rion_3_hot_lose': S('Ay02', '0302', '0311', '0331'),
    'rion_3_hot_back': S('Ay02', '0302', '0311', '0331', '0401', '0431'),
    # Tsugumi vs Short
    'tsug_1': S('Ay03', '0101', '0111', '0131'), 'tsug_1_hot': S('Ay03', '0102', '0111', '0131'),
    'tsug_2': S('Ay03', '0201', '0211', '0231'), 'tsug_2_hot': S('Ay03', '0202', '0212', '0213', '0232'),
    'tsug_3_win': S('Ay03', '0301', '0311', '0331', '0333'), 'tsug_3_lose': S('Ay03', '0301', '0311', '0331', '0334'),
    'tsug_3_back': S('Ay03', '0301', '0311', '0331', '0334', '0401', '0431'),
    'tsug_3_hot_win': S('Ay03', '0302', '0312', '0332', '0333'),
    'tsug_3_hot_lose': S('Ay03', '0302', '0312', '0332', '0334'),
    'tsug_3_hot_back': S('Ay03', '0302', '0312', '0332', '0334', '0401', '0431'),
    # Klein Stage (Sn_Aa06): Maia pushes Ichigo off the normal screen. One set per idle background k (scene
    # group 0k): failed push, takeover (slide into the Klein screen), back to normal, back with WIN; plus
    # the Klein screen's loop shown during the stage
    'klein_loop': ['Sn_Aa06_0012@39-160'],
}
# ART (Absolute Airspace, F groups), Weapon Attack zones (Ec), BIG notices (Dx03 / Df03), VA Ignition (Ya01 /
# Ia04). "A+B": scenes drawn together (a zone's background movie and its HUD).
def E(*ids):
    return [('+'.join(f'Sn_{x}' for x in i.split('+'))) for i in ids]


CLIPS.update({
    'art_start': E('Fx01_0001', 'Fx01_0011'),
    'art_loop': E('Fa00_0001'), 'art_loop_hi': E('Fa00_0002'), 'art_loop_sp': E('Fa00_0003'),
    'art_end': E('Fx02_0001'),
    'battle_in': E('Fc01_1001', 'Fc01_2001'),
    'battle_win': E('Fc01_2301', 'Fc01_4303'),
    'battle_lose': E('Fc01_3001'),
    'ashi_in': E('Fc03_0011', 'Fc04_0001'),
    'ashi_play': E('Fc04_1000', 'Fc04_1001', 'Fc04_1002', 'Fc04_1003', 'Fc04_1004', 'Fc04_1005'),
    'ashi_end': E('Fc04_1013'),
    'nade_in': E('Fc03_0012', 'Fc05_0001'),
    'nade_play': E('Fc05_0011', 'Fc05_0012', 'Fc05_0013', 'Fc05_0014', 'Fc05_0015', 'Fc05_0016'),
    'nade_end': E('Fc05_0303'),
    'wa_title': E('Ec04_0010'),
    'ai_in': E('Ec01_5001', 'Ec01_0005'),
    'ai_play': E('Ec01_0013+Ec01_0110', 'Ec01_0014+Ec01_0111', 'Ec01_0013+Ec01_0112', 'Ec01_0014+Ec01_0113'),
    'ai_end': E('Ec01_0020', 'Ec01_0028'),
    'rion_in': E('Ec01_5002', 'Ec01_0006'),
    'rion_play': E('Ec01_0021'),
    'rion_end': E('Ec01_0028'),
    'tsug_in': E('Ec01_5003', 'Ec01_0007'),
    'tsug_play': E('Ec01_0030+Ec01_0301', 'Ec01_0030+Ec01_0302'),
    'tsug_end': E('Ec01_0028'),
    'ichigo_in': E('Ec04_0008', 'Ec03_0001'),
    'ichigo_play': E('Ec03_0002+Ec03_0051'),
    'ichigo_end': E('Ec03_0004'),
    'big_chance': E('Dx03_0001'),
    'big_chance_stock': E('Dx03_0011', 'Dx03_0032'),
    'big_full_stock': E('Df03_0001', 'Df03_0002', 'Df03_0012'),
    'va_in': E('Ya01_0500', 'Ia04_0001', 'Ia04_0003', 'Ia04_0006', 'Ia04_0007'),
    'va_end': E('Ia04_0041', 'Ia04_0043'),
})
# scenes drawn over the machine's idle screen (cut-ins, banners); the rest have their own background
OVERLAY = ('Sn_Ay', 'Sn_Ya', 'Sn_Ia', 'Sn_Dx', 'Sn_Df')

# grouped by kind: the game picks klein_<kind>_0 + background
for _k in range(7):
    CLIPS[f'klein_try_{_k}'] = [f'Sn_Aa06_0{_k}01', f'Sn_Aa06_0{_k}02']
for _k in range(7):
    CLIPS[f'klein_in_{_k}'] = [f'Sn_Aa06_0{_k}11', f'Sn_Aa06_0{_k}12@0-39']
for _k in range(7):
    CLIPS[f'klein_out_{_k}'] = [f'Sn_Aa06_0{_k}13']
for _k in range(7):
    CLIPS[f'klein_win_{_k}'] = [f'Sn_Aa06_0{_k}14']


def english_subtitles():
    """1024x56 replacement images for the Japanese subtitles, white (allies) or red (enemies)"""
    path = os.path.join(HERE, 'date_text', 'effect_subs.tsv')
    if not os.path.exists(path):
        print('no effect_subs.tsv: Japanese subtitles kept')
        return
    f = ImageFont.truetype(FONT, 40)
    n = 0
    with open(path, encoding='utf8') as fh:
        for r in csv.reader((l for l in fh if l.strip() and not l.startswith('#')), delimiter='\t'):
            if len(r) < 6 or not r[5].strip():
                continue
            img = Image.new('RGBA', (1024, 56), (0, 0, 0, 0))
            d = ImageDraw.Draw(img)
            col = (255, 70, 70) if r[3].strip().upper() == 'R' else (255, 255, 255)
            size = 40
            while d.textlength(r[5], font=f) > 1000 and size > 24:
                size -= 2
                f2 = ImageFont.truetype(FONT, size)
                if d.textlength(r[5], font=f2) <= 1000:
                    f = f2
                    break
                f = f2
            w = d.textlength(r[5], font=f)
            d.text(((1024 - w) / 2, (56 - size) / 2 - 2), r[5], font=f, fill=col, stroke_width=4,
                   stroke_fill=(0, 0, 0))
            g._img_cache[int(r[1], 16)] = img
            f = ImageFont.truetype(FONT, 40)
            n += 1
    print(n, 'English subtitles')


def _digit_rsc(r):
    ls, isz, _ = g.T['RSC'][r]
    return isz in (10, 11) or (ls in (10, 11) and isz == 1)


def digit_layer(l):
    """counter digits (an image bank of 10 or 11 glyphs, set at runtime): hidden, the game shows the numbers"""
    an = l[2]
    if an[2] != 0:
        return False
    p = g.T['PTN'][g.T['PTNTBL'][an[3]][0]]
    for q in range(p[1], p[1] + p[0]):
        r = g.T['PRM'][g.T['PRMTBL'][q][0]][0]
        if r < len(g.T['RSC']) and _digit_rsc(r):
            return True
    return any(0x4F4A <= i <= 0x4F55 for i in g.pattern_images(an, 0))


def scene_length(gg, scene):
    segs, L = god.scene_timeline(gg, g.NAMES.index(scene))
    total = sum(x[2] for x in segs)
    return total or max(l[8] for l in g.layers(scene))


IDLE = os.path.join(NDS, 'build', 'idle.mp4')  # tools/make_idle.py: the machine screen the effects overlay


def idle_frames():
    """the normal-mode screen, frame after frame (the effects' titles and flashes are drawn over it)"""
    p = subprocess.Popen([MA.FF, '-v', 'error', '-i', IDLE, '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'],
                         stdout=subprocess.PIPE)
    try:
        while True:
            raw = p.stdout.read(512 * 288 * 3)
            if len(raw) < 512 * 288 * 3:
                return
            yield Image.frombytes('RGB', (512, 288), raw).resize((1024, 576), Image.BILINEAR)
    finally:
        p.kill()


def render_scene(gg, spec, dst):
    """every layer of one pass of the scene at 512x288, over the machine's idle screen (the scenes are
    overlays: titles, flashes and movies with alpha), + its sound sequence -> mkv"""
    # "scene@a-b": frames a..b-1 only
    scene, _, rng = spec.partition('@')
    scenes = scene.split('+')
    scene = scenes[0]
    n = max(scene_length(gg, x) for x in scenes)
    f0 = 0
    if rng:
        f0, f1 = map(int, rng.split('-'))
        n = f1 - f0
    ls = [l for l in g.scene_layers(scenes) if not digit_layer(l)]
    own_bg = not scene.startswith(OVERLAY)
    if scene.startswith('Sn_Aa06'):  # Maia (trigger priority 2199) stands in front of the screen parts, plates and chibis included
        ls = sorted(((1000,) + l[1:] if l[0] == 2199 else l for l in ls), key=lambda l: (-l[0], -l[1]))
    sac = next((MA.sac_by_name[k] for k in ('SD_SEQ_Pt_' + x[3:] for x in scenes) if k in MA.sac_by_name), None)
    audio = MA.render_sac(sac, (f0 + n) / FPS + 0.5)[int(f0 / FPS * MA.RATE):] if sac else np.zeros((1, 2), np.float32)
    audio = np.resize(audio, (int(n / FPS * MA.RATE), 2)) if sac else np.zeros((int(n / FPS * MA.RATE), 2),
                                                                                  np.float32)
    peak = float(np.abs(audio).max()) if len(audio) else 0
    if peak > 0.99:
        audio *= 0.99 / peak
    wav = dst + '.wav'
    with wave.open(wav, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(MA.RATE)
        w.writeframes((audio * 32767).astype('<i2').tobytes())
    enc = subprocess.Popen([MA.FF, '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', '512x288',
                            '-r', str(FPS), '-i', '-', '-i', wav, '-c:v', 'libx264', '-crf', '12', '-pix_fmt',
                            'yuv420p', '-c:a', 'pcm_s16le', '-shortest', dst], stdin=subprocess.PIPE)
    bases = idle_frames() if not own_bg else iter(())
    for f in range(n):
        base = next(bases, None)
        enc.stdin.write(g.compose(ls, f0 + f, base=base).convert('RGB').resize((512, 288), Image.LANCZOS)
                        .tobytes())
    enc.stdin.close()
    enc.wait()
    os.remove(wav)
    g._movie_cache.clear()  # whole movies decoded at 1024x576: do not keep them around


_gg = None


def _render_worker(scene):
    """one scene per process (the compositor is single threaded)"""
    global _gg
    if _gg is None:
        _gg = god.God(os.path.join(ROOT, 'cdn/extracted/rom/GODRom_L.GOD'))
        english_subtitles()
    render_scene(_gg, scene, os.path.join(WORK, scene[3:].replace('@', '_').replace('+Sn_', '+') + '.mkv'))
    return scene


def _encode(clip):
    # the encoder only works with -j 1, so clips are encoded in parallel containers instead
    subprocess.run(['docker', 'run', '--rm', '-t', '-e', 'LC_ALL=C.UTF-8', '-v', f'{NDS}:/work', '-w', '/work',
                    'lk2ds-dev', 'dotnet', 'build/fvenc/FastVideoDSEncoder.dll', '-j', '1',
                    f'build/fx/{clip}.mkv', f'game/data/fx_{clip}.bin'],
                   check=True, stdout=subprocess.DEVNULL, env=dict(os.environ, MSYS_NO_PATHCONV='1'))
    return clip


def main():
    from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor
    os.makedirs(WORK, exist_ok=True)
    clips = sys.argv[1:] or list(CLIPS)
    # scenes (shared by several clips) rendered in parallel, each movie decode stays inside its process
    # (RAM: a worker holds ~1-2 GB while decoding long movies at 1024x576)
    todo = sorted({s for c in clips for s in CLIPS[c] if not os.path.exists(os.path.join(WORK, s[3:].replace('@', '_').replace('+Sn_', '+') + '.mkv'))})
    workers = max(1, min(len(todo), (os.cpu_count() or 2) // 2, 4))
    with ProcessPoolExecutor(workers) as ex:
        for s in ex.map(_render_worker, todo):
            print(s, 'rendered', flush=True)
    for clip in clips:
        lst = os.path.join(WORK, clip + '.txt')
        with open(lst, 'w') as f:
            f.write(''.join(f"file '{os.path.join(WORK, s[3:].replace('@', '_').replace('+Sn_', '+') + '.mkv').replace(os.sep, '/')}'\n"
                            for s in CLIPS[clip]))
        subprocess.run([MA.FF, '-hide_banner', '-v', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', lst,
                        '-c', 'copy', os.path.join(WORK, clip + '.mkv')], check=True)
    with ThreadPoolExecutor(max(1, (os.cpu_count() or 2) // 2)) as ex:
        for clip in ex.map(_encode, clips):
            print(clip, 'encoded', os.path.getsize(os.path.join(NDS, 'game', 'data', f'fx_{clip}.bin')) // 1024,
                  'KB', flush=True)
    with open(os.path.join(HERE, 'effect_files.txt'), 'w') as f:
        f.write(''.join(f'fx_{c}.bin\n' for c in CLIPS))


if __name__ == '__main__':
    main()
