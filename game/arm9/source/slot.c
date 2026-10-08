#include <string.h>
#include "slot.h"

#define R SYM_REPLAY
#define B SYM_BELL
#define R7 SYM_RED7
#define W SYM_SUIKA
#define B7 SYM_BLUE7
#define C SYM_CHERRY
#define BR SYM_BAR
#define K SYM_KLEIN

// strip order from the top of r1/r2/r3.png (symbol k at rows k*24..k*24+23 on the DS)
const uint8_t slot_strips[3][SLOT_NSYM] = {
    { R, B, R7, R, B, W, R, B, W, B, B7, K, B, W, C, BR, R, B, W, C, W },
    { B, BR, R7, C, R, B, C, R, B, B, W, C, K, B, B7, W, C, R, B, C, R },
    { B, C, R7, K, W, B, R, BR, B7, W, B, R, R, R, W, B, R, W, B, R, W },
};

#undef R
#undef B
#undef R7
#undef W
#undef B7
#undef C
#undef BR
#undef K

// rows used by each line on reels 1..3 (0 = top)
const uint8_t slot_lines[SLOT_NLINES][3] = {
    { 1, 1, 1 }, { 0, 0, 0 }, { 2, 2, 2 }, { 0, 1, 2 }, { 2, 1, 0 },
};

// lottery, out of 65536 (normal game)
#define P_REPLAY 8978  // 1/7.3
#define P_BELL   9362  // 1/7.0
#define P_SUIKA  1024  // 1/64
#define P_CHERRY 1365  // 1/48
// bonus odds per machine setting 1..6 (BIG 1/300 .. 1/240, REG 1/440 .. 1/360)
// doubled with the original 150 / 40-medal bonuses: the machine made up for them with its ART, which the port lacks
static const uint16_t sBigOdds[6] = { 436, 450, 468, 490, 520, 546 };
static const uint16_t sRegOdds[6] = { 298, 308, 320, 332, 348, 364 };
// bonus game
#define PB_BELL   54613 // 1/1.2
#define PB_REPLAY 3000

#define PAY_BELL        10
#define PAY_BELL_BONUS  15  // BIG
#define PAY_BELL_REG    5   // REG: 8 payouts of 5 = 40 medals
#define PAY_SUIKA       8
#define PAY_CHERRY      4   // per line
// end conditions of the original machine (play guide): BIG = a 150-medal bonus, REG = 40 medals in 8 payouts
#define BIG_LIMIT       150
#define REG_LIMIT       40
#define REG_PAYOUTS     8

void slot_init(slot_t *s, int32_t credits)
{
    memset(s, 0, sizeof(*s));
    s->credits = credits;
    s->setting = 3;
}

void slot_set_setting(slot_t *s, int setting)
{
    s->setting = setting < 1 ? 1 : setting > 6 ? 6 : setting;
}

void slot_extend_big(slot_t *s, int medals)
{
    s->bonusActive = BONUS_BIG;
    s->bonusPaid = medals < BIG_LIMIT ? BIG_LIMIT - medals : 0;
    s->bonusEnded = false;
}

bool slot_bet(slot_t *s)
{
    if (s->bet == 3)
        return false;
    int need = 3 - s->bet;
    if (s->credits < need)
        return false;
    s->credits -= need;
    s->medalsIn += need;
    s->bet = 3;
    return true;
}

bool slot_can_start(const slot_t *s)
{
    return s->bet == 3;
}

void slot_start(slot_t *s, uint16_t rnd)
{
    s->flag = WIN_NONE;
    s->newBonus = false;
    for (int i = 0; i < 3; i++)
        s->stopped[i] = false;

    uint32_t v = rnd;
    if (s->bonusActive != BONUS_NONE)
    {
        if (v < PB_BELL)
            s->flag = WIN_BELL;
        else if (v < PB_BELL + PB_REPLAY)
            s->flag = WIN_REPLAY;
        return;
    }

    // Date Mode's extra bonus draw (independent bits of the same random number)
    if (s->boost && s->bonusFlag == BONUS_NONE)
    {
        uint32_t w = (v * 0x9E3779B1u) >> 22; // 0..1023
        if (w < s->boost)
        {
            s->bonusFlag = w % 3 ? BONUS_BIG : BONUS_REG;
            s->newBonus = true;
        }
    }

    int set = (s->setting >= 1 && s->setting <= 6 ? s->setting : 3) - 1;
    // assist: 1 in 2 games are navigated BELLs on top of the normal lottery (from another bit of rnd)
    bool assistBell = s->assist && ((v * 0x85EBCA6Bu) >> 31);
    const struct { uint16_t p; win_t w; } table[] = {
        { P_REPLAY, WIN_REPLAY }, { P_BELL, WIN_BELL }, { P_SUIKA, WIN_SUIKA },
        { P_CHERRY, WIN_CHERRY }, { sBigOdds[set], WIN_BIG }, { sRegOdds[set], WIN_REG },
    };
    for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++)
    {
        if (v < table[i].p)
        {
            if (table[i].w == WIN_BIG || table[i].w == WIN_REG)
            {
                // a bonus already carried over swallows new bonus draws
                if (s->bonusFlag == BONUS_NONE)
                {
                    s->bonusFlag = table[i].w == WIN_BIG ? BONUS_BIG : BONUS_REG;
                    s->newBonus = true;
                }
            }
            else
            {
                s->flag = table[i].w;
            }
            return;
        }
        v -= table[i].p;
    }
    if (assistBell)
        s->flag = WIN_BELL;
}

// ---------------------------------------------------------------- line evaluation

static bool is_replay(int sym)
{
    return sym == SYM_REPLAY || sym == SYM_KLEIN;
}

// combination formed by three symbols on a line (cherry is handled separately: reel 1 only)
static win_t combo3(int a, int b, int c)
{
    if (is_replay(a) && is_replay(b) && is_replay(c))
        return WIN_REPLAY;
    if (a != b || b != c)
        return WIN_NONE;
    switch (a)
    {
        case SYM_BELL: return WIN_BELL;
        case SYM_SUIKA: return WIN_SUIKA;
        case SYM_RED7:
        case SYM_BLUE7: return WIN_BIG;
        case SYM_BAR: return WIN_REG;
        default: return WIN_NONE;
    }
}

// can a partially known line (sym < 0 = unknown) still become combination w?
static bool may_form(const int sym[3], win_t w)
{
    for (int i = 0; i < 3; i++)
    {
        if (sym[i] < 0)
            continue;
        switch (w)
        {
            case WIN_REPLAY: if (!is_replay(sym[i])) return false; break;
            case WIN_BELL: if (sym[i] != SYM_BELL) return false; break;
            case WIN_SUIKA: if (sym[i] != SYM_SUIKA) return false; break;
            case WIN_REG: if (sym[i] != SYM_BAR) return false; break;
            case WIN_BIG:
                if (sym[i] != SYM_RED7 && sym[i] != SYM_BLUE7)
                    return false;
                break;
            default: return false;
        }
    }
    if (w == WIN_BIG) // all known sevens must share a colour
    {
        int col = -1;
        for (int i = 0; i < 3; i++)
        {
            if (sym[i] < 0)
                continue;
            if (col >= 0 && sym[i] != col)
                return false;
            col = sym[i];
        }
    }
    return true;
}

static bool allowed(const slot_t *s, win_t w)
{
    if (w == WIN_NONE)
        return true;
    if (w == s->flag)
        return true;
    // the carried bonus can only line up in games without a small flag (and not while a date hides it)
    if (s->flag == WIN_NONE && s->bonusActive == BONUS_NONE && !s->holdBonus)
    {
        if (w == WIN_BIG && s->bonusFlag == BONUS_BIG)
            return true;
        if (w == WIN_REG && s->bonusFlag == BONUS_REG)
            return true;
    }
    return false;
}

// score a (partial) stop layout; tops[i] < 0 means reel i is still spinning
static int score_layout(const slot_t *s, const int tops[3])
{
    int score = 0;
    int nstopped = (tops[0] >= 0) + (tops[1] >= 0) + (tops[2] >= 0);
    for (int l = 0; l < SLOT_NLINES; l++)
    {
        int sym[3];
        for (int i = 0; i < 3; i++)
            sym[i] = tops[i] >= 0 ? slot_symbol(i, tops[i], slot_lines[l][i]) : -1;

        // cherry is decided by reel 1 alone
        if (sym[0] == SYM_CHERRY)
            score += allowed(s, WIN_CHERRY) ? 1000 : -100000;

        if (nstopped == 3)
        {
            win_t w = combo3(sym[0], sym[1], sym[2]);
            if (w != WIN_NONE)
                score += allowed(s, w) ? 1000 : -100000;
        }
        else
        {
            static const win_t pullable[] = { WIN_REPLAY, WIN_BELL, WIN_SUIKA, WIN_BIG, WIN_REG };
            for (unsigned k = 0; k < sizeof(pullable) / sizeof(pullable[0]); k++)
            {
                win_t w = pullable[k];
                if (!may_form(sym, w))
                    continue;
                // keep chances for the flagged combination, avoid reaches of forbidden ones
                score += allowed(s, w) ? 10 : (nstopped == 2 ? -3 : 0);
            }
        }
    }
    return score;
}

// best slip (0..4) for a stop at `base`, and its score
static int best_slip(const slot_t *s, int reel, int base, int *score)
{
    int tops[3];
    for (int i = 0; i < 3; i++)
        tops[i] = s->stopped[i] ? s->top[i] : -1;

    int best = 0, bestScore = 0;
    for (int slip = 0; slip <= SLOT_MAXSLIP; slip++)
    {
        tops[reel] = (base - slip + SLOT_NSYM * 4) % SLOT_NSYM;
        int sc = score_layout(s, tops);
        if (slip == 0 || sc > bestScore)
        {
            best = slip;
            bestScore = sc;
        }
    }
    if (score)
        *score = bestScore;
    return best;
}

bool slot_bonus_aimable(const slot_t *s)
{
    return s->bonusFlag != BONUS_NONE && s->flag == WIN_NONE && s->bonusActive == BONUS_NONE && !s->holdBonus;
}

int slot_best_base(const slot_t *s, int reel)
{
    int bestBase = 0, bestScore = 0;
    for (int b = 0; b < SLOT_NSYM; b++)
    {
        int sc;
        best_slip(s, reel, b, &sc);
        if (b == 0 || sc > bestScore)
        {
            bestBase = b;
            bestScore = sc;
        }
    }
    return bestBase;
}

int slot_choose_slip(slot_t *s, int reel, int base)
{
    int best = best_slip(s, reel, base, NULL);
    s->stopped[reel] = true;
    s->top[reel] = (base - best + SLOT_NSYM * 4) % SLOT_NSYM;
    return best;
}

bool slot_seven_reach(const slot_t *s)
{
    for (int l = 0; l < SLOT_NLINES; l++)
    {
        int seven = -1, known = 0;
        bool ok = true;
        for (int i = 0; i < 3; i++)
        {
            if (!s->stopped[i])
                continue;
            int sym = slot_symbol(i, s->top[i], slot_lines[l][i]);
            if ((sym != SYM_RED7 && sym != SYM_BLUE7) || (seven >= 0 && sym != seven))
                ok = false;
            seven = sym;
            known++;
        }
        if (ok && known == 2)
            return true;
    }
    return false;
}

void slot_finish(slot_t *s)
{
    s->lastWin = WIN_NONE;
    s->lastPay = 0;
    s->winLines = 0;
    s->bonusStarted = false;
    s->bonusEnded = false;

    int pay = 0;
    win_t main = WIN_NONE;
    for (int l = 0; l < SLOT_NLINES; l++)
    {
        int sym[3];
        for (int i = 0; i < 3; i++)
            sym[i] = slot_symbol(i, s->top[i], slot_lines[l][i]);
        win_t w = combo3(sym[0], sym[1], sym[2]);
        int p = 0;
        if (sym[0] == SYM_CHERRY)
        {
            p += PAY_CHERRY;
            if (main == WIN_NONE)
                main = WIN_CHERRY;
        }
        switch (w)
        {
            case WIN_BELL:
                p += s->bonusActive == BONUS_BIG ? PAY_BELL_BONUS
                     : s->bonusActive == BONUS_REG ? PAY_BELL_REG : PAY_BELL;
                break;
            case WIN_SUIKA: p += PAY_SUIKA; break;
            default: break;
        }
        if (w != WIN_NONE)
            main = w;
        if (w != WIN_NONE || p)
            s->winLines |= 1 << l;
        pay += p;
    }

    s->games++;
    if (s->bonusActive == BONUS_NONE && s->sinceBonus < 0xFFFF)
        s->sinceBonus++;
    s->lastWin = main;
    s->lastPay = pay;
    s->credits += pay;
    s->medalsOut += pay;
    bool replay = main == WIN_REPLAY;

    if (s->bonusActive != BONUS_NONE)
    {
        s->bonusPaid += pay;
        if (pay)
            s->bonusPayouts++;
        bool over = s->bonusActive == BONUS_BIG ? s->bonusPaid >= BIG_LIMIT
                                                : s->bonusPaid >= REG_LIMIT || s->bonusPayouts >= REG_PAYOUTS;
        if (over)
        {
            s->bonusActive = BONUS_NONE;
            s->bonusEnded = true;
        }
    }
    else if (main == WIN_BIG || main == WIN_REG)
    {
        s->bonusActive = main == WIN_BIG ? BONUS_BIG : BONUS_REG;
        s->bonusFlag = BONUS_NONE;
        s->bonusPaid = 0;
        s->bonusPayouts = 0;
        s->bonusStarted = true;
        s->sinceBonus = 0;
        if (main == WIN_BIG)
            s->bigCount++;
        else
            s->regCount++;
    }

    // a replay re-arms the same bet for free
    s->bet = replay ? 3 : 0;
    s->replayBet = replay;
}
