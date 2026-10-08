#include "text.h"

void text_font_init(font_t *f, const u8 *data, int h)
{
    f->widths = data;
    f->h = h;
    const u8 *p = data + 95;
    for (int i = 0; i < 95; i++)
    {
        f->glyph[i] = p;
        p += h * ((f->widths[i] + 1) / 2);
    }
}

int text_width(const font_t *f, const char *s)
{
    int w = 0;
    for (; *s && *s != '|'; s++)
        if (*s >= 32 && *s < 127)
            w += f->widths[*s - 32];
    return w;
}

static inline int coverage(const u8 *g, int stride, int xx, int yy)
{
    return (g[yy * stride + xx / 2] >> ((xx & 1) * 4)) & 15;
}

// alpha-blend one glyph into the bitmap
static int draw_char(const font_t *f, u16 *bitmap, int x, int y, char c, u16 col)
{
    if (c < 32 || c > 126)
        return 0;
    int i = c - 32, w = f->widths[i], stride = (w + 1) / 2;
    const u8 *g = f->glyph[i];
    int cr = col & 31, cg = (col >> 5) & 31, cb = (col >> 10) & 31;
    for (int yy = 0; yy < f->h; yy++)
    {
        int py = y + yy;
        if (py < 0 || py >= 192)
            continue;
        u16 *row = bitmap + py * 256;
        for (int xx = 0; xx < w; xx++)
        {
            int a = coverage(g, stride, xx, yy);
            int px = x + xx;
            if (a == 0 || px < 0 || px >= 256)
                continue;
            u16 d = row[px];
            int r = ((d & 31) * (15 - a) + cr * a) / 15;
            int gg = (((d >> 5) & 31) * (15 - a) + cg * a) / 15;
            int b = (((d >> 10) & 31) * (15 - a) + cb * a) / 15;
            row[px] = r | (gg << 5) | (b << 10) | BIT(15);
        }
    }
    return w;
}

void text_draw(const font_t *f, u16 *bitmap, int x, int y, const char *s, u16 col, u16 alt)
{
    bool hi = false;
    for (; *s && *s != '|'; s++)
    {
        if (*s == '^')
        {
            hi = !hi;
            continue;
        }
        x += draw_char(f, bitmap, x, y, *s, hi ? alt : col);
    }
}

void text_draw8(const font_t *f, u8 *tiles, int x, int y, const char *s, int ramp)
{
    for (; *s && *s != '|'; s++)
    {
        if (*s < 32 || *s > 126)
            continue;
        int i = *s - 32, w = f->widths[i], stride = (w + 1) / 2;
        const u8 *g = f->glyph[i];
        for (int yy = 0; yy < f->h; yy++)
        {
            int py = y + yy;
            if (py < 0 || py >= 192)
                continue;
            for (int xx = 0; xx < w; xx++)
            {
                int a = coverage(g, stride, xx, yy);
                int px = x + xx;
                if (a == 0 || px < 0 || px >= 256)
                    continue;
                tiles[((py >> 3) * 32 + (px >> 3)) * 64 + (py & 7) * 8 + (px & 7)] = ramp + a;
            }
        }
        x += w;
    }
}
