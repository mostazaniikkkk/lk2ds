#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include "asset.h"
#include "ipc.h"

#define PAK_MAGIC 0x4B504B4C // 'LKPK'

static pak_read_t sReq ALIGN(32);
static u32 sPakBase;
static u32 sTable[PAK_COUNT][2]; // offset, size
static bool sReady;

static u32 ipc_call(u32 cmd, u32 arg)
{
    fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(cmd, arg));
    fifoWaitValue32(FIFO_USER_01);
    return fifoGetValue32(FIFO_USER_01) & IPC_CMD_ARG_MASK;
}

// read `size` bytes at `offset` of the .nds into a 32-byte aligned buffer
static bool rom_read(u32 offset, u32 size, void *buf)
{
    DC_FlushRange(buf, size); // no dirty line may later overwrite what the ARM7 stores
    sReq.offset = offset;
    sReq.size = size;
    sReq.buffer = buf;
    DC_FlushRange(&sReq, sizeof(sReq));
    u32 got = ipc_call(IPC_CMD_PAK_READ, (u32)&sReq);
    DC_InvalidateRange(buf, size);
    return got == size;
}

static char path[256];

const char *asset_rom_path(void)
{
    return path;
}

u32 asset_rom_offset(pak_id_t id)
{
    return sReady && id < PAK_COUNT ? sPakBase + sTable[id][0] : 0;
}

bool asset_init(const char *rom_path)
{
    if (rom_path == NULL || rom_path[0] == 0)
        rom_path = "fat:/lk2ds.nds";
    // the ARM7 only mounts the flashcart as "fat:"
    if (strncmp(rom_path, "sd:", 3) == 0 && !isDSiMode())
    {
        strcpy(path, "fat:");
        strncat(path, rom_path + 3, sizeof(path) - 5);
    }
    else
    {
        strncpy(path, rom_path, sizeof(path) - 1);
    }
    DC_FlushRange(path, sizeof(path));
    if (!ipc_call(IPC_CMD_PAK_OPEN, (u32)path))
        return false;

    // NitroFS holds a single file: FAT entry 0 = the pack
    u32 *buf = memalign(32, 512);
    if (buf == NULL)
        return false;
    bool ok = rom_read(0, 512, buf);
    u32 fatOffset = buf[0x48 / 4];
    ok = ok && rom_read(fatOffset & ~31, 64, buf);
    sPakBase = buf[(fatOffset & 31) / 4];
    ok = ok && rom_read(sPakBase & ~31, 64, buf);
    u32 *head = buf + (sPakBase & 31) / 4;
    ok = ok && head[0] == PAK_MAGIC && head[1] == PAK_COUNT;
    if (ok)
    {
        u32 bytes = (8 + 8 * PAK_COUNT + (sPakBase & 31) + 31) & ~31;
        u32 *table = memalign(32, bytes);
        ok = table && rom_read(sPakBase & ~31, bytes, table);
        if (ok)
            memcpy(sTable, table + 2 + (sPakBase & 31) / 4, sizeof(sTable));
        free(table);
    }
    free(buf);
    sReady = ok;
    return ok;
}

void *asset_load(pak_id_t id, u32 *size)
{
    if (!sReady || id >= PAK_COUNT)
        return NULL;
    u32 n = sTable[id][1];
    void *data = memalign(32, (n + 31) & ~31);
    if (data == NULL)
        return NULL;
    // pack entries are 32-byte aligned inside the pack; the pack itself may not be
    if (((sPakBase + sTable[id][0]) & 31) == 0)
    {
        if (!rom_read(sPakBase + sTable[id][0], (n + 31) & ~31, data))
        {
            free(data);
            return NULL;
        }
    }
    else
    {
        u32 lead = (sPakBase + sTable[id][0]) & 31;
        u8 *tmp = memalign(32, (n + lead + 31) & ~31);
        bool ok = tmp && rom_read(sPakBase + sTable[id][0] - lead, (n + lead + 31) & ~31, tmp);
        if (ok)
            memcpy(data, tmp + lead, n);
        free(tmp);
        if (!ok)
        {
            free(data);
            return NULL;
        }
    }
    if (size)
        *size = n;
    return data;
}

void asset_free(void *data)
{
    free(data);
}

bool asset_show_lz(pak_id_t id, void *vram)
{
    void *lz = asset_load(id, NULL);
    if (lz == NULL)
        return false;
    decompress(lz, vram, LZ77Vram);
    asset_free(lz);
    return true;
}
