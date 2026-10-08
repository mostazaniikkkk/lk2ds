#include <nds.h>
#include "title.h"
#include "title_layout.h"
#include "sfx.h"
#include "bgm.h"
#include "fade.h"
#include "guide.h"
#include "gallery.h"

#include "asset.h"

typedef struct
{
    u8 x, y, w, h;
} rect_t;

enum { ITEM_SIMULATOR, ITEM_TRIAL, ITEM_GALLERY, ITEM_HOWTO, ITEM_TERMS, ITEM_LICENSE, ITEM_COUNT };

static const rect_t sMenu[ITEM_COUNT] = MENU_RECTS;
static const rect_t sBack = BACK_RECT;

#define WIN_BG3     (1 << 3)
#define WIN_EFFECTS (1 << 5)
#define BLDY_CURSOR 3
#define BLDY_PRESS  8

static void title_vblank(void)
{
}

// full-screen bitmaps: BIOS LZ77 from the asset pack, decompressed straight into VRAM
static void show_bottom(pak_id_t bitmap_lz)
{
    asset_show_lz(bitmap_lz, BG_GFX_SUB);
}

// brighten one rectangle of the bottom screen (window 0 + fade to white); NULL hides it
static void highlight(const rect_t *r, int level)
{
    if (r == NULL)
    {
        REG_WIN0H_SUB = 0;
        REG_WIN0V_SUB = 0;
        return;
    }
    REG_WIN0H_SUB = (r->x << 8) | (r->x + r->w);
    REG_WIN0V_SUB = (r->y << 8) | (r->y + r->h);
    REG_BLDY_SUB = level;
}

static bool inside(const rect_t *r, const touchPosition *t)
{
    return t->px >= r->x && t->px < r->x + r->w && t->py >= r->y && t->py < r->y + r->h;
}

void title_setup_video(void)
{
    // top: 16-bit bitmap in VRAM A
    vramSetBankA(VRAM_A_MAIN_BG);
    videoSetMode(MODE_5_2D | DISPLAY_BG3_ACTIVE);
    REG_BG3CNT = (u16)BgSize_B16_256x256 | BG_PRIORITY_0;
    REG_BG3PA = 256;
    REG_BG3PB = 0;
    REG_BG3PC = 0;
    REG_BG3PD = 256;
    REG_BG3X = 0;
    REG_BG3Y = 0;
    asset_show_lz(PAK_TITLE_TOP_LZ, BG_GFX);

    // bottom: 16-bit bitmap in VRAM C, window 0 used to highlight buttons
    vramSetBankC(VRAM_C_SUB_BG);
    videoSetModeSub(MODE_5_2D | DISPLAY_BG3_ACTIVE | DISPLAY_WIN0_ON);
    REG_BG3CNT_SUB = (u16)BgSize_B16_256x256 | BG_PRIORITY_0;
    REG_BG3PA_SUB = 256;
    REG_BG3PB_SUB = 0;
    REG_BG3PC_SUB = 0;
    REG_BG3PD_SUB = 256;
    REG_BG3X_SUB = 0;
    REG_BG3Y_SUB = 0;
    REG_WININ_SUB = WIN_BG3 | WIN_EFFECTS;
    REG_WINOUT_SUB = WIN_BG3;
    REG_BLDCNT_SUB = BLEND_FADE_WHITE | BLEND_SRC_BG3;
    highlight(NULL, 0);
}

static void wait_release(void)
{
    do
    {
        swiWaitForVBlank();
        scanKeys();
    } while (keysHeld() & (KEY_TOUCH | KEY_A | KEY_B | KEY_START));
}

#define TAP_CONFIRM_FRAMES 48   // fast blink after the tap (0.8 s)

// "TAP TO START": blink the prompt band until a tap / A / START, then blink fast while the confirm
// sound plays. Gives up (false) after timeout vblanks, 0 = never.
static bool tap_to_start(u32 timeout)
{
    u8 *tap = asset_load(PAK_TITLE_TAP, NULL);
    u8 *plain = asset_load(PAK_TITLE_NOTAP, NULL);
    u16 *band = (u16 *)BG_GFX_SUB + TITLE_TAP_Y * 256;
    if (tap == NULL || plain == NULL) // no blinking without the bands
    {
        asset_free(tap);
        asset_free(plain);
        tap = plain = NULL;
    }
    for (unsigned frame = 0;; frame++)
    {
        swiWaitForVBlank();
        if (timeout && frame >= timeout)
        {
            asset_free(tap);
            asset_free(plain);
            return false;
        }
        if (tap && (frame & 31) == 0)
            dmaCopy(tap, band, TITLE_TAP_H * 512);
        else if (tap && (frame & 31) == 22)
            dmaCopy(plain, band, TITLE_TAP_H * 512);
        scanKeys();
        if (keysDown() & (KEY_TOUCH | KEY_A | KEY_START))
            break;
    }
    bgm_stop();
    sfx_play(SFX_UI_START);
    for (int frame = 0; frame < TAP_CONFIRM_FRAMES; frame++)
    {
        swiWaitForVBlank();
        if (tap && frame % 6 == 0)
            dmaCopy(tap, band, TITLE_TAP_H * 512);
        else if (tap && frame % 6 == 3)
            dmaCopy(plain, band, TITLE_TAP_H * 512);
    }
    asset_free(tap);
    asset_free(plain);
    wait_release();
    return true;
}

// info page with a BACK button
static void show_page(pak_id_t bitmap)
{
    show_bottom(bitmap);
    highlight(NULL, 0);
    bool pressing = false;
    touchPosition t = { 0 };
    while (1)
    {
        swiWaitForVBlank();
        scanKeys();
        u32 down = keysDown(), held = keysHeld();
        if (down & (KEY_B | KEY_A | KEY_START))
            break;
        if (held & KEY_TOUCH)
        {
            touchRead(&t);
            pressing = inside(&sBack, &t);
            highlight(pressing ? &sBack : NULL, BLDY_PRESS);
        }
        else if (pressing) // released over BACK
        {
            break;
        }
    }
    sfx_play(SFX_UI_BACK);
    highlight(NULL, 0);
    wait_release();
}

// one boot screen: fade in, wait (input accepted after min_frames, continues alone at max_frames), fade out
static void boot_screen(pak_id_t top_lz, pak_id_t bot_lz, int min_frames, int max_frames)
{
    irqSet(IRQ_VBLANK, title_vblank);
    setBrightness(3, -16);
    title_setup_video();
    asset_show_lz(top_lz, BG_GFX);
    show_bottom(bot_lz);
    fade(-16, 0, 20);
    for (int frame = 0; frame < max_frames; frame++)
    {
        swiWaitForVBlank();
        scanKeys();
        if (frame >= min_frames && (keysDown() & (KEY_TOUCH | KEY_A | KEY_START)))
            break;
    }
    fade(0, -16, 20);
    wait_release();
}

// boot: KONAMI logo + fan-port notice, then the 18+ / responsible gambling notice
void title_caution(void)
{
    boot_screen(PAK_SPLASH_TOP_LZ, PAK_SPLASH_BOT_LZ, 60, 180);
    boot_screen(PAK_CAUTION_TOP_LZ, PAK_CAUTION_BOT_LZ, 120, 480);
}

void title_show_howto(void)
{
    guide_run();
}

title_result_t title_run(void)
{
    irqSet(IRQ_VBLANK, title_vblank);
    setBrightness(3, -16);
    title_setup_video();
    show_bottom(PAK_TITLE_BOT_LZ);
    // the title music plays once; when it ends the opening comes back
    u32 length = bgm_play_once(BGM_TITLE);
    fade(-16, 0, 20);
    if (!tap_to_start(length > 20 ? length - 20 : length))
    {
        fade(0, -16, 20);
        bgm_stop();
        return TITLE_TIMEOUT;
    }

    int cursor = ITEM_SIMULATOR;
    int pressed = -1;
    touchPosition t = { 0 };
    show_bottom(PAK_MENU_BOT_LZ);
    bgm_play(BGM_MENU);
    while (1)
    {
        swiWaitForVBlank();
        scanKeys();
        u32 down = keysDown(), held = keysHeld();
        int choose = -1;

        if (down & (KEY_UP | KEY_DOWN))
        {
            cursor = (cursor + (down & KEY_UP ? ITEM_COUNT - 1 : 1)) % ITEM_COUNT;
            sfx_play(SFX_UI_CURSOR);
        }
        if (down & (KEY_A | KEY_START))
            choose = cursor;

        if (held & KEY_TOUCH)
        {
            touchRead(&t);
            if (down & KEY_TOUCH)
            {
                pressed = -1;
                for (int i = 0; i < ITEM_COUNT; i++)
                    if (inside(&sMenu[i], &t))
                        pressed = i;
                if (pressed >= 0)
                    sfx_play(SFX_UI_CURSOR); // restarts the sound on every new tap
            }
            if (pressed >= 0)
                cursor = pressed;
        }
        else if (pressed >= 0)
        {
            // released: activate if the finger was still on the same button
            if (inside(&sMenu[pressed], &t))
                choose = pressed;
            pressed = -1;
        }

        bool touchingButton = pressed >= 0 && inside(&sMenu[pressed], &t);
        highlight(&sMenu[cursor], touchingButton ? BLDY_PRESS : BLDY_CURSOR);

        if (choose < 0)
            continue;
        sfx_play(SFX_UI_OK);
        highlight(&sMenu[choose], BLDY_PRESS);
        wait_release();
        switch (choose)
        {
            case ITEM_SIMULATOR: return TITLE_SIMULATOR;  // menu music keeps playing in the settings
            case ITEM_TRIAL: return TITLE_TRIAL;
            case ITEM_GALLERY: gallery_run(); break;
            case ITEM_HOWTO: guide_run(); break;
            case ITEM_TERMS: show_page(PAK_PAGE_TERMS_LZ); break;
            case ITEM_LICENSE: show_page(PAK_PAGE_LICENSE_LZ); break;
        }
        show_bottom(PAK_MENU_BOT_LZ);
    }
}
