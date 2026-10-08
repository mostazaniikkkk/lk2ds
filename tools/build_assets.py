"""Turn the extracted game data (extract/extract_all.py) into the DS port's assets, then build the ROM.

Steps (each tool documents its own outputs):
  encoder    build the patched FastVideoDS encoder in the lk2ds-dev container (once)
  base       cabinet / reels / sprites / slider, SFX, BGM, title + menu, settings, guide
  video      boot opening + the normal-mode idle loop (7 clips, one per background)
  effects    continuous effects, Klein Stage, ART / zones / notices / VA clips (long: composites every layer)
  dates      date sim stills + script (all routes, Date Mode)
  songs      the machine's song list (streamed clips)
  pack       gallery, asset pack, ROM (./build.sh): output in build/sd (copy it to the SD card root)

usage: build_assets.py [step ...]      (no step: all of them, in order)
Needs Docker (image lk2ds-dev: docker build -t lk2ds-dev docker/) and Python 3.10+ (requirements.txt).
"""
import os, sys, shutil, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(REPO, 'extract'))
from paths import DATA

ENC_REPO = 'https://github.com/Gericom/FastVideoDSEncoder'
ENC_COMMIT = '1a25c4fcd7f4975beaf8312c0d8cc87496992ed6'
ENC_DIR = os.path.join(REPO, 'third_party', 'FastVideoDSEncoder')
ENC_PATCH = os.path.join(REPO, 'patches', 'fastvideods-encoder.patch')


def py(script, *args, cwd=REPO):
    print('>', script, *args, flush=True)
    subprocess.run([sys.executable, script, *args], check=True, cwd=cwd)


def docker(*cmd, tty=False):
    host = REPO.replace('\\', '/')
    subprocess.run(['docker', 'run', '--rm'] + (['-t'] if tty else []) +
                   ['-e', 'LC_ALL=C.UTF-8', '-v', f'{host}:/work', '-w', '/work', 'lk2ds-dev', *cmd],
                   check=True, env=dict(os.environ, MSYS_NO_PATHCONV='1'))


def fv_encode(src, dst):
    """FastVideoDS encode inside the container (the encoder wants a TTY for its progress bar)"""
    rel = lambda p: os.path.relpath(p, REPO).replace('\\', '/')
    docker('dotnet', 'build/fvenc/FastVideoDSEncoder.dll', '-j', '1', rel(src), rel(dst), tty=True)


def step_encoder():
    if os.path.exists(os.path.join(REPO, 'build', 'fvenc', 'FastVideoDSEncoder.dll')):
        return
    if not os.path.isdir(ENC_DIR):
        subprocess.run(['git', 'clone', ENC_REPO, ENC_DIR], check=True)
        subprocess.run(['git', '-C', ENC_DIR, 'checkout', ENC_COMMIT], check=True)
        subprocess.run(['git', '-C', ENC_DIR, 'apply', ENC_PATCH], check=True)
    docker('dotnet', 'publish', 'third_party/FastVideoDSEncoder/FastVideoDSEncoder/FastVideoDSEncoder.csproj',
           '-c', 'Release', '-o', '/work/build/fvenc')


def step_base():
    for t in ('make_icon.py', 'make_assets.py', 'make_slider.py', 'make_sfx.py', 'make_bgm.py', 'make_title.py',
              'make_settings.py', 'make_guide.py'):
        py(os.path.join('tools', t))


def step_video():
    b = os.path.join(REPO, 'build')
    os.makedirs(b, exist_ok=True)
    py(os.path.join('extract', 'make_opening.py'), os.path.join(b, 'opening.mp4'))
    fv_encode(os.path.join(b, 'opening.mp4'), os.path.join(REPO, 'game', 'data', 'video_opening.bin'))
    py(os.path.join('extract', 'make_idle.py'), os.path.join(b, 'idle.mp4'))  # backdrop of the effect clips
    py(os.path.join('extract', 'make_idle.py'), '--split', os.path.join(b, 'idle'))
    for k in range(7):
        fv_encode(os.path.join(b, 'idle', f'idle_{k}.mp4'), os.path.join(REPO, 'game', 'data', f'video_idle_{k}.bin'))


def step_effects():
    py(os.path.join('tools', 'make_effects.py'))


def step_dates():
    py(os.path.join('tools', 'make_date.py'))


def step_songs():
    py(os.path.join('tools', 'make_songs.py'))


def step_pack():
    py(os.path.join('tools', 'make_gallery.py'))
    py(os.path.join('tools', 'make_pak.py'))
    subprocess.run(['sh', os.path.join(REPO, 'build.sh')], check=True, cwd=REPO)


STEPS = {'encoder': step_encoder, 'base': step_base, 'video': step_video, 'effects': step_effects,
         'dates': step_dates, 'songs': step_songs, 'pack': step_pack}


def main():
    if not os.path.isdir(os.path.join(DATA, 'cdn', 'extracted', 'rom')):
        sys.exit(f'no extracted game data in {DATA}: run extract/extract_all.py first (or set LK2_DATA)')
    if not os.path.isdir(os.path.join(DATA, 'release', 'UnityProject')):
        sys.exit(f'no APK resources in {DATA}/release/UnityProject: run extract/extract_apk.py first')
    steps = sys.argv[1:] or list(STEPS)
    if 'encoder' not in steps and not os.path.exists(os.path.join(REPO, 'build', 'fvenc', 'FastVideoDSEncoder.dll')):
        steps.insert(0, 'encoder')
    for s in steps:
        print(f'=== {s}', flush=True)
        STEPS[s]()


if __name__ == '__main__':
    main()
