"""Where the original game data lives (never part of this repository).

DATA is a folder with the layout the extraction scripts produce:
  DATA/cdn/...                         CDN mirror, decrypted bundles, extracted images / sound / movies / GOD rom
  DATA/release/UnityProject/...        the APK's Unity resources exported by AssetRipper (extract_apk.py)
Set it with the LK2_DATA environment variable; by default it is ./data next to this repository's root
(or the repository's parent folder when that one already holds a cdn/ dump, as in the original workspace).
"""
import os

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))


def _data():
    env = os.environ.get('LK2_DATA')
    if env:
        return os.path.abspath(env)
    parent = os.path.dirname(REPO)
    if os.path.isdir(os.path.join(parent, 'cdn', 'extracted')):
        return parent
    return os.path.join(REPO, 'data')


DATA = _data()
