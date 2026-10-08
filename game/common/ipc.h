#pragma once

#define IPC_CMD_READ_FRAME            1
#define IPC_CMD_OPEN_FILE             2
#define IPC_CMD_CONTROL_AUDIO         3
#define IPC_CMD_READ_HEADER           4
#define IPC_CMD_GOTO_KEYFRAME         5
#define IPC_CMD_GOTO_NEAREST_KEYFRAME 6
#define IPC_CMD_PAK_OPEN              7   // arg = path of the .nds (asset pack inside its NitroFS) -> 1 ok / 0
#define IPC_CMD_PAK_READ              8   // arg = pak_read_t in main RAM -> bytes read
#define IPC_CMD_VIDEO_BASE            10  // arg = byte offset of the video inside the opened file
#define IPC_CMD_SETUP_PROXY           9   // arg = dldi_proxy_t in main RAM (ARM9-only DLDI driver)
#define IPC_CMD_SETUP_DLDI            13
#define IPC_CMD_SAVE                  11  // arg = save_req_t in main RAM -> bytes read / written
#define IPC_CMD_HANDSHAKE             15

#define IPC_CMD_ARG_MASK       0x0FFFFFFF
#define IPC_CMD_CMD_SHIFT      28
#define IPC_CMD_CMD_MASK       0xF0000000
#define IPC_CMD_PACK(cmd, arg) ((((u32)(cmd) << IPC_CMD_CMD_SHIFT) & IPC_CMD_CMD_MASK) | ((u32)(arg)&IPC_CMD_ARG_MASK))

#define IPC_ARG_CONTROL_AUDIO_STOP       0
#define IPC_ARG_CONTROL_AUDIO_START      1
#define IPC_ARG_CONTROL_AUDIO_STOP_CLEAR 2

// request for IPC_CMD_PAK_READ (main RAM, written by the ARM9, read by the ARM7)
typedef struct
{
    unsigned int offset; // byte offset in the .nds file
    unsigned int size;
    void *buffer;
    unsigned int pad[5];
} pak_read_t;

// request for IPC_CMD_SAVE: whole-file read or write of the save file (path: "<rom without .nds>.sav")
typedef struct
{
    const char *path;
    void *buffer;
    unsigned int size;
    unsigned int write; // 0 read, 1 write (create / overwrite)
    unsigned int pad[4];
} save_req_t;
