// HOW TO PLAY: controls page + the machine's 27 play-guide cards, translated (tools/make_guide.py).
// Text is drawn at runtime over one compressed background; the bottom screen must be in the
// title/settings video mode (16-bit bitmap on sub BG3, window 0 for button highlights).

#include <nds.h>
#include "guide.h"
#include "guide_pages.h"
#include "sfx.h"

#include "asset.h"
#include "guide_font_bin.h"
#include "guide_bold_bin.h"
#include "text.h"

typedef struct
{
    u8 x, y, w, h;
} rect_t;


static const rect_t sPrev = GUIDE_PREV_RECT;
static const rect_t sNext = GUIDE_NEXT_RECT;
static const rect_t sBack = GUIDE_BACK_RECT;
static font_t sFont, sBold;
static void *sBg; // page background, loaded while the guide is open

#define COL_TEXT      RGB15(29, 29, 30)
#define COL_HIGHLIGHT RGB15(13, 28, 31)
#define COL_TITLE     RGB15(31, 31, 31)
#define BLDY_PRESS    8

static void draw_page(int page)
{
    if (sBg)
        decompress(sBg, BG_GFX_SUB, LZ77Vram);
    text_draw(&sBold, BG_GFX_SUB, 10, 8, sGuideTitle[page], COL_TITLE, COL_TITLE);
    for (int i = 0; sGuideLines[page][i]; i++)
        text_draw(&sFont, BG_GFX_SUB, GUIDE_TEXT_X, GUIDE_TEXT_Y + i * GUIDE_LINE_H, sGuideLines[page][i], COL_TEXT,
                  COL_HIGHLIGHT);
    char num[8];
    int n = page + 1, k = 0;
    char tmp[8];
    int t = 0;
    for (int v = GUIDE_PAGES; v; v /= 10)
        tmp[t++] = '0' + v % 10;
    if (n >= 10)
        num[k++] = '0' + n / 10;
    num[k++] = '0' + n % 10;
    num[k++] = '/';
    while (t)
        num[k++] = tmp[--t];
    num[k] = 0;
    text_draw(&sFont, BG_GFX_SUB, 244 - text_width(&sFont, num), 145, num, COL_TITLE, COL_TITLE);
}

static bool inside(const rect_t *r, const touchPosition *t)
{
    return t->px >= r->x && t->px < r->x + r->w && t->py >= r->y && t->py < r->y + r->h;
}

static void highlight(const rect_t *r)
{
    if (r)
    {
        REG_WIN0H_SUB = (r->x << 8) | (r->x + r->w);
        REG_WIN0V_SUB = (r->y << 8) | (r->y + r->h);
        REG_BLDY_SUB = BLDY_PRESS;
    }
    else
    {
        REG_WIN0H_SUB = 0;
        REG_WIN0V_SUB = 0;
    }
}

static void wait_release(void)
{
    do
    {
        swiWaitForVBlank();
        scanKeys();
    } while (keysHeld() & (KEY_TOUCH | KEY_A | KEY_B | KEY_START));
}

void guide_run(void)
{
    static bool fontsReady;
    if (!fontsReady)
    {
        text_font_init(&sFont, guide_font_bin, GUIDE_FONT_H);
        text_font_init(&sBold, guide_bold_bin, GUIDE_BOLD_H);
        fontsReady = true;
    }
    sBg = asset_load(PAK_GUIDE_BG_LZ, NULL);
    int page = 0;
    draw_page(page);
    highlight(NULL);

    const rect_t *pressed = NULL;
    touchPosition t = { 0 };
    while (1)
    {
        swiWaitForVBlank();
        scanKeys();
        u32 down = keysDown(), held = keysHeld();
        int dir = 0;
        if (down & (KEY_LEFT | KEY_L))
            dir = -1;
        if (down & (KEY_RIGHT | KEY_R | KEY_A))
            dir = 1;
        if (down & (KEY_B | KEY_START))
            break;

        if (held & KEY_TOUCH)
        {
            touchRead(&t);
            if (down & KEY_TOUCH)
            {
                pressed = inside(&sPrev, &t) ? &sPrev : inside(&sNext, &t) ? &sNext
                          : inside(&sBack, &t) ? &sBack : NULL;
                if (pressed)
                    sfx_play(SFX_UI_CURSOR);
            }
            highlight(pressed && inside(pressed, &t) ? pressed : NULL);
        }
        else if (pressed)
        {
            const rect_t *r = pressed;
            pressed = NULL;
            highlight(NULL);
            if (inside(r, &t))
            {
                if (r == &sBack)
                    break;
                dir = r == &sPrev ? -1 : 1;
            }
        }

        if (dir)
        {
            int np = page + dir;
            if (np >= 0 && np < GUIDE_PAGES)
            {
                page = np;
                sfx_play(SFX_UI_CURSOR);
                draw_page(page);
            }
        }
    }
    sfx_play(SFX_UI_BACK);
    highlight(NULL);
    wait_release();
    asset_free(sBg);
    sBg = NULL;
}
