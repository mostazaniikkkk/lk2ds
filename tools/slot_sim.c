// Host simulation of game/arm9/source/slot.c
// build: gcc -O2 -I game/arm9/source tools/slot_sim.c game/arm9/source/slot.c -o build/slot_sim
#include <stdio.h>
#include <stdlib.h>
#include "slot.h"

static uint32_t rng = 12345;
static uint32_t rnd32(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

static const char *winName[WIN_COUNT] = { "none", "replay", "bell", "suika", "cherry", "BIG", "REG" };

// a skilled player aims the stop so that a seven can be pulled when a bonus is carried over
static int sMode; // 0 random, 1 skilled, 2 auto aim

static int choose_base(slot_t *s, int reel, bool skilled)
{
    if (sMode == 2)
        return slot_bonus_aimable(s) ? slot_best_base(s, reel) : (int)(rnd32() % SLOT_NSYM);
    int base = rnd32() % SLOT_NSYM;
    if (!skilled || s->bonusFlag == BONUS_NONE || s->flag != WIN_NONE || s->bonusActive)
        return base;
    int want = s->bonusFlag == BONUS_BIG ? SYM_RED7 : SYM_BAR;
    // aim: put the target symbol 2 symbols above the middle row so that the slip can bring it in
    for (int b = 0; b < SLOT_NSYM; b++)
        if (slot_strips[reel][(b + 1) % SLOT_NSYM] == want)
            return (b + 2) % SLOT_NSYM;
    return base;
}

static void run(bool skilled, long games, int setting)
{
    slot_t s;
    slot_init(&s, 1000000);
    slot_set_setting(&s, setting);
    long in = 0, out = 0;
    long flagged[WIN_COUNT] = { 0 }, won[WIN_COUNT] = { 0 }, illegal = 0, bonusGames = 0;
    long bonusDrawn = 0;

    for (long g = 0; g < games; g++)
    {
        int32_t before = s.credits;
        bool wasReplay = s.replayBet;
        if (!slot_can_start(&s))
            slot_bet(&s);
        if (!wasReplay)
            in += 3;
        slot_start(&s, rnd32() & 0xFFFF);
        if (s.newBonus)
            bonusDrawn++;
        win_t flag = s.flag;
        bonus_t carried = s.bonusFlag;
        bool inBonus = s.bonusActive != BONUS_NONE;
        if (inBonus)
            bonusGames++;
        flagged[flag]++;

        int order[3] = { 0, 1, 2 };
        if (rnd32() & 1) { order[0] = 2; order[2] = 0; } // some players stop right-first
        for (int k = 0; k < 3; k++)
            slot_choose_slip(&s, order[k], choose_base(&s, order[k], skilled));
        slot_finish(&s);
        out += s.credits - before + (wasReplay ? 0 : 3) - 3 + 3 * 0;
        (void)before;
        won[s.lastWin]++;

        bool ok = s.lastWin == WIN_NONE || s.lastWin == flag ||
                  (flag == WIN_NONE && !inBonus &&
                   ((s.lastWin == WIN_BIG && carried == BONUS_BIG) || (s.lastWin == WIN_REG && carried == BONUS_REG)));
        if (!ok)
            illegal++;
    }
    // out was computed as net; recompute payout from counts instead
    printf("== %s player, %ld games\n", skilled ? "skilled" : "random", games);
    printf("credits end %d (start 1000000)  -> net %+d\n", s.credits, s.credits - 1000000);
    printf("payout rate %.1f%%   BIG %u  REG %u  bonus games %ld  bonus drawn %ld\n",
           100.0 * (in + (s.credits - 1000000)) / in, s.bigCount, s.regCount, bonusGames, bonusDrawn);
    for (int w = 1; w < WIN_COUNT; w++)
        printf("  %-7s flagged %8ld  won %8ld\n", winName[w], flagged[w], won[w]);
    printf("  illegal wins %ld\n", illegal);
}

int main(int argc, char **argv)
{
    long games = argc > 1 ? atol(argv[1]) : 1000000;
    for (int set = 1; set <= 6; set += 5)
    {
        sMode = 0; run(false, games, set);
        sMode = 1; run(true, games, set);
        sMode = 2; run(true, games, set);
    }
    return 0;
}
