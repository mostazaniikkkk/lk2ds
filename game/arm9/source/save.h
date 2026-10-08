#pragma once
#include <nds.h>
#include "settings.h"

// Save file "<rom name>.sav" next to the .nds on the SD card, like the flashcarts of the era: credits won in
// SIMULATOR mode, gallery unlocks, play records and the pre-game settings. Read and written whole by the ARM7.

#define SAVE_ITEMS 256

typedef struct
{
    u32 magic;            // 'LKSV'
    u16 version, size;
    s32 credits;          // SIMULATOR mode balance (TRIAL mode never touches it)
    u32 owned[SAVE_ITEMS / 32]; // gallery items unlocked
    // records
    u32 games, bigCount, regCount, artCount, dates, confessions;
    u32 medalsIn, medalsOut;
    u32 bestArt;          // most medals won in one Absolute Airspace
    game_settings_t settings;
    u32 sum;              // checksum of everything above
} save_t;

extern save_t gSave;

#define SAVE_START_CREDITS 1000
#define SAVE_FILE_SIZE     4096 // the .sav is created at this size (build.sh ships an empty one)

// load at boot (defaults when there is no valid file); write after changes (purchases, end of a session)
void save_load(void);
bool save_write(void);
bool save_owned(int item);
void save_unlock(int item);
