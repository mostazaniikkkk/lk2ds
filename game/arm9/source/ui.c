#include <nds.h>
#include "check.h"
#include "ui.h"
#include "layout.h"

#include "seg_tiles_bin.h"
#include "seg_pal_bin.h"
#include "stop_tiles_bin.h"
#include "stop_pal_bin.h"
#include "bet_tiles_bin.h"
#include "bet_pal_bin.h"
#include "push_tiles_bin.h"
#include "push_pal_bin.h"
#include "lever_tiles_bin.h"
#include "lever_pal_bin.h"
#include "lamp_tiles_bin.h"
#include "lamp_pal_bin.h"
#include "cnt_label_tiles_bin.h"
#include "cnt_label_pal_bin.h"
#include "slider_keys_tiles_bin.h"
#include "slider_keys_pal_bin.h"
#include "slider_chance_tiles_bin.h"
#include "slider_chance_pal_bin.h"

#define SEG_BLANK 10

// sub sprite palettes (the panel has one palette per frame: 6 key frames, then 4 CHANCE frames)
enum { PAL_PAY, PAL_STOP, PAL_LAMP, PAL_BET, PAL_PUSH, PAL_LEVER, PAL_KEYS, PAL_CHANCE = PAL_KEYS + 6 };
// main sprite palettes
enum { PAL_MAIN_SEG, PAL_MAIN_LABEL };

// sub OAM slots (lower index is drawn on top)
#define OAM_PANEL  0   // 6: per half a 64x32 + two 32x16
#define OAM_STOP   6   // 3
#define OAM_BET    9
#define OAM_PUSH   10
#define OAM_LEVER  11
#define OAM_PAY    12  // 2
#define OAM_LAMP   14  // 4
// main OAM slots
#define OAM_CNT_DIGIT 0   // 4 fields x 5 digits
#define OAM_CNT_LABEL 20  // 8 strips of 32x16

// slide-in panel frames (tools/make_slider.py): 1024 + 256 + 256 bytes each
#define PANEL_FRAME_BYTES 1536
enum { KEYS_CLOSED, KEYS_OPEN, KEYS_UP, KEYS_RIGHT, KEYS_DOWN, KEYS_LEFT };
enum { CHANCE_NORMAL, CHANCE_PUSH, CHANCE_LAMP, CHANCE_PUSH_LAMP };

#define AFFINE_PUSHED 0
#define PUSHED_INV_SCALE 269   // 1 / 0.95 in 8.8 (ObjectButtonSprite.PushedScale)
#define LEVER_FRAMES 6         // ObjectButtonLever.TimeRemain = 0.1 s at 60 Hz

static u16 *sSegMain[11], *sPay[11], *sStop[2], *sBet[4], *sPush[4], *sLever, *sLamp;
static const u8 sLampPos[4][2] = LAMP_POS;
static const u8 sStopPos[3][2] = { { STOP1_SPR_X, STOP1_SPR_Y }, { STOP2_SPR_X, STOP2_SPR_Y },
                                   { STOP3_SPR_X, STOP3_SPR_Y } };
static int sLeverT; // 0 = up .. LEVER_FRAMES = down
static const u8 sCntFields[CNT_PAGES][4][2] = CNT_FIELDS;
static u16 *sLabel[CNT_PAGES * 8];
// panel: VRAM slots [half][part] and the frame each slot holds / should hold
static u16 *sPanelGfx[2][3];
static int sPanelFrame[2] = { -1, -1 }, sPanelWant[2];
static int sPanelX = SLIDER_X_CLOSED;

static void load_frames(u16 **dst, int n, const u8 *src, SpriteSize size, int bytes)
{
    for (int i = 0; i < n; i++)
    {
        dst[i] = oamAllocateGfx(&oamSub, size, SpriteColorFormat_16Color);
        dmaCopy(src + i * bytes, dst[i], bytes);
    }
}

void ui_init(void)
{
    vramSetBankE(VRAM_E_MAIN_SPRITE);
    oamInit(&oamMain, SpriteMapping_1D_32, false);
    oamInit(&oamSub, SpriteMapping_1D_32, false);

    for (int n = 0; n < 11; n++)
    {
        sSegMain[n] = oamAllocateGfx(&oamMain, SpriteSize_8x16, SpriteColorFormat_16Color);
        dmaCopy(seg_tiles_bin + n * 64, sSegMain[n], 64);
    }
    load_frames(sPay, 11, seg_tiles_bin, SpriteSize_8x16, 64);
    load_frames(sStop, 2, stop_tiles_bin, SpriteSize_32x32, 512);
    load_frames(sBet, 4, bet_tiles_bin, SpriteSize_64x32, 1024);
    load_frames(sPush, 4, push_tiles_bin, SpriteSize_64x32, 1024);
    load_frames(&sLever, 1, lever_tiles_bin, SpriteSize_32x32, 512);
    load_frames(&sLamp, 1, lamp_tiles_bin, SpriteSize_8x8, 32);
    for (int i = 0; i < CNT_PAGES * 8; i++)
    {
        sLabel[i] = oamAllocateGfx(&oamMain, SpriteSize_32x16, SpriteColorFormat_16Color);
        dmaCopy(cnt_label_tiles_bin + i * 256, sLabel[i], 256);
    }
    for (int h = 0; h < 2; h++)
    {
        sPanelGfx[h][0] = oamAllocateGfx(&oamSub, SpriteSize_64x32, SpriteColorFormat_16Color);
        sPanelGfx[h][1] = oamAllocateGfx(&oamSub, SpriteSize_32x16, SpriteColorFormat_16Color);
        sPanelGfx[h][2] = oamAllocateGfx(&oamSub, SpriteSize_32x16, SpriteColorFormat_16Color);
        sPanelFrame[h] = -1;
    }
    sPanelWant[0] = KEYS_CLOSED;
    sPanelWant[1] = CHANCE_NORMAL;
    sPanelX = SLIDER_X_CLOSED;

    dmaCopy(seg_pal_bin, &SPRITE_PALETTE[PAL_MAIN_SEG * 16], 32);
    dmaCopy(cnt_label_pal_bin, &SPRITE_PALETTE[PAL_MAIN_LABEL * 16], 32);
    dmaCopy(slider_keys_pal_bin, &SPRITE_PALETTE_SUB[PAL_KEYS * 16], 6 * 32);
    dmaCopy(slider_chance_pal_bin, &SPRITE_PALETTE_SUB[PAL_CHANCE * 16], 4 * 32);
    dmaCopy(seg_pal_bin, &SPRITE_PALETTE_SUB[PAL_PAY * 16], 32);
    dmaCopy(stop_pal_bin, &SPRITE_PALETTE_SUB[PAL_STOP * 16], 32);
    dmaCopy(lamp_pal_bin, &SPRITE_PALETTE_SUB[PAL_LAMP * 16], 32);
    dmaCopy(bet_pal_bin, &SPRITE_PALETTE_SUB[PAL_BET * 16], 32);
    dmaCopy(push_pal_bin, &SPRITE_PALETTE_SUB[PAL_PUSH * 16], 32);
    dmaCopy(lever_pal_bin, &SPRITE_PALETTE_SUB[PAL_LEVER * 16], 32);

    oamClear(&oamMain, 0, 128);
    oamClear(&oamSub, 0, 128);
    oamRotateScale(&oamSub, AFFINE_PUSHED, 0, PUSHED_INV_SCALE, PUSHED_INV_SCALE);
}

// right-aligned number, leading zeros blanked (the last digit is always shown)
static void put_number(OamState *oam, u16 **gfx, int pal, int id, int x, int dx, int y, int ndigits,
                       int value, bool show)
{
    int max = 1;
    for (int i = 0; i < ndigits; i++)
        max *= 10;
    if (value >= max)
        value = max - 1;
    if (value < 0)
        value = 0;
    for (int i = ndigits - 1; i >= 0; i--)
    {
        bool lead = value == 0 && i != ndigits - 1;
        int d = lead ? SEG_BLANK : value % 10;
        oamSet(oam, id + i, x + i * dx, y, 0, pal, SpriteSize_8x16, SpriteColorFormat_16Color, gfx[d], -1,
               false, !show, false, false, false);
        value /= 10;
    }
}

// ObjectButtonSprite: frame = lamp + 2 * pushed (only 2 frames for STOP: pushed reuses them)
static void put_button(int id, int x, int y, SpriteSize size, int pal, u16 **frames, int nframes, bool held,
                       bool lamp)
{
    int f = (lamp ? 1 : 0) + (held && nframes == 4 ? 2 : 0);
    oamSet(&oamSub, id, x, y, 0, pal, size, SpriteColorFormat_16Color, frames[f], held ? AFFINE_PUSHED : -1,
           false, false, false, false, false);
}

void ui_update(const slot_t *s, const ui_buttons_t *b, bool idle, unsigned frame)
{
    // top data counter: the page chosen with the panel's cross key (DataPanel of the app)
    int page = b->page < CNT_PAGES ? b->page : 0;
    int values[4] = { 0 };
    switch (page)
    {
        case UI_PAGE_MAIN:
            values[0] = s->credits;
            values[1] = s->games;
            values[2] = s->bigCount;
            values[3] = s->regCount;
            break;
        case UI_PAGE_MEDALS:
            values[0] = s->sinceBonus;
            values[1] = s->medalsIn;
            values[2] = s->medalsOut;
            break;
        case UI_PAGE_ODDS: // 1/x bonus odds so far, payout percentage
            values[0] = s->bigCount ? s->games / s->bigCount : 0;
            values[1] = s->regCount ? s->games / s->regCount : 0;
            values[2] = s->medalsIn ? (int)((u64)s->medalsOut * 100 / s->medalsIn) : 0;
            break;
        case UI_PAGE_VOLUME:
            values[0] = b->volume;
            break;
        case UI_PAGE_ART:
        case UI_PAGE_ZONE:
        case UI_PAGE_VA:
            for (int i = 0; i < 3; i++)
                values[i] = b->mode[i];
            break;
    }
    for (int f = 0; f < 4; f++)
    {
        int n = sCntFields[page][f][1];
        for (int i = n; i < 5; i++) // unused digit sprites of this field
            oamSetHidden(&oamMain, OAM_CNT_DIGIT + f * 5 + i, true);
        if (n)
            put_number(&oamMain, sSegMain, PAL_MAIN_SEG, OAM_CNT_DIGIT + f * 5, sCntFields[page][f][0], 8, 0, n,
                       values[f], true);
    }
    for (int c = 0; c < 8; c++)
        oamSet(&oamMain, OAM_CNT_LABEL + c, c * 32, 0, 0, PAL_MAIN_LABEL, SpriteSize_32x16, SpriteColorFormat_16Color,
               sLabel[page * 8 + c], -1, false, false, false, false, false);

    // slide-in panel: eases toward its target; frames are streamed into its VRAM slots in the vblank
    int target = b->panelOpen ? SLIDER_X_OPEN : SLIDER_X_CLOSED;
    if (sPanelX != target)
    {
        int step = (target - sPanelX) / 3;
        if (step == 0)
            step = target > sPanelX ? 1 : -1;
        sPanelX += step;
    }
    bool open = sPanelX < SLIDER_X_CLOSED;
    sPanelWant[0] = !open ? KEYS_CLOSED : b->cross == UI_CROSS_NONE ? KEYS_OPEN : KEYS_UP + b->cross;
    sPanelWant[1] = (b->held[UI_BTN_CHANCE] ? CHANCE_PUSH : CHANCE_NORMAL) + (b->lamp[UI_BTN_CHANCE] ? 2 : 0);
    for (int h = 0; h < 2; h++)
    {
        int x = sPanelX + h * SLIDER_KEYS_W;
        // the palette is set from the frame the VRAM slot holds, so tiles and palette always match
        int pal = h ? PAL_CHANCE + sPanelFrame[1] : PAL_KEYS + sPanelFrame[0];
        if (sPanelFrame[h] < 0)
            pal = h ? PAL_CHANCE : PAL_KEYS;
        bool hide = x >= 256;
        oamSet(&oamSub, OAM_PANEL + h * 3, x, SLIDER_Y, 0, pal, SpriteSize_64x32, SpriteColorFormat_16Color,
               sPanelGfx[h][0], -1, false, hide, false, false, false);
        oamSet(&oamSub, OAM_PANEL + h * 3 + 1, x, SLIDER_Y + 32, 0, pal, SpriteSize_32x16,
               SpriteColorFormat_16Color, sPanelGfx[h][1], -1, false, hide, false, false, false);
        oamSet(&oamSub, OAM_PANEL + h * 3 + 2, x + 32, SLIDER_Y + 32, 0, pal, SpriteSize_32x16,
               SpriteColorFormat_16Color, sPanelGfx[h][2], -1, false, hide || x + 32 >= 256, false, false, false);
    }

    // buttons
    for (int i = 0; i < 3; i++)
        put_button(OAM_STOP + i, sStopPos[i][0], sStopPos[i][1], SpriteSize_32x32, PAL_STOP, sStop, 2,
                   b->held[UI_BTN_STOP1 + i], b->lamp[UI_BTN_STOP1 + i]);
    put_button(OAM_BET, BET_SPR_X, BET_SPR_Y, SpriteSize_64x32, PAL_BET, sBet, 4, b->held[UI_BTN_BET],
               b->lamp[UI_BTN_BET]);
    put_button(OAM_PUSH, PUSH_SPR_X, PUSH_SPR_Y, SpriteSize_64x32, PAL_PUSH, sPush, 4, b->held[UI_BTN_CHANCE],
               b->lamp[UI_BTN_CHANCE]);

    // lever: travels down while held, back up when released
    if (b->held[UI_BTN_LEVER])
    {
        if (sLeverT < LEVER_FRAMES)
            sLeverT++;
    }
    else if (sLeverT > 0)
    {
        sLeverT--;
    }
    int lx = LEVER_UP_X + (LEVER_DN_X - LEVER_UP_X) * sLeverT / LEVER_FRAMES;
    int ly = LEVER_UP_Y + (LEVER_DN_Y - LEVER_UP_Y) * sLeverT / LEVER_FRAMES;
    oamSet(&oamSub, OAM_LEVER, lx, ly, 0, PAL_LEVER, SpriteSize_32x32, SpriteColorFormat_16Color, sLever, -1,
           false, false, false, false, false);

    // payout 7-seg (BodyManager.SetSegLedPayOut): bonus payout during a bonus, else the last payout
    int pay = s->bonusActive != BONUS_NONE ? s->bonusPaid : s->lastPay;
    put_number(&oamSub, sPay, PAL_PAY, OAM_PAY, PAY_DIGIT_X, PAY_DIGIT_DX, PAY_DIGIT_Y, 2, pay,
               s->bonusActive != BONUS_NONE || s->lastPay > 0);

    // LEDs: bet / in-medal (bet accepted) / start (lever armed, blinks) / replay
    bool blink = (frame >> 3) & 1;
    bool lit[4] = {
        s->bet == 3,
        idle && s->bet < 3 && s->credits >= 3,
        idle && s->bet == 3 && blink,
        s->replayBet,
    };
    for (int i = 0; i < 4; i++)
        oamSet(&oamSub, OAM_LAMP + i, sLampPos[i][0], sLampPos[i][1], 0, PAL_LAMP, SpriteSize_8x8,
               SpriteColorFormat_16Color, sLamp, -1, false, !lit[i], false, false, false);
}

int ui_panel_x(void)
{
    return sPanelX;
}

// copy a panel frame into its sprite VRAM slots (32-bit CPU writes: DMA3 belongs to the video)
static void panel_stream(int h)
{
    const u32 *src = (const u32 *)((h ? slider_chance_tiles_bin : slider_keys_tiles_bin) +
                                   sPanelWant[h] * PANEL_FRAME_BYTES);
    static const int bytes[3] = { 1024, 256, 256 };
    for (int p = 0; p < 3; p++)
    {
        u32 *dst = (u32 *)sPanelGfx[h][p];
        for (int i = 0; i < bytes[p] / 4; i++)
            dst[i] = *src++;
    }
    sPanelFrame[h] = sPanelWant[h];
}

void ui_vblank(void)
{
    // hang finder: main loop stopped -> last checkpoint in the GAME field
    static u32 lastTick, still;
    if (gMainTick != lastTick)
    {
        lastTick = gMainTick;
        still = 0;
    }
    else if (++still >= CHECK_STUCK_VBLANKS)
    {
        put_number(&oamMain, sSegMain, PAL_MAIN_SEG, OAM_CNT_DIGIT + 5, sCntFields[0][1][0], 8, 0, 4, gCheck, true);
    }
    // new panel frame: tiles and the palette in OAM switch in the same vblank
    for (int h = 0; h < 2; h++)
        if (sPanelFrame[h] != sPanelWant[h])
        {
            panel_stream(h);
            for (int p = 0; p < 3; p++)
                oamSub.oamMemory[OAM_PANEL + h * 3 + p].palette = (h ? PAL_CHANCE : PAL_KEYS) + sPanelFrame[h];
        }
    // not oamUpdate(): it uses DMA3, which the video player keeps busy feeding the GX FIFO
    // (re-programming it mid-transfer cuts the motion-compensation display list)
    DC_FlushRange(oamMain.oamMemory, SPRITE_COUNT * sizeof(SpriteEntry));
    DC_FlushRange(oamSub.oamMemory, SPRITE_COUNT * sizeof(SpriteEntry));
    dmaCopyWords(2, oamMain.oamMemory, OAM, SPRITE_COUNT * sizeof(SpriteEntry));
    dmaCopyWords(2, oamSub.oamMemory, OAM_SUB, SPRITE_COUNT * sizeof(SpriteEntry));
}
