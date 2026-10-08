#include <string.h>
#include "reels.h"
#include "layout.h"

#include "cab_tiles_bin.h"
#include "cab_map_bin.h"
#include "cab_pal_bin.h"
#include "reel1_strip_bin.h"
#include "reel2_strip_bin.h"
#include "reel3_strip_bin.h"
#include "reel1_pal_bin.h"
#include "reel2_pal_bin.h"
#include "reel3_pal_bin.h"
#include "vignette_bin.h"

// sub BG VRAM layout (0x06200000)
#define CAB_CHAR_BASE   0       // 0x0000: cabinet tiles (~34 KB)
#define CAB_MAP_BASE    17      // 0x8800
#define REEL_MAP_BASE   18      // 0x9000, 0x9800, 0xA000
#define REEL_CHAR_BASE  3       // 0xC000: 3 reels x 16 tile rows x 8 tiles, then 1 blank tile
#define OVERLAY_MAP_BASE  21     // 0xA800: overlay map (free between the reel maps and tiles)
#define OVERLAY_CHAR_BASE 5      // 0x14000: overlay tiles, 32x24 8bpp = 48 KB (up to 0x20000)
#define OVERLAY_DIM       9      // BLDY of the cabinet behind the overlay
#define RING_ROWS       16
#define BLANK_TILE      (3 * RING_ROWS * 8)

// position counter wraps at lcm(strip height, 256-pixel map) so map row / ring slot / strip row stay in phase
#define POS_WRAP        16128
#define SPIN_SPEED      11      // pixels per frame (~80 rpm)
#define SYM_OFFSET      (((REEL_WIN_Y1 - REEL_WIN_Y0) - 3 * REEL_SYM_H) / 2)

static const u8 *const sStrips[3] = { reel1_strip_bin, reel2_strip_bin, reel3_strip_bin };
static const u8 *const sPals[3] = { reel1_pal_bin, reel2_pal_bin, reel3_pal_bin };
static const int sReelX[3] = { REEL1_X, REEL2_X, REEL3_X };

typedef struct
{
    reel_state_t state;
    int pos;            // strip pixel row shown at the top of the window
    int remain;         // pixels left to travel while REEL_STOPPING
    s16 loaded[RING_ROWS];
} reel_t;

static reel_t sReels[3];
static u16 sVignette[192];
static u16 sDim[192];           // constant BLDY while the overlay is up
static volatile bool sOverlay;

static inline u8 *reel_tile_vram(int reel, int slot)
{
    return (u8 *)BG_TILE_RAM_SUB(REEL_CHAR_BASE) + (reel * RING_ROWS + slot) * 8 * 64;
}

// make sure all strip tile rows visible at `pos` are in the ring, then set the scroll
static void reel_sync(int i)
{
    reel_t *r = &sReels[i];
    int t0 = r->pos >> 3;
    int t1 = (r->pos + (REEL_WIN_Y1 - REEL_WIN_Y0) + 7) >> 3;
    for (int t = t0; t <= t1; t++)
    {
        int tt = t % (POS_WRAP / 8);
        int slot = tt % RING_ROWS;
        if (r->loaded[slot] == tt)
            continue;
        r->loaded[slot] = tt;
        const u8 *src = sStrips[i] + (tt % REEL_STRIP_TROWS) * 8 * 64;
        dmaCopyWords(1, src, reel_tile_vram(i, slot), 8 * 64); // DMA0 is the HBlank BLDY channel
    }
    // screen y = REEL_WIN_Y0 shows strip row pos
    u16 vofs = (r->pos - REEL_WIN_Y0) & 255;
    switch (i)
    {
        case 0: REG_BG1VOFS_SUB = vofs; break;
        case 1: REG_BG2VOFS_SUB = vofs; break;
        case 2: REG_BG3VOFS_SUB = vofs; break;
    }
}

// strip position that shows symbol `sym` in the top row (3 symbols centred in the window)
static int sym_pos(int sym)
{
    return sym * REEL_SYM_H - SYM_OFFSET + REEL_STRIP_H; // + one strip height keeps it positive
}

void reels_init(void)
{
    vramSetBankC(VRAM_C_SUB_BG);
    vramSetBankH(VRAM_H_LCD);
    vramSetBankI(VRAM_I_SUB_SPRITE);

    videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE | DISPLAY_BG1_ACTIVE | DISPLAY_BG2_ACTIVE |
                    DISPLAY_BG3_ACTIVE | DISPLAY_BG_EXT_PALETTE);

    // ext palettes: slot n = BGn, palette 0 of each slot
    dmaCopy(cab_pal_bin, VRAM_H, 512);
    for (int i = 0; i < 3; i++)
        dmaCopy(sPals[i], (u8 *)VRAM_H + (i + 1) * 0x2000, 512);
    vramSetBankH(VRAM_H_SUB_BG_EXT_PALETTE);
    BG_PALETTE_SUB[0] = RGB15(0, 0, 0);

    // cabinet
    REG_BG0CNT_SUB = BG_32x32 | BG_COLOR_256 | BG_PRIORITY_0 | BG_MAP_BASE(CAB_MAP_BASE) |
                     BG_TILE_BASE(CAB_CHAR_BASE);
    dmaCopy(cab_tiles_bin, BG_TILE_RAM_SUB(CAB_CHAR_BASE), cab_tiles_bin_size);
    u16 *cmap = BG_MAP_RAM_SUB(CAB_MAP_BASE);
    dmaFillHalfWords(0, cmap, 32 * 32 * 2);
    dmaCopy(cab_map_bin, cmap, cab_map_bin_size);

    // reels: static map, row m / column c -> ring slot (m % 16), tile c
    memset(reel_tile_vram(0, 0), 0, (BLANK_TILE + 1) * 64);
    for (int i = 0; i < 3; i++)
    {
        u16 *map = BG_MAP_RAM_SUB(REEL_MAP_BASE + i);
        for (int m = 0; m < 32; m++)
            for (int c = 0; c < 32; c++)
                map[m * 32 + c] = c < 8 ? (i * RING_ROWS + (m % RING_ROWS)) * 8 + c : BLANK_TILE;
        for (int s = 0; s < RING_ROWS; s++)
            sReels[i].loaded[s] = -1;
        sReels[i].state = REEL_STOPPED;
        sReels[i].pos = sym_pos(3 + 2 * i);
    }
    REG_BG1CNT_SUB = BG_32x32 | BG_COLOR_256 | BG_PRIORITY_1 | BG_MAP_BASE(REEL_MAP_BASE + 0) |
                     BG_TILE_BASE(REEL_CHAR_BASE);
    REG_BG2CNT_SUB = BG_32x32 | BG_COLOR_256 | BG_PRIORITY_1 | BG_MAP_BASE(REEL_MAP_BASE + 1) |
                     BG_TILE_BASE(REEL_CHAR_BASE);
    REG_BG3CNT_SUB = BG_32x32 | BG_COLOR_256 | BG_PRIORITY_1 | BG_MAP_BASE(REEL_MAP_BASE + 2) |
                     BG_TILE_BASE(REEL_CHAR_BASE);
    REG_BG1HOFS_SUB = (256 - sReelX[0]) & 255;
    REG_BG2HOFS_SUB = (256 - sReelX[1]) & 255;
    REG_BG3HOFS_SUB = (256 - sReelX[2]) & 255;
    for (int i = 0; i < 3; i++)
        reel_sync(i);

    // cylinder shading: fade the reel layers towards black, BLDY reloaded every scanline by HBlank DMA
    for (int y = 0; y < 192; y++)
        sVignette[y] = vignette_bin[y];
    DC_FlushRange(sVignette, sizeof(sVignette));
    REG_BLDCNT_SUB = BLEND_FADE_BLACK | BLEND_SRC_BG1 | BLEND_SRC_BG2 | BLEND_SRC_BG3;
}

void reels_vblank(void)
{
    // restart the per-line BLDY table
    const u16 *bldy = sOverlay ? sDim : sVignette;
    DMA_CR(0) = 0;
    REG_BLDY_SUB = bldy[0];
    DMA_SRC(0) = (u32)&bldy[1];
    DMA_DEST(0) = (u32)&REG_BLDY_SUB;
    DMA_CR(0) = DMA_ENABLE | DMA_REPEAT | DMA_START_HBL | DMA_16_BIT | DMA_SRC_INC | DMA_DST_RESET | 1;

    for (int i = 0; i < 3; i++)
    {
        reel_t *r = &sReels[i];
        if (r->state == REEL_STOPPED)
            continue;
        // symbols travel downwards: the strip position decreases
        int step = SPIN_SPEED;
        if (r->state == REEL_STOPPING)
        {
            if (step >= r->remain)
            {
                step = r->remain;
                r->state = REEL_STOPPED;
            }
            r->remain -= step;
        }
        r->pos -= step;
        if (r->pos < 0)
            r->pos += POS_WRAP;
        reel_sync(i);
    }
}

u8 *reels_overlay_tiles(void)
{
    return (u8 *)BG_TILE_RAM_SUB(OVERLAY_CHAR_BASE);
}

void reels_overlay_show(const u16 *pal)
{
    for (int y = 0; y < 192; y++)
        sDim[y] = OVERLAY_DIM;
    DC_FlushRange(sDim, sizeof(sDim));
    u16 *map = BG_MAP_RAM_SUB(OVERLAY_MAP_BASE);
    for (int i = 0; i < 32 * 24; i++)
        map[i] = i;
    swiWaitForVBlank();
    // BG1 (reel 1) becomes the overlay: its ext palette slot, map and tiles, drawn over the cabinet
    vramSetBankH(VRAM_H_LCD);
    dmaCopyWords(1, pal, (u8 *)VRAM_H + 0x2000, 512);
    vramSetBankH(VRAM_H_SUB_BG_EXT_PALETTE);
    REG_BG1CNT_SUB = BG_32x32 | BG_COLOR_256 | BG_PRIORITY_0 | BG_MAP_BASE(OVERLAY_MAP_BASE) |
                     BG_TILE_BASE(OVERLAY_CHAR_BASE);
    REG_BG0CNT_SUB = (REG_BG0CNT_SUB & ~3) | BG_PRIORITY_1;
    REG_BG1HOFS_SUB = 0;
    REG_BG1VOFS_SUB = 0;
    // dim everything but the overlay; sprites (buttons, panel) are hidden meanwhile
    REG_BLDCNT_SUB = BLEND_FADE_BLACK | BLEND_SRC_BG0 | BLEND_SRC_BG2 | BLEND_SRC_BG3;
    REG_DISPCNT_SUB &= ~DISPLAY_SPR_ACTIVE;
    sOverlay = true;
}

void reels_overlay_hide(void)
{
    if (!sOverlay)
        return;
    swiWaitForVBlank();
    sOverlay = false;
    vramSetBankH(VRAM_H_LCD);
    dmaCopyWords(1, sPals[0], (u8 *)VRAM_H + 0x2000, 512);
    vramSetBankH(VRAM_H_SUB_BG_EXT_PALETTE);
    REG_BG1CNT_SUB = BG_32x32 | BG_COLOR_256 | BG_PRIORITY_1 | BG_MAP_BASE(REEL_MAP_BASE + 0) |
                     BG_TILE_BASE(REEL_CHAR_BASE);
    REG_BG0CNT_SUB = (REG_BG0CNT_SUB & ~3) | BG_PRIORITY_0;
    REG_BG1HOFS_SUB = (256 - sReelX[0]) & 255;
    reel_sync(0);
    REG_BLDCNT_SUB = BLEND_FADE_BLACK | BLEND_SRC_BG1 | BLEND_SRC_BG2 | BLEND_SRC_BG3;
    REG_DISPCNT_SUB |= DISPLAY_SPR_ACTIVE;
}

void reels_start(void)
{
    for (int i = 0; i < 3; i++)
        sReels[i].state = REEL_SPINNING;
}

// aligned stop position reached next while moving downwards (<= pos)
static int stop_floor(int pos)
{
    int p = pos + SYM_OFFSET;
    return p - p % REEL_SYM_H - SYM_OFFSET;
}

int reels_stop_base(int i)
{
    int irq = enterCriticalSection();
    int p = stop_floor(sReels[i].pos);
    leaveCriticalSection(irq);
    return ((p + SYM_OFFSET) / REEL_SYM_H) % REEL_NSYM;
}

void reels_stop(int i, int slip)
{
    reel_t *r = &sReels[i];
    int irq = enterCriticalSection();
    if (r->state == REEL_SPINNING)
    {
        r->remain = r->pos - stop_floor(r->pos) + slip * REEL_SYM_H;
        r->state = REEL_STOPPING;
    }
    leaveCriticalSection(irq);
}

reel_state_t reels_state(int i)
{
    return sReels[i].state;
}

bool reels_all_stopped(void)
{
    return sReels[0].state == REEL_STOPPED && sReels[1].state == REEL_STOPPED &&
           sReels[2].state == REEL_STOPPED;
}

int reels_symbol_at(int i, int row)
{
    int p = sReels[i].pos + SYM_OFFSET + row * REEL_SYM_H + REEL_SYM_H / 2;
    return (p / REEL_SYM_H) % REEL_NSYM;
}
