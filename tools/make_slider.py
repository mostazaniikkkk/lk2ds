"""Slide-in button panel of the app (GameMain PopUpWindow / PopupWindowManager): a red tab, the cross key
(ButtonKeys) and the CHANCE button, from the Sozai04_Play01 atlas, scaled to 1/4 for the bottom screen.

Each half is 64x48 (a 64x32 sprite over two 32x16 sprites; the DS has no 64x16 size) whose frames are streamed into the same
VRAM slot at runtime, so only one frame of each lives in sprite VRAM:
  slider_keys_tiles.bin    6 frames: tab > (closed), tab < (open) + cross idle / up / right / down / left
  slider_chance_tiles.bin  4 frames: normal, pushed, lamp, pushed + lamp (SpriteChanceNormal/Push + AddLamp)
  slider_*_pal.bin         one 16-colour palette per frame (10 sub palettes in all)
Frame layout: 64x32 tiles (1024 bytes), then the left and right 32x16 tiles (256 bytes each). Preview: build/preview_slider.png
"""
import os
import numpy as np
from PIL import Image
from make_assets import rgb555, write, PRE, RES

ATLAS = os.path.join(RES, 'image/sozai04_play/Sozai04_Play01.png')
SCALE = 0.25
# Sozai04_Play01 sprite rects (x, y from the bottom, w, h) from the .meta
RECT = {
    'keys': (1, 1114, 199, 190), 'up': (201, 1114, 199, 190), 'right': (401, 1114, 199, 190),
    'down': (601, 1114, 199, 190), 'left': (801, 1114, 199, 190),
    'tab_close': (1401, 1202, 52, 190),                                  # ImageOpenButtonMain while open
    'chance': (1024, 1202, 188, 190), 'chance_push': (1212, 1202, 188, 190),
    'chance_lamp': (621, 1356, 188, 190),                                # SpriteChanceAddLamp (additive)
}
W, H = 64, 48


def quantize(img_rgb, ncol, mask):
    """like make_assets.quantize, but octree on the opaque pixels only: keeps small saturated areas
    (the lit cross quadrant) that median cut merges away"""
    a = np.array(img_rgb)
    pix = Image.fromarray(a[mask].reshape(1, -1, 3))
    q = pix.quantize(colors=ncol, method=Image.Quantize.FASTOCTREE)
    pal = np.array(q.getpalette()[:ncol * 3], dtype=np.uint8).reshape(-1, 3)
    full = img_rgb.quantize(palette=q, dither=Image.Dither.FLOYDSTEINBERG)
    idx = np.array(full, dtype=np.uint8) + 1
    idx[~mask] = 0
    return idx, np.vstack([[0, 0, 0], pal])


def main():
    atlas = Image.open(ATLAS).convert('RGBA')
    big = {}
    for n, (x, y, w, h) in RECT.items():
        big[n] = atlas.crop((x, atlas.height - y - h, x + w, atlas.height - y))

    def small(img, w):
        return img.resize((round(w * SCALE), round(190 * SCALE)), Image.LANCZOS)

    def additive(base, add):
        a = np.array(base).astype(np.int32)
        b = np.array(add).astype(np.int32)
        a[:, :, :3] = np.minimum(255, a[:, :, :3] + b[:, :, :3] * b[:, :, 3:4] * 3 // (255 * 5))
        return Image.fromarray(a.astype(np.uint8))

    keys = []
    for i, n in enumerate(['keys', 'keys', 'up', 'right', 'down', 'left']):
        img = big[n].copy()
        if i > 0:  # open: the tab shows the close arrow
            img.alpha_composite(big['tab_close'], (0, 0))
        keys.append(small(img, 199))
    chance = [small(big['chance'], 188), small(big['chance_push'], 188),
              small(additive(big['chance'], big['chance_lamp']), 188),
              small(additive(big['chance_push'], big['chance_lamp']), 188)]

    sizes = {}
    for name, frames in (('keys', keys), ('chance', chance)):
        out, pals = b'', b''
        prev = np.zeros((H * len(frames), W, 4), np.uint8)
        for i, f in enumerate(frames):
            img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
            img.paste(f, (0, 0))
            a = np.array(img)
            idx, pal = quantize(img.convert('RGB'), 15, a[:, :, 3] >= 128)
            for part in (idx[:32], idx[32:, :32], idx[32:, 32:]):  # 64x32, then 2x 32x16
                h, w = part.shape
                t = part.reshape(h // 8, 8, w // 8, 8).swapaxes(1, 2).reshape(-1, 8, 8)
                out += (t[:, :, 0::2] | (t[:, :, 1::2] << 4)).astype(np.uint8).tobytes()
            p = np.zeros(16, np.uint16)
            p[:len(pal)] = rgb555(pal)
            pals += p.astype('<u2').tobytes()
            prev[i * H:(i + 1) * H, :, :3] = pal[idx]
            prev[i * H:(i + 1) * H, :, 3] = (idx != 0) * 255
        write(f'slider_{name}_tiles.bin', out)
        write(f'slider_{name}_pal.bin', pals)
        sizes[name] = frames[0].width
        Image.fromarray(prev).save(os.path.join(PRE, f'preview_slider_{name}.png'))
    print('slider widths', sizes)


if __name__ == '__main__':
    main()
