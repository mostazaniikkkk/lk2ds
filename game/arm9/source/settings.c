#include <nds.h>
#include "settings.h"
#include "settings_layout.h"
#include "sfx.h"

#include "asset.h"

game_settings_t gSettings = {
    .setting = SETTING_RANDOM,
    .order = 0,
    .waitCut = false,
    .autoAim = false,
    .guide = false,  // HOW TO PLAY is in the menu already: do not force it
    .volume = 10,
};

const uint8_t gStopOrders[6][3] = {
    { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 },
};

typedef struct
{
    u8 x, y, w, h;
} rect_t;

enum { ROW_SETTING, ROW_ORDER, ROW_WAIT, ROW_AIM, ROW_START, ROW_GUIDE, ROW_VOLUME };
enum { KIND_LINEAR, KIND_CYCLIC, KIND_TOGGLE, KIND_DISABLED };

static const u8 sKind[SET_ROWS] = { KIND_LINEAR, KIND_CYCLIC, KIND_TOGGLE, KIND_TOGGLE,
                                    KIND_DISABLED, KIND_TOGGLE, KIND_LINEAR };
static const u8 sBase[SET_ROWS] = SET_ROW_BASE;
static const u8 sCount[SET_ROWS] = SET_ROW_COUNT;
static const rect_t sHits[SET_ROWS][2] = SET_ROW_HITS;
static const rect_t sTitleBtn = SET_TITLE_RECT;
static const rect_t sPlayBtn = SET_PLAY_RECT;

#define WIN_BG3     (1 << 3)
#define WIN_EFFECTS (1 << 5)
#define BLDY_CURSOR 3
#define BLDY_PRESS  8

static u16 sStrip[SET_CTRL_W * SET_ROW_H] ALIGN(32);
static u8 *sCtrl, *sDesc; // packs loaded while the screen is up

static const void *pack_entry(const u8 *pack, int i)
{
    const u32 *offs = (const u32 *)pack;
    return pack + offs[1 + i];
}

// value index of a row (strip order: toggles store ON first)
static int row_value(int row)
{
    switch (row)
    {
        case ROW_SETTING: return gSettings.setting;
        case ROW_ORDER: return gSettings.order;
        case ROW_WAIT: return gSettings.waitCut ? 0 : 1;
        case ROW_AIM: return gSettings.autoAim ? 0 : 1;
        case ROW_GUIDE: return gSettings.guide ? 0 : 1;
        case ROW_VOLUME: return gSettings.volume;
        default: return 0;
    }
}

static void set_row_value(int row, int v)
{
    switch (row)
    {
        case ROW_SETTING: gSettings.setting = v; break;
        case ROW_ORDER: gSettings.order = v; break;
        case ROW_WAIT: gSettings.waitCut = v == 0; break;
        case ROW_AIM: gSettings.autoAim = v == 0; break;
        case ROW_GUIDE: gSettings.guide = v == 0; break;
        case ROW_VOLUME:
            gSettings.volume = v;
            sfx_set_volume(v);
            break;
        default: break;
    }
}

static void draw_row(int row)
{
    if (sCtrl == NULL)
        return;
    decompress(pack_entry(sCtrl, sBase[row] + row_value(row)), sStrip, LZ77);
    DC_FlushRange(sStrip, sizeof(sStrip));
    u16 *dst = (u16 *)BG_GFX_SUB + (SET_ROW_Y0 + row * SET_ROW_H) * 256 + SET_CTRL_X;
    for (int y = 0; y < SET_ROW_H; y++)
        dmaCopy(&sStrip[y * SET_CTRL_W], dst + y * 256, SET_CTRL_W * 2);
}

static void draw_desc(int row)
{
    if (sDesc)
        decompress(pack_entry(sDesc, row), (u16 *)BG_GFX + SET_DESC_Y * 256, LZ77Vram);
}

// dir: -1 = left arrow / ON, +1 = right arrow / OFF; returns true if the value changed
static bool change(int row, int dir)
{
    int v = row_value(row), n = sCount[row], nv = v;
    switch (sKind[row])
    {
        case KIND_LINEAR: nv = v + dir < 0 ? 0 : v + dir >= n ? n - 1 : v + dir; break;
        case KIND_CYCLIC: nv = (v + dir + n) % n; break;
        case KIND_TOGGLE: nv = dir < 0 ? 0 : 1; break;
        default: return false;
    }
    if (nv == v)
        return false;
    set_row_value(row, nv);
    draw_row(row);
    return true;
}

static bool inside(const rect_t *r, const touchPosition *t)
{
    return t->px >= r->x && t->px < r->x + r->w && t->py >= r->y && t->py < r->y + r->h;
}

static void highlight(const rect_t *r, int level)
{
    REG_WIN0H_SUB = (r->x << 8) | (r->x + r->w);
    REG_WIN0V_SUB = (r->y << 8) | (r->y + r->h);
    REG_BLDY_SUB = level;
}

static void setup_video(void)
{
    vramSetBankA(VRAM_A_MAIN_BG);
    videoSetMode(MODE_5_2D | DISPLAY_BG3_ACTIVE);
    REG_BG3CNT = (u16)BgSize_B16_256x256 | BG_PRIORITY_0;
    REG_BG3PA = 256;
    REG_BG3PB = 0;
    REG_BG3PC = 0;
    REG_BG3PD = 256;
    REG_BG3X = 0;
    REG_BG3Y = 0;
    asset_show_lz(PAK_TITLE_TOP_LZ, BG_GFX); // same art; the description band covers the copyright

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
    asset_show_lz(PAK_SETTINGS_BOT_LZ, BG_GFX_SUB);
    for (int r = 0; r < SET_ROWS; r++)
        draw_row(r);
}

static void settings_free(void)
{
    asset_free(sCtrl);
    asset_free(sDesc);
    sCtrl = sDesc = NULL;
}

static void wait_release(void)
{
    do
    {
        swiWaitForVBlank();
        scanKeys();
    } while (keysHeld() & (KEY_TOUCH | KEY_A | KEY_B | KEY_START));
}

bool settings_run(bool trial)
{
    (void)trial;
    sCtrl = asset_load(PAK_SETTINGS_CTRL, NULL);
    sDesc = asset_load(PAK_SETTINGS_DESC, NULL);
    setup_video();
    int cursor = ROW_SETTING;
    draw_desc(cursor);

    const rect_t *pressedBtn = NULL;
    touchPosition t = { 0 };
    while (1)
    {
        swiWaitForVBlank();
        scanKeys();
        u32 down = keysDown(), held = keysHeld();

        if (down & (KEY_UP | KEY_DOWN))
        {
            cursor = (cursor + (down & KEY_UP ? SET_ROWS - 1 : 1)) % SET_ROWS;
            draw_desc(cursor);
            sfx_play(SFX_UI_CURSOR);
        }
        if (down & (KEY_LEFT | KEY_RIGHT))
        {
            if (change(cursor, down & KEY_LEFT ? -1 : 1))
                sfx_play(SFX_UI_CURSOR);
        }
        if (down & (KEY_A | KEY_START))
        {
            sfx_play(SFX_UI_OK);
            highlight(&sPlayBtn, BLDY_PRESS);
            wait_release();
            settings_free();
            return true;
        }
        if (down & KEY_B)
        {
            sfx_play(SFX_UI_BACK);
            highlight(&sTitleBtn, BLDY_PRESS);
            wait_release();
            settings_free();
            return false;
        }

        if (held & KEY_TOUCH)
        {
            touchRead(&t);
            if (down & KEY_TOUCH)
            {
                pressedBtn = inside(&sPlayBtn, &t) ? &sPlayBtn : inside(&sTitleBtn, &t) ? &sTitleBtn : NULL;
                if (pressedBtn)
                    sfx_play(SFX_UI_CURSOR);
                for (int r = 0; r < SET_ROWS && !pressedBtn; r++)
                {
                    int y0 = SET_ROW_Y0 + r * SET_ROW_H;
                    if (t.py < y0 || t.py >= y0 + SET_ROW_H)
                        continue;
                    if (r != cursor)
                    {
                        cursor = r;
                        draw_desc(cursor);
                    }
                    if (inside(&sHits[r][0], &t) ? change(r, -1) : inside(&sHits[r][1], &t) ? change(r, 1) : false)
                        sfx_play(SFX_UI_CURSOR);
                }
            }
        }
        else if (pressedBtn)
        {
            // released over the same button: activate it
            if (inside(pressedBtn, &t))
            {
                bool play = pressedBtn == &sPlayBtn;
                sfx_play(play ? SFX_UI_OK : SFX_UI_BACK);
                wait_release();
                settings_free();
                return play;
            }
            pressedBtn = NULL;
        }

        if (pressedBtn && inside(pressedBtn, &t))
            highlight(pressedBtn, BLDY_PRESS);
        else
        {
            rect_t row = { 0, SET_ROW_Y0 + cursor * SET_ROW_H, 255, SET_ROW_H };
            highlight(&row, BLDY_CURSOR);
        }
    }
}
