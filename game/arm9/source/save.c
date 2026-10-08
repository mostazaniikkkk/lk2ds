#include <string.h>
#include "save.h"
#include "asset.h"
#include "ipc.h"

#define SAVE_MAGIC   0x56534B4C // 'LKSV'
#define SAVE_VERSION 1

save_t gSave;
u32 gSaveLast; // debug: bytes of the last write
static save_t sIo ALIGN(32);
static save_req_t sReq ALIGN(32);
static char sPath[260] ALIGN(32);

static u32 checksum(const save_t *s)
{
    const u8 *p = (const u8 *)s;
    u32 h = 2166136261u;
    for (size_t i = 0; i < offsetof(save_t, sum); i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

// "<rom path without .nds>.sav"
static const char *save_path(void)
{
    strncpy(sPath, asset_rom_path(), sizeof(sPath) - 5);
    sPath[sizeof(sPath) - 5] = 0;
    char *dot = strrchr(sPath, '.');
    char *slash = strrchr(sPath, '/');
    if (dot && (!slash || dot > slash))
        *dot = 0;
    strcat(sPath, ".sav");
    return sPath;
}

static u32 save_io(bool write)
{
    sReq.path = save_path();
    sReq.buffer = &sIo;
    sReq.size = sizeof(sIo);
    sReq.write = write;
    DC_FlushRange(sPath, sizeof(sPath));
    DC_FlushRange(&sIo, sizeof(sIo));
    DC_FlushRange(&sReq, sizeof(sReq));
    fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_SAVE, (u32)&sReq));
    fifoWaitValue32(FIFO_USER_01);
    u32 n = fifoGetValue32(FIFO_USER_01) & IPC_CMD_ARG_MASK;
    DC_InvalidateRange(&sIo, sizeof(sIo));
    return n;
}

void save_load(void)
{
    memset(&gSave, 0, sizeof(gSave));
    gSave.credits = SAVE_START_CREDITS;
    gSave.settings = gSettings;
    if (asset_rom_path()[0] == 0)
        return;
    if (save_io(false) == sizeof(sIo) && sIo.magic == SAVE_MAGIC && sIo.version == SAVE_VERSION &&
        sIo.size == sizeof(save_t) && sIo.sum == checksum(&sIo))
    {
        gSave = sIo;
        gSettings = gSave.settings;
    }
}

bool save_write(void)
{
    if (asset_rom_path()[0] == 0)
        return false;
    gSave.magic = SAVE_MAGIC;
    gSave.version = SAVE_VERSION;
    gSave.size = sizeof(save_t);
    gSave.settings = gSettings;
    gSave.sum = checksum(&gSave);
    sIo = gSave;
    gSaveLast = save_io(true);
    return gSaveLast == sizeof(sIo);
}

bool save_owned(int item)
{
    return item >= 0 && item < SAVE_ITEMS && (gSave.owned[item / 32] >> (item % 32)) & 1;
}

void save_unlock(int item)
{
    if (item >= 0 && item < SAVE_ITEMS)
        gSave.owned[item / 32] |= 1u << (item % 32);
}
