"""Shared drawing helpers for the pre-game settings screen (MachineSetting scene assets)."""
import os, sys, re
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'extract'))
from paths import DATA as ROOT  # original game data (extract/paths.py)
R = os.path.join(ROOT, 'release/UnityProject/ExportedProject/Assets/Resources/image')
FONT_BLACK = 'C:/Windows/Fonts/ariblk.ttf'
FONT_BOLD = 'C:/Windows/Fonts/arialbd.ttf'
FONT_SYM = 'C:/Windows/Fonts/seguisym.ttf'
LZ = Image.LANCZOS
_cache = {}


def atlas(name):
    if name not in _cache:
        _cache[name] = Image.open(os.path.join(R, name + '.png')).convert('RGBA')
    return _cache[name]


def sprite(meta_name, sprite_name):
    t = open(os.path.join(R, meta_name + '.png.meta'), encoding='utf8').read()
    m = re.search(r'name: ' + sprite_name + r'\n(?:.*\n)*?\s+rect:\n.*\n\s+x: ([\d.]+)\n\s+y: ([\d.]+)\n\s+width: '
                  r'([\d.]+)\n\s+height: ([\d.]+)', t)
    x, y, w, h = [int(float(v)) for v in m.groups()]
    im = atlas(meta_name)
    return im.crop((x, im.height - y - h, x + w, im.height - y))


def cfg(n):
    return sprite('sozai03_config/Sozai03_Config01', f'Sozai03_Config01_{n}')


def cfg2(n):
    return sprite('sozai03_config/Sozai03_Config02', f'Sozai03_Config02_{n}')


def fit_h(im, h):
    return im.resize((max(1, round(im.width * h / im.height)), h), LZ)


def stretch(im, w, cap=0.3):
    """widen keeping the rounded ends: stretch only the middle columns"""
    c = int(im.width * cap)
    left, mid, right = im.crop((0, 0, c, im.height)), im.crop((c, 0, im.width - c, im.height)), \
        im.crop((im.width - c, 0, im.width, im.height))
    out = Image.new('RGBA', (w, im.height), (0, 0, 0, 0))
    out.paste(left, (0, 0))
    out.paste(mid.resize((w - 2 * c, im.height), LZ), (c, 0))
    out.paste(right, (w - c, 0))
    return out


def blank(im, x0, x1, src_col):
    a = np.array(im)
    a[:, x0:x1] = a[:, src_col:src_col + 1]
    return Image.fromarray(a)


def silver_text(text, size, font=FONT_BLACK, stroke=None):
    """metallic white-to-grey label like the config value sprites (drawn at 4x, caller downsizes)"""
    f = ImageFont.truetype(font, size)
    d = ImageDraw.Draw(Image.new('RGBA', (1, 1)))
    l, t, r, b = d.textbbox((0, 0), text, font=f)
    pad = size // 6 + 2
    w, h = r - l + 2 * pad, b - t + 2 * pad
    mask = Image.new('L', (w, h), 0)
    ImageDraw.Draw(mask).text((pad - l, pad - t), text, font=f, fill=255)
    grad = Image.new('RGBA', (w, h))
    for y in range(h):
        v = 255 - int(110 * max(0, (y - pad) / max(1, h - 2 * pad)))
        ImageDraw.Draw(grad).line([(0, y), (w, y)], fill=(v, v, min(255, v + 8), 255))
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    st = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(st).text((pad - l, pad - t), text, font=f, fill=(20, 20, 24, 255),
                            stroke_width=stroke or max(2, size // 10), stroke_fill=(20, 20, 24, 255))
    out.alpha_composite(st)
    out.paste(grad, (0, 0), mask)
    return out


def tag(text):
    """option label: the black diamond tag (Config01_2 style) with an English label"""
    t = blank(cfg(2), 78, 354, 72)  # text starts at x~85, the diamond ends at ~65
    f = ImageFont.truetype(FONT_BLACK, 40)
    d = ImageDraw.Draw(t)
    size = 40
    while d.textlength(text, font=f) > 250:
        size -= 2
        f = ImageFont.truetype(FONT_BLACK, size)
    l, tt, r, b = d.textbbox((0, 0), text, font=f)
    y = (60 - (b - tt)) / 2 - tt
    d.text((96, y), text, font=f, fill=(255, 255, 255), stroke_width=3, stroke_fill=(0, 0, 0))
    return t


def disabled_box():
    """grey 'not available' box (Config01_31) with its label erased"""
    return blank(cfg(31), 12, 368, 9)


def wrap(draw, text, font, width):
    words, lines, cur = text.split(' '), [], ''
    for w in words:
        nxt = (cur + ' ' + w).strip()
        if cur and draw.textlength(nxt, font=font) > width:
            lines.append(cur)
            cur = w
        else:
            cur = nxt
    if cur:
        lines.append(cur)
    return lines


def red_button(src, text, arrow='left'):
    """red config button with its label replaced; keeps the small arrow (left: x~60, right: x~0.9w)"""
    if arrow == 'left':
        b = blank(src, 96, int(src.width * 0.93), 80)
    else:
        b = blank(src, int(src.width * 0.08), int(src.width * 0.86), int(src.width * 0.075))
    f = ImageFont.truetype(FONT_BLACK, 50)
    d = ImageDraw.Draw(b)
    size = 50
    while d.textlength(text, font=f) > b.width * 0.72:
        size -= 2
        f = ImageFont.truetype(FONT_BLACK, size)
    l, t, r, bb = d.textbbox((0, 0), text, font=f)
    x = (b.width - (r - l)) / 2 + b.width * 0.02
    y = (b.height - (bb - t)) / 2 - t
    d.text((x, y), text, font=f, fill=(255, 255, 255), stroke_width=4, stroke_fill=(90, 0, 15))
    return b
