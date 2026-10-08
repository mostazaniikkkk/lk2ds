#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nds.h>
#include "gallery.h"
#include "gallery_items.h"
#include "asset.h"
#include "bgm.h"
#include "save.h"
#include "sfx.h"
#include "text.h"
#include "title.h"
#include "guide_pages.h"
#include "guide_font_bin.h"
#include "guide_bold_bin.h"

void play_movie(int vid); // main.c

#define TOP_PIC_Y  24      // preview picture rows on the top bitmap (256x144)
#define COL_WHITE  RGB15(31, 31, 31)
#define COL_GOLD   RGB15(31, 27, 14)
#define COL_PINK   RGB15(31, 15, 21)
#define COL_HEAD   RGB15(31, 26, 28)
#define COL_BAR    RGB15(6, 2, 8)

static font_t sFont, sBold;
static u16 *sPage;         // decompressed bottom page (RAM copy, redrawn over)
static int sTab, sPageNo, sSel, sMusic = -1;

static int item_at(int tab, int page, int cell)
{
    for (int i = 0; i < GAL_ITEMS; i++)
        if (sGalItems[i].tab == tab && sGalItems[i].page == page && sGalItems[i].cell == cell)
            return i;
    return -1;
}

static int page_count(void)
{
    int n = 0;
    while (item_at(sTab, sPageNo, n) >= 0 && n < 9)
        n++;
    return n;
}

static bool owned(int i)
{
    return sGalItems[i].price == 0 || save_owned(sGalItems[i].sid);
}

static void fill(u16 *bmp, int x0, int y0, int x1, int y1, u16 col)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            bmp[y * 256 + x] = col | BIT(15);
}

// darken (and grey) a rectangle of a bitmap
static void dim(u16 *bmp, int x0, int y0, int x1, int y1, int keep /* /16 */)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
        {
            u16 c = bmp[y * 256 + x];
            int l = ((c & 31) + ((c >> 5) & 31) + ((c >> 10) & 31)) / 3 * keep / 16;
            bmp[y * 256 + x] = l | (l << 5) | (l << 10) | BIT(15);
        }
}

static void frame(u16 *bmp, int x0, int y0, int x1, int y1, u16 col)
{
    for (int x = x0; x <= x1; x++)
        bmp[y0 * 256 + x] = bmp[(y0 + 1) * 256 + x] = bmp[y1 * 256 + x] = bmp[(y1 - 1) * 256 + x] = col | BIT(15);
    for (int y = y0; y <= y1; y++)
        bmp[y * 256 + x0] = bmp[y * 256 + x0 + 1] = bmp[y * 256 + x1] = bmp[y * 256 + x1 - 1] = col | BIT(15);
}

static void price_text(char *buf, size_t n, u32 v)
{
    if (v >= 1000)
        snprintf(buf, n, "%lu,%03lu", (unsigned long)(v / 1000), (unsigned long)(v % 1000));
    else
        snprintf(buf, n, "%lu", (unsigned long)v);
}

static void load_page(void)
{
    void *lz = asset_load(sGalPagePak[sTab][sPageNo], NULL);
    if (lz && sPage)
        decompress(lz, sPage, LZ77);
    asset_free(lz);
}

static void draw_bottom(void)
{
    u16 *bmp = (u16 *)BG_GFX_SUB;
    if (sPage == NULL)
        return;
    memcpy(bmp, sPage, 256 * 192 * 2);
    char buf[48];
    for (int c = 0; c < 9; c++)
    {
        int i = item_at(sTab, sPageNo, c);
        if (i < 0)
            break;
        int x = GAL_CELL_X + (c % 3) * GAL_PITCH_X, y = GAL_CELL_Y + (c / 3) * GAL_PITCH_Y;
        if (!owned(i))
        {
            dim(bmp, x, y, x + GAL_CELL_W - 1, y + GAL_CELL_H - 1, 6);
            price_text(buf, sizeof(buf), sGalItems[i].price);
            text_draw(&sBold, bmp, x + (GAL_CELL_W - text_width(&sBold, buf)) / 2, y + 15, buf, COL_GOLD, COL_GOLD);
        }
        else if (sGalItems[i].kind == GAL_MUSIC && sMusic == i)
        {
            text_draw(&sBold, bmp, x + 4, y + 2, "PLAYING", COL_PINK, COL_PINK);
        }
        if (c == sSel)
            frame(bmp, x - 2, y - 2, x + GAL_CELL_W + 1, y + GAL_CELL_H + 1, COL_PINK);
    }
    snprintf(buf, sizeof(buf), "PAGE %d/%d", sPageNo + 1, sGalPages[sTab]);
    text_draw(&sFont, bmp, 8, 27, buf, COL_WHITE, COL_WHITE);
    price_text(buf, sizeof(buf), gSave.credits > 0 ? gSave.credits : 0);
    char line[64];
    snprintf(line, sizeof(line), "CREDIT  %s", buf);
    text_draw(&sBold, bmp, 248 - text_width(&sBold, line), 26, line, COL_GOLD, COL_GOLD);
    text_draw(&sFont, bmp, 8, 182, "L/R: tab   B: back", RGB15(18, 16, 22), RGB15(18, 16, 22));
}

static void draw_top(bool bars)
{
    u16 *bmp = (u16 *)BG_GFX;
    int i = item_at(sTab, sPageNo, sSel);
    fill(bmp, 0, 0, 255, 191, 0);
    if (i < 0)
        return;
    bool own = owned(i);
    if (own)
    {
        asset_show_lz(sGalItems[i].preview, bmp + TOP_PIC_Y * 256);
    }
    else // nothing to see until it is bought
    {
        fill(bmp, 0, TOP_PIC_Y, 255, TOP_PIC_Y + 143, RGB15(3, 1, 5));
        frame(bmp, 40, TOP_PIC_Y + 50, 215, TOP_PIC_Y + 93, RGB15(14, 8, 18));
        const char *na = "NOT AVAILABLE";
        text_draw(&sBold, bmp, 128 - text_width(&sBold, na) / 2, TOP_PIC_Y + 58, na, RGB15(22, 20, 26),
                  RGB15(22, 20, 26));
        const char *how = "Unlock it with credits";
        text_draw(&sFont, bmp, 128 - text_width(&sFont, how) / 2, TOP_PIC_Y + 76, how, RGB15(16, 14, 20),
                  RGB15(16, 14, 20));
    }
    if (!bars)
        return;
    fill(bmp, 0, 0, 255, TOP_PIC_Y - 2, COL_BAR);
    text_draw(&sBold, bmp, 6, 4, "GALLERY", COL_HEAD, COL_HEAD);
    static const char *const tabName[GAL_TABS] = { "CG", "MOVIES", "MUSIC", "SONGS" };
    text_draw(&sFont, bmp, 250 - text_width(&sFont, tabName[sTab]), 6, tabName[sTab], COL_WHITE, COL_WHITE);
    fill(bmp, 0, TOP_PIC_Y + 146, 255, 191, COL_BAR);
    text_draw(&sBold, bmp, 6, TOP_PIC_Y + 150, sGalItems[i].name, COL_WHITE, COL_WHITE);
    char buf[48], p[16];
    if (own)
        snprintf(buf, sizeof(buf), sGalItems[i].kind == GAL_CG ? "A: full screen" : sGalItems[i].kind == GAL_MOVIE
                                                                                    ? "A: play" : "A: play / stop");
    else
    {
        price_text(p, sizeof(p), sGalItems[i].price);
        snprintf(buf, sizeof(buf), "Locked  -  %s credits", p);
    }
    text_draw(&sFont, bmp, 6, TOP_PIC_Y + 166, buf, own ? COL_HEAD : COL_GOLD, COL_GOLD);
}

static void redraw(void)
{
    draw_top(true);
    draw_bottom();
}

static void wait_release(void)
{
    do
    {
        swiWaitForVBlank();
        scanKeys();
    } while (keysHeld() & (KEY_TOUCH | KEY_A | KEY_B));
}

// yes / no box on the bottom screen; true = yes
static bool confirm(const char *msg, bool ask)
{
    u16 *bmp = (u16 *)BG_GFX_SUB;
    fill(bmp, 28, 60, 228, 150, RGB15(5, 2, 8));
    frame(bmp, 28, 60, 228, 150, COL_PINK);
    text_draw(&sBold, bmp, 128 - text_width(&sBold, msg) / 2, 80, msg, COL_WHITE, COL_WHITE);
    const char *lab[2] = { ask ? "YES" : "OK", "NO" };
    int n = ask ? 2 : 1, sel = 0;
    for (;;)
    {
        for (int b = 0; b < n; b++)
        {
            int x = n == 1 ? 95 : 50 + b * 90;
            fill(bmp, x, 110, x + 66, 134, b == sel ? COL_PINK : RGB15(11, 11, 14));
            text_draw(&sBold, bmp, x + 33 - text_width(&sBold, lab[b]) / 2, 116, lab[b], COL_WHITE, COL_WHITE);
        }
        swiWaitForVBlank();
        scanKeys();
        u32 d = keysDown();
        if (n == 2 && (d & (KEY_LEFT | KEY_RIGHT)))
        {
            sel ^= 1;
            sfx_play(SFX_UI_CURSOR);
        }
        if (d & KEY_A)
            break;
        if (d & KEY_B)
        {
            sel = 1;
            break;
        }
        if (d & KEY_TOUCH)
        {
            touchPosition t;
            touchRead(&t);
            for (int b = 0; b < n; b++)
            {
                int x = n == 1 ? 95 : 50 + b * 90;
                if (t.px >= x && t.px <= x + 66 && t.py >= 110 && t.py <= 134)
                {
                    sel = b;
                    goto done;
                }
            }
        }
    }
done:
    wait_release();
    return sel == 0;
}

static void activate(void)
{
    int i = item_at(sTab, sPageNo, sSel);
    if (i < 0)
        return;
    const gal_item_t *it = &sGalItems[i];
    if (!owned(i))
    {
        char p[16], msg[48];
        price_text(p, sizeof(p), it->price);
        if (gSave.credits < (s32)it->price)
        {
            snprintf(msg, sizeof(msg), "Not enough credits (%s)", p);
            sfx_play(SFX_UI_BACK);
            confirm(msg, false);
        }
        else
        {
            snprintf(msg, sizeof(msg), "Unlock for %s credits?", p);
            sfx_play(SFX_UI_OK);
            if (confirm(msg, true))
            {
                gSave.credits -= it->price;
                save_unlock(it->sid);
                if (!save_write())
                {
                    extern u32 gSaveLast;
                    char e[48];
                    snprintf(e, sizeof(e), "SAVE FAILED o%lu w%lu c%lu", (unsigned long)(gSaveLast >> 12) & 63, (unsigned long)(gSaveLast >> 6) & 63, (unsigned long)gSaveLast & 63);
                    confirm(e, false);
                }
                sfx_play(SFX_BIG_START);
            }
            else
            {
                sfx_play(SFX_UI_BACK);
            }
        }
        redraw();
        return;
    }
    sfx_play(SFX_UI_OK);
    switch (it->kind)
    {
        case GAL_CG: // the picture alone until a key / tap
            draw_top(false);
            wait_release();
            do
            {
                swiWaitForVBlank();
                scanKeys();
            } while (!(keysDown() & (KEY_A | KEY_B | KEY_TOUCH)));
            wait_release();
            draw_top(true);
            break;
        case GAL_MOVIE:
            bgm_stop();
            sMusic = -1;
            play_movie(it->target);
            title_setup_video();
            load_page();
            redraw();
            bgm_play(BGM_MENU);
            setBrightness(3, 0);
            break;
        case GAL_MUSIC:
            if (sMusic == i)
            {
                sMusic = -1;
                bgm_play(BGM_MENU);
            }
            else
            {
                sMusic = i;
                bgm_play(it->target);
            }
            draw_bottom();
            break;
    }
}

static void go_page(int tab, int page, int sel)
{
    sTab = tab;
    sPageNo = page;
    load_page();
    int n = page_count();
    sSel = sel < n ? sel : n - 1;
    redraw();
}

void gallery_run(void)
{
    static bool fonts;
    if (!fonts)
    {
        text_font_init(&sFont, guide_font_bin, GUIDE_FONT_H);
        text_font_init(&sBold, guide_bold_bin, GUIDE_BOLD_H);
        fonts = true;
    }
    sPage = malloc(256 * 192 * 2);
    if (sPage == NULL)
        return;
    sMusic = -1;
    REG_WIN0H_SUB = 0; // the menu's button highlight
    REG_WIN0V_SUB = 0;
    go_page(0, 0, 0);
    wait_release();
    for (;;)
    {
        swiWaitForVBlank();
        scanKeys();
        u32 d = keysDown();
        int n = page_count(), col = sSel % 3, row = sSel / 3, sel = sSel;
        if (d & (KEY_B | KEY_START))
            break;
        if (d & (KEY_L | KEY_R))
        {
            sfx_play(SFX_UI_CURSOR);
            go_page((sTab + (d & KEY_R ? 1 : GAL_TABS - 1)) % GAL_TABS, 0, 0);
            continue;
        }
        if (d & KEY_RIGHT)
        {
            if (col < 2 && sSel + 1 < n)
                sel++;
            else if (sPageNo + 1 < sGalPages[sTab])
            {
                sfx_play(SFX_UI_CURSOR);
                go_page(sTab, sPageNo + 1, row * 3);
                continue;
            }
        }
        if (d & KEY_LEFT)
        {
            if (col > 0)
                sel--;
            else if (sPageNo > 0)
            {
                sfx_play(SFX_UI_CURSOR);
                go_page(sTab, sPageNo - 1, row * 3 + 2);
                continue;
            }
        }
        if ((d & KEY_DOWN) && sSel + 3 < n)
            sel += 3;
        if ((d & KEY_UP) && row > 0)
            sel -= 3;
        if (d & KEY_TOUCH)
        {
            touchPosition t;
            touchRead(&t);
            if (t.py <= 22) // tabs
            {
                int tab = (t.px - GAL_TAB_X) / GAL_TAB_PITCH;
                if (tab >= 0 && tab < GAL_TABS && tab != sTab)
                {
                    sfx_play(SFX_UI_CURSOR);
                    go_page(tab, 0, 0);
                }
                continue;
            }
            for (int c = 0; c < n; c++)
            {
                int x = GAL_CELL_X + (c % 3) * GAL_PITCH_X, y = GAL_CELL_Y + (c / 3) * GAL_PITCH_Y;
                if (t.px >= x && t.px < x + GAL_CELL_W && t.py >= y && t.py < y + GAL_CELL_H)
                {
                    if (c == sSel)
                    {
                        wait_release();
                        activate();
                    }
                    else
                    {
                        sSel = c;
                        sfx_play(SFX_UI_CURSOR);
                        redraw();
                    }
                }
            }
            continue;
        }
        if (sel != sSel)
        {
            sSel = sel;
            sfx_play(SFX_UI_CURSOR);
            redraw();
        }
        if (d & KEY_A)
        {
            wait_release();
            activate();
        }
    }
    sfx_play(SFX_UI_BACK);
    if (sMusic >= 0)
        bgm_play(BGM_MENU);
    free(sPage);
    sPage = NULL;
    title_setup_video();
    wait_release();
}
