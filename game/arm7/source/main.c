// LK2 DS - ARM7 core
// Input/system FIFO from libnds + FastVideoDS ARM7 side (file streaming through DLDI, ADPCM audio).
// Timers 0-3 and sound channels 1/3 belong to the video player.

#include <nds.h>
#include "fv/fvPlayer7.h"
#include "fv/fat.h"

volatile bool exit_loop = false;

// boot progress for the ARM9 (debug aid): 4-bit stage in IPC_SYNC output bits
static inline void stage7(int n)
{
    REG_IPC_SYNC = (REG_IPC_SYNC & ~0x0F00) | ((n & 15) << 8);
}

static void power_button_callback(void)
{
    exit_loop = true;
}

static void vblank_handler(void)
{
    inputGetAndSend();
}

int main(int argc, char *argv[])
{
    stage7(1);
    // clear sound registers
    dmaFillWords(0, (void *)0x04000400, 0x100);

    enableSound();
    stage7(2);
    readUserSettings();
    stage7(3);
    ledBlink(LED_ALWAYS_ON);
    touchInit();
    stage7(4);

    irqInit();
    fifoInit();
    stage7(5);

    installSoundFIFO();
    installSystemFIFO();
    stage7(6);

    setPowerButtonCB(power_button_callback);

    irqSet(IRQ_VBLANK, vblank_handler);
    irqEnable(IRQ_VBLANK);

    fat_init();
    fv_init();
    stage7(7);

    while (!exit_loop)
        fv_main();

    return 0;
}
