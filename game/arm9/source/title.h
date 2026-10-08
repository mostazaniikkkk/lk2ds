#pragma once

// Title ("TAP TO START") and main menu, built from the app's own title assets (tools/make_title.py).
// Uses VRAM A (main BG bitmap) and VRAM C (sub BG bitmap); the game reconfigures both afterwards.

typedef enum
{
    TITLE_SIMULATOR,
    TITLE_TRIAL,
    TITLE_TIMEOUT, // the title music ended without input: show the opening again (attract mode)
} title_result_t;

// boot screens: KONAMI logo + fan-port notice, then 18+ / responsible gambling (once, before the title)
void title_caution(void);
// runs until the player picks a game mode (the menu music keeps playing), or TITLE_TIMEOUT
title_result_t title_run(void);
// title / menu video mode: 16-bit bitmaps on BG3 of both screens (VRAM A top, VRAM C bottom), title art on top
void title_setup_video(void);
// HOW TO PLAY page (bottom screen must still be in the title/settings video mode)
void title_show_howto(void);
