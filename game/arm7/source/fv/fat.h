#pragma once
#include "fat/ff.h"

extern FATFS gFatFs;

void fat_init(void);
bool fat_mountDldi(const void *driver);
bool fat_mountProxy(void *proxy);
