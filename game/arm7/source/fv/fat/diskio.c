// FatFs disk glue for LK2 DS.
// Two modes, picked by the ARM9 from the DLDI driver's FEATURE_ARM7_CAPABLE flag:
//  - direct: the ARM9 relocates the loader-patched driver into main RAM and the ARM7 calls it (DSpico)
//  - proxy:  the driver only runs on the ARM9 (e.g. melonDS): sector reads are requested over
//            FIFO_USER_02 and done by the ARM9 into main RAM (bounce buffer for ARM7-only memory)

#include <nds.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
#include "dldiproxy.h"

typedef bool (*dldi_startup_fn)(void);
typedef bool (*dldi_read_fn)(u32 sector, u32 count, void *buffer);
typedef bool (*dldi_write_fn)(u32 sector, u32 count, const void *buffer);

typedef struct
{
    u32 ioType;
    u32 features;
    dldi_startup_fn startup;
    void *isInserted;
    dldi_read_fn readSectors;
    dldi_write_fn writeSectors;
    void *clearStatus;
    void *shutdown;
} dldi_io_t;

static const dldi_io_t *sDldi;
static dldi_proxy_t *sProxy;
static bool sStarted;

void fat_setDldi(const void *driver)
{
    sDldi = (const dldi_io_t *)((const u8 *)driver + 0x60);
    sProxy = NULL;
    sStarted = false;
}

void fat_setProxy(void *proxy)
{
    sProxy = (dldi_proxy_t *)proxy;
    sDldi = NULL;
    sStarted = true; // the ARM9 already started the driver
}

DSTATUS disk_status(BYTE pdrv)
{
    return (pdrv == 0 && sStarted) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0)
        return STA_NOINIT;
    if (!sStarted && sDldi)
        sStarted = sDldi->startup();
    return sStarted ? 0 : STA_NOINIT;
}

static bool proxy_io(u32 sector, u32 count, void *buf, bool write)
{
    sProxy->sector = sector;
    sProxy->count = count;
    sProxy->buffer = buf;
    sProxy->write = write;
    fifoSendValue32(FIFO_USER_02, (u32)sProxy);
    while (!fifoCheckValue32(FIFO_USER_02))
        ;
    return fifoGetValue32(FIFO_USER_02) != 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv != 0 || !sStarted)
        return RES_NOTRDY;
    if (sDldi)
        return sDldi->readSectors(sector, count, buff) ? RES_OK : RES_ERROR;

    // proxy: the ARM9 can only reach main RAM; word-aligned main RAM targets are filled directly
    u32 addr = (u32)buff;
    if (addr >= 0x02000000 && addr < 0x03000000 && (addr & 3) == 0)
        return proxy_io(sector, count, buff, false) ? RES_OK : RES_ERROR;
    while (count)
    {
        UINT n = count > DLDI_PROXY_SECTORS ? DLDI_PROXY_SECTORS : count;
        if (!proxy_io(sector, n, sProxy->bounce, false))
            return RES_ERROR;
        memcpy(buff, sProxy->bounce, n * 512);
        buff += n * 512;
        sector += n;
        count -= n;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv != 0 || !sStarted)
        return RES_NOTRDY;
    if (sDldi)
        return sDldi->writeSectors(sector, count, buff) ? RES_OK : RES_ERROR;
    while (count) // proxy: always through the bounce buffer (main RAM)
    {
        UINT n = count > DLDI_PROXY_SECTORS ? DLDI_PROXY_SECTORS : count;
        memcpy(sProxy->bounce, buff, n * 512);
        if (!proxy_io(sector, n, sProxy->bounce, true))
            return RES_ERROR;
        buff += n * 512;
        sector += n;
        count -= n;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    return RES_OK;
}
