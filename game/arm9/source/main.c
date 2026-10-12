// LK2 DS - proof of concept
// Top screen: FastVideoDS clip under the cabinet header. Bottom screen: cabinet + reels.
// Controls: MAX BET = R / L / UP or touch BET, lever = DOWN or touch the lever,
//           STOP = Y / B / A or touch the stop buttons, CHANCE (PUSH) = X or touch it.

#include <nds.h>
#include "check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <nds/arm9/dldi.h>
#include <string.h>

#include "ipc.h"
#include "dldiproxy.h"
#include "fv/fvPlayer.h"
#include "fv/mpu.h"
#include "reels.h"
#include "slot.h"
#include "ui.h"
#include "sfx.h"
#include "title.h"
#include "settings.h"
#include "bgm.h"
#include "fade.h"
#include "asset.h"
#include "video_ids.h"
#include "date.h"
#include "save.h"
#include "top_header_bin.h"


static DTCM_BSS fv_player_t sPlayer;
volatile u16 gCheck;
volatile u32 gMainTick;

// touch areas (bottom screen pixels), centres taken from the cabinet sprite coordinates
typedef struct
{
    s16 x, y, r;
} hit_t;
static const hit_t sLeverHit = { 25, 158, 16 };
static const hit_t sBetHit = { 55, 132, 12 };
static const hit_t sChanceHit = { 145, 132, 12 };
static const hit_t sStopHit[3] = { { 89, 161, 13 }, { 126, 161, 13 }, { 164, 161, 13 } };

static bool hit(const hit_t *h, const touchPosition *t)
{
    int dx = t->px - h->x, dy = t->py - h->y;
    return dx * dx + dy * dy <= h->r * h->r;
}

// boot progress marker: top screen backdrop colour (debug aid while bringing the port up)
static void stage(u16 color)
{
    videoSetMode(MODE_0_2D);
    BG_PALETTE[0] = color;
}

// "fat:/data/lk2ds/movies/<file>" (same drive as the ROM): FastVideoDS streams each clip from its own file
static const char *video_path(video_id_t id)
{
    static char path[96] ALIGN(32);
    const char *rom = asset_rom_path();
    const char *colon = strchr(rom, ':');
    int n = colon ? colon - rom + 1 : 0;
    if (n == 0 || n > 8)
    {
        strcpy(path, "fat:");
        n = 4;
    }
    else
    {
        memcpy(path, rom, n);
    }
    snprintf(path + n, sizeof(path) - n, "/data/lk2ds/movies/%s", sVideoFile[id]);
    DC_FlushRange(path, sizeof(path));
    return path;
}

static u32 ipc_call(u32 cmd, u32 arg)
{
    fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(cmd, arg));
    fifoWaitValue32(FIFO_USER_01);
    return fifoGetValue32(FIFO_USER_01) & IPC_CMD_ARG_MASK;
}

// ARM9-only DLDI drivers (melonDS): the ARM7's FatFs asks for sectors over FIFO_USER_02
static void dldi_proxy_handler(u32 value, void *userdata)
{
    const DLDI_INTERFACE *io = io_dldi_data;
    dldi_proxy_t *req = (dldi_proxy_t *)value;
    DC_InvalidateRange(req, 32);
    u32 bytes = req->count * 512;
    // reads: drop any cached copy of the target first (the driver may write with the CPU or DMA);
    // writes: the ARM7 filled the buffer in RAM, so stale cached lines must not be used (or flushed over it)
    if (req->write)
        DC_InvalidateRange(req->buffer, bytes);
    else
        DC_FlushRange(req->buffer, bytes);
    bool ok = req->write ? io->ioInterface.writeSectors(req->sector, req->count, req->buffer)
                         : io->ioInterface.readSectors(req->sector, req->count, req->buffer);
    DC_FlushRange(req->buffer, bytes);
    fifoSendValue32(FIFO_USER_02, ok ? 1 : 0);
}

// Give the SD card to the ARM7 (it streams the video while the ARM9 decodes). ARM7-capable drivers
// (DSpico) are relocated into main RAM and run there; ARM9-only drivers are proxied.
static bool setup_dldi(void)
{
    const DLDI_INTERFACE *io = io_dldi_data;
    if (io == NULL || io->magicNumber != 0xBF8DA5ED)
        return false;

    if (!(io->ioInterface.features & FEATURE_ARM7_CAPABLE))
    {
        if (!io->ioInterface.startup())
            return false;
        dldi_proxy_t *proxy = memalign(32, sizeof(dldi_proxy_t));
        if (proxy == NULL)
            return false;
        DC_FlushRange(proxy, sizeof(dldi_proxy_t));
        fifoSetValue32Handler(FIFO_USER_02, dldi_proxy_handler, NULL);
        return ipc_call(IPC_CMD_SETUP_PROXY, (u32)proxy) != 0;
    }

    size_t size = 1u << io->allocatedSize;
    DLDI_INTERFACE *copy = memalign(32, size);
    if (copy == NULL)
        return false;
    memset(copy, 0, size);
    memcpy(copy, io, 1u << io->driverSize);
    dldiRelocate(copy, copy);
    DC_FlushRange(copy, size);
    sysSetCardOwner(false); // slot-1 bus belongs to the ARM7 from now on
    return ipc_call(IPC_CMD_SETUP_DLDI, (u32)copy) != 0;
}

// header + solid colour on the top screen when no video is playing
static void show_static_top(u16 color)
{
    fv_clearFrames(color);
    vramSetBankA(VRAM_A_MAIN_BG);
    videoSetMode(MODE_5_2D | DISPLAY_BG2_ACTIVE | (REG_DISPCNT & 0x80F01070));
    REG_BG2CNT = (u16)BgSize_B16_256x256 | BG_WRAP_ON | BG_PRIORITY_3;
    REG_BG2PA = 256;
    REG_BG2PB = 0;
    REG_BG2PC = 0;
    REG_BG2PD = 256;
    REG_BG2X = 0;
    REG_BG2Y = -(FV_TOP_OFFSET << 8);
}

// ---------------------------------------------------------------- opening

static void vblank_idle(void);
static bool sHaveCard;

static volatile int sOpTicks;
static volatile int sOpBright; // -16..0, applied in vblank

static void opening_vblank(void)
{
    sOpTicks++;
    setBrightness(3, sOpBright);
}

// boot opening (the BIG clip over "Resonation Wings", Sn_Dd01_0001): top screen, centred, skippable
// a clip from data/lk2ds/movies on the top screen, centred and skippable (opening, gallery movies);
// the bottom screen shows "PRESS START TO SKIP". Leaves both screens black.
void play_movie(int vid)
{
    if (!sHaveCard)
        return;
    setBrightness(3, -16);
    DMA_CR(3) = 0;
    DC_FlushAll();
    vramSetBankA(VRAM_A_LCD);
    vramSetBankB(VRAM_B_LCD);
    vramSetBankD(VRAM_D_LCD);
    // black bands around the centred 144-line picture: opaque black (bit 15), a 0 pixel would be
    // transparent and show the backdrop colour
    u16 *black = malloc(FV_TOP_OFFSET * 256 * sizeof(u16));
    if (black)
    {
        for (int i = 0; i < FV_TOP_OFFSET * 256; i++)
            black[i] = RGB15(0, 0, 0) | BIT(15);
        DC_FlushRange(black, FV_TOP_OFFSET * 256 * sizeof(u16));
        fv_setHeader(black);
        free(black);
    }
    BG_PALETTE[0] = 0;
    fv_clearFrames(RGB15(0, 0, 0) | BIT(15));
    // bottom screen: "PRESS START TO SKIP" (16-bit bitmap in VRAM C, free while the video uses A/B/D)
    BG_PALETTE_SUB[0] = 0;
    REG_BLDCNT_SUB = 0; // the menu's highlight (fade to white) may still be set
    vramSetBankC(VRAM_C_SUB_BG);
    bool prompt = asset_show_lz(PAK_OPENING_BOT_LZ, BG_GFX_SUB);
    videoSetModeSub(prompt ? MODE_5_2D | DISPLAY_BG3_ACTIVE : MODE_0_2D);
    REG_BG3CNT_SUB = (u16)BgSize_B16_256x256 | BG_PRIORITY_0;
    REG_BG3PA_SUB = 256;
    REG_BG3PB_SUB = 0;
    REG_BG3PC_SUB = 0;
    REG_BG3PD_SUB = 256;
    REG_BG3X_SUB = 0;
    REG_BG3Y_SUB = 0;

    ipc_call(IPC_CMD_VIDEO_BASE, 0);
    if (!fv_initPlayer(&sPlayer, video_path(vid), false))
        return;
    sOpBright = -16;
    sOpTicks = 0;
    fv_vblankHook = opening_vblank;
    fv_startPlayer(&sPlayer);
    REG_BG2Y = -(((192 - 144) / 2) << 8); // centred instead of below the game header
    int fadeOutAt = -1;
    while (!fv_isFinished(&sPlayer))
    {
        fv_updatePlayer(&sPlayer);
        int t = sOpTicks;
        // blinks like TAP TO START: shown 22 of every 32 frames
        if (prompt)
            videoSetModeSub((t & 31) < 22 ? MODE_5_2D | DISPLAY_BG3_ACTIVE : MODE_5_2D);
        scanKeys();
        if (fadeOutAt < 0 && t > 30 && (keysDown() & (KEY_TOUCH | KEY_A | KEY_START)))
            fadeOutAt = t;
        if (fadeOutAt >= 0)
        {
            sOpBright = -((t - fadeOutAt) * 16 / 20);
            if (t - fadeOutAt >= 20)
                break;
        }
        else
        {
            sOpBright = t < 20 ? -16 + t * 16 / 20 : 0;
        }
    }
    sOpBright = -16;
    swiWaitForVBlank();
    while (REG_DISPCAPCNT & DCAP_ENABLE)
        ;
    fv_destroyPlayer(&sPlayer);
    fv_vblankHook = NULL;
    setBrightness(3, -16);
    irqSet(IRQ_VBLANK, vblank_idle);
    // let go of the skip input before the title screen sees it
    do
    {
        swiWaitForVBlank();
        scanKeys();
    } while (keysHeld() & (KEY_TOUCH | KEY_A | KEY_START));
}

static void play_opening(void)
{
    play_movie(VID_OPENING);
}

// ---------------------------------------------------------------- game

typedef enum
{
    GAME_IDLE,
    GAME_WAIT,  // lever accepted, waiting for the 4.1 s game interval (WAIT CUT off)
    GAME_SPIN,
} game_state_t;

#define WAIT_TICKS 246  // 4.1 s at 60 Hz, measured from the previous reel start

static game_state_t sGame = GAME_IDLE;
static slot_t sSlot;
static u32 sFrame;
static bool sTrial;     // TRIAL MODE: credits are refilled after every game
static bool sQuit;      // back to the menu
static volatile u32 sTicks;  // vblanks
static u32 sLastStart;
static bool sStartedOnce;
// date sim (BIG "date notice"): requested when a BIG starts, runs with the video stopped
static bool sDateRequest, sDating;
// Date Mode (super high chance stage with Ichigo): entered on some CHERRY wins, lasts DM_GAMES games;
// extra bonus draw per game (out of 1024) grows with her affection; a bonus won stays hidden until the end
static bool sDmRequest, sDateMode;
// Continuous effects of normal play (連続演出, tools/make_effects.py): the chibi race for vacation days and three
// battles (ECM: Ai vs Fold, Rion vs Koon, Tsugumi vs Short). One clip per game, started by the lever; between
// games the last frame stays on screen. A bonus drawn in normal play may be hidden behind one (the heroine wins,
// sometimes after a comeback); they also run with no bonus, and those are always lost. A "hot" start (race on
// fire, red gauges / angry close-ups) is more likely when there is a bonus.
#define FX_ODDS_BONUS 2    // 1 in 2 bonuses start an effect
#define FX_ODDS_FAKE  100  // 1 in 100 games without a bonus
#define FX_HOT_WIN    40   // % hot starts when won / lost
#define FX_HOT_LOSE   10
#define FX_COMEBACK   25   // % of battles won by a comeback after losing
enum { FX_RACE, FX_ECM, FX_RION, FX_TSUG, FX_COUNT };
// battle clips in pack order: _1 _1_hot _2 _2_hot _3_win _3_lose _3_back _3_hot_win _3_hot_lose _3_hot_back
static const video_id_t sBattle[3] = { VID_FX_ECM_1, VID_FX_RION_1, VID_FX_TSUG_1 };
static const bgm_id_t sFxBgm[FX_COUNT] = { BGM_RACE, BGM_ECM, BGM_RION, BGM_TSUG };
static video_id_t sFx[4];
static int sFxLen, sFxStep = -1; // -1: no effect running
static int sFxKind;
// Klein Stage (Sn_Aa06, the play guide's high-chance stage entered on some WATERMELON wins): Maia pushes
// Ichigo off the normal screen and takes it over for KLEIN_GAMES games, with an extra bonus draw kept hidden
// until the end; then Ichigo comes back, with a WIN if a bonus was drawn. Clips per idle background.
#define FX_KLEIN        FX_COUNT       // effect kinds past FX_COUNT have no BGM
#define KLEIN_ODDS      3              // 1 in 3 WATERMELON wins try a takeover
#define KLEIN_TAKE      50             // % of tries that succeed (85 with a bonus already drawn)
#define KLEIN_TAKE_BONUS 85
#define KLEIN_GAMES     8
#define KLEIN_BOOST     40             // extra bonus draw per game, out of 1024 (~1/26)
#define IDLE_BGS        7
static int sIdleBg;                    // background of the idle clip on screen (VID_IDLE_0 + n)
static bool sKlein, sKleinEntering;    // stage running / takeover clip queued
static bool sKleinBack;               // stage over, way-back clip queued
static int sKleinGames;

// ---- Absolute Airspace (ART), Weapon Attack zones, BIG notices, VA Ignition (play guide; tools/make_effects.py)
typedef enum
{
    MODE_NONE,
    MODE_NOTICE,     // a BIG started: pick the notice type (chance / full / date)
    MODE_BONUS_END,  // a bonus ended: chance-notice reveal, then a Weapon Attack zone and the ART if stocked
    MODE_BATTLE,     // Dimension Battle (ART high chance), won -> an add zone
    MODE_ART_END,    // ART over: total
    MODE_VA_IN,      // VA Ignition starts
    MODE_VA_END,     // VA over -> ART
} art_mode_t;
enum { NOTICE_NONE, NOTICE_CHANCE, NOTICE_FULL, NOTICE_DATE };
#define STOCK_CHANCE    40   // % a chance-notice BIG stocks the ART (+STOCK_HOT after a hinted bonus)
#define STOCK_FULL      50   // % the golden shutter closes
#define STOCK_REG       20
#define STOCK_HOT       30   // bonus revealed by a won effect, Klein Stage or Date Mode
#define ART_HIGH_GAMES  10   // high chance after a CHERRY / WATERMELON
#define BATTLE_ODDS     5    // 1 in 5 high-chance games start a Dimension Battle
#define VA_SINCE        400  // games without a bonus before VA Ignition can start
#define VA_ODDS         25   // then 1 in 25 games
#define VA_ART_GAMES    30
static art_mode_t sMode;
static int sNotice;
static bool sNoticeStock, sBonusHot;
static int sArtStock;
static bool sArt, sVa;
static int sArtGames, sArtTotal, sArtAdd, sArtHigh, sArtHighGames;
static int sVaTarget, sVaLeft;
static int sZoneG, sZoneHits;
static bool sFxBgmPending; // the effect's BGM is loaded between two clips: the pack reads share the video's FIFO
static video_id_t sClipId;
static bool sClipRequest;            // play sClipId now (breaks the idle loop)
static int sDmGames;
#define DM_ENTRY_ODDS 3   // 1 in 3 CHERRY wins
#define DM_GAMES      10
#define DM_BOOST      48  // ~1/21 per game
#define DM_BOOST_STEP 16  // per affection point
// slide-in panel (PopupWindowManager) and the top counter page it selects
static bool sPanelOpen, sPanelAuto; // sPanelAuto: opened by the game (RequestWindows), not by the player
static bool sPrevChanceLamp;
static u8 sPage;                     // UI_PAGE_MAIN .. UI_PAGE_ODDS
static int sVolumeShow;             // frames left showing UI_PAGE_VOLUME

#define VOLUME_SHOW_FRAMES 90
#define CROSS_R 14                   // cross key touch radius (pixels)

// plan the clips of an effect won or lost; the first one plays at the next lever
static void fx_begin(bool win)
{
    bool hot = rand() % 100 < (win ? FX_HOT_WIN : FX_HOT_LOSE);
    sFxKind = rand() % FX_COUNT;
    sFxLen = 0;
    if (sFxKind == FX_RACE)
    {
        bool a = rand() & 1; // rival: brown (a) or pink-haired girl
        sFx[sFxLen++] = VID_FX_RACE_INTRO;
        sFx[sFxLen++] = hot ? VID_FX_RACE_START_HOT : VID_FX_RACE_START;
        if (!win && rand() % 3 == 0)
        {
            sFx[sFxLen++] = VID_FX_RACE_NET_LOSE;
        }
        else
        {
            sFx[sFxLen++] = a ? VID_FX_RACE_RIVAL_A : VID_FX_RACE_RIVAL_B;
            sFx[sFxLen++] = a ? (win ? VID_FX_RACE_A_WIN : VID_FX_RACE_A_LOSE)
                              : (win ? VID_FX_RACE_B_WIN : VID_FX_RACE_B_LOSE);
        }
    }
    else
    {
        video_id_t c = sBattle[sFxKind - FX_ECM];
        int end = !win ? 1 : rand() % 100 < FX_COMEBACK ? 2 : 0; // win, lose, comeback
        sFx[sFxLen++] = c + (hot ? 1 : 0);
        sFx[sFxLen++] = c + (hot ? 3 : 2);
        sFx[sFxLen++] = c + 4 + (hot ? 3 : 0) + end;
    }
    sFxStep = 0;
    sSlot.holdBonus = win; // revealed by the winning clip
}

static void fx_end(void)
{
    sFxStep = -1;
    if (sSlot.holdBonus && sSlot.bonusFlag != BONUS_NONE)
        sBonusHot = true;
    if (sFxKind == FX_KLEIN)
    {
        // takeover done: the stage starts (the bonus, if any, stays hidden); back from the stage: all over
        sKlein = sKleinEntering;
        sKleinEntering = false;
        sKleinGames = 0;
        sSlot.holdBonus = sKlein;
        sSlot.boost = sKlein ? KLEIN_BOOST : 0;
        return;
    }
    sSlot.holdBonus = false;
    bgm_stop();
}

// one Klein clip at the next lever: takeover try (success or not) or the way back
static void klein_clip(video_id_t first, bool entering)
{
    sFxKind = FX_KLEIN;
    sFx[0] = first + sIdleBg;
    sFxLen = 1;
    sFxStep = 0;
    sKleinEntering = entering;
    sSlot.holdBonus = true;
}

static void start_game(void)
{
    CHECK(31);
    if (sFxStep >= 0 && sFxStep < sFxLen)
    {
        if (sFxStep == 0 && sFxKind < FX_COUNT)
            sFxBgmPending = true;
        sClipId = sFx[sFxStep++];
        sClipRequest = true;
    }
    CHECK(32);
    slot_start(&sSlot, (rand() ^ sFrame ^ REG_VCOUNT) & 0xFFFF);
    CHECK(33);
    reels_start();
    sfx_play(SFX_LEVER);
    sLastStart = sTicks;
    sStartedOnce = true;
    sGame = GAME_SPIN;
}

static void stop_reel(int i)
{
    int base = reels_stop_base(i);
    if (gSettings.autoAim && slot_bonus_aimable(&sSlot))
        base = slot_best_base(&sSlot, i);
    reels_stop(i, slot_choose_slip(&sSlot, i, base));
    sfx_play(slot_seven_reach(&sSlot) ? SFX_TENPAI : SFX_STOP);
}

static void play_result_sfx(const slot_t *s)
{
    if (s->bonusStarted)
        sfx_play(s->bonusActive == BONUS_BIG ? SFX_BIG_START : SFX_REG_START);
    else if (s->bonusEnded)
        sfx_play(SFX_BONUS_END);
    switch (s->lastWin)
    {
        case WIN_REPLAY: sfx_play(SFX_PAY_REPLAY); break;
        case WIN_BELL: sfx_play(SFX_PAY_BELL); break;
        case WIN_SUIKA: sfx_play(SFX_PAY_SUIKA); break;
        case WIN_CHERRY: sfx_play(SFX_PAY_CHERRY); break;
        default: break;
    }
}

static void game_update(void)
{
    gMainTick++;
    CHECK(10);
    sFrame++;
    scanKeys();
    u32 down = keysDown();
    u32 held = keysHeld();
    touchPosition touch = { 0 };
    bool touching = (held & KEY_TOUCH) != 0;
    bool touchNew = (down & KEY_TOUCH) != 0;
    if (touching)
        touchRead(&touch);

    // a touch on the slide-in panel belongs to it, not to the cabinet buttons below
    int px = ui_panel_x();
    bool onPanel = touching && touch.py >= SLIDER_Y && touch.px >= px;
    bool cab = touching && !onPanel;
    bool cabNew = touchNew && !onPanel;
    ui_buttons_t ui = { 0 };
    ui.cross = UI_CROSS_NONE;
    bool chanceTouch = false;
    if (onPanel)
    {
        int x = touch.px - px, y = touch.py - SLIDER_Y;
        if (x < SLIDER_TAB_W)
        {
            if (touchNew)
            {
                sPanelOpen = !sPanelOpen;
                sPanelAuto = false;
                sfx_play(sPanelOpen ? SFX_UI_OK : SFX_UI_BACK);
            }
        }
        else if (sPanelOpen && x >= SLIDER_KEYS_W)
        {
            chanceTouch = true;
        }
        else if (sPanelOpen)
        {
            int dx = x - SLIDER_CROSS_DX, dy = y - SLIDER_CROSS_DY;
            if (dx * dx + dy * dy <= CROSS_R * CROSS_R && dx * dx + dy * dy > 9)
            {
                if (abs(dx) > abs(dy))
                    ui.cross = dx > 0 ? UI_CROSS_RIGHT : UI_CROSS_LEFT;
                else
                    ui.cross = dy > 0 ? UI_CROSS_DOWN : UI_CROSS_UP;
            }
        }
    }
    // the D-pad's up/down are BET / lever: only left/right double as the cross key
    if (held & KEY_LEFT)
        ui.cross = UI_CROSS_LEFT;
    else if (held & KEY_RIGHT)
        ui.cross = UI_CROSS_RIGHT;
    static int sPrevCross = UI_CROSS_NONE;
    if (ui.cross != UI_CROSS_NONE && ui.cross != sPrevCross)
    {
        // left/right: data page (DataPanel); up/down: volume, shown on the counter for a moment
        if (ui.cross == UI_CROSS_LEFT || ui.cross == UI_CROSS_RIGHT)
        {
            sPage = (sPage + (ui.cross == UI_CROSS_RIGHT ? 1 : UI_PAGE_ODDS)) % (UI_PAGE_ODDS + 1);
            sVolumeShow = 0;
        }
        else
        {
            int v = gSettings.volume + (ui.cross == UI_CROSS_UP ? 1 : -1);
            gSettings.volume = v < 0 ? 0 : v > 10 ? 10 : v;
            sfx_set_volume(gSettings.volume);
            sVolumeShow = VOLUME_SHOW_FRAMES;
        }
        sfx_play(SFX_UI_CURSOR);
    }
    sPrevCross = ui.cross;
    if (sVolumeShow > 0)
        sVolumeShow--;

    static const u32 stopKeys[3] = { KEY_Y, KEY_B, KEY_A };
    for (int i = 0; i < 3; i++)
        ui.held[UI_BTN_STOP1 + i] = (held & stopKeys[i]) || (cab && hit(&sStopHit[i], &touch));
    ui.held[UI_BTN_BET] = (held & (KEY_R | KEY_L | KEY_UP)) || (cab && hit(&sBetHit, &touch));
    ui.held[UI_BTN_CHANCE] = (held & KEY_X) || (cab && hit(&sChanceHit, &touch)) || chanceTouch;
    ui.held[UI_BTN_LEVER] = (held & KEY_DOWN) || (cab && hit(&sLeverHit, &touch));

    // "pressed this frame": a new key press or a new touch inside the button
    bool betPress = (down & (KEY_R | KEY_L | KEY_UP)) || (cabNew && hit(&sBetHit, &touch));
    bool leverPress = (down & KEY_DOWN) || (cabNew && hit(&sLeverHit, &touch));
    if ((down & KEY_X) || (cabNew && hit(&sChanceHit, &touch)) || (touchNew && chanceTouch))
        sfx_play(SFX_CHANCE);

    switch (sGame)
    {
        case GAME_IDLE:
            if (down & KEY_START)
                sQuit = true;
            if (betPress && slot_bet(&sSlot))
                sfx_play(SFX_BET);
            if (leverPress)
            {
                if (!slot_can_start(&sSlot) && slot_bet(&sSlot)) // lever without bet: auto MAX BET
                    sfx_play(SFX_BET);
                if (slot_can_start(&sSlot))
                {
                    if (!gSettings.waitCut && sStartedOnce && sTicks - sLastStart < WAIT_TICKS)
                    {
                        sfx_play(SFX_WAIT);
                        sGame = GAME_WAIT;
                    }
                    else
                    {
                        start_game();
                    }
                }
            }
            break;

        case GAME_WAIT:
            if (sTicks - sLastStart >= WAIT_TICKS)
            {
                sfx_stop(SFX_WAIT);
                start_game();
            }
            break;

        case GAME_SPIN:
        {
            if (gSettings.autoAim)
            {
                // single-button play: any STOP stops the next reel in the chosen STOP ORDER
                bool press = (down & (KEY_Y | KEY_B | KEY_A)) != 0;
                for (int i = 0; i < 3; i++)
                    press = press || (cabNew && hit(&sStopHit[i], &touch));
                for (int k = 0; k < 3 && press; k++)
                {
                    int i = gStopOrders[gSettings.order][k];
                    if (reels_state(i) == REEL_SPINNING)
                    {
                        stop_reel(i);
                        break;
                    }
                }
            }
            else
            {
                for (int i = 0; i < 3; i++)
                {
                    if (reels_state(i) != REEL_SPINNING)
                        continue;
                    if ((down & stopKeys[i]) || (cabNew && hit(&sStopHit[i], &touch)))
                        stop_reel(i);
                }
            }
            if (reels_all_stopped())
            {
                CHECK(19);
                slot_finish(&sSlot);
                CHECK(20);
                play_result_sfx(&sSlot);
                if (sTrial && sSlot.credits < 1000)
                    sSlot.credits = 1000;
                sGame = GAME_IDLE;
                if (sKlein)
                {
                    if (++sKleinGames >= KLEIN_GAMES)
                    {
                        sKlein = false; // the Klein screen stays until the way back plays at the next lever
                        klein_clip(sSlot.bonusFlag != BONUS_NONE ? VID_FX_KLEIN_WIN_0 : VID_FX_KLEIN_OUT_0, false);
                        sKleinBack = true;
                    }
                }
                else if (sDateMode)
                {
                    int boost = DM_BOOST + DM_BOOST_STEP * date_score();
                    sSlot.boost = boost < DM_BOOST_STEP ? DM_BOOST_STEP : boost;
                    date_set_hot(sSlot.bonusFlag != BONUS_NONE); // sunset scenes: a bonus is waiting
                    if (++sDmGames >= DM_GAMES)
                    {
                        bool won = sSlot.bonusFlag != BONUS_NONE;
                        sBonusHot = sBonusHot || won;
                        sSlot.boost = 0;
                        sSlot.holdBonus = false; // the bonus can line up from now on
                        sDateMode = sDating = false;
                        date_finale_mode(won);
                    }
                    else
                    {
                        CHECK(23);
                        date_advance();
                    }
                }
                else if (sDating && sSlot.bonusEnded)
                {
                    // the confession comes after the last bonus game: she stocks the Absolute Airspace
                    sDating = false;
                    if (date_finale())
                    {
                        sArtStock++;
                        gSave.confessions++;
                    }
                    sMode = MODE_BONUS_END;
                }
                else if (sDating)
                {
                    CHECK(24);
                    date_advance();
                }
                else if (sSlot.bonusStarted && sSlot.bonusActive == BONUS_BIG)
                {
                    sMode = MODE_NOTICE; // leave the video loop: notice type
                }
                else if (sSlot.bonusStarted)
                {
                    if (rand() % 100 < STOCK_REG + (sBonusHot ? STOCK_HOT : 0))
                        sArtStock++;
                }
                else if (sSlot.bonusEnded)
                {
                    sMode = MODE_BONUS_END;
                }
                else if (sSlot.bonusActive != BONUS_NONE)
                {
                }
                else if (sArt)
                {
                    sArtTotal += sSlot.lastPay;
                    if (sArtHigh)
                    {
                        if (--sArtHighGames <= 0)
                            sArtHigh = 0;
                        else if (rand() % BATTLE_ODDS == 0)
                            sMode = MODE_BATTLE;
                    }
                    else if (sSlot.lastWin == WIN_CHERRY || sSlot.lastWin == WIN_SUIKA)
                    {
                        sArtHigh = sSlot.lastWin == WIN_SUIKA && rand() % 3 == 0 ? 2 : 1;
                        sArtHighGames = ART_HIGH_GAMES;
                    }
                    if (--sArtGames <= 0 && sMode == MODE_NONE)
                        sMode = MODE_ART_END;
                }
                else if (sVa)
                {
                    sVaLeft -= sSlot.lastPay;
                    if (sVaLeft <= 0)
                    {
                        sVaLeft = 0;
                        sMode = MODE_VA_END;
                    }
                }
                else if (sFxStep < 0 && !sKleinBack && !sKlein && sSlot.bonusFlag == BONUS_NONE &&
                         sSlot.sinceBonus >= VA_SINCE && rand() % VA_ODDS == 0)
                {
                    sMode = MODE_VA_IN;
                }
                else if (sFxStep < 0 && !sKleinBack && sSlot.bonusActive == BONUS_NONE && sSlot.newBonus &&
                         rand() % FX_ODDS_BONUS == 0)
                {
                    CHECK(21);
                    fx_begin(true);
                }
                else if (sFxStep < 0 && !sKleinBack && sSlot.bonusActive == BONUS_NONE && sSlot.bonusFlag == BONUS_NONE &&
                         rand() % FX_ODDS_FAKE == 0)
                {
                    CHECK(22);
                    fx_begin(false);
                }
                else if (sFxStep < 0 && sSlot.bonusActive == BONUS_NONE && sSlot.lastWin == WIN_SUIKA &&
                         rand() % KLEIN_ODDS == 0)
                {
                    bool take = rand() % 100 < (sSlot.bonusFlag != BONUS_NONE ? KLEIN_TAKE_BONUS : KLEIN_TAKE);
                    klein_clip(take ? VID_FX_KLEIN_IN_0 : VID_FX_KLEIN_TRY_0, take);
                }
                else if (sFxStep < 0 && sSlot.bonusActive == BONUS_NONE && sSlot.lastWin == WIN_CHERRY &&
                         rand() % DM_ENTRY_ODDS == 0)
                {
                    sDmRequest = true; // leave the video loop: Date Mode
                }
            }
            break;
        }
    }

    // lamps
    bool idle = sGame == GAME_IDLE;
    for (int i = 0; i < 3; i++)
        ui.lamp[UI_BTN_STOP1 + i] = reels_state(i) == REEL_SPINNING;
    ui.lamp[UI_BTN_BET] = idle && sSlot.bet < 3 && sSlot.credits >= 3;
    // demake: the chance button lights up while a bonus is carried over (reveal)
    ui.lamp[UI_BTN_CHANCE] = idle && sSlot.bonusFlag != BONUS_NONE && !sSlot.holdBonus; // not during a date
    // RequestWindows: the panel comes out by itself when its CHANCE lamp lights, and goes back if the
    // game opened it
    if (ui.lamp[UI_BTN_CHANCE] && !sPrevChanceLamp && !sPanelOpen)
        sPanelOpen = sPanelAuto = true;
    else if (!ui.lamp[UI_BTN_CHANCE] && sPrevChanceLamp && sPanelAuto)
        sPanelOpen = sPanelAuto = false;
    sPrevChanceLamp = ui.lamp[UI_BTN_CHANCE];
    ui.panelOpen = sPanelOpen;
    ui.page = sVolumeShow > 0 ? UI_PAGE_VOLUME : sArt ? UI_PAGE_ART : sVa ? UI_PAGE_VA : sPage;
    ui.volume = gSettings.volume;
    ui.mode[0] = sArt ? sArtGames : sVaTarget;
    ui.mode[1] = sArt ? sArtAdd : sVaLeft;
    ui.mode[2] = sArtTotal;
    CHECK(29);
    ui_update(&sSlot, &ui, idle, sFrame);
    CHECK(30);
}

static void vblank_idle(void)
{
}

// ---------------------------------------------------------------- ART / zones / notices / VA

static void vblank(void);

// counter page while a blocking sequence runs (the reels are stopped)
static void ui_mode(int page, int a, int b, int c)
{
    ui_buttons_t ui = { 0 };
    ui.cross = UI_CROSS_NONE;
    ui.page = page;
    ui.volume = gSettings.volume;
    ui.mode[0] = a;
    ui.mode[1] = b;
    ui.mode[2] = c;
    ui_update(&sSlot, &ui, true, sFrame);
}

// one clip, blocking; tick(frame) runs every video frame (input of the zones). Its last frame stays.
static void play_clip(video_id_t id, void (*tick)(int))
{
    if (!sHaveCard)
        return;
    DMA_CR(3) = 0;
    ipc_call(IPC_CMD_VIDEO_BASE, 0);
    if (!fv_initPlayer(&sPlayer, video_path(id), false))
        return;
    fv_startPlayer(&sPlayer);
    u32 t0 = sTicks;
    while (!fv_isFinished(&sPlayer))
    {
        fv_updatePlayer(&sPlayer);
        gMainTick++;
        scanKeys();
        if (tick)
            tick(sTicks - t0);
    }
    while (REG_DISPCAPCNT & DCAP_ENABLE)
        ;
    fv_holdLastFrame(&sPlayer);
    fv_destroyPlayer(&sPlayer);
    irqSet(IRQ_VBLANK, vblank);
}

static void art_page(int f)
{
    (void)f;
    ui_mode(UI_PAGE_ART, sArtGames, sArtAdd, sArtTotal);
}

// ---- Weapon Attack zones (Ec01 / Ec03): the player's input adds games to the first ART set
enum { ZONE_AI, ZONE_RION, ZONE_TSUGUMI, ZONE_ICHIGO, ZONE_ASHI, ZONE_NADE };
static int sZoneKind, sZoneTimer, sZoneLast;

static bool touch_rub(void)
{
    // a rub: the stylus (or UP / DOWN) changes direction
    static int lastX = -1, dir;
    u32 down = keysDown();
    if (down & (KEY_UP | KEY_DOWN))
        return true;
    if (!(keysHeld() & KEY_TOUCH))
    {
        lastX = -1;
        return false;
    }
    touchPosition t;
    touchRead(&t);
    bool rub = false;
    if (lastX >= 0 && abs(t.px - lastX) > 6)
    {
        int d = t.px > lastX ? 1 : -1;
        rub = d != dir;
        dir = d;
        lastX = t.px;
    }
    else if (lastX < 0)
    {
        lastX = t.px;
    }
    return rub;
}

static void zone_tick(int f)
{
    u32 down = keysDown(), held = keysHeld();
    switch (sZoneKind)
    {
        case ZONE_AI: // mash LEFT to break the gates
            if (down & (KEY_LEFT | KEY_TOUCH | KEY_Y))
            {
                sZoneHits++;
                sZoneG = 40 + sZoneHits / 3;
                sfx_play(SFX_UI_CURSOR);
            }
            break;
        case ZONE_RION: // the longer the missile flies, the more games
            if (f % 6 == 0 && rand() % 8)
            {
                sZoneHits += 1 + rand() % 9;
                sZoneG = 45 + sZoneHits / 8;
            }
            if (down & (KEY_B | KEY_TOUCH))
                sfx_play(SFX_CHANCE);
            break;
        case ZONE_TSUGUMI: // hold RIGHT to keep firing: +1 / +3 / +5
            if ((held & (KEY_RIGHT | KEY_TOUCH | KEY_A)) && f - sZoneLast >= 15)
            {
                int r = rand() % 100;
                sZoneG += r < 70 ? 1 : r < 95 ? 3 : 5;
                sZoneHits++;
                sZoneLast = f;
                sfx_play(SFX_STOP);
            }
            break;
        case ZONE_ICHIGO: // rub the lever
        case ZONE_NADE:   // stroke her
            if (touch_rub())
            {
                sZoneHits++;
                sZoneG = (sZoneKind == ZONE_ICHIGO ? 60 : 100) + sZoneHits / 2;
                sfx_play(SFX_UI_OK);
            }
            break;
        case ZONE_ASHI: // hold the STOP button: the punishment adds while it lasts
            if ((held & (KEY_A | KEY_B | KEY_Y | KEY_TOUCH)) && f - sZoneLast >= 6 && sZoneTimer == 0)
            {
                sZoneG++;
                sZoneHits++;
                sZoneLast = f;
                if (sZoneG > 20 && rand() % 60 == 0)
                    sZoneTimer = 1; // she breaks free
            }
            break;
    }
    ui_mode(UI_PAGE_ZONE, sZoneG, sZoneHits, 0);
}

static int run_zone(int kind)
{
    static const struct
    {
        video_id_t in, play, end;
        const char *title, *how1, *how2;
    } Z[] = {
        [ZONE_AI] = { VID_FX_AI_IN, VID_FX_AI_PLAY, VID_FX_AI_END, "WEAPON ATTACK: AI", "Mash LEFT or tap", "to break the LOCKs!" },
        [ZONE_RION] = { VID_FX_RION_IN, VID_FX_RION_PLAY, VID_FX_RION_END, "WEAPON ATTACK: RION",
                        "The longer the missile flies,", "the more games you get!" },
        [ZONE_TSUGUMI] = { VID_FX_TSUG_IN, VID_FX_TSUG_PLAY, VID_FX_TSUG_END, "WEAPON ATTACK: TSUGUMI",
                           "Hold RIGHT or touch", "to keep firing!" },
        [ZONE_ICHIGO] = { VID_FX_ICHIGO_IN, VID_FX_ICHIGO_PLAY, VID_FX_ICHIGO_END, "ICHIGO ATTACK",
                          "Rub the screen left and right", "(or press UP / DOWN)!" },
        [ZONE_ASHI] = { VID_FX_ASHI_IN, VID_FX_ASHI_PLAY, VID_FX_ASHI_END, "ASHITSUBO PUNISH", "Hold a STOP button",
                        "to punish her feet!" },
        [ZONE_NADE] = { VID_FX_NADE_IN, VID_FX_NADE_PLAY, VID_FX_NADE_END, "NADENADE LOVEKYURE",
                        "Rub the screen to pet her!", "Every stroke adds games!" },
    };
    sZoneKind = kind;
    sZoneG = kind == ZONE_TSUGUMI ? 40 : kind == ZONE_ASHI ? 20 : kind == ZONE_RION ? 45 : kind == ZONE_AI ? 40
           : kind == ZONE_ICHIGO ? 60 : 100;
    sZoneHits = sZoneTimer = sZoneLast = 0;
    bgm_play(kind == ZONE_ICHIGO ? BGM_ICHIGO : kind == ZONE_ASHI ? BGM_ASHI : kind == ZONE_NADE ? BGM_NADE : BGM_ZONE);
    if (kind <= ZONE_TSUGUMI)
        play_clip(VID_FX_WA_TITLE, NULL);
    play_clip(Z[kind].in, NULL);
    date_panel_show(Z[kind].title, Z[kind].how1, Z[kind].how2);
    play_clip(Z[kind].play, zone_tick);
    char line[32];
    snprintf(line, sizeof(line), "+%d GAMES!", sZoneG);
    date_panel(Z[kind].title, line, NULL);
    sfx_play(SFX_BIG_START);
    play_clip(Z[kind].end, NULL);
    date_panel_hide();
    return sZoneG;
}

static void art_begin(int games)
{
    bgm_play(BGM_ART);
    sArtGames = games;
    sArtAdd = games;
    sArtTotal = 0;
    sArtHigh = 0;
    play_clip(VID_FX_ART_START, art_page);
    gSave.artCount++;
    sArt = true;
    sSlot.assist = true;
}

static void run_mode(art_mode_t m)
{
    switch (m)
    {
        case MODE_NONE:
            break;
        case MODE_NOTICE:
        {
            static const char *const opts[] = { "Chance notice", "Full notice", "Date notice" };
            int c = date_menu("BIG BONUS! Pick a notice", opts, 3);
            int odds = (c == 1 ? STOCK_FULL : STOCK_CHANCE) + (sBonusHot ? STOCK_HOT : 0);
            sNotice = c == 0 ? NOTICE_CHANCE : c == 1 ? NOTICE_FULL : NOTICE_DATE;
            sNoticeStock = false;
            if (sNotice == NOTICE_DATE)
            {
                show_static_top(RGB15(0, 0, 0) | BIT(15));
                sDating = date_pick_partner();
                if (sDating)
                    gSave.dates++;
            }
            else if (rand() % 100 < odds)
            {
                if (sNotice == NOTICE_FULL)
                {
                    play_clip(VID_FX_BIG_FULL_STOCK, NULL); // the golden shutter closes
                    sArtStock++;
                }
                else
                {
                    sNoticeStock = true; // revealed after the BIG
                }
            }
            break;
        }
        case MODE_BONUS_END:
            if (sNotice == NOTICE_CHANCE)
            {
                play_clip(VID_FX_BIG_CHANCE, NULL); // "Aim for RED 7 from the left!"
                if (sNoticeStock)
                {
                    play_clip(VID_FX_BIG_CHANCE_STOCK, NULL);
                    sArtStock++;
                }
            }
            sNotice = NOTICE_NONE;
            sNoticeStock = sBonusHot = false;
            while (sArtStock > 0)
            {
                sArtStock--;
                int g = run_zone(rand() % 4);
                if (sArt)
                {
                    sArtGames += g;
                    sArtAdd = g;
                    bgm_play(BGM_ART);
                }
                else
                {
                    art_begin(g);
                }
            }
            break;
        case MODE_BATTLE:
        {
            bgm_play(BGM_BATTLE);
            play_clip(VID_FX_BATTLE_IN, art_page);
            bool win = sSlot.bonusFlag != BONUS_NONE || rand() % 100 < (sArtHigh == 2 ? 80 : 50);
            if (win)
            {
                play_clip(VID_FX_BATTLE_WIN, art_page);
                int g = run_zone(rand() % 5 == 0 ? ZONE_NADE : ZONE_ASHI);
                sArtGames += g;
                sArtAdd = g;
            }
            else
            {
                play_clip(VID_FX_BATTLE_LOSE, art_page);
            }
            sArtHigh = 0;
            bgm_play(BGM_ART);
            break;
        }
        case MODE_ART_END:
            bgm_stop();
            date_panel_show("ABSOLUTE AIRSPACE", "Over!", NULL);
            {
                char line[32];
                snprintf(line, sizeof(line), "TOTAL: %d medals", sArtTotal);
                date_panel("ABSOLUTE AIRSPACE", "Over!", line);
            }
            play_clip(VID_FX_ART_END, art_page);
            date_panel_hide();
            if ((u32)sArtTotal > gSave.bestArt)
                gSave.bestArt = sArtTotal;
            sArt = false;
            sSlot.assist = false;
            break;
        case MODE_VA_IN:
        {
            bgm_play(BGM_VA);
            play_clip(VID_FX_VA_IN, NULL);
            static const int shown[] = { 10, 20, 30, 50 };
            bool super = rand() % 8 == 0;
            int n = shown[rand() % 4];
            sVaTarget = sVaLeft = n * (super ? 15 : 5);
            char line[32];
            snprintf(line, sizeof(line), "%d x %d = %d medals!", n, super ? 15 : 5, sVaTarget);
            date_panel_show(super ? "SUPER VA IGNITION" : "VA IGNITION", line, "Win them to reach the ART!");
            for (int f = 0; f < 180; f++)
            {
                swiWaitForVBlank();
                scanKeys();
                if (f > 30 && (keysDown() & (KEY_A | KEY_TOUCH)))
                    break;
            }
            date_panel_hide();
            sVa = true;
            sSlot.assist = true;
            break;
        }
        case MODE_VA_END:
            play_clip(VID_FX_VA_END, NULL);
            sVa = false;
            art_begin(VA_ART_GAMES);
            break;
    }
}

static void vblank(void)
{
    sTicks++;
    reels_vblank();
    ui_vblank();
}

static void game_run(bool trial)
{
    sGame = GAME_IDLE;
    sQuit = false;
    sTrial = trial;
    sStartedOnce = false;
    sDateRequest = sDating = sDmRequest = sDateMode = false;
    sFxStep = -1;
    sClipRequest = false;
    sFxBgmPending = false;
    sKlein = sKleinEntering = sKleinBack = false;
    sMode = MODE_NONE;
    sNotice = NOTICE_NONE;
    sNoticeStock = sBonusHot = sArt = sVa = false;
    sArtStock = sArtGames = sArtTotal = sArtAdd = sArtHigh = 0;
    sKleinGames = 0;
    sIdleBg = 0;
    sPanelOpen = sPanelAuto = sPrevChanceLamp = false;
    sPage = UI_PAGE_MAIN;
    sVolumeShow = 0;

    // a DMA left armed by the previous video (GX FIFO mode) would block every dmaCopy() below
    DMA_CR(3) = 0;
    DC_FlushAll();

    // boot progress on the top screen backdrop while it has no VRAM (debug aid)
    reels_init();
    // SIMULATOR mode plays with the saved balance (a fresh 1000 when broke); TRIAL mode with free credits
    slot_init(&sSlot, trial || gSave.credits < 3 ? SAVE_START_CREDITS : gSave.credits);
    // "SETTING ?" hides a random setting 1..6
    slot_set_setting(&sSlot, gSettings.setting == SETTING_RANDOM ? 1 + rand() % 6 : gSettings.setting + 1);

    vramSetBankA(VRAM_A_LCD);
    vramSetBankB(VRAM_B_LCD);
    vramSetBankD(VRAM_D_LCD);
    ui_init();
    date_init();
    fv_setHeader((const u16 *)top_header_bin);
    show_static_top(RGB15(0, 0, 0) | BIT(15));
    if (!sHaveCard)
        show_static_top(RGB15(12, 0, 0) | BIT(15)); // dark red: no DLDI / SD access

    fv_vblankHook = vblank;
    irqSet(IRQ_VBLANK, vblank);
    fade(-16, 0, 20);

    bool video = sHaveCard;
    while (!sQuit)
    {
        CHECK(40);
        if (sDating)
        {
            swiWaitForVBlank();
            date_tick();
            game_update();
            continue;
        }
        if (sMode != MODE_NONE)
        {
            art_mode_t m = sMode;
            sMode = MODE_NONE;
            run_mode(m);
            continue;
        }
        if (sDmRequest)
        {
            sDmRequest = false;
            CHECK(42);
            show_static_top(RGB15(0, 0, 0) | BIT(15));
            sDateMode = sDating = true;
            sDmGames = 0;
            sSlot.holdBonus = true;
            sSlot.boost = DM_BOOST;
            date_begin_mode();
            continue;
        }
        if (sDateRequest)
        {
            sDateRequest = false;
            CHECK(43);
            show_static_top(RGB15(0, 0, 0) | BIT(15));
            sDating = date_pick_partner();
            if (sDating)
                gSave.dates++;
            continue;
        }
        // the clip is part of the asset pack: the ARM7 opens the .nds and seeks from its offset
        bool clip = sClipRequest;
        sClipRequest = false;
        // idle: the Klein screen during the stage (and until the way back), else the current background
        bool kleinScreen = sKlein || sKleinBack;
        video_id_t vid = clip ? sClipId : kleinScreen ? VID_FX_KLEIN_LOOP : VID_IDLE_0 + sIdleBg;
        if (!clip && sArt)
            vid = sArtHigh == 2 ? VID_FX_ART_LOOP_SP : sArtHigh ? VID_FX_ART_LOOP_HI : VID_FX_ART_LOOP;
        // a GX FIFO DMA left armed by the previous clip would block the DMA copies of the next start-up
        DMA_CR(3) = 0;
        // no player is running here, so the ARM7 replies to pack reads go to asset_load() and not to the player
        if (sFxBgmPending)
        {
            sFxBgmPending = false;
            bgm_play(sFxBgm[sFxKind]);
        }
        CHECK(44);
        if (video)
            ipc_call(IPC_CMD_VIDEO_BASE, 0);
        CHECK(45);
        if (video && fv_initPlayer(&sPlayer, video_path(vid), false))
        {
            CHECK(46);
            fv_startPlayer(&sPlayer);
            CHECK(47);
            while (!fv_isFinished(&sPlayer) && !sQuit && !sDateRequest && !sDmRequest && !sClipRequest &&
                   sMode == MODE_NONE)
            {
                CHECK(48);
                fv_updatePlayer(&sPlayer);
                game_update();
            }
            CHECK(49);
            bool done = fv_isFinished(&sPlayer);
            while (REG_DISPCAPCNT & DCAP_ENABLE)
                ;
            CHECK(50);
            if (!clip && done && !kleinScreen && !sArt)
                sIdleBg = (sIdleBg + 1) % IDLE_BGS; // next background
            if (clip && done && sClipId == sFx[0] && sFxKind == FX_KLEIN && !sKleinEntering)
                sKleinBack = false;                 // the way back (or a failed try) has played
            if (clip && done)
                fv_holdLastFrame(&sPlayer);
            fv_destroyPlayer(&sPlayer);
            CHECK(51);
            irqSet(IRQ_VBLANK, vblank);
            if (clip && done)
            {
                if (sFxStep >= sFxLen)
                    fx_end(); // last clip: back to the idle loop
                else            // keep the last frame until the next lever
                    while (!sQuit && !sClipRequest)
                    {
                        swiWaitForVBlank();
                        game_update();
                    }
            }
        }
        else
        {
            if (video)
                show_static_top(RGB15(0, 0, 12) | BIT(15)); // dark blue: video file missing
            video = false;
            swiWaitForVBlank();
            game_update();
            if (sClipRequest) // no video: the effect runs without pictures
            {
                sClipRequest = false;
                if (sFxStep >= sFxLen)
                    fx_end();
            }
        }
    }

    if (!trial)
    {
        gSave.credits = sSlot.credits;
        gSave.games += sSlot.games;
        gSave.bigCount += sSlot.bigCount;
        gSave.regCount += sSlot.regCount;
        gSave.medalsIn += sSlot.medalsIn;
        gSave.medalsOut += sSlot.medalsOut;
        save_write();
    }
    fade(0, -16, 20);
    // leave the screens clean for the title (detach the vblank work first: it re-arms the reel DMA)
    fv_vblankHook = NULL;
    irqSet(IRQ_VBLANK, vblank_idle);
    oamClear(&oamMain, 0, 128);
    oamClear(&oamSub, 0, 128);
    oamUpdate(&oamMain);
    oamUpdate(&oamSub);
    DMA_CR(0) = 0; // stop the reel shading HBlank DMA (it rewrites BLDY_SUB every line)
    DMA_CR(3) = 0; // and the video's GX FIFO DMA
}

int main(int argc, char **argv)
{
    stage(RGB15(31, 0, 0)); // red: started
    DC_FlushAll();
    mpu_enableVramCache();
    stage(RGB15(31, 31, 0)); // yellow: mpu ok

    fifoSetValue32Handler(FIFO_USER_01, NULL, NULL);
    // handshake; while waiting, show the ARM7 boot stage (IPC_SYNC) as the backdrop colour
    {
        static const u16 stageColor[16] = {
            RGB15(31, 31, 0),  RGB15(31, 16, 0),  RGB15(31, 0, 31),  RGB15(16, 0, 31),
            RGB15(0, 0, 31),   RGB15(0, 16, 31),  RGB15(0, 31, 31),  RGB15(0, 31, 16),
            RGB15(16, 31, 0),  RGB15(31, 31, 31), RGB15(16, 16, 16), RGB15(8, 8, 8),
            RGB15(31, 16, 16), RGB15(16, 31, 16), RGB15(16, 16, 31), RGB15(24, 24, 24),
        };
        // the ARM7 may still be booting (its fifoInit() would drop the message): wait for its main loop
        while ((REG_IPC_SYNC & 15) < 8)
            BG_PALETTE[0] = stageColor[REG_IPC_SYNC & 15];
        fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_HANDSHAKE, 0));
        while (!fifoCheckValue32(FIFO_USER_01))
            BG_PALETTE[0] = stageColor[REG_IPC_SYNC & 15];
        fifoGetValue32(FIFO_USER_01);
    }
    stage(RGB15(0, 31, 0)); // green: arm7 answered

    lcdMainOnTop();
    sfx_init();
    sfx_set_volume(gSettings.volume);
    srand(REG_VCOUNT ^ (REG_KEYINPUT << 8));
    sHaveCard = setup_dldi();
    // screens, music and video live in a pack inside the .nds; the ARM7 reads it on demand
    if (sHaveCard)
        asset_init(argc > 0 ? argv[0] : NULL);
    save_load(); // credits, gallery, records and settings from the .sav next to the ROM
    sfx_set_volume(gSettings.volume);

    title_caution();
    play_opening();
    while (1)
    {
        title_result_t mode = title_run();
        if (mode == TITLE_TIMEOUT)
        {
            play_opening();
            continue;
        }
        if (!settings_run(mode == TITLE_TRIAL))
            continue; // TITLE: back to the title screen
        // short black transition: lets the confirm sound finish and hides the VRAM switch-over
        fade(0, -16, 30);
        bgm_stop();
        if (gSettings.guide)
        {
            setBrightness(3, 0);
            title_show_howto();
            fade(0, -16, 20);
        }
        game_run(mode == TITLE_TRIAL);
    }
}
