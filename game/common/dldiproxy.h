#pragma once

// Sector-read request shared in main RAM when the DLDI driver can only run on the ARM9.
// The ARM7 fills it and sends its address over FIFO_USER_02; the ARM9 reads the sectors and
// answers with 1 (ok) or 0 (error).

#define DLDI_PROXY_SECTORS 16

typedef struct
{
    u32 sector;
    u32 count;
    void *buffer;
    u32 write;                                    // 0: read sectors into buffer, 1: write buffer to them
    u32 pad[4];                                   // keep the bounce buffer cache-line aligned
    u8 bounce[DLDI_PROXY_SECTORS * 512];
} dldi_proxy_t;
