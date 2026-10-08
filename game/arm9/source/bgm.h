#pragma once

// Looping background music embedded in the ROM (tools/make_bgm.py -> data/bgm_<name>.bin).
// Two hardware channels (11 = L, 12 = R) with hardware ADPCM looping.
typedef enum
{
    BGM_TITLE,
    BGM_MENU,
    BGM_RACE,  // continuous effects (Sn_Ay04 / 01 / 02 / 03)
    BGM_ECM,
    BGM_RION,
    BGM_TSUG,
    BGM_ART,
    BGM_ARTSP,
    BGM_ZONE,
    BGM_ICHIGO,
    BGM_BATTLE,
    BGM_ASHI,
    BGM_NADE,
    BGM_VA,
} bgm_id_t;

void bgm_play(bgm_id_t id);
// play the track once (intro + one pass of the loop); returns its length in vblanks, 0 if it could not play
u32 bgm_play_once(bgm_id_t id);
void bgm_stop(void);
