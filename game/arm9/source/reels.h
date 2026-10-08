#pragma once
#include <nds.h>

// Bottom screen (sub engine): cabinet + 3 reels.
//   VRAM C = sub BG, VRAM H = sub BG ext palettes, VRAM I = sub sprites.
//   BG0 cabinet (8bpp, prio 0), BG1-3 reels (8bpp, prio 1, streamed tiles, per-line darkening).

typedef enum
{
    REEL_STOPPED,
    REEL_SPINNING,
    REEL_STOPPING,
} reel_state_t;

void reels_init(void);
// call once per frame from the vblank interrupt
void reels_vblank(void);

void reels_start(void);
// top-row symbol a spinning reel would show if stopped now with no slip; slip s shows (base - s)
int reels_stop_base(int i);
// stop reel i (0..2) after sliding `slip` extra symbols (0..4)
void reels_stop(int i, int slip);
reel_state_t reels_state(int i);
bool reels_all_stopped(void);
// symbol index (0..20 on the strip) shown in the given row (0 = top .. 2 = bottom) of a stopped reel
int reels_symbol_at(int i, int row);

// Full-screen 8bpp overlay over the cabinet (the date sim's choices), only while the reels are stopped:
// BG1 (reel 1) is borrowed with its own map, tiles and ext palette; the rest is dimmed, sprites hidden.
// Tiles: 32x24 8bpp tiles in row order (map = identity), index 0 transparent.
u8 *reels_overlay_tiles(void);
void reels_overlay_show(const u16 *pal);   // 256 colours
void reels_overlay_hide(void);
