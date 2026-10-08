"""Render looping BGM (SD_BGM_* SAC sequences) for the DS, embedded in the ROM.

File format (game/data/bgm_<name>.bin, little endian):
  u32 magic 'LKBG', u32 sample rate, u32 bytes per channel, u32 loop point (words, from channel start)
  left channel  (DS IMA-ADPCM: 4-byte header + nibbles)
  right channel (same size)
Previews: build/bgm_ds/<name>.wav (what the DS will play, one loop).
"""
import os, sys, struct, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'extract'))
from paths import DATA as ROOT  # original game data (extract/paths.py)
sys.path.insert(0, HERE)
import movies_audio as ma
from make_sfx import adpcm_encode

SD = os.path.join(HERE, '..', 'game', 'data')
PRE = os.path.join(HERE, '..', 'build', 'bgm_ds')
RATE = 32728

# (output name, SAC sequence)
BGM = [
    ('title', 'SD_BGM_AyXX_A'),  # TAP TO START
    ('menu', 'SD_BGM_Ia12'),     # main menu
    ('race', 'SD_BGM_Ay04'),     # continuous effects: chibi race for vacation days (Sn_Ay04)
    ('ecm', 'SD_BGM_Ay01'),      # ECM battle (Sn_Ay01)
    ('rion', 'SD_BGM_Ay02'),     # Rion vs Koon (Sn_Ay02)
    ('tsug', 'SD_BGM_Ay03'),     # Tsugumi vs Short (Sn_Ay03)
    ('art', 'SD_BGM_FaXX_A', 22050, True),    # Absolute Airspace (ART); long tracks: mono, lower rate (RAM)
    ('artsp', 'SD_BGM_FaXX_C', 22050, True),  # ART, super high chance
    ('zone', 'SD_BGM_Ec01'),     # Weapon Attack zones (Ai, Rion, Tsugumi)
    ('ichigo', 'SD_BGM_Ec03_A'), # Ichigo Attack
    ('battle', 'SD_BGM_Fc01'),   # Dimension Battle
    ('ashi', 'SD_BGM_Fc04_A'),   # add zone: Ashitsubo Punish
    ('nade', 'SD_BGM_Fc05'),     # add zone: Nadenade LoveKyure
    ('va', 'SD_BGM_FaXX_B', 22050, True),     # VA Ignition
]


def resample(a, rate=RATE):
    n = int(len(a) * rate / ma.RATE)
    n -= n % 8  # part ends on a word boundary (8 ADPCM nibbles)
    t = np.arange(n) * len(a) / n
    return np.stack([np.interp(t, np.arange(len(a)), a[:, c]) for c in range(2)], 1)


def loop_samples(sac_name, rate=RATE):
    """a sequence playing one looping wav (PLAY wav,0,loop) or an intro wav then a looping one
    (PLAY intro,loop,1), with its VOL. Returns (channels, intro length in samples)."""
    sac = ma.sac_by_name[sac_name]
    plays = [a for _, _, op, a in sorted(ma.steps[sac]) if op == 9]
    assert len(plays) == 1, f'{sac_name}: only one PLAY is supported'
    vol = [a[0] / 255 for _, _, op, a in ma.steps[sac] if op == 5]
    vol = vol[0] if vol else 1.0
    parts = [resample(ma.load_wav(w) * vol, rate) for w in (plays[0][:2] if plays[0][1] else plays[0][:1])]
    a = np.concatenate(parts)
    intro = len(parts[0]) if len(parts) > 1 else 0
    return [np.clip(a[:, c] * 32767, -32767, 32767).astype(np.int16) for c in range(2)], intro


def main():
    os.makedirs(SD, exist_ok=True)
    os.makedirs(PRE, exist_ok=True)
    for entry in BGM:
        name, sac = entry[:2]
        rate = entry[2] if len(entry) > 2 else RATE
        mono = len(entry) > 3 and entry[3]
        chans, intro = loop_samples(sac, rate)
        if mono:  # one channel played on both speakers (magic 'LKBM')
            m = ((chans[0].astype(np.int32) + chans[1]) // 2).astype(np.int16)
            chans = [m]
        enc = [adpcm_encode(c) for c in chans]
        data = [e[0] for e in enc]
        size = len(data[0])
        assert all(len(d) == size for d in data)
        with open(os.path.join(SD, f'bgm_{name}.bin'), 'wb') as f:
            # loop point in words: after the ADPCM header and the intro (8 samples per word)
            f.write(struct.pack('<4I', 0x4D424B4C if mono else 0x47424B4C, rate, size, 1 + intro // 8))
            for d in data:
                f.write(d)
        with wave.open(os.path.join(PRE, f'{name}.wav'), 'wb') as w:
            w.setnchannels(len(enc)); w.setsampwidth(2); w.setframerate(rate)
            w.writeframes(np.stack([e[1] for e in enc], 1).tobytes())
        print(f'{name}: {sac} {intro / rate:.2f}s intro + {(len(chans[0]) - intro) / rate:.2f}s loop, '
              f'{len(data) * size // 1024} KB')


if __name__ == '__main__':
    main()
