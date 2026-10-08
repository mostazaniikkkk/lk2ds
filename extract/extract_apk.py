"""Export the Unity resources of the game's APK (cabinet, reels, buttons, title / menu / settings / manual art)
with AssetRipper, into DATA/release/UnityProject. The CDN only holds the LCD content (images, sounds, movies,
GOD rom); the machine's own artwork ships inside the APK.

Needs:
  - the app package: jp.konami.ps_lk2 1.0.3, as .xapk (APKPure) or base.apk
  - AssetRipper 2.0.0 (https://github.com/AssetRipper/AssetRipper/releases, AssetRipper_win_x64.zip or the
    Linux / macOS build); pass the path of its GUI executable (it is driven headless through its web API)

usage: extract_apk.py <app.xapk | base.apk> <AssetRipper.GUI.Free executable>
"""
import os, sys, time, shutil, zipfile, subprocess, tempfile, urllib.request, urllib.parse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paths import DATA

PORT = 8765
OUT = os.path.join(DATA, 'release', 'UnityProject')
SETTINGS = {  # same export settings the port was made with
    'BundledAssetsExportMode': 'GroupByBundleName', 'ScriptContentLevel': 'Level2',
    'AudioExportFormat': 'PreferWav', 'ImageExportFormat': 'Png', 'SpriteExportMode': 'Texture2D',
}


def post(path, fields):
    data = urllib.parse.urlencode(fields, doseq=True).encode()
    with urllib.request.urlopen(urllib.request.Request(f'http://127.0.0.1:{PORT}{path}', data), timeout=3600) as r:
        return r.read()


def unpack(pkg, tmp):
    """the APK's assets/bin/Data folder (from an .xapk: its base apk)"""
    apk = pkg
    if pkg.lower().endswith('.xapk'):
        with zipfile.ZipFile(pkg) as z:
            name = next(n for n in z.namelist() if n.endswith('.apk') and 'config.' not in n)
            z.extract(name, tmp)
            apk = os.path.join(tmp, name)
    with zipfile.ZipFile(apk) as z:
        for n in z.namelist():
            if n.startswith('assets/bin/Data/'):
                z.extract(n, tmp)
    return os.path.join(tmp, 'assets', 'bin', 'Data')


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    pkg, ripper = sys.argv[1], sys.argv[2]
    tmp = tempfile.mkdtemp(prefix='lk2apk_')
    try:
        data_dir = unpack(pkg, tmp)
        proc = subprocess.Popen([ripper, '--headless', '--port', str(PORT)], stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        try:
            for _ in range(60):  # wait for the web server
                try:
                    urllib.request.urlopen(f'http://127.0.0.1:{PORT}/', timeout=2)
                    break
                except OSError:
                    time.sleep(1)
            post('/Settings/Update', SETTINGS)
            print('loading', data_dir, flush=True)
            post('/LoadFolder', {'Path': data_dir})
            os.makedirs(OUT, exist_ok=True)
            print('exporting to', OUT, flush=True)
            post('/Export/UnityProject', {'Path': OUT})
        finally:
            proc.terminate()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    res = os.path.join(OUT, 'ExportedProject', 'Assets', 'Resources', 'image')
    print('ok' if os.path.isdir(res) else 'WARNING: no Resources/image in the export', res)


if __name__ == '__main__':
    main()
