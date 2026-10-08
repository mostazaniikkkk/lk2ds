"""Date sim (BIG bonus "date notice" route of the app: Sn_Db02 partner select, Sn_Dg01 bus stop,
Sn_Dg02 amusement park, Sn_Dg03 confession) for the DS: art from the GOD scenes, text from
tools/date_text/*.tsv (localized English), script authored here.

The original scenes are templates (question / reaction / location) whose text is picked at runtime by
the sub board, so the order of the lines below is a reinterpretation built from the dialogue banks.

Outputs:
  game/data/date_<route>_<still>_lz.bin   256x144 BGR555 stills (LZ77) without the Japanese text layers;
                                          moved into the asset pack by make_pak.py (DATE_FILES list)
  game/data/date_icons_tiles.bin / _pal   main-engine sprites: heart 64x64, UP 64x32, DOWN 64x32 (4bpp)
  game/include/date_script.h              strings + beats per route
  tools/date_files.txt                     pack entries (read by make_pak.py)
Previews: build/date_<route>.png
"""
import os, sys, csv
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'extract'))
from paths import DATA as ROOT  # original game data (extract/paths.py)
sys.path.insert(0, HERE)
import god_render as g
import lz77
from make_assets import quantize, rgb555

OUT = os.path.join(HERE, '..', 'game', 'data')
INC = os.path.join(HERE, '..', 'game', 'include')
PRE = os.path.join(HERE, '..', 'build')
TXT = os.path.join(HERE, 'date_text')

# resource banks holding Japanese text (dialogue, name plate box, choices, cursor, prompt)
TEXT_RSC = {930, 936, 927, 928, 368, 369} | set(range(954, 962))


def rsc(an, f):
    psz, dur, pt, pid = an
    if pt == 1:
        return set()
    p = g.T['PTN'][g.T['PTNTBL'][pid + (f % psz)][0]]
    return {g.T['PRM'][g.T['PRMTBL'][q][0]][0] for q in range(p[1], p[1] + p[0])}


DM_POSE = 0x2037  # Date Mode Ichigo (RSC 342), as in Sn_Ba02_0911


def title_card():
    """Date Mode title (Sn_Ba01_0001) with the Japanese logo replaced by English text"""
    from PIL import ImageDraw, ImageFont, ImageFilter
    bg = g.load_image(0x2318).convert('RGBA')
    f = ImageFont.truetype('C:/Windows/Fonts/ariblk.ttf' if os.name == 'nt' else
                           '/usr/share/fonts/truetype/msttcorefonts/Arial_Black.ttf', 120)
    txt = 'DATE MODE'
    d = ImageDraw.Draw(bg)
    w = d.textlength(txt, font=f)
    x, y = (1024 - w) / 2, 200
    glow = Image.new('RGBA', bg.size, (0, 0, 0, 0))
    ImageDraw.Draw(glow).text((x, y), txt, font=f, fill=(255, 40, 120, 255), stroke_width=18,
                              stroke_fill=(255, 40, 120, 255))
    bg.alpha_composite(glow.filter(ImageFilter.GaussianBlur(14)))
    d.text((x, y), txt, font=f, fill=(255, 255, 255), stroke_width=10, stroke_fill=(190, 20, 90))
    return bg.convert('RGB')


def still(spec):
    """256x144 RGB still: (scene, frame, {image id: replacement id}), ('image', id), ('pose', bg, pose),
    ('movie', id, frame), ('title',) or None (black)"""
    if spec is None:
        return Image.new('RGB', (256, 144))
    if spec[0] == 'image':
        return g.load_image(spec[1]).convert('RGB').resize((256, 144), Image.LANCZOS)
    if spec[0] == 'pose':  # Date Mode: location background + Ichigo
        c = g.load_image(spec[1]).convert('RGBA').resize((1024, 576), Image.LANCZOS)
        pose = g.load_image(spec[2])
        c.alpha_composite(pose, ((1024 - pose.width) // 2, 576 - pose.height))
        return c.convert('RGB').resize((256, 144), Image.LANCZOS)
    if spec[0] == 'over':  # transparent close-up over a background
        c = g.load_image(spec[1]).convert('RGBA').resize((1024, 576), Image.LANCZOS)
        c.alpha_composite(g.load_image(spec[2]).resize((1024, 576), Image.LANCZOS))
        return c.convert('RGB').resize((256, 144), Image.LANCZOS)
    if spec[0] == 'movie':
        return g.movie_frame(spec[1], spec[2]).convert('RGB').resize((256, 144), Image.LANCZOS)
    if spec[0] == 'title':
        return title_card().resize((256, 144), Image.LANCZOS)
    scene, f, swap = spec
    saved = {}
    for a, b in (swap or {}).items():  # runtime-selected art: draw another image in place of the template one
        saved[a] = g._img_cache.get(a)
        g._img_cache[a] = g.load_image(b).resize(g.T['IMA'][a][1:3], Image.LANCZOS)
    ls = [l for l in g.scene_layers([scene]) if not (rsc(l[2], max(0, f - l[6])) & TEXT_RSC)]
    im = g.compose(ls, f).convert('RGB').resize((256, 144), Image.LANCZOS)
    for a, v in saved.items():
        if v is None:
            g._img_cache.pop(a, None)
        else:
            g._img_cache[a] = v
    return im


def bgr555(img):
    a = np.array(img).astype(np.uint16) >> 3
    return (a[:, :, 0] | (a[:, :, 1] << 5) | (a[:, :, 2] << 10) | 0x8000).astype('<u2')


def load_text(names):
    rows = {}
    for name in names if isinstance(names, list) else [names]:
        with open(os.path.join(TXT, name), encoding='utf8') as f:
            for r in csv.reader((l for l in f if l.strip() and not l.startswith('#')), delimiter='\t'):
                if len(r) > 1 and r[1].strip().isdigit():
                    rows[(r[0], int(r[1]))] = r[3] if len(r) > 3 else ''
    return rows


# ---------------------------------------------------------------- routes
# beats: ('line', still, text)                      one game
#        ('ask', still, text, [(choice, reply, delta), ...])   question; the reply is the next beat
#        ('places', still, text, reply)              pick the next location among those not visited yet
# blocks: per location, played when picked; filler: random lines once the script runs out;
# endings: shown after the BIG (advanced by tapping): 'love' (parade), 'night' (CELF base), 'fail'.
ROUTES = {
    'ichigo': dict(
        name='Ichigo',
        text='big_ichigo.tsv',
        reward='[She confessed! ABSOLUTE AIRSPACE stocked!]',
        stills={
            'bus': ('Sn_Dg01_1011', 30, None),
            'park': ('Sn_Dg02_1003', 25, None),
            'park_eve': ('Sn_Dg02_1003', 25, {0xB775: 0xB774}),
            'rest': ('Sn_Dg02_1302', 25, {0xB770: 0xB772}),
            'haunted': ('Sn_Dg02_1302', 25, None),
            'fortune': ('Sn_Dg02_1302', 25, {0xB770: 0xB773}),
            'ferris': ('Sn_Dg02_1302', 25, {0xB770: 0xB771}),
            'cg_lunch': ('Sn_Dg02_1220', 50, None),
            'cg_haunted': ('image', 0x43EE),
            'cg_fortune': ('Sn_Dg02_1420', 50, None),
            'cg_ferris': ('Sn_Dg02_1520', 50, None),
            'parade': ('Sn_Dg03_1002', 25, None),
            'confess': ('Sn_Dg03_1102', 25, None),
            'love': ('Sn_Dg03_1111', 25, None),
            'congrats': ('Sn_Dg03_1113', 90, None),
            'base': ('Sn_Dg03_1201', 5, None),
            'base_confess': ('Sn_Dg03_1203', 25, None),
            'dark': None,
        },
        script=[
            ('line', 'bus', 'I0'),
            ('ask', 'bus', 'I1', [('C0', 'I2', -1), ('C1', 'I3', 1), ('C2', 'I4', 1)]),
            ('ask', 'bus', 'I5', [('C3', 'I6', -1), ('C4', 'I7', 1), ('C5', 'I8', 0)]),
            ('ask', 'bus', 'I9', [('C6', 'I11', 1), ('C7', 'I12', 1), ('C8', 'I10', 0)]),
            ('line', 'bus', 'I14'),
            ('line', 'bus', 'I15'),
            ('line', 'park', 'I16'),
            ('ask', 'park', 'I17', [('C9', 'I18', -1), ('C10', 'I19', 0), ('C11', 'I20', 1)]),
            ('ask', 'park', 'I21', [('C12', 'I22', -1), ('C13', 'I23', 1), ('C14', 'I24', 0)]),
            ('places', 'park', 'I25', 'I26'),
            ('ask', 'park', 'I47', [('C22', 'I49', 0), ('C24', 'I48', 1), ('C23', 'I50', -1)]),
            ('places', 'park', 'I25', 'I26'),
            ('line', 'park', 'I13'),
            ('places', 'park', 'I25', 'I26'),
            ('places', 'park', 'I25', 'I26'),
        ],
        places=[
            ('C15', [('line', 'rest', 'I27'), ('line', 'rest', 'I28'), ('line', 'cg_lunch', 'I32'),
                     ('line', 'cg_lunch', 'I33'), ('line', 'rest', 'I34')]),
            ('C16', [('line', 'haunted', 'I35'), ('line', 'haunted', 'I36'), ('line', 'cg_haunted', 'I37'),
                     ('line', 'haunted', 'I38')]),
            ('C17', [('line', 'fortune', 'I39'), ('line', 'cg_fortune', 'I40'), ('line', 'cg_fortune', 'I41'),
                     ('line', 'fortune', 'I42')]),
            ('C18', [('line', 'ferris', 'I43'),
                     ('ask', 'cg_ferris', 'I44', [('C25', 'I46', 0), ('C26', 'I52', 0), ('C27', 'I45', 1)])]),
        ],
        filler=[('park', t) for t in ('I51', 'I54', 'I55', 'I56', 'I57', 'I58', 'I59', 'I60', 'I61')],
        endings={
            'love': [('park_eve', 'I62'), ('parade', 'I64'), ('parade', 'I65'), ('parade', 'I67'),
                     ('confess', 'I69'), ('confess', 'I70'), ('love', 'I72'), ('congrats', '')],
            'night': [('park_eve', 'I62'), ('park_eve', 'I63'), ('base', 'I73'), ('base_confess', 'I69'),
                      ('base_confess', 'I70'), ('love', 'I72'), ('congrats', '')],
            'fail': [('park_eve', 'I62'), ('parade', 'I64'), ('parade', 'I66'), ('confess', 'I69'),
                     ('confess', 'I70'), ('parade', 'I71'), ('dark', 'N0')],
        },
    ),
}
def std_stills(c, haunted):
    """the BIG date scenes of girl c (scene digit: 1 Ichigo, 2 Ai, 3 Rion, 4 Tsugumi), as in Ichigo's route"""
    return {
        'bus': (f'Sn_Dg01_{c}011', 30, None),
        'park': (f'Sn_Dg02_{c}003', 25, None),
        'park_eve': (f'Sn_Dg02_{c}003', 25, {0xB775: 0xB774}),
        'rest': (f'Sn_Dg02_{c}302', 25, {0xB770: 0xB772}),
        'haunted': (f'Sn_Dg02_{c}302', 25, None),
        'fortune': (f'Sn_Dg02_{c}302', 25, {0xB770: 0xB773}),
        'ferris': (f'Sn_Dg02_{c}302', 25, {0xB770: 0xB771}),
        'cg_lunch': (f'Sn_Dg02_{c}220', 50, None),
        'cg_haunted': ('image', haunted),
        'cg_fortune': (f'Sn_Dg02_{c}420', 50, None),
        'cg_ferris': (f'Sn_Dg02_{c}520', 50, None),
        'parade': (f'Sn_Dg03_{c}002', 25, None),
        'confess': (f'Sn_Dg03_{c}102', 25, None),
        'love': (f'Sn_Dg03_{c}111', 25, None),
        'congrats': (f'Sn_Dg03_{c}113', 90, None),
        'base': (f'Sn_Dg03_{c}201', 5, None),
        'base_confess': (f'Sn_Dg03_{c}203', 25, None),
        'dark': None,
    }


def std_endings(eve, eve2, parade, confess, fake, love, base):
    """parade[0..2]: lines at the parade; confess: the two hesitant lines"""
    return {
        'love': [('park_eve', eve)] + [('parade', t) for t in parade] +
                [('confess', t) for t in confess] + [('love', love), ('congrats', '')],
        'night': [('park_eve', eve), ('park_eve', eve2), ('base', base)] +
                 [('base_confess', t) for t in confess] + [('love', love), ('congrats', '')],
        'fail': [('park_eve', eve), ('parade', parade[0]), ('parade', parade[1])] +
                [('confess', t) for t in confess] + [('parade', fake), ('dark', 'N0')],
    }


REWARD = '[She confessed! ABSOLUTE AIRSPACE stocked!]'

ROUTES['ai'] = dict(
    name='Ai', prefix='AI', text=['big_ai.tsv', 'big_choices.tsv'], reward=REWARD,
    stills=std_stills(2, 0x43E4),
    script=[
        ('line', 'bus', 'G0'),
        ('ask', 'bus', 'G1', [('C29', 'G3', 0), ('C30', 'G4', 1), ('C31', 'G8', -1)]),
        ('ask', 'bus', 'G5', [('C32', 'G7', 0), ('C33', 'G6', 1), ('C37', 'G2', -1)]),
        ('line', 'bus', 'G9'),
        ('ask', 'bus', 'G12', [('C35', 'G14', 1), ('C36', 'G13', 1), ('C34', 'G15', -1)]),
        ('line', 'bus', 'G16'),
        ('line', 'park', 'G17'),
        ('ask', 'park', 'G18', [('C38', 'G19', 0), ('C42', 'G11', 1), ('C39', 'G21', 0)]),
        ('ask', 'park', 'G22', [('C40', 'G24', 1), ('C41', 'G25', 0), ('C55', 'G23', 1)]),
        ('places', 'park', 'G26', 'G27'),
        ('ask', 'park', 'G48', [('C51', 'G49', 0), ('C53', 'G50', 1), ('C52', 'G51', -1)]),
        ('places', 'park', 'G26', 'G27'),
        ('line', 'park', 'G10'),
        ('places', 'park', 'G26', 'G27'),
        ('places', 'park', 'G26', 'G27'),
    ],
    places=[
        ('C43', [('line', 'rest', 'G32'), ('line', 'rest', 'G33'), ('line', 'cg_lunch', 'G34'),
                 ('line', 'rest', 'G35')]),
        ('C44', [('line', 'haunted', 'G36'), ('line', 'haunted', 'G37'), ('line', 'cg_haunted', 'G38'),
                 ('line', 'haunted', 'G39')]),
        ('C45', [('line', 'fortune', 'G40'), ('line', 'cg_fortune', 'G41'), ('line', 'cg_fortune', 'G42'),
                 ('line', 'fortune', 'G43')]),
        ('C46', [('line', 'ferris', 'G44'),
                 ('ask', 'cg_ferris', 'G45', [('C54', 'G47', 0), ('C55', 'G54', 1), ('C56', 'G46', 1)])]),
    ],
    filler=[('park', t) for t in ('G52', 'G53', 'G56', 'G57', 'G58', 'G59', 'G60', 'G61')],
    endings=std_endings('G62', 'G63', ['G64', 'G65', 'G66'], ['G69', 'G70'], 'G71', 'G72', 'G73'),
)

ROUTES['rion'] = dict(
    name='Rion', prefix='RION', text=['big_rion.tsv', 'big_choices.tsv'], reward=REWARD,
    stills=std_stills(3, 0x4401),
    script=[
        ('line', 'bus', 'G0'),
        ('ask', 'bus', 'G1', [('C58', 'G3', 1), ('C59', 'G4', 1), ('C60', 'G2', -1)]),
        ('ask', 'bus', 'G10', [('C61', 'G8', -1), ('C62', 'G5', 1), ('C63', 'G7', 0)]),
        ('ask', 'bus', 'G12', [('C64', 'G14', 0), ('C65', 'G13', 1), ('C66', 'G20', -1)]),
        ('line', 'bus', 'G9'),
        ('line', 'bus', 'G16'),
        ('line', 'park', 'G17'),
        ('ask', 'park', 'G18', [('C67', 'G19', 0), ('C71', 'G23', 1), ('C68', 'G22', 0)]),
        ('ask', 'park', 'G21', [('C69', 'G25', 1), ('C70', 'G24', 0), ('C66', 'G20', -1)]),
        ('places', 'park', 'G26', 'G27'),
        ('ask', 'park', 'G48', [('C81', 'G49', 0), ('C82', 'G50', 1), ('C80', 'G51', -1)]),
        ('places', 'park', 'G26', 'G27'),
        ('line', 'park', 'G11'),
        ('places', 'park', 'G26', 'G27'),
        ('places', 'park', 'G26', 'G27'),
    ],
    places=[
        ('C72', [('line', 'rest', 'G32'), ('line', 'rest', 'G33'), ('line', 'cg_lunch', 'G34'),
                 ('line', 'rest', 'G35')]),
        ('C73', [('line', 'haunted', 'G36'), ('line', 'haunted', 'G37'), ('line', 'cg_haunted', 'G38'),
                 ('line', 'haunted', 'G39')]),
        ('C74', [('line', 'fortune', 'G40'), ('line', 'cg_fortune', 'G41'), ('line', 'cg_fortune', 'G42'),
                 ('line', 'fortune', 'G43')]),
        ('C75', [('line', 'ferris', 'G44'),
                 ('ask', 'cg_ferris', 'G45', [('C83', 'G59', 0), ('C84', 'G47', -1), ('C85', 'G46', 1)])]),
    ],
    filler=[('park', t) for t in ('G52', 'G53', 'G54', 'G55', 'G56', 'G57', 'G58', 'G60')],
    endings=std_endings('G61', 'G62', ['G63', 'G64', 'G66'], ['G68', 'G69'], 'G70', 'G71', 'G72'),
)

ROUTES['tsugumi'] = dict(
    name='Tsugumi', prefix='TSUGUMI', text=['big_tsugumi.tsv', 'big_choices.tsv'], reward=REWARD,
    stills=std_stills(4, 0x43F8),
    script=[
        ('line', 'bus', 'G0'),
        ('ask', 'bus', 'G1', [('C86', 'G2', 1), ('C87', 'G3', 0), ('C88', 'G4', -1)]),
        ('ask', 'bus', 'G5', [('C89', 'G8', -1), ('C90', 'G7', 0), ('C91', 'G6', 1)]),
        ('line', 'bus', 'G9'),
        ('ask', 'bus', 'G12', [('C92', 'G15', -1), ('C93', 'G14', 0), ('C94', 'G13', 1)]),
        ('line', 'bus', 'G16'),
        ('line', 'park', 'G17'),
        ('ask', 'park', 'G18', [('C95', 'G21', 0), ('C97', 'G20', 0), ('C96', 'G22', -1)]),
        ('ask', 'park', 'G23', [('C98', 'G25', 1), ('C99', 'G26', 0), ('C100', 'G24', 1)]),
        ('places', 'park', 'G27', 'G28'),
        ('ask', 'park', 'G49', [('C108', 'G50', 0), ('C110', 'G51', 1), ('C109', 'G52', -1)]),
        ('places', 'park', 'G27', 'G28'),
        ('line', 'park', 'G10'),
        ('places', 'park', 'G27', 'G28'),
        ('places', 'park', 'G27', 'G28'),
    ],
    places=[
        ('C101', [('line', 'rest', 'G33'), ('line', 'rest', 'G34'), ('line', 'cg_lunch', 'G35'),
                  ('line', 'rest', 'G36')]),
        ('C102', [('line', 'haunted', 'G37'), ('line', 'haunted', 'G38'), ('line', 'cg_haunted', 'G39'),
                  ('line', 'haunted', 'G40')]),
        ('C103', [('line', 'fortune', 'G41'), ('line', 'cg_fortune', 'G42'), ('line', 'cg_fortune', 'G43'),
                  ('line', 'fortune', 'G44')]),
        ('C106', [('line', 'ferris', 'G45'), ('line', 'cg_ferris', 'G46'),
                  ('ask', 'cg_ferris', 'G53', [('C111', 'G48', 0), ('C112', 'G54', 1), ('C113', 'G56', 1)])]),
    ],
    filler=[('park', t) for t in ('G55', 'G57', 'G58', 'G59', 'G60', 'G61', 'G62')],
    endings=std_endings('G63', 'G64', ['G65', 'G66', 'G67'], ['G70', 'G71'], 'G72', 'G73', 'G74'),
)


def klein_end(c, eve, parade, confess, fake, love, base, base_lines):
    return {
        'love': [('park_eve', eve)] + [(f'parade{c}', t) for t in parade] +
                [(f'confess{c}', t) for t in confess] + [(f'love{c}', love), (f'congrats{c}', '')],
        'night': [('park_eve', eve), (f'base{c}', base)] + [(f'base_confess{c}', t) for t in base_lines] +
                 [(f'love{c}', love), (f'congrats{c}', '')],
        'fail': [('park_eve', eve), (f'parade{c}', parade[0])] + [(f'confess{c}', t) for t in confess] +
                [(f'parade{c}', fake), ('dark', 'N0')],
    }


# Klein group date (Sn_Dg01/02 5xxx, endings Dg03 5xxx Maia, 6xxx Short, 7xxx Fold, 8xxx Koon): all four come;
# each location belongs to one of them and the girl of the last place visited is the one who confesses.
_ks = {
    'bus': ('Sn_Dg01_5011', 30, None), 'park': ('Sn_Dg02_5005', 25, None), 'park_eve': ('Sn_Dg02_5009', 25, None),
    'rest': ('Sn_Dg02_5202', 25, None), 'cg_lunch': ('Sn_Dg02_5210', 50, None),
    'haunted': ('Sn_Dg02_5302', 25, None), 'cg_haunted': ('image', 0x440B),
    'fortune': ('Sn_Dg02_5402', 25, None), 'cg_fortune': ('Sn_Dg02_5410', 50, None),
    'ferris': ('Sn_Dg02_5502', 25, None), 'cg_ferris': ('Sn_Dg02_5510', 50, None),
    'dark': None,
}
for _c in '5678':
    _ks.update({f'parade{_c}': (f'Sn_Dg03_{_c}002', 25, None), f'confess{_c}': (f'Sn_Dg03_{_c}102', 25, None),
                f'love{_c}': (f'Sn_Dg03_{_c}111', 25, None), f'congrats{_c}': (f'Sn_Dg03_{_c}113', 90, None),
                f'base{_c}': (f'Sn_Dg03_{_c}201', 5, None), f'base_confess{_c}': (f'Sn_Dg03_{_c}203', 25, None)})
ROUTES['klein'] = dict(
    name='Klein', prefix='MAIA', text=['big_klein.tsv', 'big_choices.tsv'], reward=REWARD, stills=_ks,
    script=[
        ('line', 'bus', 'S0'),
        ('ask', 'bus', 'M0', [('C114', 'F0', 0), ('C115', 'M1', 1), ('C116', 'S1', 0)]),
        ('line', 'bus', 'F2'),
        ('line', 'bus', 'S2'),
        ('line', 'bus', 'K2'),
        ('ask', 'park', 'M2', [('C117', 'S4', 1), ('C118', 'K0', 0), ('C119', 'M4', 1)]),
        ('ask', 'park', 'M3', [('C120', 'M5', 1), ('C121', 'S3', 0), ('C122', 'M1', -1)]),
        ('ask', 'park', 'F3', [('C123', 'S13', 0), ('C124', 'K3', 0), ('C125', 'F4', 1)]),
        ('places', 'park', 'K5', 'M6'),
        ('line', 'park', 'F14'),
        ('places', 'park', 'K5', 'M6'),
        ('line', 'park', 'M7'),
        ('places', 'park', 'K5', 'M6'),
        ('places', 'park', 'K5', 'M6'),
    ],
    places=[
        ('C129', [('line', 'rest', 'F5'), ('line', 'rest', 'F6'), ('line', 'cg_lunch', 'F8'),
                  ('line', 'cg_lunch', 'F9'), ('line', 'rest', 'F7')]),
        ('C128', [('line', 'haunted', 'S7'), ('line', 'haunted', 'S8'), ('line', 'cg_haunted', 'S10'),
                  ('line', 'haunted', 'S12')]),
        ('C126', [('line', 'fortune', 'K6'), ('line', 'cg_fortune', 'K9'), ('line', 'fortune', 'K4')]),
        ('C130', [('line', 'ferris', 'M8'), ('line', 'ferris', 'M9'), ('line', 'cg_ferris', 'M10'),
                  ('line', 'cg_ferris', 'M11')]),
    ],
    filler=[('park', t) for t in ('M12', 'K3', 'S14', 'S17', 'F11', 'F16', 'K10')],
    # one ending set per place (same order as places): Fold, Short, Koon, Maia
    endings=[
        klein_end('7', 'F12', ['F13', 'F15', 'F17'], ['F18', 'F19'], 'F20', 'F21', 'F22', ['F23', 'F24']),
        klein_end('6', 'S18', ['S15', 'S16', 'S19'], ['S20', 'S21'], 'S22', 'S23', 'S24', ['S25', 'S26']),
        klein_end('8', 'K14', ['K11', 'K13', 'K16'], ['K17', 'K21'], 'K18', 'K23', 'K20', ['K22']),
        klein_end('5', 'M13', ['M14', 'M15', 'M16'], ['M19', 'M20'], 'M21', 'M22', 'M23', ['M24']),
    ],
)


def P(bg):
    return ('pose', bg, DM_POSE)


# Date Mode (Sn_Ba01 / Ba02 / Bx): Ichigo only, 10 games (the clock of Bx06): title, meeting, the drive
# (pick an area), five steps in the area, then the ending. Sunset scenes ('hot') hint a hidden bonus.
ROUTES['dm'] = dict(
    name='Ichigo',
    text=['dm_lines_a.tsv', 'dm_lines_b.tsv', 'dm_extra.tsv'],
    reward='[Bonus won! Line up the 7s!]',
    stills={
        'title': ('title',),
        'meet': P(0x2091),
        'drive': P(0x20A8), 'drive_eve': P(0x20A9),
        'fountain': P(0x2092), 'fountain_eve': P(0x2093),
        'cg_fountain': ('image', 0x20D4), 'cg_fountain_eve': ('image', 0x20E0),
        'bike': P(0x2094), 'bike_eve': P(0x2095),
        'promenade': P(0x2096), 'promenade_eve': P(0x2097),
        'cg_dog': ('image', 0x20D6), 'cg_dog_eve': ('image', 0x20E2),
        'lobby': P(0x2098), 'game': P(0x2099), 'cg_crane': ('image', 0x20DD),
        'karaoke': P(0x209A), 'cg_karaoke': ('image', 0x20DE),
        'mall': P(0x209C), 'clothes': P(0x209E), 'cg_clothes': ('image', 0x20DA),
        'food': P(0x209F), 'cg_soda': ('image', 0x20DC),
        'tunnel': P(0x20A0), 'dolphin': P(0x20A1), 'cg_dolphin': ('image', 0x20D7),
        'eels': P(0x20A2), 'cg_eels': ('image', 0x20D8),
        'dark': None, 'hand': ('over', 0x2318, 0x20E3), 'heart': ('movie', 214, 30),
        'congrats': ('Sn_Dg03_1113', 90, None),
    },
    hot={'drive': 'drive_eve', 'fountain': 'fountain_eve', 'cg_fountain': 'cg_fountain_eve',
         'bike': 'bike_eve', 'promenade': 'promenade_eve', 'cg_dog': 'cg_dog_eve'},
    script=[
        ('line', 'title', 'X0'),
        ('line', 'meet', 'D0'),
        ('ask', 'meet', 'D5', [('D214', 'D6', -1), ('D215', 'D21', 0), ('D223', 'D7', 1)]),
        ('ask', 'meet', 'D9', [('D217', 'D11', 0), ('D224', 'D10', 1), ('D220', 'D16', -1)]),
        ('places', 'drive', 'D8', 'D14'),
    ],
    places=[
        ('X3', [('line', 'fountain', 'D53'), ('line', 'cg_fountain', 'D59'),
                ('ask', 'bike', 'D60', [('D263', 'D61', 1), ('D241', 'D65', 0), ('D242', 'D66', -1)]),
                ('ask', 'promenade', 'D67', [('D243', 'D71', 1), ('D245', 'D70', 0), ('D244', 'D72', -1)]),
                ('line', 'cg_dog', 'D73')]),
        ('X4', [('line', 'lobby', 'D80'),
                ('ask', 'game', 'D98', [('D273', 'D101', 1), ('D264', 'D97', 0), ('D261', 'D100', -1)]),
                ('line', 'cg_crane', 'D116'),
                ('ask', 'karaoke', 'D102', [('D279', 'D117', 1), ('D265', 'D107', 0), ('D274', 'D105', -1)]),
                ('line', 'cg_karaoke', 'D106')]),
        ('X5', [('line', 'mall', 'D119'),
                ('ask', 'clothes', 'D141', [('D286', 'D155', 1), ('D289', 'D139', 0), ('D291', 'D145', -1)]),
                ('line', 'cg_clothes', 'D156'),
                ('ask', 'food', 'D150', [('D294', 'D152', 1), ('D295', 'D158', 0), ('D257', 'D19', -1)]),
                ('line', 'cg_soda', 'D151')]),
        ('X6', [('line', 'tunnel', 'D160'), ('line', 'dolphin', 'D178'), ('line', 'cg_dolphin', 'D182'),
                ('ask', 'eels', 'D184', [('D300', 'D187', 1), ('D239', 'D188', 0), ('D238', 'D153', -1)]),
                ('line', 'cg_eels', 'D198')]),
    ],
    filler=[('meet', t) for t in ('D17', 'D20', 'D85')],
    endings={
        'love': [('meet', 'D46'), ('dark', 'X1'), ('hand', 'X2'), ('heart', ''), ('congrats', '')],
        'night': [('meet', 'D46'), ('dark', 'X1'), ('hand', 'X2'), ('heart', ''), ('congrats', '')],
        'fail': [('meet', 'D47'), ('meet', 'D213')],
    },
)
NARRATION = ["[Today's date is over.]"]


# Klein group date: each line names its speaker ("@Name@text", shown on the name tab)
SPEAKER = {'M': ('MAIA', 'Maia'), 'K': ('KOON', 'Koon'), 'S': ('SHORT', 'Short'), 'F': ('FOLD', 'Fold')}


def text_of(rows, ref, prefix='ICHIGO'):
    if ref == '':
        return ''
    kind, i = ref[0], int(ref[1:])
    if kind == 'N':
        return NARRATION[i]
    if kind in SPEAKER:
        bank, name = SPEAKER[kind]
        return f'@{name}@' + rows[(bank, i)]
    return rows[({'I': 'ICHIGO', 'G': prefix, 'C': 'CHOICE', 'D': 'DM', 'X': 'DMX'}[kind], i)]


def c_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('♪', '~').replace('♥', '<3') + '"'


def icons():
    """heart (64x64), UP and DOWN (64x32) from the reaction scenes, 4bpp main sprites"""
    def sprite(img, w, h):
        img = img.copy()
        img.thumbnail((w, h), Image.LANCZOS)
        c = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        c.alpha_composite(img, ((w - img.width) // 2, (h - img.height) // 2))
        return c
    parts = [sprite(g.load_image(0x4523), 64, 64), sprite(g.load_image(0x20FD), 64, 32),
             sprite(g.load_image(0x20FC), 64, 32)]
    tiles, pals = b'', b''
    for p in parts:
        a = np.array(p)
        idx, pal = quantize(p.convert('RGB'), 15, a[:, :, 3] >= 128)
        h, w = idx.shape
        t = idx.reshape(h // 8, 8, w // 8, 8).swapaxes(1, 2).reshape(-1, 8, 8)
        tiles += (t[:, :, 0::2] | (t[:, :, 1::2] << 4)).astype(np.uint8).tobytes()
        pp = np.zeros(16, np.uint16)
        pp[:len(pal)] = rgb555(pal)
        pals += pp.astype('<u2').tobytes()
    open(os.path.join(OUT, 'date_icons_tiles.bin'), 'wb').write(tiles)
    open(os.path.join(OUT, 'date_icons_pal.bin'), 'wb').write(pals)


def main():
    files = []
    h = ['// generated by tools/make_date.py', '#pragma once', '#include <nds.h>', '#include "pak_ids.h"', '',
         'typedef enum { BEAT_LINE, BEAT_ASK, BEAT_PLACES } beat_kind_t;',
         '// ASK: nchoice options from choice0; PLACES: reply = line said after picking a place',
         'typedef struct { u8 kind, still, nchoice, choice0; s16 text, reply; } date_beat_t;',
         'typedef struct { s16 text, reply; s8 delta; } date_choice_t;',
         'typedef struct { u8 still; s16 text; } date_end_t;  // text -1: picture only',
         'typedef struct',
         '{',
         '    const char *name;',
         '    const char *const *text;',
         '    const u16 *stills; // pak ids',
         '    const u8 *hot;     // still to show instead while a bonus is hidden (Date Mode hint), 255 = none',
         '    const char *reward; // narration after a successful ending',
         '    const date_beat_t *script;',
         '    u8 nscript;',
         '    const date_choice_t *choices;',
         '    u8 placeChoice0;   // choices[placeChoice0 + n] = name of place n',
         '    u8 nplaces;',
         '    const date_beat_t *places;',
         '    const u8 *placeStart; // nplaces + 1 entries',
         '    const u8 *filler;     // still, text pairs',
         '    u8 nfiller;',
         '    const date_end_t *const *ending; // [set * 3 + DATE_END_*]; set = girl of the last place (Klein)',
         '    const u8 *nending;',
         '    u8 endSets;',
         '} date_route_t;',
         'enum { DATE_END_LOVE, DATE_END_NIGHT, DATE_END_FAIL };', '']
    for rname, R in ROUTES.items():
        rows = load_text(R['text'])
        stills = list(R['stills'])
        prev = []
        for sname in stills:
            im = still(R['stills'][sname])
            fname = f'date_{rname}_{sname}.bin'
            data = lz77.compress(bgr555(im).tobytes())
            open(os.path.join(OUT, fname.replace('.bin', '_lz.bin')), 'wb').write(data)
            files.append(fname.replace('.bin', '_lz.bin'))
            prev.append(im)
        sheet = Image.new('RGB', (4 * 256, ((len(prev) + 3) // 4) * 144))
        for k, im in enumerate(prev):
            sheet.paste(im, ((k % 4) * 256, (k // 4) * 144))
        sheet.save(os.path.join(PRE, f'date_{rname}.png'))

        texts, tindex = [], {}

        def T(ref):
            if ref not in tindex:
                tindex[ref] = len(texts)
                texts.append(text_of(rows, ref, R.get('prefix', 'ICHIGO')))
            return tindex[ref]
        S = {s: i for i, s in enumerate(stills)}
        choices = []

        def beat(b):
            if b[0] == 'line':
                return f'{{BEAT_LINE, {S[b[1]]}, 0, 0, {T(b[2])}, -1}}'
            if b[0] == 'ask':
                c0 = len(choices)
                for c, rep, d in b[3]:
                    choices.append((T(c), T(rep), d))
                return f'{{BEAT_ASK, {S[b[1]]}, {len(b[3])}, {c0}, {T(b[2])}, -1}}'
            return f'{{BEAT_PLACES, {S[b[1]]}, 0, 0, {T(b[2])}, {T(b[3])}}}'
        script = [beat(b) for b in R['script']]
        place_beats, place_start = [], []
        place_c0 = len(choices)
        for cname, blk in R['places']:
            choices.append((T(cname), -1, 0))
        for cname, blk in R['places']:
            place_start.append(len(place_beats))
            place_beats += [beat(b) for b in blk]
        place_start.append(len(place_beats))
        filler = []
        for s, t in R['filler']:
            filler += [S[s], T(t)]
        end_sets = R['endings'] if isinstance(R['endings'], list) else [R['endings']]
        ends = [[f'{{{S[s]}, {T(t) if t else -1}}}' for s, t in es[k]] for es in end_sets
                for k in ('love', 'night', 'fail')]

        p = f's_{rname}'
        h.append(f'static const char *const {p}_text[] = {{')
        h += [f'    {c_str(t)},' for t in texts]
        h.append('};')
        h.append(f'static const u16 {p}_stills[] = {{ ' +
                 ', '.join(f'PAK_{f[:-4].upper()}' for f in files[-len(stills):]) + ' };')
        h.append(f'static const date_beat_t {p}_script[] = {{ ' + ', '.join(script) + ' };')
        h.append(f'static const date_choice_t {p}_choices[] = {{ ' +
                 ', '.join(f'{{{c}, {r}, {d}}}' for c, r, d in choices) + ' };')
        h.append(f'static const date_beat_t {p}_places[] = {{ ' + ', '.join(place_beats) + ' };')
        h.append(f'static const u8 {p}_place_start[] = {{ ' + ', '.join(map(str, place_start)) + ' };')
        h.append(f'static const u8 {p}_filler[] = {{ ' + ', '.join(map(str, filler)) + ' };')
        for k, e in enumerate(ends):
            h.append(f'static const date_end_t {p}_end_{k}[] = {{ ' + ', '.join(e) + ' };')
        h.append(f'static const date_end_t *const {p}_ends[] = {{ ' + ', '.join(f'{p}_end_{k}' for k in range(len(ends))) + ' };')
        h.append(f'static const u8 {p}_nends[] = {{ ' + ', '.join(str(len(e)) for e in ends) + ' };')
        hot = [S[R.get('hot', {}).get(s)] if R.get('hot', {}).get(s) else 255 for s in stills]
        h.append(f'static const u8 {p}_hot[] = {{ ' + ', '.join(map(str, hot)) + ' };')
        h.append(f'static const date_route_t {p}_route = {{ {c_str(R["name"])}, {p}_text, {p}_stills, {p}_hot, '
                 f'{c_str(R["reward"])}, '
                 f'{p}_script, {len(script)}, {p}_choices, {place_c0}, {len(R["places"])}, {p}_places, '
                 f'{p}_place_start, {p}_filler, {len(filler) // 2}, '
                 f'{p}_ends, {p}_nends, {len(end_sets)} }};')
        h.append('')
        print(f'{rname}: {len(stills)} stills, {len(texts)} lines, {len(choices)} choices')

    with open(os.path.join(INC, 'date_script.h'), 'w', encoding='utf8') as f:
        f.write('\n'.join(h) + '\n')
    with open(os.path.join(HERE, 'date_files.txt'), 'w') as f:
        f.write('\n'.join(files) + '\n')
    icons()


if __name__ == '__main__':
    main()
