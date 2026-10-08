#pragma once
#include <nds.h>

// Master-brightness fade on both screens: -16 = black, 0 = normal. Waits one vblank per step.
static inline void fade(int from, int to, int frames)
{
    for (int f = 1; f <= frames; f++)
    {
        setBrightness(3, from + (to - from) * f / frames);
        swiWaitForVBlank();
    }
}
