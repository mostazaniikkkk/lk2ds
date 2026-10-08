"""Pre-game settings screen (MachineSetting scene) built from the Sozai03_Config atlases.

Outputs (game/data):
  (top screen art: reuses title_top_lz.bin; the description band covers its copyright)
  settings_bot_lz.bin     bottom screen: background, TITLE / PLAY buttons, option labels (LZ77)
  settings_ctrl.bin       pack of LZ77 control strips (CTRL_W x ROW_H, BGR555) for every row/value
  settings_desc.bin       pack of LZ77 description bands (256 x DESC_H) shown on the top screen
  game/include/settings_layout.h
Pack format: u32 count, u32 offset[count] (from pack start), LZ77 blobs.
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from settings_art import *
import lz77

OUT = os.path.join(HERE, '..', 'game', 'data')
INC = os.path.join(HERE, '..', 'game', 'include')
PRE = os.path.join(HERE, '..', 'build')

ROW_H, Y0 = 23, 28
CTRL_X, CTRL_W = 98, 156
DESC_Y, DESC_H = 126, 66  # down to the screen edge: hides the reused copyright

ORDERS = ['L > C > R', 'L > R > C', 'C > L > R', 'C > R > L', 'R > L > C', 'R > C > L']
SETTINGS = [f'SETTING {n}' for n in range(1, 7)] + ['SETTING ?']
ROWS = [  # (label, kind, values, description)
    ('SETTING', 'linear', SETTINGS,
     'Machine setting: 6 gives the best bonus odds, 1 the worst. "?" picks a hidden setting at random.'),
    ('STOP ORDER', 'cyclic', ORDERS,
     'With AUTO AIM on, any STOP button stops the reels in this order.'),
    ('WAIT CUT', 'toggle', None,
     'ON skips the 4.1 second wait between games. OFF plays like the real machine.'),
    ('AUTO AIM', 'toggle', None,
     'ON stops every reel at the best timing, so the bonus sevens line up by themselves.'),
    ('START MODE', 'disabled', ['NONE'], 'Not available in this version.'),
    ('PLAY GUIDE', 'toggle', None, 'ON shows the HOW TO PLAY page before the game starts.'),
    ('VOLUME', 'volume', list(range(11)), 'Overall volume of the music, the video and the sound effects.'),
]


def bgr555(img):
    a = np.array(img.convert('RGB')).astype(np.uint16) >> 3
    return (a[:, :, 0] | (a[:, :, 1] << 5) | (a[:, :, 2] << 10) | 0x8000).astype('<u2')


def centred(parts, gap=1):
    """lay out [(image, dy)] left to right, centred in the control area; returns (strip, x positions)"""
    w = sum(p.width for p, _ in parts) + gap * (len(parts) - 1)
    out = Image.new('RGBA', (CTRL_W, ROW_H), (0, 0, 0, 0))
    x = (CTRL_W - w) // 2
    xs = []
    for p, dy in parts:
        out.alpha_composite(p, (x, dy))
        xs.append((x, p.width))
        x += p.width + gap
    return out, xs


def arrows(left_on, right_on):
    return fit_h(cfg2(13) if left_on else cfg2(15), 18), fit_h(cfg2(14) if right_on else cfg2(16), 18)


def value_ctrl(text, left_on, right_on, disabled=False):
    h = 21
    box = fit_h(stretch(disabled_box() if disabled else cfg2(0), 645 if disabled else 980), h)  # same width
    t = fit_h(silver_text(text, 64), 13)
    if t.width > box.width - 10:
        t = t.resize((box.width - 10, 13), LZ)
    if disabled:
        t = Image.fromarray((np.array(t) * [0.6, 0.6, 0.6, 1]).astype('uint8'))
    box.alpha_composite(t, ((box.width - t.width) // 2, (h - 13) // 2))
    al, ar = arrows(left_on, right_on)
    return centred([(al, 3), (box, 1), (ar, 3)])


def volume_ctrl(level):
    v = fit_h(cfg([40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50][level]), 22)
    al, ar = arrows(level > 0, level < 10)
    return centred([(al, 3), (v, 0), (ar, 3)])


def toggle_ctrl(on):
    t = fit_h(cfg2(2 if on else 3), 21)
    strip, xs = centred([(t, 1)])
    x0, w = xs[0]
    # ON / OFF halves of the switch sprite
    return strip, [(x0, w // 2), (x0 + w // 2, w - w // 2)]


def pack(blobs):
    head = struct.pack('<I', len(blobs))
    offs, data = [], b''
    base = 4 + 4 * len(blobs)
    for b in blobs:
        offs.append(base + len(data))
        data += b
    return head + struct.pack(f'<{len(blobs)}I', *offs) + data


def main():
    art = atlas('sozai01_title/Sozai_Title_01')
    top = art.crop((470, 250, 1690, 1165)).resize((256, 192), LZ)
    bg = art.crop((470, 1133, 1690, 2048)).resize((256, 192), LZ)
    bg = Image.alpha_composite(bg, Image.new('RGBA', bg.size, (0, 5, 25, 150)))

    bot = bg.copy()
    btn_title = fit_h(red_button(cfg2(8), 'TITLE').resize((118, 35), LZ), 24)
    btn_play = fit_h(red_button(cfg2(9), 'PLAY', 'right').resize((118, 35), LZ), 24)
    title_rect = (4, 2, btn_title.width, 24)
    play_rect = (256 - btn_play.width - 4, 2, btn_play.width, 24)
    bot.alpha_composite(btn_title, title_rect[:2])
    bot.alpha_composite(btn_play, play_rect[:2])
    for i, (label, *_rest) in enumerate(ROWS):
        bot.alpha_composite(fit_h(tag(label), 19), (2, Y0 + i * ROW_H + 2))

    # control strips, composited over the exact background they cover
    blobs, base, count, hits = [], [], [], []
    preview = bot.copy()
    for i, (label, kind, values, _) in enumerate(ROWS):
        y = Y0 + i * ROW_H
        under = bot.crop((CTRL_X, y, CTRL_X + CTRL_W, y + ROW_H))
        base.append(len(blobs))
        if kind == 'toggle':
            variants = [toggle_ctrl(True), toggle_ctrl(False)]  # value 0 = ON, 1 = OFF
        elif kind == 'volume':
            variants = [volume_ctrl(v) for v in values]
        else:
            n = len(values)
            variants = [value_ctrl(v, kind == 'cyclic' or k > 0, kind == 'cyclic' or k < n - 1, kind == 'disabled')
                        for k, v in enumerate(values)]
        count.append(len(variants))
        for strip, _ in variants:
            img = under.copy()
            img.alpha_composite(strip)
            blobs.append(lz77.compress(bgr555(img).tobytes()))
        # touch areas: arrows (left/right) or the ON / OFF halves, from the first variant
        xs = variants[0][1]
        if kind == 'toggle':
            a, b = xs
        else:
            a, b = xs[0], xs[-1]
        hits.append(((CTRL_X + a[0], y, a[1], ROW_H), (CTRL_X + b[0], y, b[1], ROW_H)))
        preview.alpha_composite(variants[0][0], (CTRL_X, y))

    # description bands for the top screen
    descs = []
    fbig = ImageFont.truetype(FONT_BLACK, 12)
    fb = ImageFont.truetype(FONT_BOLD, 10)
    for label, kind, values, text in ROWS:
        band = top.crop((0, DESC_Y, 256, DESC_Y + DESC_H))
        # opaque panel: compresses far better than art showing through
        band.alpha_composite(Image.new('RGBA', (248, DESC_H - 2), (8, 6, 24, 255)), (4, 0))
        d = ImageDraw.Draw(band)
        d.text((10, 3), label, font=fbig, fill=(255, 200, 120), stroke_width=1, stroke_fill=(0, 0, 0))
        for k, line in enumerate(wrap(d, text, fb, 236)):
            d.text((10, 21 + k * 12), line, font=fb, fill=(235, 235, 245))
        descs.append(lz77.compress(bgr555(band).tobytes()))
        if label == 'STOP ORDER':
            top_preview = top.copy()
            top_preview.alpha_composite(band, (0, DESC_Y))

    def w(name, data):
        with open(os.path.join(OUT, name), 'wb') as f:
            f.write(data)
    if os.path.exists(os.path.join(OUT, 'settings_top_lz.bin')):
        os.remove(os.path.join(OUT, 'settings_top_lz.bin'))
    w('settings_bot_lz.bin', lz77.compress(bgr555(bot).tobytes()))
    w('settings_ctrl.bin', pack(blobs))
    w('settings_desc.bin', pack(descs))

    with open(os.path.join(INC, 'settings_layout.h'), 'w') as f:
        f.write('// generated by tools/make_settings.py\n#pragma once\n')
        f.write(f'#define SET_ROWS {len(ROWS)}\n#define SET_ROW_Y0 {Y0}\n#define SET_ROW_H {ROW_H}\n')
        f.write(f'#define SET_CTRL_X {CTRL_X}\n#define SET_CTRL_W {CTRL_W}\n')
        f.write(f'#define SET_DESC_Y {DESC_Y}\n#define SET_DESC_H {DESC_H}\n')
        f.write('#define SET_ROW_BASE {' + ', '.join(map(str, base)) + '}\n')
        f.write('#define SET_ROW_COUNT {' + ', '.join(map(str, count)) + '}\n')
        f.write('// per row: {left arrow / ON}, {right arrow / OFF} as {x, y, w, h}\n')
        f.write('#define SET_ROW_HITS {' + ', '.join(
            '{{%d, %d, %d, %d}, {%d, %d, %d, %d}}' % (a + b) for a, b in hits) + '}\n')
        f.write('#define SET_TITLE_RECT {%d, %d, %d, %d}\n#define SET_PLAY_RECT {%d, %d, %d, %d}\n'
                % (title_rect + play_rect))
    preview.convert('RGB').save(os.path.join(PRE, 'settings_bot.png'))
    top_preview.convert('RGB').save(os.path.join(PRE, 'settings_top.png'))
    print('ctrl strips', len(blobs), sum(map(len, blobs)) // 1024, 'KB; desc', sum(map(len, descs)) // 1024, 'KB')


if __name__ == '__main__':
    main()
