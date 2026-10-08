#define SFX_IMPLEMENTATION
#include "sfx.h"

static const u8 sChannel[SFX_COUNT] = {
    [SFX_BET] = 4,
    [SFX_LEVER] = 5,
    [SFX_STOP] = 6,
    [SFX_TENPAI] = 6,
    [SFX_PAY_REPLAY] = 7,
    [SFX_PAY_BELL] = 7,
    [SFX_PAY_SUIKA] = 7,
    [SFX_PAY_CHERRY] = 8,
    [SFX_BIG_START] = 9,
    [SFX_REG_START] = 9,
    [SFX_BONUS_END] = 9,
    [SFX_CHANCE] = 10,
    [SFX_UI_START] = 13,
    [SFX_UI_OK] = 13,
    [SFX_UI_BACK] = 13,
    [SFX_UI_CURSOR] = 14,
    [SFX_WAIT] = 15,
};

// last sound id started on each hardware channel
static int sLast[16] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

void sfx_init(void)
{
    soundEnable();
}

void sfx_play(sfx_id_t id)
{
    if (id >= SFX_COUNT)
        return;
    int ch = sChannel[id];
    // a channel that is still playing ignores a new start: stop it first so fast repeats retrigger
    if (sLast[ch] >= 0)
        soundKill(sLast[ch]);
    sLast[ch] = soundPlaySampleChannel(ch, sfx_table[id].data, SoundFormat_ADPCM, sfx_table[id].size,
                                       SFX_RATE, 127, 64, false, 0);
}

void sfx_stop(sfx_id_t id)
{
    if (id >= SFX_COUNT)
        return;
    int ch = sChannel[id];
    if (sLast[ch] >= 0)
        soundKill(sLast[ch]);
    sLast[ch] = -1;
}

void sfx_set_volume(int level)
{
    // 0 (mute) .. 10 (MAX) -> master volume 0..127 (covers effects, music and the video sound)
    soundSetMasterVolume(level <= 0 ? 0 : level >= 10 ? 127 : level * 127 / 10);
}
