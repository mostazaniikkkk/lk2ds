#include <nds.h>
#include "check.h"
#include "bgm.h"
#include "asset.h"

#define BGM_MAGIC   0x47424B4C // 'LKBG': left channel then right channel
#define BGM_MAGIC_M 0x4D424B4C // 'LKBM': one channel for both speakers
#define BGM_CH_L    11
#define BGM_CH_R    12

static const pak_id_t sTrack[] = {
    [BGM_TITLE] = PAK_BGM_TITLE,
    [BGM_MENU] = PAK_BGM_MENU,
    [BGM_RACE] = PAK_BGM_RACE,
    [BGM_ECM] = PAK_BGM_ECM,
    [BGM_RION] = PAK_BGM_RION,
    [BGM_TSUG] = PAK_BGM_TSUG,
    [BGM_ART] = PAK_BGM_ART,
    [BGM_ARTSP] = PAK_BGM_ARTSP,
    [BGM_ZONE] = PAK_BGM_ZONE,
    [BGM_ICHIGO] = PAK_BGM_ICHIGO,
    [BGM_BATTLE] = PAK_BGM_BATTLE,
    [BGM_ASHI] = PAK_BGM_ASHI,
    [BGM_NADE] = PAK_BGM_NADE,
    [BGM_VA] = PAK_BGM_VA,
};

static u8 *sData;           // loaded from the ROM's asset pack only while the track plays
static int sSnd[2] = { -1, -1 };

static u32 play(bgm_id_t id, bool looping)
{
    CHECK(70);
    bgm_stop();
    sData = asset_load(sTrack[id], NULL);
    if (sData == NULL)
        return 0;
    const u32 *h = (const u32 *)sData;
    if (h[0] != BGM_MAGIC && h[0] != BGM_MAGIC_M)
    {
        bgm_stop();
        return 0;
    }
    u32 rate = h[1], bytes = h[2], loop = h[3];
    // the hardware plays loop point + length words, and libnds sets length = whole size: subtract the
    // loop point or every loop would read one word past the channel
    u32 len = looping ? bytes - loop * 4 : bytes;
    u32 lp = looping ? loop : 0;
    sSnd[0] = soundPlaySampleChannel(BGM_CH_L, sData + 16, SoundFormat_ADPCM, len, rate, 127, 0, looping, lp);
    u8 *right = h[0] == BGM_MAGIC_M ? sData + 16 : sData + 16 + bytes;
    sSnd[1] = soundPlaySampleChannel(BGM_CH_R, right, SoundFormat_ADPCM, len, rate, 127, 127,
                                     looping, lp);
    // IMA-ADPCM: 4-byte header, then 2 samples per byte; vblanks run at 59.826 Hz
    return (u32)((u64)(bytes - 4) * 2 * 59826 / ((u64)rate * 1000));
}

void bgm_play(bgm_id_t id)
{
    play(id, true);
}

u32 bgm_play_once(bgm_id_t id)
{
    return play(id, false);
}

void bgm_stop(void)
{
    CHECK(71);
    for (int i = 0; i < 2; i++)
    {
        if (sSnd[i] >= 0)
            soundKill(sSnd[i]);
        sSnd[i] = -1;
    }
    if (sData)
    {
        swiWaitForVBlank(); // let the ARM7 stop the channels before the memory can be reused
        asset_free(sData);
        sData = NULL;
    }
}
