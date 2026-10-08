"""Gallery (unlocked with the credits won in SIMULATOR mode): CG stills, movies and music.

Items reuse what the game already has: date sim stills (pak), FastVideoDS clips (lk2ds/movies), BGMs (pak).
Writes into game/data (moved into the pack by make_pak.py, list in tools/gallery_files.txt):
  gal_page_<tab>_<n>_lz.bin   bottom screen of one page: tabs + 3x3 thumbnails (prices / locks drawn at runtime)
  gal_prev_<i>_lz.bin         top screen preview 256x144 of movie / music items (CG items show their still)
  game/include/gallery_items.h
Run after make_date.py and make_effects.py (thumbnails come from their outputs).
"""
import os, sys, subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageEnhance

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lz77

NDS = os.path.normpath(os.path.join(HERE, '..'))
OUT = os.path.join(NDS, 'game', 'data')
SRC = os.path.join(NDS, 'game', 'pak_src')
INC = os.path.join(NDS, 'game', 'include')
PRE = os.path.join(NDS, 'build')
FONT_B = 'C:/Windows/Fonts/arialbd.ttf' if os.name == 'nt' else '/usr/share/fonts/truetype/msttcorefonts/Arial_Bold.ttf'
try:
    import imageio_ffmpeg
    FF = imageio_ffmpeg.get_ffmpeg_exe()
except ImportError:
    FF = 'ffmpeg'

TABS = ['CG', 'MOVIES', 'MUSIC', 'SONGS']
TAB_X, TAB_W, TAB_PITCH = 6, 56, 62
CELL_X, CELL_Y, CELL_W, CELL_H, PITCH_X, PITCH_Y = 8, 42, 76, 43, 82, 48

# (name, source, price)  CG source: date still pak name; MOVIES: clip id (video_ids); MUSIC: (bgm id, picture clip)
CG = [
    ('Lunch together', 'date_ichigo_cg_lunch', 500), ('Haunted house', 'date_ichigo_cg_haunted', 800),
    ('Fortune teller', 'date_ichigo_cg_fortune', 800), ('Ferris wheel', 'date_ichigo_cg_ferris', 1500),
    ('Night parade', 'date_ichigo_parade', 1000), ('The confession', 'date_ichigo_confess', 2000),
    ('"I love you!"', 'date_ichigo_love', 5000), ('Congratulations', 'date_ichigo_congrats', 3000),
    ('CELF base at night', 'date_ichigo_base_confess', 2500),
    ('Fountain plaza', 'date_dm_cg_fountain', 600), ('Fountain at sunset', 'date_dm_cg_fountain_eve', 1200),
    ('The doggy', 'date_dm_cg_dog', 600), ('Crane game', 'date_dm_cg_crane', 800),
    ('Karaoke', 'date_dm_cg_karaoke', 800), ('Shopping', 'date_dm_cg_clothes', 1000),
    ('Melon soda', 'date_dm_cg_soda', 800), ('Dolphin show', 'date_dm_cg_dolphin', 1000),
    ('Garden eels', 'date_dm_cg_eels', 1000), ('Holding hands', 'date_dm_hand', 4000),
    ('Her heart', 'date_dm_heart', 6000),
]
# added after the first release: their save ids come after every original item (owned bits stay valid)
CG_NEW = []
for _r, _n in (('ai', 'Ai'), ('rion', 'Rion'), ('tsugumi', 'Tsugumi')):
    CG_NEW += [(f'{_n}: lunch together', f'date_{_r}_cg_lunch', 600), (f'{_n}: haunted house', f'date_{_r}_cg_haunted', 800),
               (f'{_n}: fortune teller', f'date_{_r}_cg_fortune', 800), (f'{_n}: Ferris wheel', f'date_{_r}_cg_ferris', 1500),
               (f'{_n}: night parade', f'date_{_r}_parade', 1000), (f'{_n}: "I love you!"', f'date_{_r}_love', 5000),
               (f'{_n}: Congratulations', f'date_{_r}_congrats', 3000)]
CG_NEW += [('Fold: lunch for everyone', 'date_klein_cg_lunch', 1200), ('Short: haunted house', 'date_klein_cg_haunted', 1200),
           ('Koon: fortune teller', 'date_klein_cg_fortune', 1200), ('Maia: Ferris wheel', 'date_klein_cg_ferris', 2000)]
for _c, _n in (('5', 'Maia'), ('6', 'Short'), ('7', 'Fold'), ('8', 'Koon')):
    CG_NEW += [(f'{_n}: "I love you!"', f'date_klein_love{_c}', 6000), (f'{_n}: Congratulations', f'date_klein_congrats{_c}', 4000)]
MOVIES = [
    ('Opening: Resonation Wings', 'OPENING', 0, 'opening'), ('Vacation race', 'FX_RACE_A_WIN', 1500, 'race_a_win'),
    ('ECM battle', 'FX_ECM_3_WIN', 2000, 'ecm_3_win'), ('Rion vs Koon', 'FX_RION_3_WIN', 2000, 'rion_3_win'),
    ('Tsugumi vs Short', 'FX_TSUG_3_WIN', 2000, 'tsug_3_win'), ('Maia takes over', 'FX_KLEIN_IN_0', 1500, 'klein_in_0'),
    ('Absolute Airspace', 'FX_ART_START', 3000, 'art_start'), ('Golden shutter', 'FX_BIG_FULL_STOCK', 2500, 'big_full_stock'),
    ('Weapon Attack: Ai', 'FX_AI_IN', 2500, 'ai_play'), ('Weapon Attack: Rion', 'FX_RION_IN', 2500, 'rion_play'),
    ('Weapon Attack: Tsugumi', 'FX_TSUG_IN', 2500, 'tsug_in'), ('Ichigo Attack', 'FX_ICHIGO_IN', 4000, 'ichigo_in'),
    ('Dimension Battle', 'FX_BATTLE_WIN', 5000, 'battle_win'), ('Nadenade LoveKyure', 'FX_NADE_IN', 8000, 'nade_in'),
    ('Ashitsubo Punish', 'FX_ASHI_IN', 6000, 'ashi_in'), ('VA Ignition', 'FX_VA_IN', 4000, 'va_in'),
    ('Super high chance', 'FX_ART_LOOP_SP', 10000, 'art_loop_sp'),
]
MUSIC = [
    ('Title theme', 'TITLE', 0, 'opening'), ('Menu theme', 'MENU', 300, 'opening'),
    ('Vacation race', 'RACE', 800, 'race_intro'), ('ECM battle', 'ECM', 800, 'ecm_1'),
    ('Rion vs Koon', 'RION', 800, 'rion_1'), ('Tsugumi vs Short', 'TSUG', 800, 'tsug_1'),
    ('Absolute Airspace', 'ART', 2000, 'art_loop'), ('Super high chance', 'ARTSP', 4000, 'art_loop_sp'),
    ('Weapon Attack', 'ZONE', 1500, 'ai_play'), ('Ichigo Attack', 'ICHIGO', 2000, 'ichigo_play'),
    ('Dimension Battle', 'BATTLE', 2500, 'battle_in'), ('Ashitsubo Punish', 'ASHI', 2500, 'ashi_play'),
    ('Nadenade LoveKyure', 'NADE', 3000, 'nade_play'), ('VA Ignition', 'VA', 2000, 'va_in'),
]


def bgr555(img):
    a = np.array(img.convert('RGB')).astype(np.uint16) >> 3
    return (a[:, :, 0] | (a[:, :, 1] << 5) | (a[:, :, 2] << 10) | 0x8000).astype('<u2')


def write_lz(name, img):
    with open(os.path.join(OUT, name), 'wb') as f:
        f.write(lz77.compress(bgr555(img).tobytes()))


def pak_still(name):
    """256x144 still of the date sim (pak_src or data)"""
    for d in (SRC, OUT):
        p = os.path.join(d, name + '_lz.bin')
        if os.path.exists(p):
            raw = lz77.decompress(open(p, 'rb').read())
            a = np.frombuffer(raw, '<u2').reshape(144, 256)
            rgb = np.stack([(a & 31), (a >> 5) & 31, (a >> 10) & 31], 2).astype(np.uint8) << 3
            return Image.fromarray(rgb)
    raise FileNotFoundError(name)


def clip_frame(clip):
    """the most detailed of a few frames of the clip (skips flashes and black transitions)"""
    import re
    src = os.path.join(PRE, 'opening.mp4') if clip == 'opening' else os.path.join(PRE, 'fx', clip + '.mkv')
    probe = subprocess.run([FF, '-i', src], capture_output=True, text=True).stderr
    m = re.search(r'Duration: (\d+):(\d+):([\d.]+)', probe)
    dur = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3)) if m else 2
    best, score = None, -1
    for at in (0.2, 0.35, 0.5, 0.65, 0.8, 0.92):
        raw = subprocess.run([FF, '-v', 'error', '-ss', f'{dur * at:.2f}', '-i', src, '-vframes', '1', '-vf',
                              'scale=256:144', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], capture_output=True).stdout
        if len(raw) != 256 * 144 * 3:
            continue
        a = np.frombuffer(raw, np.uint8).reshape(144, 256, 3).astype(np.float32)
        sc = a.std() - abs(a.mean() - 110) * 0.5
        if sc > score:
            best, score = Image.fromarray(a.astype(np.uint8)), sc
    return best or Image.new('RGB', (256, 144))


def page_image(tab, thumbs):
    b = Image.new('RGB', (256, 192), (18, 10, 34))
    d = ImageDraw.Draw(b)
    f = ImageFont.truetype(FONT_B, 10)
    for i, tn in enumerate(TABS):
        x = TAB_X + i * TAB_PITCH
        d.rounded_rectangle((x, 4, x + TAB_W, 22), 5, fill=(255, 120, 170) if i == tab else (60, 40, 80))
        d.text((x + TAB_W / 2 - d.textlength(tn, font=f) / 2, 8), tn, font=f, fill=(255, 255, 255))
    for k, th in enumerate(thumbs):
        x, y = CELL_X + (k % 3) * PITCH_X, CELL_Y + (k // 3) * PITCH_Y
        b.paste(th.resize((CELL_W, CELL_H), Image.LANCZOS), (x, y))
        d.rectangle((x - 1, y - 1, x + CELL_W, y + CELL_H), outline=(90, 70, 110))
    return b


def music_picture(img):
    """picture of a music item: the scene, darkened, with a note"""
    img = ImageEnhance.Brightness(img).enhance(0.55)
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype(FONT_B, 60)
    d.text((128 - d.textlength('~', font=f) / 2, 30), '~', font=f, fill=(255, 210, 230))
    return img


def main():
    files, items = [], []
    pages = []
    n_orig = len(CG) + len(MOVIES) + len(MUSIC)
    sid_new = n_orig
    songs = []  # tools/make_songs.py: the machine's song list, streamed clips
    sl = os.path.join(HERE, 'song_list.txt')
    if os.path.exists(sl):
        for line in open(sl, encoding='utf8'):
            n, name, price = line.rstrip(chr(10)).split(chr(9))
            songs.append((name, int(n), int(price)))
    for tab, lst in enumerate((CG + CG_NEW, MOVIES, MUSIC, songs)):
        thumbs = []
        for n, entry in enumerate(lst):
            idx = len(items)
            if tab == 3:
                name, sn, price = entry
                img = pak_still(f'gal_song_{sn:02d}')
                prev = f'PAK_GAL_SONG_{sn:02d}_LZ'
                files.append(f'gal_song_{sn:02d}_lz.bin')
                kind, target = 'GAL_MOVIE', f'VID_SONG_{sn:02d}'
            elif tab == 0:
                name, still, price = entry
                img = pak_still(still)
                prev = f'PAK_{still.upper()}_LZ'
                kind, target = 'GAL_CG', f'PAK_{still.upper()}_LZ'
            else:
                name, tid, price, clip = entry
                img = clip_frame(clip)
                if tab == 2:
                    img = music_picture(img)
                pn = f'gal_prev_{idx}_lz.bin'
                write_lz(pn, img)
                files.append(pn)
                prev = f'PAK_GAL_PREV_{idx}_LZ'
                kind, target = ('GAL_MOVIE', f'VID_{tid}') if tab == 1 else ('GAL_MUSIC', f'BGM_{tid}')
            thumbs.append(img)
            if (tab == 0 and n >= len(CG)) or tab == 3:
                sid, sid_new = sid_new, sid_new + 1
            else:
                sid = idx - (len(CG_NEW) if tab > 0 else 0)
            items.append((tab, n // 9, n % 9, prev, kind, target, price, name, sid))
        for p in range(0, len(thumbs), 9):
            pn = f'gal_page_{tab}_{p // 9}_lz.bin'
            im = page_image(tab, thumbs[p:p + 9])
            write_lz(pn, im)
            files.append(pn)
            im.save(os.path.join(PRE, f'gallery_{tab}_{p // 9}.png'))
        pages.append((len(thumbs) + 8) // 9)

    h = ['// generated by tools/make_gallery.py', '#pragma once', '#include "pak_ids.h"', '#include "video_ids.h"',
         '#include "bgm.h"', '',
         'typedef enum { GAL_CG, GAL_MOVIE, GAL_MUSIC } gal_kind_t;',
         'typedef struct { u8 tab, page, cell, kind; u16 preview; s16 target; u32 price; const char *name; u16 sid; } gal_item_t;',
         f'#define GAL_TABS {len(TABS)}', f'#define GAL_TAB_X {TAB_X}', f'#define GAL_TAB_PITCH {TAB_PITCH}',
         f'#define GAL_CELL_X {CELL_X}', f'#define GAL_CELL_Y {CELL_Y}', f'#define GAL_CELL_W {CELL_W}',
         f'#define GAL_CELL_H {CELL_H}', f'#define GAL_PITCH_X {PITCH_X}', f'#define GAL_PITCH_Y {PITCH_Y}',
         'static const u8 sGalPages[GAL_TABS] = { ' + ', '.join(map(str, pages)) + ' };',
         'static const u16 sGalPagePak[GAL_TABS][8] = {']
    for tab in range(len(TABS)):
        h.append('    { ' + ', '.join(f'PAK_GAL_PAGE_{tab}_{p}_LZ' for p in range(pages[tab])) + ' },')
    h.append('};')
    h.append(f'#define GAL_ITEMS {len(items)}')
    h.append('static const gal_item_t sGalItems[GAL_ITEMS] = {')
    for tab, page, cell, prev, kind, target, price, name, sid in items:
        nm = name.replace('"', '\\"')
        h.append(f'    {{ {tab}, {page}, {cell}, {kind}, {prev}, {target}, {price}, "{nm}", {sid} }},')
    h.append('};')
    with open(os.path.join(INC, 'gallery_items.h'), 'w', encoding='utf8') as f:
        f.write('\n'.join(h) + '\n')
    with open(os.path.join(HERE, 'gallery_files.txt'), 'w') as f:
        f.write('\n'.join(files) + '\n')
    print(len(items), 'items,', len(files), 'files, pages', pages)


if __name__ == '__main__':
    main()
