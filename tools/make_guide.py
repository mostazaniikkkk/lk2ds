"""HOW TO PLAY guide: controls page + the 27 play-guide cards of the app (Sozai04_Play01..05), translated.

Text is drawn at runtime with a small anti-aliased font, so 28 pages cost a few KB of RAM.
Outputs (game/data):
  guide_bg_lz.bin     bottom screen page background (LZ77, BGR555): panel, arrows, BACK button
  guide_font.bin      glyphs 32..126: u8 width[95], then 4-bit alpha rows (FONT_H rows, ceil(w/2) bytes each)
  guide_bold.bin      same for titles
  game/include/guide_pages.h   page titles / pre-wrapped lines, layout rectangles
Markup inside lines: '^' toggles the highlight colour (feature names, like the cyan text on the cards).
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from settings_art import *
import lz77

OUT = os.path.join(HERE, '..', 'game', 'data')
INC = os.path.join(HERE, '..', 'game', 'include')
PRE = os.path.join(HERE, '..', 'build')

FONT_H, BOLD_H = 11, 14
TEXT_X, TEXT_Y, TEXT_W, LINE_H = 12, 30, 232, 11
MAX_LINES = 11
AA = '^ABSOLUTE AIRSPACE^'
UAA = '^URA ABSOLUTE AIRSPACE^'

# (title, [bullets]); the controls page first, then cards 1/27 .. 27/27
PAGES = [
    ('HOW TO PLAY', [
        'MAX BET: R / L / Up, or tap the BET button.',
        'LEVER: Down, or tap the lever.',
        'STOP: Y / B / A, or tap each STOP button.',
        'CHANCE: X, or tap the PUSH button.',
        'MENU: START while the reels are stopped.',
        'Line up 7-7-7 for BIG and BAR-BAR-BAR for REG. Reels slide up to 4 symbols: aim for the 7!',
        'The pages that follow are the original play guide of the machine.',
    ]),
    ('NORMAL MODE', [
        'Aim for a bonus.',
        'Bonuses come from single hits, reach patterns and stacked small wins, in a good balance.',
        'WATERMELON can lead to ^Klein Stage^ (high chance), CHERRY to ^Date Mode^ (super high chance).',
        f'A bonus in high-chance mode is a shot at {AA} (ART)! In super high it is almost sure!?',
    ]),
    ('KLEIN STAGE', [
        'A high-chance stage entered on some WATERMELON hits.',
        f'A bonus here is a chance for {AA} (ART)!',
    ]),
    ('DATE MODE', [
        'A super-high-chance stage entered on some CHERRY hits.',
        f'A bonus here makes {AA} (ART) very likely!?',
        'The date location hints at your ^VA Ignition^ chances!?',
    ]),
    ('CONFIRMATION SCREENS', [
        'Even when a REG bonus is announced, maybe...!?',
        'The kind of screen hides a secret...',
    ]),
    ('VA IGNITION', [
        'A rescue mode after a long dry spell in normal play.',
        'Expect medals equal to the number shown, multiplied!',
        '^Super VA Ignition^ triples the expected value!?',
    ]),
    ('BIG BONUS (CHANCE NOTICE)', [
        'A 150-medal bonus.',
        f'The RED 7 "Aim!" cut-in is a chance to stock {AA}!!',
        'When "Aim!" appears, hitting the lever a few times may raise the chance.',
    ]),
    ('BIG BONUS (FULL NOTICE)', [
        f'If the golden shutter closes, {AA} is stocked!?',
    ]),
    ('BIG BONUS (DATE NOTICE)', [
        f'A confession in the last game stocks {AA}!?',
        'New: a date with Klein! The unlock condition is...',
        'The ending screen changes with the character you chose.',
    ]),
    ('REG BONUS', [
        'A 40-medal bonus (8 payouts).',
        'The character who shows up hides a secret...',
    ]),
    ('SCRAMBLE BONUS', [
        'The bonus that decides whether ^SONIC MODE^ goes on!',
        'If the scramble take-off succeeds, you made it!?',
        'Replays are a chance! Lining up RED 7 is red hot!!',
    ]),
    ('STAND-BY', [
        f'After a bonus, a chance to line up the LOVEKYURE logo! A BAR line-up means {UAA} (ART)!?',
        f'CHERRY / WATERMELON draw extra games for {AA}.',
        f'After 50 games on stand-by, {UAA} (ART) is likely!?',
    ]),
    ('ZONE: WEAPON ATTACK (AI)', [
        'Opening bonus zone: +50 games on average (40-400G).',
        'Mash the LEFT button to break through the gates (+10G).',
        'The total added may end up a repdigit...!?',
        'A "PUSH TOGETHER" prompt after it ends is red hot!!',
    ]),
    ('ZONE: WEAPON ATTACK (RION)', [
        'Opening bonus zone: +60 games on average (45-400G).',
        'The longer it lasts, the bigger the add.',
        'The voice you hear when pressing the CENTER button hides a secret...',
        'A "PUSH TOGETHER" prompt after it ends is red hot!!',
    ]),
    ('ZONE: WEAPON ATTACK (TSUGUMI)', [
        'Opening bonus zone: +70 games on average (50-400G).',
        'Hold the RIGHT button to add games.',
        '"+1G", "+3G" and "+5G" are the usual numbers. If any other shows up...',
        'A "PUSH TOGETHER" prompt after it ends is red hot!!',
    ]),
    ('ZONE: ICHIGO ATTACK', [
        'Opening bonus zone: +100 games on average (60-1000G).',
        'Rub the lever to make Ichigo feel good!!',
        'The bigger the heart, the more games are added.',
        "Ichigo's reaction changes with your rubbing timing!?",
    ]),
    ('NYAGOTIATION CHANCE', [
        f'A chance to enter {UAA} (ART).',
        'Mash MAX BET to win over all four Klein members!',
        'Line up the LOVEKYURE logo at the end to succeed!!',
    ]),
    ('ABSOLUTE AIRSPACE', [
        'An ART of 30+ games per set that keeps adding games.',
        'Mostly CHERRY / WATERMELON move you to high chance and draw a ^Dimension Battle^.',
        'High chance from rare wins plus high chance from games played stack into super high!',
        'In super high, any win can bring a ^Dimension Battle^!',
    ]),
    ('URA ABSOLUTE AIRSPACE', [
        'An ART of 30 games per set, with a continuation rate.',
        'Four continuation rates: 50% / 67% / 75% / 90%.',
        'A LOVEKYURE line-up or a middle-line CHERRY likely raises the rate!?',
        'The current stage hints that the set will continue.',
    ]),
    ('DIMENSION BATTLE', [
        'Win the battle for ^Ashitsubo Punish^ or ^Nadenade LoveKyure^!?',
        'Victory is drawn at the start of the battle and on rare wins during it.',
        'A bonus during the battle almost guarantees victory!?',
    ]),
    ('ADD ZONE: ASHITSUBO PUNISH', [
        'Adds +45 games on average (40-500G).',
        'Pick the character to punish and hold the STOP button!!',
        'A two-at-once pattern doubles the expected add!?',
    ]),
    ('ADD ZONE: NADENADE LOVEKYURE', [
        "The machine's strongest add zone: +200 games on average (100-1000G).",
    ]),
    ('SONIC MODE', [
        'Lasts until the next BIG bonus!?',
        'During EX, even a BIG bonus does not end it!?',
        'You can pick your favourite song.',
    ]),
    ('VA IGNITION (ABSOLUTE AIRSPACE)', [
        'The game countdown stops until VA Ignition ends.',
        'Bonuses give generous game adds!',
        'You can pick your favourite song.',
    ]),
    ('SUPER VA IGNITION (ABSOLUTE AIRSPACE)', [
        'The game countdown stops until Super VA Ignition ends.',
        'A bonus adds three-digit games!?',
        'You can pick your favourite song.',
    ]),
    ('VA IGNITION (URA ABSOLUTE AIRSPACE)', [
        'The game countdown stops until VA Ignition ends.',
        f'Bonuses give generous {AA} game adds!',
        'Aim for BAR line-ups to raise the continuation rate.',
        'You can pick your favourite song.',
    ]),
    ('SUPER VA IGNITION (URA ABSOLUTE AIRSPACE)', [
        'The game countdown stops until Super VA Ignition ends.',
        f'A bonus adds three-digit {AA} games!?',
        'Aim for BAR line-ups to raise the continuation rate.',
        'You can pick your favourite song.',
    ]),
    ('ENDING', [
        'You ran the whole advantage section!?',
    ]),
]


def glyphs(font_path, size, h):
    """4-bit alpha glyphs 32..126, baseline-aligned in an h-pixel cell"""
    f = ImageFont.truetype(font_path, size)
    asc, desc = f.getmetrics()
    widths, data = [], b''
    for c in range(32, 127):
        ch = chr(c)
        w = max(1, int(round(f.getlength(ch))))
        img = Image.new('L', (w + 2, h), 0)
        ImageDraw.Draw(img).text((0, h - asc - desc + (asc + desc - h) // 2 + 1), ch, font=f, fill=255)
        img = img.crop((0, 0, w, h))
        a = (np.array(img) >> 4).astype(np.uint8)
        if w & 1:
            a = np.hstack([a, np.zeros((h, 1), np.uint8)])
        packed = (a[:, 0::2] | (a[:, 1::2] << 4)).astype(np.uint8)
        widths.append(w)
        data += packed.tobytes()
    return bytes(widths) + data, f


def measure(text, f):
    return f.getlength(text.replace('^', ''))


def wrap_bullets(bullets, f):
    lines = []
    for b in bullets:
        words, cur, first = b.split(' '), '', True
        for w in words:
            nxt = (cur + ' ' + w).strip()
            if cur and measure(('- ' if first else '  ') + nxt, f) > TEXT_W:
                lines.append(('- ' if first else '  ') + cur)
                first = False
                cur = w
            else:
                cur = nxt
        lines.append(('- ' if first else '  ') + cur)
    return lines


def c_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def main():
    font_bin, f = glyphs(FONT_BOLD, 10, FONT_H)
    bold_bin, fbold = glyphs(FONT_BLACK, 11, BOLD_H)
    with open(os.path.join(OUT, 'guide_font.bin'), 'wb') as fo:
        fo.write(font_bin)
    with open(os.path.join(OUT, 'guide_bold.bin'), 'wb') as fo:
        fo.write(bold_bin)

    # page background: darkened title art + blue panel (card style), arrows, BACK
    art = atlas('sozai01_title/Sozai_Title_01')
    bg = art.crop((470, 1133, 1690, 2048)).resize((256, 192), LZ)
    bg = Image.alpha_composite(bg, Image.new('RGBA', bg.size, (0, 5, 25, 170)))
    d = ImageDraw.Draw(bg)
    d.rounded_rectangle((4, 4, 251, 158), 4, fill=(6, 14, 40, 235), outline=(40, 150, 255, 255), width=2)
    d.rectangle((6, 6, 249, 24), fill=(10, 50, 120, 255))
    d.line((6, 25, 249, 25), fill=(40, 150, 255, 255), width=1)
    left, right = fit_h(cfg2(13), 22), fit_h(cfg2(14), 22)
    prev_rect = (8, 164, left.width + 8, 26)
    next_rect = (248 - right.width - 8, 164, right.width + 8, 26)
    bg.alpha_composite(left, (12, 166))
    bg.alpha_composite(right, (244 - right.width, 166))
    c1 = sprite('sozai99_common01/Sozai99_Common01', 'Sozai99_Common01_3')
    bw = c1.width
    back = fit_h(blank(c1, int(bw * 0.2), int(bw * 0.88), int(bw * 0.19)), 24)
    # BACK label (same style as the menu)
    from make_title import label
    back = fit_h(label(blank(c1, int(bw * 0.2), int(bw * 0.88), int(bw * 0.19)), 'BACK', 58), 24)
    back_rect = ((256 - back.width) // 2, 164, back.width, 24)
    bg.alpha_composite(back, back_rect[:2])
    with open(os.path.join(OUT, 'guide_bg_lz.bin'), 'wb') as fo:
        a = np.array(bg.convert('RGB')).astype(np.uint16) >> 3
        fo.write(lz77.compress((a[:, :, 0] | (a[:, :, 1] << 5) | (a[:, :, 2] << 10) | 0x8000).astype('<u2').tobytes()))

    # pages
    pages = []
    for title, bullets in PAGES:
        lines = wrap_bullets(bullets, f)
        assert len(lines) <= MAX_LINES, (title, len(lines))
        t = title
        while measure(t, fbold) > 236:
            t = t[:-1]
        pages.append((t, lines))
    with open(os.path.join(INC, 'guide_pages.h'), 'w', encoding='utf8') as h:
        h.write('// generated by tools/make_guide.py\n#pragma once\n\n')
        h.write(f'#define GUIDE_PAGES {len(pages)}\n#define GUIDE_FONT_H {FONT_H}\n#define GUIDE_BOLD_H {BOLD_H}\n')
        h.write(f'#define GUIDE_TEXT_X {TEXT_X}\n#define GUIDE_TEXT_Y {TEXT_Y}\n#define GUIDE_LINE_H {LINE_H}\n')
        h.write('#define GUIDE_PREV_RECT {%d, %d, %d, %d}\n#define GUIDE_NEXT_RECT {%d, %d, %d, %d}\n'
                % (prev_rect + next_rect))
        h.write('#define GUIDE_BACK_RECT {%d, %d, %d, %d}\n\n' % back_rect)
        h.write('static const char *const sGuideTitle[GUIDE_PAGES] = {\n')
        for t, _ in pages:
            h.write(f'    {c_str(t)},\n')
        h.write('};\n\nstatic const char *const sGuideLines[GUIDE_PAGES][%d] = {\n' % (MAX_LINES + 1))
        for _, lines in pages:
            h.write('    { ' + ', '.join(c_str(l) for l in lines) + ', NULL },\n')
        h.write('};\n')

    # preview of one page, drawn the way the DS will
    page = bg.copy()
    d = ImageDraw.Draw(page)
    t, lines = pages[18]
    d.text((10, 8), t, font=fbold, fill=(255, 255, 255))
    for i, l in enumerate(lines):
        x = TEXT_X
        for k, part in enumerate(l.split('^')):
            col = (110, 230, 255) if k % 2 else (235, 235, 245)
            d.text((x, TEXT_Y + i * LINE_H), part, font=f, fill=col)
            x += f.getlength(part)
    d.text((200, 147), '19/28', font=f, fill=(255, 255, 255))
    page.convert('RGB').resize((768, 576), Image.NEAREST).save(os.path.join(PRE, 'guide_preview.png'))
    print(len(pages), 'pages; font', len(font_bin), 'B; bold', len(bold_bin), 'B')


if __name__ == '__main__':
    main()
