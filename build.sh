#!/bin/sh
# Run a command inside the lk2ds-dev container with nds/ mounted at /work.
#   ./build.sh               -> build the ROM (game/lk2ds.nds)
#   ./build.sh <cmd...>      -> run <cmd> in /work
set -e
cd "$(dirname "$0")"
HOST_DIR="$(pwd -W 2>/dev/null || pwd)"
TTY=""
[ -t 1 ] && TTY="-t"
if [ $# -eq 0 ]; then
    # build the ROM, then put it in build/sd: melonDS uses that folder as its SD card (DLDI FolderSync),
    # with the clips in build/sd/lk2ds/movies (tools/make_pak.py)
    MSYS_NO_PATHCONV=1 docker run --rm $TTY -e LC_ALL=C.UTF-8 -v "$HOST_DIR:/work" -w /work lk2ds-dev make -C game -j8
    mkdir -p build/sd
    cp game/lk2ds.nds build/sd/lk2ds.nds
    # empty fixed-size save, written in place by the game (no free space needed on the card)
    [ -f build/sd/lk2ds.sav ] && [ "$(wc -c < build/sd/lk2ds.sav)" -ge 4096 ] || head -c 4096 /dev/zero > build/sd/lk2ds.sav
    exit 0
fi
MSYS_NO_PATHCONV=1 exec docker run --rm $TTY -e LC_ALL=C.UTF-8 -v "$HOST_DIR:/work" -w /work lk2ds-dev "$@"
