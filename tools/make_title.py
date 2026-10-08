"""Title / menu screens from the app's own title assets (Sozai_Title_01/02, Sozai99_Common01/02).

Outputs (game/data, 256x192 BGR555 bitmaps with bit 15 set unless noted):
  title_top.bin        top screen: title art (both girls) + Konami copyright
  title_bot.bin        bottom screen: continuation of the art + logo, without the prompt
  title_tap.bin        256xTAP_H band holding "TAP TO START" (blinks by copying it over the bitmap)
  opening_bot.bin      bottom screen during the boot opening: "PRESS START TO SKIP" in the TAP TO START style
  menu_bot.bin         bottom screen: menu buttons (labels translated to English)
  page_howto.bin / page_terms.bin / page_license.bin   info pages with a BACK button
  game/include/title_layout.h   button rectangles
Previews in build/title_*.png
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lz77
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'extract'))
from paths import DATA as ROOT  # original game data (extract/paths.py)
R = os.path.join(ROOT, 'release/UnityProject/ExportedProject/Assets/Resources/image')
OUT = os.path.join(HERE, '..', 'game', 'data')
INC = os.path.join(HERE, '..', 'game', 'include')
PRE = os.path.join(HERE, '..', 'build')
FONT_BLACK = 'C:/Windows/Fonts/ariblk.ttf' if os.name == 'nt' else '/usr/share/fonts/truetype/msttcorefonts/Arial_Black.ttf'
FONT_BOLD = 'C:/Windows/Fonts/arialbd.ttf' if os.name == 'nt' else '/usr/share/fonts/truetype/msttcorefonts/Arial_Bold.ttf'
LZ = Image.LANCZOS
TAP_Y, TAP_H = 128, 32


def atlas(name):
    return Image.open(os.path.join(R, name + '.png')).convert('RGBA')


def sprite_rect(meta_name, sprite):
    t = open(os.path.join(R, meta_name + '.png.meta'), encoding='utf8').read()
    m = re.search(r'name: ' + sprite + r'\n(?:.*\n)*?\s+rect:\n.*\n\s+x: ([\d.]+)\n\s+y: ([\d.]+)\n\s+width: ([\d.]+)'
                  r'\n\s+height: ([\d.]+)', t)
    return [int(float(v)) for v in m.groups()]


def sp(im, x, y, w, h):  # Unity sprite rect (origin bottom-left)
    return im.crop((x, im.height - y - h, x + w, im.height - y))


def fit(im, w):
    return im.resize((w, round(im.height * w / im.width)), LZ)


def blank(button, x0, x1, src_col):
    """erase a label: the button gradient is vertical, so copy a clean column over the text"""
    a = np.array(button)
    a[:, x0:x1] = a[:, src_col:src_col + 1]
    return Image.fromarray(a)


def label(button, text, size, fill=(255, 255, 255), stroke=(90, 0, 15)):
    b = button.copy()
    d = ImageDraw.Draw(b)
    while True:
        font = ImageFont.truetype(FONT_BLACK, size)
        w = d.textlength(text, font=font)
        if w <= b.width * 0.8:
            break
        size -= 2
    bbox = d.textbbox((0, 0), text, font=font)
    x = (b.width - w) / 2
    y = (b.height - (bbox[3] - bbox[1])) / 2 - bbox[1]
    sh = Image.new('RGBA', b.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).text((x + 4, y + 5), text, font=font, fill=(40, 0, 0, 160))
    b.alpha_composite(sh.filter(ImageFilter.GaussianBlur(2)))
    d.text((x, y), text, font=font, fill=fill, stroke_width=max(2, size // 12), stroke_fill=stroke)
    return b


def bgr555(img):
    a = np.array(img.convert('RGB')).astype(np.uint16) >> 3
    return (a[:, :, 0] | (a[:, :, 1] << 5) | (a[:, :, 2] << 10) | 0x8000).astype('<u2')


def write(name, img_or_arr, compress=False):
    arr = img_or_arr if isinstance(img_or_arr, np.ndarray) else bgr555(img_or_arr)
    data = arr.tobytes()
    if compress:  # full screens: BIOS LZ77, decompressed straight into VRAM
        data = lz77.compress(data)
        name = name.replace('.bin', '_lz.bin')
    with open(os.path.join(OUT, name), 'wb') as f:
        f.write(data)


def tap_text(txt='TAP TO START', size=17, h=TAP_H, y=4):
    font = ImageFont.truetype(FONT_BLACK, size)
    t = Image.new('RGBA', (256, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(t)
    x = (256 - d.textlength(txt, font=font)) / 2
    glow = Image.new('RGBA', t.size, (0, 0, 0, 0))
    ImageDraw.Draw(glow).text((x, y), txt, font=font, fill=(255, 60, 120, 255))
    glow = glow.filter(ImageFilter.GaussianBlur(3))
    for _ in range(2):
        t.alpha_composite(glow)
    d.text((x, y), txt, font=font, fill=(255, 255, 255, 255), stroke_width=2, stroke_fill=(150, 10, 50, 255))
    return t


def page(bg, title, lines, back):
    """info page: darkened art, panel with title + text, BACK button at the bottom"""
    p = Image.alpha_composite(bg, Image.new('RGBA', bg.size, (0, 0, 20, 110)))
    panel = Image.new('RGBA', (240, 146), (10, 8, 30, 200))
    p.alpha_composite(panel, (8, 6))
    d = ImageDraw.Draw(p)
    ft = ImageFont.truetype(FONT_BLACK, 13)
    fb = ImageFont.truetype(FONT_BOLD, 9)
    d.text((128 - d.textlength(title, font=ft) / 2, 10), title, font=ft, fill=(255, 210, 230),
           stroke_width=1, stroke_fill=(120, 0, 40))
    y = 30
    for line in lines:
        # (label, text) -> two columns; str -> word-wrapped paragraph line
        if isinstance(line, tuple):
            d.text((14, y), line[0], font=fb, fill=(255, 200, 120))
            x0, text = 64, line[1]
        else:
            x0, text = 14, line
        words, cur = text.split(' '), ''
        for w in words:
            nxt = (cur + ' ' + w).strip()
            if cur and d.textlength(nxt, font=fb) > 242 - x0:
                d.text((x0, y), cur, font=fb, fill=(240, 240, 250))
                y += 11
                cur = w
            else:
                cur = nxt
        if cur:
            d.text((x0, y), cur, font=fb, fill=(240, 240, 250))
        y += 11
    b = fit(back, 100)
    bx, by = (256 - b.width) // 2, 192 - b.height - 6
    p.alpha_composite(b, (bx, by))
    return p, (bx, by, b.width, b.height)


def main():
    os.makedirs(OUT, exist_ok=True)
    art = atlas('sozai01_title/Sozai_Title_01')
    t2 = atlas('sozai01_title/Sozai_Title_02')
    c1 = atlas('sozai99_common01/Sozai99_Common01')
    c2 = atlas('sozai99_common01/Sozai99_Common02')

    logo = sp(t2, 104, 1693, 914, 321)
    copyright_ = sp(t2, 1243, 76, 757, 110)
    red = blank(sp(t2, 15, 901, 569, 139), 36, 534, 30)
    lic_src = sp(c2, *sprite_rect('sozai99_common01/Sozai99_Common02', 'Sozai99_Common02_1'))
    lw = lic_src.width
    lic_btn = label(blank(lic_src, int(lw * 0.17), int(lw * 0.92), int(lw * 0.16)), 'LICENSE', 38,
                    stroke=(20, 10, 30))
    back_src = sp(c1, *sprite_rect('sozai99_common01/Sozai99_Common01', 'Sozai99_Common01_3'))
    bw = back_src.width
    back_btn = label(blank(back_src, int(bw * 0.2), int(bw * 0.88), int(bw * 0.19)), 'BACK', 58)

    # top screen: 4:3 crop with both girls; bottom screen continues it (same scale and x range)
    top = art.crop((470, 250, 1690, 1165)).resize((256, 192), LZ)
    cp = fit(copyright_, 140)
    top.alpha_composite(cp, (256 - cp.width - 3, 192 - cp.height - 2))
    bg = art.crop((470, 1133, 1690, 2048)).resize((256, 192), LZ)
    bg = Image.alpha_composite(bg, Image.new('RGBA', bg.size, (0, 10, 40, 90)))

    title = bg.copy()
    lg = fit(logo, 220)
    title.alpha_composite(lg, ((256 - lg.width) // 2, 22))
    with_tap = title.copy()
    with_tap.alpha_composite(tap_text(), (0, TAP_Y))

    menu = bg.copy()
    rects = []
    y = 6
    for text in ('SIMULATOR MODE', 'TRIAL MODE', 'GALLERY', 'HOW TO PLAY', 'TERMS OF USE'):
        b = fit(label(red, text, 58), 112)
        x = (256 - b.width) // 2
        menu.alpha_composite(b, (x, y))
        rects.append((x, y, b.width, b.height))
        y += b.height + 3
    lb = fit(lic_btn, 72)
    lx = (256 - lb.width) // 2
    menu.alpha_composite(lb, (lx, y + 1))
    rects.append((lx, y + 1, lb.width, lb.height))

    terms, back_rect = page(bg, 'TERMS OF USE', [
        'Non-commercial fan demake for Nintendo DS.',
        '',
        'Bouku Shoujo Lovekyure 2',
        '(C)2025 Konami Digital Entertainment',
        '(C)2025 Konami Amusement',
        'Characters, art, video and sound belong to KONAMI.',
        'Not affiliated with or endorsed by KONAMI.',
        'Do not sell this program or its assets.',
    ], back_btn)
    license_, _ = page(bg, 'LICENSE', [
        'BlocksDS SDK / libnds - Zlib, MPL-2.0',
        '   Antonio Nino Diaz and contributors',
        'FastVideoDS player and encoder - (C) Gericom',
        'FatFs - (C) ChaN, BSD-1-Clause',
        'DSpico DLDI - LNH team',
        '',
        'Ported by Mostazaniikkkk',
    ], back_btn)

    for old in ('title_top', 'title_bot', 'menu_bot', 'page_howto', 'page_terms', 'page_license', 'page_howto_lz'):
        if os.path.exists(os.path.join(OUT, old + '.bin')):
            os.remove(os.path.join(OUT, old + '.bin'))
    # boot screens: KONAMI logo splash + notices (layout of the app's caution screen, inverted colours)
    def notice(heading, paragraphs, footer=None, heading_col=(230, 30, 30), y0=44):
        img = Image.new('RGBA', (256, 192), (0, 0, 0, 255))
        d = ImageDraw.Draw(img)
        size = 16
        fh = ImageFont.truetype(FONT_BLACK, size)
        while heading and d.textlength(heading, font=fh) > 232:
            size -= 1
            fh = ImageFont.truetype(FONT_BLACK, size)
        fb = ImageFont.truetype(FONT_BOLD, 11)
        y = y0 if heading else 18
        if heading:
            d.text((128 - d.textlength(heading, font=fh) / 2, y), heading, font=fh, fill=heading_col)
            y += 30
        for para in paragraphs:
            words, cur, lines = para.split(' '), '', []
            for w_ in words:
                nxt = (cur + ' ' + w_).strip()
                if cur and d.textlength(nxt, font=fb) > 228:
                    lines.append(cur)
                    cur = w_
                else:
                    cur = nxt
            lines.append(cur)
            for line in lines:
                d.text((128 - d.textlength(line, font=fb) / 2, y), line, font=fb, fill=(255, 255, 255))
                y += 14
            y += 8
        if footer:
            ff = ImageFont.truetype(FONT_BOLD, 9)
            d.text((128 - d.textlength(footer, font=ff) / 2, 174), footer, font=ff, fill=(150, 150, 150))
        return img
    caution_top = notice('18+ ONLY', [
        'This pachislot simulator is for adults and for entertainment only.',
        'No real money, medals or prizes can be won or lost.',
    ])
    caution_bot = notice(None, [
        'Gambling with real money can cause serious problems: debt, damaged health and broken families.',
        'Always play responsibly, set limits and never gamble money you cannot afford to lose.',
        'If gambling is hurting you or someone close to you, please ask for help.',
    ], 'TAP TO CONTINUE')
    # KONAMI splash: centre of the app's logo screen (red with the white logo), cropped 4:3
    konami = atlas('sozai00_logo/Sozai00_logo_KONAMI').crop((499, 606, 1549, 1393)).resize((256, 192), LZ)
    splash_bot = notice('UNOFFICIAL FAN-MADE PORT', [
        'This Nintendo DS version is a non-commercial fan project, not produced, endorsed or supported by KONAMI.',
        'Bouku Shoujo Lovekyure 2 (C)2025 Konami Digital Entertainment / Konami Amusement.',
        'Ported by Mostazaniikkkk',
    ], heading_col=(255, 255, 255), y0=20)
    write('splash_top.bin', konami, True)
    write('splash_bot.bin', splash_bot, True)
    konami.convert('RGB').save(os.path.join(PRE, 'title_splash_top.png'))
    splash_bot.convert('RGB').save(os.path.join(PRE, 'title_splash_bot.png'))
    write('caution_top.bin', caution_top, True)
    write('caution_bot.bin', caution_bot, True)
    caution_top.convert('RGB').save(os.path.join(PRE, 'title_caution_top.png'))
    caution_bot.convert('RGB').save(os.path.join(PRE, 'title_caution_bot.png'))

    write('title_top.bin', top, True)
    write('title_bot.bin', title, True)
    write('title_tap.bin', bgr555(with_tap)[TAP_Y:TAP_Y + TAP_H])
    write('title_notap.bin', bgr555(title)[TAP_Y:TAP_Y + TAP_H])
    op = Image.new('RGBA', (256, 192), (0, 0, 0, 255))
    op.alpha_composite(tap_text('PRESS START TO SKIP', 17), (0, (192 - TAP_H) // 2))
    write('opening_bot.bin', op, True)
    op.convert('RGB').save(os.path.join(PRE, 'opening_bot.png'))
    write('menu_bot.bin', menu, True)
    write('page_terms.bin', terms, True)
    write('page_license.bin', license_, True)

    with open(os.path.join(INC, 'title_layout.h'), 'w') as f:
        f.write('// generated by tools/make_title.py\n#pragma once\n')
        f.write(f'#define TITLE_TAP_Y {TAP_Y}\n#define TITLE_TAP_H {TAP_H}\n')
        f.write('// menu buttons: simulator, trial, gallery, how to play, terms, license\n')
        f.write('#define MENU_RECTS {' + ', '.join('{%d, %d, %d, %d}' % r for r in rects) + '}\n')
        f.write('#define BACK_RECT {%d, %d, %d, %d}\n' % back_rect)

    for name, img in (('top', top), ('title', with_tap), ('menu', menu), ('terms', terms),
                      ('license', license_)):
        img.convert('RGB').save(os.path.join(PRE, f'title_{name}.png'))
    print('menu rects', rects, 'back', back_rect)


if __name__ == '__main__':
    main()
