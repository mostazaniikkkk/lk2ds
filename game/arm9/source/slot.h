#pragma once
// Pachislot rules for the demake: lottery, reel control (slip choice) and payouts.
// Plain C with no hardware access so it can be simulated on the host (tools/slot_sim.c).

#include <stdbool.h>
#include <stdint.h>

#define SLOT_NSYM   21
#define SLOT_NLINES 5
#define SLOT_MAXSLIP 4

typedef enum
{
    SYM_REPLAY, // CELFA
    SYM_BELL,   // helmet
    SYM_RED7,   // ICHIGO
    SYM_SUIKA,  // watermelon
    SYM_BLUE7,  // MAIA
    SYM_CHERRY,
    SYM_BAR,    // LoveKyure logo
    SYM_KLEIN,  // acts as a replay symbol
} symbol_t;

typedef enum
{
    WIN_NONE,
    WIN_REPLAY,
    WIN_BELL,
    WIN_SUIKA,
    WIN_CHERRY,
    WIN_BIG,
    WIN_REG,
    WIN_COUNT
} win_t;

typedef enum
{
    BONUS_NONE,
    BONUS_BIG,
    BONUS_REG,
} bonus_t;

typedef struct
{
    // persistent
    int32_t credits;
    uint32_t games;
    uint16_t bigCount, regCount;
    bonus_t bonusFlag;   // won in the lottery but not aligned yet (carried over)
    bonus_t bonusActive; // bonus game in progress
    int16_t bonusPaid;
    uint8_t bonusPayouts; // games that paid during the current bonus (REG ends after 8)
    uint8_t setting;     // machine setting 1..6 (bonus odds)
    // play data (top counter pages)
    uint32_t medalsIn, medalsOut;
    uint16_t sinceBonus; // games since the last bonus (not counting bonus games)
    // Date Mode: extra bonus draw per game (out of 1024), and a won bonus kept hidden until the date ends
    uint16_t boost;
    bool holdBonus;
    // assist time (Absolute Airspace / VA Ignition): most games without a flag get a BELL
    bool assist;

    // current game
    uint8_t bet;         // 0..3
    bool replayBet;      // bet was granted by a replay
    win_t flag;          // small flag drawn this game (WIN_NONE, REPLAY, BELL, SUIKA, CHERRY)
    bool newBonus;       // bonus flag drawn this very game
    bool stopped[3];
    uint8_t top[3];      // top-row symbol index of each stopped reel

    // result of the last finished game
    win_t lastWin;
    uint16_t lastPay;
    uint8_t winLines;    // bit mask of the lines that paid
    bool bonusStarted, bonusEnded;
} slot_t;

extern const uint8_t slot_strips[3][SLOT_NSYM];
extern const uint8_t slot_lines[SLOT_NLINES][3];

void slot_init(slot_t *s, int32_t credits);
// machine setting 1 (worst) .. 6 (best bonus odds)
void slot_set_setting(slot_t *s, int setting);
// MAX BET; returns false when there are not enough credits (or already bet)
bool slot_bet(slot_t *s);
// keep the BIG going for `medals` more (date sim confession reward)
void slot_extend_big(slot_t *s, int medals);
bool slot_can_start(const slot_t *s);
// lever: draw the flags (rnd = 16-bit random value)
void slot_start(slot_t *s, uint16_t rnd);
// choose how many symbols reel `reel` slides when stopped while showing `base` on top (no slip)
int slot_choose_slip(slot_t *s, int reel, int base);
// AUTO AIM: only aims while a carried bonus can be lined up (no small flag, not in a bonus game)
bool slot_bonus_aimable(const slot_t *s);
// the stop timing (top symbol with no slip) that gives the best result for this reel
int slot_best_base(const slot_t *s, int reel);
// two reels stopped showing the same seven on a line whose third reel still spins
bool slot_seven_reach(const slot_t *s);
// all reels stopped: pay out and update the bonus state
void slot_finish(slot_t *s);
// symbol shown at `row` (0..2) of a reel stopped with `top` on the top row
static inline symbol_t slot_symbol(int reel, int top, int row)
{
    return (symbol_t)slot_strips[reel][(top + row) % SLOT_NSYM];
}
