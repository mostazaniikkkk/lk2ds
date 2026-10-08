#include "check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "date.h"
#include "date_script.h"
#include "asset.h"
#include "save.h"
#include "reels.h"
#include "sfx.h"
#include "text.h"
#include "guide_pages.h"
#include "guide_font_bin.h"
#include "guide_bold_bin.h"
#include "date_icons_tiles_bin.h"
#include "date_icons_pal_bin.h"

// ---------------------------------------------------------------- routes

typedef struct
{
    const char *name;
    const date_route_t *route; // NULL: not available yet
} partner_t;

static const partner_t sPartners[] = {
    { "Ichigo", &s_ichigo_route },
    { "Ai", &s_ai_route },
    { "Rion", &s_rion_route },
    { "Tsugumi", &s_tsugumi_route },
    { "Klein (group date)", &s_klein_route },
};
#define PARTNER_KLEIN 4
// a confession is recorded in the save (gallery flag SAVE_CONFESSED + partner); Klein joins the list once
// KLEIN_UNLOCK different CELF girls have confessed
#define SAVE_CONFESSED 200
#define KLEIN_UNLOCK   2
static int sPartner = -1;
static int sLastPlace = -1;
#define NPARTNERS (int)(sizeof(sPartners) / sizeof(sPartners[0]))

// confession odds: 20% + 15% per affection point, 5..95%
#define LOVE_BASE   20
#define LOVE_STEP   15
#define ICON_FRAMES 90

static font_t sFont, sBold;
static const date_route_t *sRoute;
static int sStep;                  // next script beat
static int sPlace = -1, sPlaceStep; // place block being played
static u32 sVisited;
static int sScore;
static int sIconTimer;
static bool sHot;

// main sprites for the reaction icons (the counter uses OAM 0..27)
#define OAM_ICON_HEART 28
#define OAM_ICON_UPDOWN 29
enum { PAL_ICON_HEART = 2, PAL_ICON_UP, PAL_ICON_DOWN };
static u16 *sIconGfx[3];

// ---------------------------------------------------------------- top screen

// dialogue box over the bottom of the still (bitmap rows; the screen shows them 48 lines lower)
#define BOX_Y0   100
#define BOX_Y1   141
#define BOX_X0   4
#define BOX_X1   251
#define TAB_Y0   91
#define TEXT_X   12
#define TEXT_Y   106
#define LINE_H   14
#define WRAP_W   (BOX_X1 - TEXT_X - 6)

#define COL_BOX     RGB15(31, 30, 31)
#define COL_BORDER  RGB15(29, 15, 21)
#define COL_TAB     RGB15(28, 7, 15)
#define COL_TEXT    RGB15(7, 4, 6)
#define COL_NAME    RGB15(31, 31, 31)

static inline u16 blend(u16 d, u16 c, int a) // a: 0..16
{
    int r = ((d & 31) * (16 - a) + (c & 31) * a) >> 4;
    int g = (((d >> 5) & 31) * (16 - a) + ((c >> 5) & 31) * a) >> 4;
    int b = (((d >> 10) & 31) * (16 - a) + ((c >> 10) & 31) * a) >> 4;
    return r | (g << 5) | (b << 10) | BIT(15);
}

// rounded rectangle (radius 3) blended into the top bitmap; border 0 = none
static void top_rect(int x0, int y0, int x1, int y1, u16 fill, int alpha, u16 border)
{
    u16 *bmp = (u16 *)BG_GFX;
    for (int y = y0; y <= y1; y++)
    {
        int dy = y < y0 + 3 ? y0 + 3 - y : y > y1 - 3 ? y - (y1 - 3) : 0;
        int inset = dy == 3 ? 2 : dy == 2 ? 1 : dy == 1 ? 0 : 0;
        if (dy == 3)
            inset = 2;
        for (int x = x0 + inset; x <= x1 - inset; x++)
        {
            bool edge = border && (y == y0 || y == y1 || x == x0 + inset || x == x1 - inset);
            u16 *p = &bmp[y * 256 + x];
            *p = edge ? border | BIT(15) : blend(*p, fill, alpha);
        }
    }
}

// next line of s that fits in w pixels: returns its length, *next = start of the following line
static int wrap(const font_t *f, const char *s, int w, const char **next)
{
    int width = 0, lastSpace = -1, i = 0;
    for (; s[i] && s[i] != '|'; i++)
    {
        if (s[i] == ' ')
            lastSpace = i;
        if (s[i] >= 32 && s[i] < 127)
            width += f->widths[s[i] - 32];
        if (width > w && lastSpace > 0)
        {
            *next = s + lastSpace + 1;
            return lastSpace;
        }
    }
    *next = s[i] == '|' ? s + i + 1 : s + i;
    return i;
}

// one line of n characters; x < 0 centres it
static void draw_line(const font_t *f, int x, int y, const char *s, int n, u16 col)
{
    char buf[96];
    if (n > (int)sizeof(buf) - 1)
        n = sizeof(buf) - 1;
    memcpy(buf, s, n);
    buf[n] = 0;
    if (x < 0)
        x = (256 - text_width(f, buf)) / 2;
    text_draw(f, (u16 *)BG_GFX, x, y, buf, col, col);
}

// still + dialogue box; text NULL / "" shows the picture only, a text starting with '[' is narration
static void show(int still, const char *text)
{
    if (sHot && sRoute->hot[still] != 255)
        still = sRoute->hot[still];
    asset_show_lz(sRoute->stills[still], BG_GFX);
    if (text == NULL || text[0] == 0)
        return;
    bool narration = text[0] == '[';
    // "@Name@text": the speaker (group date); else the route's girl
    char name[16];
    const char *who = sRoute->name;
    if (text[0] == '@')
    {
        const char *e = strchr(text + 1, '@');
        int n = e ? e - text - 1 : 0;
        if (n > 0 && n < (int)sizeof(name))
        {
            memcpy(name, text + 1, n);
            name[n] = 0;
            who = name;
            text = e + 1;
        }
    }
    top_rect(BOX_X0, BOX_Y0, BOX_X1, BOX_Y1, COL_BOX, 13, COL_BORDER);
    if (!narration)
    {
        int w = text_width(&sBold, who) + 12;
        top_rect(8, TAB_Y0, 8 + w, TAB_Y0 + 14, COL_TAB, 16, 0);
        text_draw(&sBold, (u16 *)BG_GFX, 14, TAB_Y0 + 1, who, COL_NAME, COL_NAME);
    }
    const char *s = text;
    for (int l = 0; l < 2 && *s; l++)
    {
        const char *next;
        int n = wrap(&sFont, s, WRAP_W, &next);
        draw_line(&sFont, narration ? -1 : TEXT_X, TEXT_Y + l * LINE_H + (narration ? LINE_H / 2 : 0), s, n, COL_TEXT);
        s = next;
    }
}

static void show_icon(int delta)
{
    if (delta == 0)
        return;
    // UP: heart in the middle + "UP"; DOWN: "DOWN" (Dg02 reaction scenes); screen y = bitmap y + 48
    if (delta > 0)
        oamSet(&oamMain, OAM_ICON_HEART, 96, 48 + 30, 0, PAL_ICON_HEART, SpriteSize_64x64,
               SpriteColorFormat_16Color, sIconGfx[0], -1, false, false, false, false, false);
    oamSet(&oamMain, OAM_ICON_UPDOWN, 180, 48 + 4, 0, delta > 0 ? PAL_ICON_UP : PAL_ICON_DOWN, SpriteSize_64x32,
           SpriteColorFormat_16Color, sIconGfx[delta > 0 ? 1 : 2], -1, false, false, false, false, false);
    sIconTimer = ICON_FRAMES;
    sfx_play(delta > 0 ? SFX_UI_OK : SFX_UI_BACK);
}

static void hide_icons(void)
{
    oamSetHidden(&oamMain, OAM_ICON_HEART, true);
    oamSetHidden(&oamMain, OAM_ICON_UPDOWN, true);
    sIconTimer = 0;
}

void date_tick(void)
{
    gMainTick++; // the date's own wait loops count as progress
    if (sIconTimer > 0 && --sIconTimer == 0)
        hide_icons();
}

// ---------------------------------------------------------------- bottom screen choices

enum
{
    IX_FILL = 1, IX_BORDER, IX_HI_FILL, IX_HI_BORDER, IX_PROMPT, IX_LOCK_FILL, IX_LOCK_BORDER,
    RAMP_TEXT = 16, RAMP_HI = 32, RAMP_PROMPT = 48, RAMP_LOCK = 64,
};
#define MAX_OPTIONS 6

typedef struct
{
    int n, sel, y0, h, gap;
    const char *opt[MAX_OPTIONS];
    bool locked[MAX_OPTIONS];
} menu_t;

static u16 sPal[256] ALIGN(4);
static u8 *sTiles; // 48 KB RAM copy of the overlay, copied to VRAM when it changes

static u16 col24(int r, int g, int b)
{
    return RGB15(r >> 3, g >> 3, b >> 3);
}

static void ramp(int base, u16 from, u16 to)
{
    for (int a = 0; a < 16; a++)
        sPal[base + a] = blend(from, to, a * 16 / 15) & 0x7FFF;
}

static void palette_init(void)
{
    memset(sPal, 0, sizeof(sPal));
    sPal[IX_FILL] = col24(245, 245, 250);
    sPal[IX_BORDER] = col24(150, 150, 170);
    sPal[IX_HI_FILL] = col24(255, 120, 170);
    sPal[IX_HI_BORDER] = col24(200, 40, 100);
    sPal[IX_PROMPT] = col24(40, 10, 40);
    sPal[IX_LOCK_FILL] = col24(95, 95, 105);
    sPal[IX_LOCK_BORDER] = col24(70, 70, 80);
    ramp(RAMP_TEXT, sPal[IX_FILL], col24(50, 40, 60));
    ramp(RAMP_HI, sPal[IX_HI_FILL], col24(255, 255, 255));
    ramp(RAMP_PROMPT, sPal[IX_PROMPT], col24(255, 210, 230));
    ramp(RAMP_LOCK, sPal[IX_LOCK_FILL], col24(185, 185, 195));
}

static inline void put(int x, int y, int ix)
{
    sTiles[((y >> 3) * 32 + (x >> 3)) * 64 + (y & 7) * 8 + (x & 7)] = ix;
}

static void panel_rect(int x0, int y0, int x1, int y1, int fill, int border)
{
    for (int y = y0; y <= y1; y++)
    {
        int dy = y - y0 < 4 ? 4 - (y - y0) : y1 - y < 4 ? 4 - (y1 - y) : 0;
        int inset = dy >= 4 ? 3 : dy == 3 ? 1 : dy == 2 ? 1 : 0;
        for (int x = x0 + inset; x <= x1 - inset; x++)
        {
            bool edge = y == y0 || y == y1 || x <= x0 + inset || x >= x1 - inset;
            put(x, y, edge ? border : fill);
        }
    }
}

static void menu_layout(menu_t *m)
{
    m->h = m->n > 5 ? 20 : m->n > 3 ? 24 : 30;
    m->gap = m->n > 5 ? 4 : m->n > 3 ? 6 : 12;
    int total = m->n * m->h + (m->n - 1) * m->gap;
    m->y0 = 40 + (152 - total) / 2;
}

static void menu_draw(const menu_t *m, const char *prompt)
{
    if (sTiles == NULL)
        return;
    memset(sTiles, 0, 32 * 24 * 64);
    int pw = text_width(&sBold, prompt) + 20;
    panel_rect(128 - pw / 2, 8, 128 + pw / 2, 30, IX_PROMPT, IX_PROMPT);
    text_draw8(&sBold, sTiles, 128 - text_width(&sBold, prompt) / 2, 12, prompt, RAMP_PROMPT);
    for (int i = 0; i < m->n; i++)
    {
        int y = m->y0 + i * (m->h + m->gap);
        bool hi = i == m->sel;
        int fill = m->locked[i] ? IX_LOCK_FILL : hi ? IX_HI_FILL : IX_FILL;
        int border = m->locked[i] ? IX_LOCK_BORDER : hi ? IX_HI_BORDER : IX_BORDER;
        int r = m->locked[i] ? RAMP_LOCK : hi ? RAMP_HI : RAMP_TEXT;
        panel_rect(16, y, 239, y + m->h, fill, border);
        text_draw8(&sFont, sTiles, 128 - text_width(&sFont, m->opt[i]) / 2, y + (m->h - sFont.h) / 2 + 1,
                   m->opt[i], r);
    }
    // the overlay layer is only shown while the reels are stopped: a plain CPU copy is fine
    u32 *dst = (u32 *)reels_overlay_tiles();
    const u32 *src = (const u32 *)sTiles;
    for (int i = 0; i < 32 * 24 * 64 / 4; i++)
        dst[i] = src[i];
}

static int menu_next(const menu_t *m, int from, int dir)
{
    for (int k = 1; k <= m->n; k++)
    {
        int i = (from + dir * k + m->n * 2) % m->n;
        if (!m->locked[i])
            return i;
    }
    return from;
}

// blocking choice on the bottom screen: touch, or up/down + A
static int menu_run(menu_t *m, const char *prompt)
{
    menu_layout(m);
    if (m->locked[m->sel])
        m->sel = menu_next(m, m->sel, 1);
    menu_draw(m, prompt);
    reels_overlay_show(sPal);
    int pressed = -1;
    while (1)
    {
        swiWaitForVBlank();
        date_tick();
        scanKeys();
        u32 down = keysDown(), held = keysHeld();
        if (down & (KEY_UP | KEY_DOWN))
        {
            m->sel = menu_next(m, m->sel, down & KEY_UP ? -1 : 1);
            sfx_play(SFX_UI_CURSOR);
            menu_draw(m, prompt);
        }
        if (down & KEY_A)
            break;
        if (held & KEY_TOUCH)
        {
            touchPosition t;
            touchRead(&t);
            int hit = -1;
            for (int i = 0; i < m->n; i++)
            {
                int y = m->y0 + i * (m->h + m->gap);
                if (t.px >= 16 && t.px <= 239 && t.py >= y && t.py <= y + m->h && !m->locked[i])
                    hit = i;
            }
            if ((down & KEY_TOUCH) && hit >= 0)
            {
                pressed = hit;
                if (m->sel != hit)
                {
                    m->sel = hit;
                    menu_draw(m, prompt);
                }
                sfx_play(SFX_UI_CURSOR);
            }
            else if (pressed >= 0 && hit != pressed)
            {
                pressed = -1; // slid off the button: cancel
            }
        }
        else if (pressed >= 0)
        {
            break; // released on the button
        }
    }
    sfx_play(SFX_UI_OK);
    reels_overlay_hide();
    return m->sel;
}

// ---------------------------------------------------------------- shared panels (bonus notice choice, zones)

int date_menu(const char *prompt, const char *const *options, int n)
{
    menu_t m = { .n = n > MAX_OPTIONS ? MAX_OPTIONS : n };
    for (int i = 0; i < m.n; i++)
        m.opt[i] = options[i];
    return menu_run(&m, prompt);
}

void date_panel(const char *title, const char *line1, const char *line2)
{
    if (sTiles == NULL)
        return;
    menu_t m = { .n = 0 };
    menu_draw(&m, title); // clears the overlay and draws the title
    int y = 70;
    for (const char *l = line1; l; l = l == line1 ? line2 : NULL)
    {
        int w = text_width(&sBold, l) + 24;
        panel_rect(128 - w / 2, y, 128 + w / 2, y + 26, IX_FILL, IX_BORDER);
        text_draw8(&sBold, sTiles, 128 - text_width(&sBold, l) / 2, y + 6, l, RAMP_TEXT);
        y += 40;
    }
    u32 *dst = (u32 *)reels_overlay_tiles();
    const u32 *src = (const u32 *)sTiles;
    for (int i = 0; i < 32 * 24 * 64 / 4; i++)
        dst[i] = src[i];
}

void date_panel_show(const char *title, const char *line1, const char *line2)
{
    date_panel(title, line1, line2);
    reels_overlay_show(sPal);
}

void date_panel_hide(void)
{
    reels_overlay_hide();
}

// ---------------------------------------------------------------- script

void date_init(void)
{
    static bool fonts;
    if (!fonts)
    {
        text_font_init(&sFont, guide_font_bin, GUIDE_FONT_H);
        text_font_init(&sBold, guide_bold_bin, GUIDE_BOLD_H);
        palette_init();
        sTiles = malloc(32 * 24 * 64); // kept for the whole run (48 KB)
        fonts = true;
    }
    for (int i = 0; i < 3; i++)
    {
        SpriteSize size = i ? SpriteSize_64x32 : SpriteSize_64x64;
        int bytes = i ? 1024 : 2048;
        sIconGfx[i] = oamAllocateGfx(&oamMain, size, SpriteColorFormat_16Color);
        dmaCopy(date_icons_tiles_bin + (i ? 2048 + (i - 1) * 1024 : 0), sIconGfx[i], bytes);
    }
    dmaCopy(date_icons_pal_bin, &SPRITE_PALETTE[PAL_ICON_HEART * 16], 3 * 32);
    hide_icons();
    sRoute = NULL;
}

static const char *T(int i)
{
    return i >= 0 ? sRoute->text[i] : "";
}

static void run_beat(const date_beat_t *b);
static void start(const date_route_t *r);

static void ask(const date_beat_t *b)
{
    menu_t m = { .n = b->nchoice };
    for (int i = 0; i < m.n; i++)
        m.opt[i] = T(sRoute->choices[b->choice0 + i].text);
    int c = menu_run(&m, "What do you say?");
    const date_choice_t *ch = &sRoute->choices[b->choice0 + c];
    sScore += ch->delta;
    show(b->still, T(ch->reply));
    show_icon(ch->delta);
}

static bool places_left(void)
{
    return sVisited != (1u << sRoute->nplaces) - 1;
}

static void pick_place(const date_beat_t *b)
{
    menu_t m = { 0 };
    int map[MAX_OPTIONS];
    for (int p = 0; p < sRoute->nplaces && m.n < MAX_OPTIONS; p++)
        if (!(sVisited & (1u << p)))
        {
            map[m.n] = p;
            m.opt[m.n++] = T(sRoute->choices[sRoute->placeChoice0 + p].text);
        }
    int p = map[menu_run(&m, "Where to next?")];
    sVisited |= 1u << p;
    sPlace = p;
    sLastPlace = p;
    sPlaceStep = sRoute->placeStart[p];
    show(b->still, T(b->reply));
}

static void run_beat(const date_beat_t *b)
{
    switch (b->kind)
    {
        case BEAT_LINE:
            show(b->still, T(b->text));
            break;
        case BEAT_ASK:
            show(b->still, T(b->text));
            ask(b);
            break;
        case BEAT_PLACES:
            show(b->still, T(b->text));
            pick_place(b);
            break;
    }
}

void date_advance(void)
{
    if (sRoute == NULL)
        return;
    hide_icons();
    // inside a place: its beats first
    if (sPlace >= 0)
    {
        run_beat(&sRoute->places[sPlaceStep++]);
        if (sPlaceStep >= sRoute->placeStart[sPlace + 1])
            sPlace = -1;
        return;
    }
    while (sStep < sRoute->nscript)
    {
        const date_beat_t *b = &sRoute->script[sStep++];
        if (b->kind == BEAT_PLACES && !places_left())
            continue;
        run_beat(b);
        return;
    }
    // script done: small talk until the BIG ends
    int k = rand() % sRoute->nfiller;
    show(sRoute->filler[k * 2], T(sRoute->filler[k * 2 + 1]));
}

bool date_pick_partner(void)
{
    menu_t m = { .n = NPARTNERS + 1 };
    static char label[NPARTNERS][48];
    for (int i = 0; i < NPARTNERS; i++)
    {
        int confessed = 0;
        for (int g = 0; g < PARTNER_KLEIN; g++)
            confessed += save_owned(SAVE_CONFESSED + g);
        if (i == PARTNER_KLEIN && confessed < KLEIN_UNLOCK)
        {
            snprintf(label[i], sizeof(label[i]), "Klein (%d/%d confessions)", confessed, KLEIN_UNLOCK);
            m.opt[i] = label[i];
            m.locked[i] = true;
        }
        else
        {
            m.opt[i] = sPartners[i].name;
        }
    }
    m.opt[NPARTNERS] = "No date this time";
    int c = menu_run(&m, "Who's your date today?");
    if (c >= NPARTNERS)
        return false;
    start(sPartners[c].route);
    sPartner = c;
    return true;
}

// tap / A / START, or a few seconds
static void wait_advance(int frames)
{
    for (int f = 0; f < frames; f++)
    {
        swiWaitForVBlank();
        date_tick();
        scanKeys();
        if (f > 20 && (keysDown() & (KEY_TOUCH | KEY_A | KEY_START)))
            break;
    }
}

int date_score(void)
{
    return sScore;
}

void date_set_hot(bool hot)
{
    sHot = hot;
}

static void start(const date_route_t *r)
{
    sRoute = r;
    sStep = 0;
    sPlace = -1;
    sVisited = 0;
    sScore = 0;
    sHot = false;
    sLastPlace = -1;
    sPartner = -1;
    hide_icons();
    date_advance();
}

void date_begin_mode(void)
{
    start(&s_dm_route);
}

static bool play_end(int end, bool love)
{
    // group date: the girl of the last place visited (one ending set per place)
    if (sRoute->endSets > 1)
        end += 3 * (sLastPlace >= 0 && sLastPlace < sRoute->endSets ? sLastPlace : sRoute->endSets - 1);
    hide_icons();
    for (int i = 0; i < sRoute->nending[end]; i++)
    {
        const date_end_t *e = &sRoute->ending[end][i];
        show(e->still, T(e->text));
        wait_advance(e->text >= 0 ? 240 : 120);
    }
    if (love)
    {
        show(sRoute->nending[end] ? sRoute->ending[end][sRoute->nending[end] - 1].still : 0, sRoute->reward);
        sfx_play(SFX_BIG_START);
        wait_advance(240);
    }
    sRoute = NULL;
    sHot = false;
    return love;
}

bool date_finale(void)
{
    if (sRoute == NULL)
        return false;
    int chance = LOVE_BASE + LOVE_STEP * sScore;
    chance = chance < 5 ? 5 : chance > 95 ? 95 : chance;
    bool love = rand() % 100 < chance;
    if (love && sPartner >= 0 && sPartner < PARTNER_KLEIN)
        save_unlock(SAVE_CONFESSED + sPartner);
    return play_end(!love ? DATE_END_FAIL : rand() % 3 == 0 ? DATE_END_NIGHT : DATE_END_LOVE, love);
}

void date_finale_mode(bool won)
{
    if (sRoute != NULL)
        play_end(won ? DATE_END_LOVE : DATE_END_FAIL, won);
}
