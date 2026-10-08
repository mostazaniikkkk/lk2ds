#pragma once
#include <nds.h>

// Hang finder: code paths record a checkpoint; the main loop bumps gMainTick. If the main loop stops for
// CHECK_STUCK_VBLANKS, ui_vblank() shows the last checkpoint in the GAME field of the top counter.
extern volatile u16 gCheck;
extern volatile u32 gMainTick;
#define CHECK(n) (gCheck = (n))
#define CHECK_STUCK_VBLANKS 120
