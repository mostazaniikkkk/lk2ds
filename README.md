# LK2 DS — a Nintendo DS demake of *Bouku Shoujo Lovekyure 2*

A fan-made reinterpretation of Konami's pachislot **パチスロ 防空少女ラブキューレ２ ～極限の共鳴～**
(app `jp.konami.ps_lk2`) for the Nintendo DS, written in C with [BlocksDS](https://blocksds.skylyrac.net/).

The top screen plays the machine's LCD (full-motion video streamed from the SD card with
[FastVideoDS](https://github.com/Gericom/FastVideoDSPlayer)); the bottom screen is the cabinet: reels, buttons,
lever and the slide-in panel, all touch-driven. The text is translated to English.

> **This repository contains no game assets.** Every image, sound, movie and text of the original belongs to
> Konami. You need your own copy of the app's data: the scripts in [`extract/`](extract) download and unpack it,
> and the ones in [`tools/`](tools) turn it into the DS assets. Non-commercial fan project, not affiliated with
> or endorsed by Konami.

## Features

- Slot machine with the original reel strips, payouts, settings 1–6, stop orders, 4.1 s wait, auto aim
- BIG (150 medals) / REG (8 payouts), with the three BIG notices: **chance**, **full** (golden shutter) and **date**
- **Date sim** (BIG date notice): Ichigo, Ai, Rion, Tsugumi and the Klein group date — questions, affection,
  confessions; Klein unlocks after two confessions
- **Date Mode** with Ichigo, the **Klein Stage**, the continuous effects (vacation race, ECM battle,
  Rion vs Koon, Tsugumi vs Short)
- **Absolute Airspace** (ART) with Weapon Attack zones (mash / hold / rub the touch screen), Dimension Battle,
  Ashitsubo Punish and Nadenade LoveKyure add zones, and **VA Ignition**
- Boot opening, play guide, settings screen, data counter pages
- **Gallery**: CGs, movies, music and the machine's full song list, bought with the credits you win;
  progress in a classic `.sav` file next to the ROM

## Hardware

Made for the **DSpico**; tested in melonDS. It runs from any flashcart with a working DLDI driver (ARM7-capable
drivers are used directly, ARM9-only ones through a sector proxy). The game reads its own `.nds` and the movie
files from the card, so it needs the files below on the card and roughly 750 MB of space. Like an Android app's
external data, the movies live in `data/lk2ds` at the root of the card; the ROM and its save can go in any folder
(e.g. `/games`), as long as the `.sav` sits next to the `.nds` with the same name.

```
/games/lk2ds.nds
/games/lk2ds.sav             (4 KB, created by the build)
/data/lk2ds/movies/*.fv      (all videos and songs)
```

## Building

Requirements: **Docker**, **Python 3.10+** (`pip install -r requirements.txt`) and git.
On Windows, use Git Bash for the shell scripts.

1. **Build environment** (BlocksDS, ffmpeg libraries, .NET for the video encoder):
   ```sh
   docker build -t lk2ds-dev docker/
   ```
2. **Game data.** Pick a folder for it (default `./data`, or set `LK2_DATA`). Download the CDN and unpack it:
   ```sh
   python extract/extract_all.py
   ```
   The machine's own artwork (cabinet, title, menus, manual) is inside the APK, not on the CDN. Get the app
   package (`jp.konami.ps_lk2` 1.0.3, `.xapk` or `base.apk`) and [AssetRipper 2.0.0](https://github.com/AssetRipper/AssetRipper/releases),
   then:
   ```sh
   python extract/extract_apk.py path/to/app.xapk path/to/AssetRipper.GUI.Free
   ```
   (or run both at once: `extract_all.py --apk ... --assetripper ...`). The CDN is Konami's and may disappear;
   keep your dump.
3. **DS assets and ROM:**
   ```sh
   python tools/build_assets.py
   ```
   This builds the FastVideoDS encoder (cloned from upstream and patched with
   [`patches/fastvideods-encoder.patch`](patches/fastvideods-encoder.patch)), renders every clip from the original
   scene data, bakes in the English text (date sim script in [`tools/date_text`](tools/date_text), the rest in
   the tools), packs everything and builds the ROM. The full run takes a while (it composites thousands of frames);
   steps can be run alone, e.g. `build_assets.py base pack`.
4. Copy `build/sd/data/` to the root of the SD card and `lk2ds.nds` + `lk2ds.sav` to your games folder.

After the first build, `./build.sh` rebuilds only the code.

## Layout

| Path | |
|---|---|
| `game/` | DS code: `arm9/source` (game, video player, date sim, gallery…), `arm7/source` (FatFs, audio, video streaming), `common` |
| `extract/` | CDN download (`cdn_dump.py`), EB01 decryption, Unity bundle / CRI CPK + USM extraction, GOD graphics-rom parser and scene compositor (`god.py`, `god_render.py`), sound sequences (`movies_audio.py`) |
| `tools/` | Asset converters (`make_*.py`), asset pack, translations (`date_text/`), `build_assets.py` |
| `docker/`, `build.sh` | Build environment |
| `patches/` | Changes to the FastVideoDS encoder |

## How it works (short)

- The original LCD is drawn by Konami's **GOD** engine from a graphics rom (`GODRom_L.GOD`): scenes made of
  triggers, layers, image patterns, movies and keyframed actions. `extract/god_render.py` re-implements enough
  of it (layer order, blend modes, per-frame motion) to render the scenes offline; the DS plays the result as
  FastVideoDS clips.
- Sound sequences (`sacinfo.csv`) are replayed to rebuild each scene's audio and the BGMs.
- On the DS the ARM7 streams the clips and the asset pack from the SD card while the ARM9 decodes video and
  runs the game; full-screen pictures are LZ77-compressed and loaded on demand.

## Credits

- Original game, art, music and text: © Konami Digital Entertainment / Konami Amusement
- Port: Mostazaniikkkk
- [BlocksDS](https://github.com/blocksds/sdk) (toolchain and libraries), [FatFs](http://elm-chan.org/fsw/ff/)
- [FastVideoDS](https://github.com/Gericom/FastVideoDSPlayer) by Gericom: the video player in `game/*/source/fv`
  is derived from it, and the encoder is fetched from its repository; both remain under their authors' terms
- [AssetRipper](https://github.com/AssetRipper/AssetRipper), [UnityPy](https://github.com/K0lb3/UnityPy)

## License

The original code, tools and translations in this repository are licensed under
[CC BY-NC-SA 4.0](LICENSE). Third-party code keeps its own license. No Konami content is included or licensed;
the translation tables in `tools/date_text` quote the original Japanese lines next to the English only as a
reference for translators.
