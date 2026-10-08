"""Banner icon (game/icon.gif, 32x32, 16 colours): the app icon's protagonist salute crop.
Source: the APK's Resources/icon/And_512_00.png (extract/extract_apk.py)."""
import os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'extract'))
from paths import DATA

SRC = os.path.join(DATA, 'release/UnityProject/ExportedProject/Assets/Resources/icon/And_512_00.png')


def main():
    src = Image.open(SRC).convert('RGBA')
    im = src.crop((104, 26, 424, 346)).resize((32, 32), Image.LANCZOS)
    bg = Image.new('RGBA', (32, 32), (255, 255, 255, 255))
    bg.alpha_composite(im)
    q = bg.convert('RGB').quantize(15, method=Image.MEDIANCUT, dither=Image.NONE)
    q.save(os.path.join(HERE, '..', 'game', 'icon.gif'))


if __name__ == '__main__':
    main()
