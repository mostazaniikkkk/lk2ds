#pragma once
#include <stdbool.h>
#include <stdint.h>

// Pre-game settings (original MachineSetting scene), shown before SIMULATOR and TRIAL mode.

#define SETTING_RANDOM 6   // "SETTING ?": a hidden setting picked at game start

typedef struct
{
    uint8_t setting;   // 0..5 = setting 1..6, SETTING_RANDOM = "?"
    uint8_t order;     // stop order (AUTO AIM single-button stops): L>C>R, L>R>C, C>L>R, C>R>L, R>L>C, R>C>L
    bool waitCut;      // skip the 4.1 s wait between games
    bool autoAim;      // aim the carried bonus automatically
    bool guide;        // show HOW TO PLAY before the game
    uint8_t volume;    // 0 (mute) .. 10 (MAX)
} game_settings_t;

extern game_settings_t gSettings;
extern const uint8_t gStopOrders[6][3];

// returns true to play with these settings, false to go back to the title
bool settings_run(bool trial);
