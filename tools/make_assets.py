"""Convert LK2 cabinet / reel textures into NDS-ready binaries (layout 'A': real cabinet crop).

Outputs (nds/game/data):
  cab_tiles.bin / cab_map.bin / cab_pal.bin   bottom-screen cabinet, 8bpp tiles, 32x24 map, ext palette (BG0)
  reelN_strip.bin / reelN_pal.bin               reel strip N (1..3): 63x8 8bpp tiles row-major, streamed at runtime
  top_header.bin                                 256x48 BGR555 (bit15 set) header shown above the LCD video
  vignette.bin                                   192 BLDY values (one per scanline) for the reel cylinder shading
  layout.h                                       window coordinates shared with the C code
Also writes preview PNGs rebuilt from the binaries (nds/build/preview_*.png).
"""
import os, sys, struct
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'extract'))
from paths import DATA as ROOT  # original game data (extract/paths.py)
RES = os.path.join(ROOT, 'release/UnityProject/ExportedProject/Assets/Resources')
KYO = os.path.join(RES, 'image/sozai_kyotai')
OUT = os.path.join(HERE, '..', 'game', 'data')
INC = os.path.join(HERE, '..', 'game', 'include')
PRE = os.path.join(HERE, '..', 'build')
for d in (OUT, INC, PRE):
    os.makedirs(d, exist_ok=True)

SW, SH = 256, 192
# bottom crop of the 640x1024 cabinet (layout A)
CROP_X, CROP_Y, CROP_W = 88, 400, 464
S = SW / CROP_W
# reel window in cabinet coords (alpha < 255 region)
WIN_X0, WIN_X1, WIN_Y0, WIN_Y1 = 142, 494, 422, 564
REEL_W, SYM_H, NSYM = 64, 24, 21
STRIP_H = SYM_H * NSYM  # 504


def rgb555(rgb):
    r, g, b = (np.asarray(rgb, dtype=np.int32) >> 3).T
    return (r | (g << 5) | (b << 10)).astype(np.uint16)


def quantize(img_rgb, ncol, mask=None, dither=False):
    """Quantize to ncol colours (index 0 reserved for transparency). Returns (indices HxW uint8, palette Nx3)."""
    q = img_rgb.quantize(colors=ncol, method=Image.MEDIANCUT,
                         dither=Image.FLOYDSTEINBERG if dither else Image.NONE)
    idx = np.array(q, dtype=np.uint8) + 1
    pal = np.array(q.getpalette()[:ncol * 3], dtype=np.uint8).reshape(-1, 3)
    if mask is not None:
        idx[~mask] = 0
    pal = np.vstack([[0, 0, 0], pal])
    return idx, pal


def to_tiles(idx):
    h, w = idx.shape
    return idx.reshape(h // 8, 8, w // 8, 8).swapaxes(1, 2)  # [ty][tx][8][8]


def dedup(tiles_2d, first_blank=True):
    """Dedup tiles (with h/v flips). Returns tile bank (N,8,8) and map entries [ty][tx] (u16 with flip bits)."""
    bank, lut = [], {}
    if first_blank:
        bank.append(np.zeros((8, 8), np.uint8)); lut[bank[0].tobytes()] = 0
    th, tw = tiles_2d.shape[:2]
    m = np.zeros((th, tw), np.uint16)
    for ty in range(th):
        for tx in range(tw):
            t = tiles_2d[ty, tx]
            for flip, v in ((0, t), (0x400, t[:, ::-1]), (0x800, t[::-1, :]), (0xC00, t[::-1, ::-1])):
                k = v.tobytes()
                if k in lut:
                    m[ty, tx] = lut[k] | flip
                    break
            else:
                lut[t.tobytes()] = len(bank); m[ty, tx] = len(bank); bank.append(t)
    return np.array(bank), m


def write(name, data):
    with open(os.path.join(OUT, name), 'wb') as f:
        f.write(data)
    return len(data)


def pal_bytes(pal):
    p = np.zeros(256, np.uint16)
    p[:len(pal)] = rgb555(pal)
    return p.astype('<u2').tobytes()


def render(bank, m, pal):
    th, tw = m.shape
    out = np.zeros((th * 8, tw * 8), np.uint8)
    for ty in range(th):
        for tx in range(tw):
            e = int(m[ty, tx]); t = bank[e & 0x3FF]
            if e & 0x400: t = t[:, ::-1]
            if e & 0x800: t = t[::-1, :]
            out[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8] = t
    return pal[out], out


def main():
    body = Image.open(os.path.join(KYO, 'unique/body0.png')).convert('RGBA')
    spr = lambda n: Image.open(os.path.join(KYO, 'common', n + '.png')).convert('RGBA')
    LZ = Image.LANCZOS

    # ---------------- bottom cabinet layer ----------------
    cab = Image.new('RGBA', body.size, (0, 0, 0, 0))
    cab.alpha_composite(body)
    # buttons, lever and lamps are sprites (animated at runtime), the cabinet layer is the bare body
    crop_h = round(CROP_W * SH / SW)
    cabs = cab.crop((CROP_X, CROP_Y, CROP_X + CROP_W, CROP_Y + crop_h)).resize((SW, SH), LZ)

    # reel window on the DS screen
    wx0 = round((WIN_X0 - CROP_X) * S); wx1 = round((WIN_X1 - CROP_X) * S)
    wy0 = round((WIN_Y0 - CROP_Y) * S); wy1 = round((WIN_Y1 - CROP_Y) * S)
    mask = np.ones((SH, SW), bool)
    mask[wy0:wy1, wx0:wx1] = False
    idx, pal = quantize(cabs.convert('RGB'), 255, mask, dither=True)
    bank, m = dedup(to_tiles(idx))
    n_tiles = write('cab_tiles.bin', bank.astype(np.uint8).tobytes())
    write('cab_map.bin', m.astype('<u2').tobytes())
    write('cab_pal.bin', pal_bytes(pal))
    cab_rgb, cab_idx = render(bank, m, pal)
    print(f'cabinet: {len(bank)} tiles ({n_tiles // 1024} KB), window x{wx0}-{wx1} y{wy0}-{wy1}')

    # vignette: brightness decrease per scanline from the cabinet alpha at window centre
    A = np.array(body)[:, :, 3].astype(np.float32)
    vig = np.zeros(SH, np.uint8)
    for y in range(wy0, wy1):
        sy = CROP_Y + (y + 0.5) / S
        a = A[int(sy), (WIN_X0 + WIN_X1) // 2]
        vig[y] = min(16, round(16 * a / 255))
    write('vignette.bin', vig.tobytes())

    # ---------------- reels ----------------
    gap = (wx1 - wx0 - 3 * REEL_W) // 2
    reel_x = [wx0 + i * (REEL_W + gap) for i in range(3)]
    reels_preview = []
    total = 0
    for i in range(3):
        strip = Image.open(os.path.join(RES, f'r{i + 1}.png')).convert('RGB').resize((REEL_W, STRIP_H), LZ)
        ridx, rpal = quantize(strip, 255)
        # full strip in natural tile order (row-major, 8 tiles per row); streamed into VRAM at runtime
        tiles = to_tiles(ridx).reshape(-1, 8, 8)
        total += write(f'reel{i + 1}_strip.bin', tiles.astype(np.uint8).tobytes())
        write(f'reel{i + 1}_pal.bin', pal_bytes(rpal))
        reels_preview.append(rpal[ridx])

    # ---------------- top header (above LCD) ----------------
    LCD_X0, LCD_X1, LCD_Y0 = 90, 548, 114
    ts = SW / (LCD_X1 - LCD_X0)
    hdr = body.crop((LCD_X0, LCD_Y0 - round(48 / ts), LCD_X1, LCD_Y0)).resize((SW, 48), LZ).convert('RGB')
    # data counter strip (top 16 rows): dark band here; labels and digits are main-engine sprites so the
    # cross key of the slide-in panel can page through the data (DataPanel of the app)
    d = ImageDraw.Draw(hdr)
    d.rectangle((0, 0, SW - 1, 15), fill=(8, 8, 12))
    COUNTER_PAGES = [
        [('CREDIT', 5), ('GAME', 4), ('BIG', 2), ('REG', 2)],
        [('SINCE BONUS', 4), ('IN', 5), ('OUT', 5)],
        [('BIG 1/', 4), ('REG 1/', 4), ('PAYOUT', 3, '%')],
        [('VOLUME', 2)],
        [('ART', 3, 'G'), ('ADD', 3, 'G'), ('TOTAL', 5)],   # Absolute Airspace: games left, last add, medals won
        [('ZONE', 3, 'G'), ('HITS', 3)],                   # Weapon Attack / add zones: games added, inputs
        [('VA', 4), ('LEFT', 4)],                          # VA Ignition: medals to win, still to go
    ]
    counter = []    # per page: [(x of first digit, digit count)] (4 fields)
    label_strips = []
    for fields in COUNTER_PAGES:
        strip = Image.new('RGBA', (SW, 16), (0, 0, 0, 0))
        ds = ImageDraw.Draw(strip)
        x, page = 3, []
        for field in fields:
            label, nd = field[0], field[1]
            tw = ds.textlength(label)
            ds.text((x, 3), label, fill=(200, 200, 210, 255))
            page.append((x + int(tw) + 3, nd))
            x += int(tw) + 3 + nd * 8
            if len(field) > 2:  # unit after the digits
                ds.text((x + 1, 3), field[2], fill=(200, 200, 210, 255))
                x += int(ds.textlength(field[2])) + 1
            x += 8
        assert x <= SW + 8, fields
        counter.append(page + [(0, 0)] * (4 - len(page)))
        label_strips.append(strip)
    write('top_header.bin', (rgb555(np.array(hdr).reshape(-1, 3)) | 0x8000).astype('<u2').tobytes())

    # ---------------- sprites (4bpp, 1D mapping) ----------------
    def sprite_tiles(img, w, h):
        """RGBA w x h -> (4bpp tiles bytes, 16-colour palette bytes); alpha < 128 -> transparent."""
        a = np.array(img)
        mask = a[:, :, 3] >= 128
        idx, pal = quantize(img.convert('RGB'), 15, mask)
        t = idx.reshape(h // 8, 8, w // 8, 8).swapaxes(1, 2).reshape(-1, 8, 8)
        packed = (t[:, :, 0::2] | (t[:, :, 1::2] << 4)).astype(np.uint8)
        p = np.zeros(16, np.uint16)
        p[:len(pal)] = rgb555(pal)
        return packed.tobytes(), p.astype('<u2').tobytes()

    def scaled_sprite(name, x, y, w, h):
        """sprite drawn at cabinet (x, y), scaled like the bottom screen; returns (img w x h, ds x, ds y)."""
        canvas = Image.new('RGBA', body.size, (0, 0, 0, 0))
        src = Image.open(os.path.join(KYO, name + '.png')).convert('RGBA')
        canvas.alpha_composite(src, (x, y))
        big = canvas.crop((CROP_X, CROP_Y, CROP_X + CROP_W, CROP_Y + crop_h)).resize((SW, SH), LZ)
        sx, sy = int((x - CROP_X) * S), int((y - CROP_Y) * S)
        return big.crop((sx, sy, sx + w, sy + h)), sx, sy

    # 7-segment digits 0-9 + blank for the top data counter, glyph 7x11 inside an 8x16 sprite
    segs = [Image.open(os.path.join(KYO, 'common', 'seg_%02d.png' % n)).convert('RGBA') for n in range(10)]
    bbox = segs[8].getbbox()
    digits = Image.new('RGBA', (8, 16 * 11), (0, 0, 0, 0))
    for n, g in enumerate(segs):
        digits.alpha_composite(g.crop(bbox).resize((7, 11), LZ), (0, n * 16 + 2))
    # sprite tiles must be laid out per 8x16 sprite: stack them vertically, tiles come out in order
    t, p = sprite_tiles(digits, 8, 16 * 11)
    write('seg_tiles.bin', t)
    write('seg_pal.bin', p)

    # counter labels: every page is a 256x16 strip cut into eight 32x16 sprites (stacked vertically)
    labels = Image.new('RGBA', (32, 16 * 8 * len(label_strips)), (0, 0, 0, 0))
    for pg, strip in enumerate(label_strips):
        for c in range(8):
            labels.paste(strip.crop((c * 32, 0, c * 32 + 32, 16)), (0, (pg * 8 + c) * 16))
    t, p = sprite_tiles(labels, 32, labels.height)
    write('cnt_label_tiles.bin', t)
    write('cnt_label_pal.bin', p)

    def frames_sprite(out, names, x, y, w, h):
        """several frames of one cabinet sprite at (x, y): shared palette, tiles frame after frame."""
        imgs = []
        for n in names:
            img, sx, sy = scaled_sprite(n, x, y, w, h)
            imgs.append(img)
        stack = Image.new('RGBA', (w, h * len(imgs)))
        for i, img in enumerate(imgs):
            stack.paste(img, (0, i * h))
        t, p = sprite_tiles(stack, w, h * len(imgs))
        write(out + '_tiles.bin', t)
        write(out + '_pal.bin', p)
        return sx, sy

    # payout 7-seg window (SegRight_L / SegRight_R in GameMain.unity): the original glyphs (scale 0.3)
    # would be ~6 px tall on the DS, so the payout uses the 7x11 counter digits, 2 digits in the window
    pay_x, pay_y, pay_dx = 118, 101, 10

    # buttons (ObjectButtonSprite: default, lamp, pushed, pushed+lamp = _00 _01 _02 _03)
    stop_pos = [frames_sprite('stop', ['common/STOP_x223y665_x291y665_x359y665_00',
                                       'common/STOP_x223y665_x291y665_x359y665_01'], x, 665, 32, 32)
                for x in (223, 291, 359)]
    bet_pos = frames_sprite('bet', [f'common/BET_x156y625_0{i}' for i in range(4)], 156, 625, 64, 32)
    push_pos = frames_sprite('push', [f'common/PUSH_x312y625_0{i}' for i in range(4)], 312, 625, 64, 32)
    # lever knob: up at (108, 661), down at (114, 670) (ObjectButtonLever: 0.1 s travel)
    lev_up = frames_sprite('lever', ['common/LV_x108y661_x114y670'], 108, 661, 32, 32)
    lev_dn = (int((114 - CROP_X) * S), int((670 - CROP_Y) * S))
    # LEDs (BodyManager.RendererBet / InMedal / Start / Replay)
    lamp_pos = [(275, 585), (359, 585), (359, 595), (359, 605)]
    lamps = [frames_sprite('lamp', ['unique/DOT_x275y585_x275y595_x275y605_x359y585_x359y595_x359y605'], x, y, 8, 8)
             for x, y in lamp_pos]

    with open(os.path.join(INC, 'layout.h'), 'w') as f:
        f.write('// generated by tools/make_assets.py\n#pragma once\n')
        f.write(f'#define REEL_WIN_Y0 {wy0}\n#define REEL_WIN_Y1 {wy1}\n')
        for i in range(3):
            f.write(f'#define REEL{i + 1}_X {reel_x[i]}\n')
        f.write(f'#define REEL_W {REEL_W}\n#define REEL_SYM_H {SYM_H}\n#define REEL_NSYM {NSYM}\n')
        f.write(f'#define REEL_STRIP_H {STRIP_H}\n#define REEL_STRIP_TROWS {STRIP_H // 8}\n')
        for i, (sx, sy) in enumerate(stop_pos):
            f.write(f'#define STOP{i + 1}_SPR_X {sx}\n#define STOP{i + 1}_SPR_Y {sy}\n')
        f.write(f'#define BET_SPR_X {bet_pos[0]}\n#define BET_SPR_Y {bet_pos[1]}\n')
        f.write(f'#define PUSH_SPR_X {push_pos[0]}\n#define PUSH_SPR_Y {push_pos[1]}\n')
        f.write(f'#define LEVER_UP_X {lev_up[0]}\n#define LEVER_UP_Y {lev_up[1]}\n')
        f.write(f'#define LEVER_DN_X {lev_dn[0]}\n#define LEVER_DN_Y {lev_dn[1]}\n')
        f.write('// LED order: bet, in-medal, start, replay\n')
        f.write('#define LAMP_POS {' + ', '.join(f'{{{sx}, {sy}}}' for sx, sy in lamps) + '}\n')
        f.write(f'#define PAY_DIGIT_X {pay_x}\n#define PAY_DIGIT_Y {pay_y}\n#define PAY_DIGIT_DX {pay_dx}\n')
        f.write('// top data counter pages: {x of the first digit, digit count} per field\n')
        f.write(f'#define CNT_PAGES {len(counter)}\n#define CNT_FIELDS {{' + ', '.join(
            '{' + ', '.join(f'{{{x}, {n}}}' for x, n in pg) + '}' for pg in counter) + '}\n')

    # ---------------- preview from binaries ----------------
    scr = np.zeros((SH, SW, 3), np.uint8)
    for i in range(3):
        pos = [3, 5, 7][i] * SYM_H  # symbol index at window top
        for y in range(wy0, wy1):
            row = reels_preview[i][(pos + y - wy0 - (wy1 - wy0 - 3 * SYM_H) // 2) % STRIP_H]
            x = reel_x[i]
            scr[y, x:x + REEL_W] = (row.astype(np.float32) * (1 - vig[y] / 16)).astype(np.uint8)
    scr[cab_idx != 0] = cab_rgb[cab_idx != 0]
    top = np.zeros((SH, SW, 3), np.uint8)
    top[:48] = np.array(hdr)
    lab = np.array(label_strips[0])
    top[:16][lab[:, :, 3] > 0] = lab[:, :, :3][lab[:, :, 3] > 0]
    Image.fromarray(scr).save(os.path.join(PRE, 'preview_bottom.png'))
    Image.fromarray(top).save(os.path.join(PRE, 'preview_top.png'))
    print('reel tiles total', total // 1024, 'KB')


if __name__ == '__main__':
    main()
