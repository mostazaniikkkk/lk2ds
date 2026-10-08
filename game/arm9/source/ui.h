#pragma once
#include "slot.h"

// Sprite overlays: data counter on the top screen (main engine, VRAM E); cabinet buttons, lever,
// LEDs and payout 7-seg on the bottom screen (sub engine, VRAM I).
// Behaviour follows the original BodyManager / ObjectButtonSprite / ObjectButtonLever:
//   buttons have 4 sprites (off, lamp, pushed, pushed+lamp) and shrink to 95% while held;
//   the lever knob travels from (108,661) to (114,670) in 0.1 s while held.

// Slide-in panel (PopupWindowManager): tab, cross key and CHANCE button at the bottom-right corner.
#define SLIDER_Y        144
#define SLIDER_W        97
#define SLIDER_TAB_W    13
#define SLIDER_X_OPEN   (256 - SLIDER_W)
#define SLIDER_X_CLOSED (256 - SLIDER_TAB_W)
#define SLIDER_KEYS_W   50  // cross key half; the CHANCE half follows
#define SLIDER_CROSS_DX 34  // cross key centre, relative to the panel
#define SLIDER_CROSS_DY 24

enum { UI_CROSS_NONE = -1, UI_CROSS_UP, UI_CROSS_RIGHT, UI_CROSS_DOWN, UI_CROSS_LEFT };
// top counter pages (layout.h CNT_FIELDS)
enum { UI_PAGE_MAIN, UI_PAGE_MEDALS, UI_PAGE_ODDS, UI_PAGE_VOLUME, UI_PAGE_ART, UI_PAGE_ZONE, UI_PAGE_VA };

typedef struct
{
    bool held[6];   // UI_BTN_* currently pressed
    bool lamp[6];   // UI_BTN_* lamp lit
    bool panelOpen; // slide-in panel target state
    int8_t cross;   // UI_CROSS_* lit on the panel's cross key
    uint8_t page;   // UI_PAGE_*
    uint8_t volume; // shown on UI_PAGE_VOLUME
    int mode[3];    // values of UI_PAGE_ART / ZONE / VA
} ui_buttons_t;

enum
{
    UI_BTN_STOP1,
    UI_BTN_STOP2,
    UI_BTN_STOP3,
    UI_BTN_BET,
    UI_BTN_CHANCE,
    UI_BTN_LEVER,
};

void ui_init(void);
void ui_update(const slot_t *s, const ui_buttons_t *b, bool idle, unsigned frame);
// left edge of the slide-in panel this frame (SLIDER_X_CLOSED .. SLIDER_X_OPEN), for touch tests
int ui_panel_x(void);
// call from the vblank interrupt
void ui_vblank(void);
