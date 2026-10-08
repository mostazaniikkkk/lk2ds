#include <nds.h>
#include <string.h>
#include "fat/ff.h"
#include "fat.h"

FATFS gFatFs;

void fat_setDldi(const void *driver);
void fat_setProxy(void *proxy);

void fat_init(void)
{
    memset(&gFatFs, 0, sizeof(gFatFs));
}

bool fat_mountDldi(const void *driver)
{
    fat_setDldi(driver);
    return f_mount(&gFatFs, "fat:", 1) == FR_OK;
}

bool fat_mountProxy(void *proxy)
{
    fat_setProxy(proxy);
    return f_mount(&gFatFs, "fat:", 1) == FR_OK;
}
