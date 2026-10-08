"""Download and unpack everything the DS port is built from, into DATA (see paths.py):

  1. cdn_dump.py        download the asset CDN (manifest.0 + chunks, MD5-checked, reassembled into cdn/files)
  2. decrypt_all.py     EB01 decryption -> cdn/decrypted (Unity bundles, sound tables)
  3. extract_bundles.py LCD textures -> cdn/extracted/images, AudioClips -> cdn/extracted/sound,
                        GOD graphics rom + VM bytecode -> cdn/extracted/rom
  4. extract_movies.py  CRI CPK/USM movies -> cdn/extracted/movies (MPEG-1 colour + alpha streams)
  5. extract_apk.py     the APK's own artwork via AssetRipper (only with --apk / --assetripper)

usage: extract_all.py [--apk app.xapk --assetripper path/to/AssetRipper.GUI.Free] [--skip-download] [--workers N]
Needs Python 3.10+ with: numpy pillow UnityPy imageio-ffmpeg (pip install -r requirements.txt).
The CDN belongs to Konami and may go offline: keep your dump.
"""
import os, sys, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from paths import DATA


def run(*args):
    print('>', ' '.join(args), flush=True)
    subprocess.run([sys.executable, *args], check=True, cwd=HERE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--apk')
    ap.add_argument('--assetripper')
    ap.add_argument('--skip-download', action='store_true')
    ap.add_argument('--workers', type=int, default=4)
    a = ap.parse_args()
    print('data folder:', DATA)
    if not a.skip_download:
        run('cdn_dump.py', 'Android')
    run('decrypt_all.py')
    procs = [subprocess.Popen([sys.executable, 'extract_bundles.py', 'images', str(w), str(a.workers)], cwd=HERE)
             for w in range(a.workers)]
    run('extract_bundles.py', 'sound')
    run('extract_bundles.py', 'rom')
    for p in procs:
        if p.wait():
            sys.exit('image extraction failed')
    run('extract_movies.py')
    if a.apk and a.assetripper:
        run('extract_apk.py', a.apk, a.assetripper)
    else:
        print('skipping the APK (pass --apk and --assetripper): the cabinet / title artwork comes from it')


if __name__ == '__main__':
    main()
