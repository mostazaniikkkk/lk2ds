#pragma once
#include <nds.h>

// Runtime text from the anti-aliased fonts made by tools/make_guide.py (4-bit coverage per pixel).

typedef struct
{
    const u8 *widths;
    const u8 *glyph[95];
    int h;
} font_t;

void text_font_init(font_t *f, const u8 *data, int h);
// width in pixels of s, up to the end of the string or a '|' (line break)
int text_width(const font_t *f, const char *s);
// blend into a 256-pixel-wide 16-bit bitmap (192 lines); '^' toggles between col and alt; stops at '|'
void text_draw(const font_t *f, u16 *bitmap, int x, int y, const char *s, u16 col, u16 alt);
// write into an 8bpp tiled buffer (32 tiles per row, 24 rows): pixel index = ramp + coverage (1..15),
// so palette entries ramp..ramp+15 must fade from the background to the text colour; stops at '|'
void text_draw8(const font_t *f, u8 *tiles, int x, int y, const char *s, int ramp);
