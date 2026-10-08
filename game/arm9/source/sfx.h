#pragma once
#include <nds.h>
#include "sfx_list.h"

// Cabinet sound effects rendered from the original SAC sequences (tools/make_sfx.py).
// Channels 1 and 3 belong to the video player; effects use fixed channels 4..10 (one per kind,
// so a new STOP cuts the previous one like on the machine).
void sfx_init(void);
void sfx_play(sfx_id_t id);
void sfx_stop(sfx_id_t id);
// master volume 0 (mute) .. 10 (MAX)
void sfx_set_volume(int level);
