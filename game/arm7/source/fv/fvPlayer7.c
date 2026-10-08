#include <nds.h>
#include <string.h>
#include "isdprint.h"
#include "ipc.h"
#include "fat.h"
#include "adpcm.h"
#include "irqWait.h"
#include "twlwram.h"
#include "fvPlayer7.h"

#define FV_AUDIO_START_OFFSET 12

#define FV_AUDIO_CH_LEFT  1
#define FV_AUDIO_CH_RIGHT 3

static fv_player7_t sPlayer;
static FIL sPak;
static bool sPakOpen;
// the video is a file inside the .nds asset pack: every seek is relative to where it starts
static u32 sVideoBase;

// video sound level: kept below the cabinet sound effects (which play at full volume)
#define FV_AUDIO_VOLUME 0x58

// debug aid: 4-bit progress visible to the ARM9 through IPC_SYNC
#define DBG_STAGE(n) (REG_IPC_SYNC = (REG_IPC_SYNC & ~0x0F00) | ((n) << 8))

void fv_init(void)
{
    memset(&sPlayer, 0, sizeof(sPlayer));
}

static inline int getAudioTimerValue(int rate)
{
    return (16756991 + ((rate + 1) >> 1)) / rate;
}

static void audioFrameIrq(void)
{
}

static void decodeAudioFrame(void)
{
    adpcm_decompress(sPlayer.audioQueueL[sPlayer.queueReadPtr], FV_AUDIO_FRAME_SIZE,
                     sPlayer.audioRingL[sPlayer.ringPos]);
    adpcm_decompress(sPlayer.audioQueueR[sPlayer.queueReadPtr], FV_AUDIO_FRAME_SIZE,
                     sPlayer.audioRingR[sPlayer.ringPos]);

    // sPlayer.ringVideoFrameIds[sPlayer.ringPos] = sPlayer.queueVideoFrameIds[sPlayer.queueReadPtr];

    if (++sPlayer.ringPos == FV_AUDIO_RING_FRAMES)
        sPlayer.ringPos = 0;

    if (++sPlayer.queueReadPtr == FV_AUDIO_QUEUE_FRAMES)
        sPlayer.queueReadPtr = 0;

    sPlayer.queueFrameCount--;

    sPlayer.audioFramesNeeded--;
    sPlayer.audioFramesProvided++;
}

static void startAudio(void)
{
    if (sPlayer.audioStarted)
        return;

    sPlayer.ringPos = 0;
    sPlayer.audioFramesNeeded = 0;
    sPlayer.audioFramesProvided = 0;

    for (int i = 0; i < FV_AUDIO_START_OFFSET; i++)
    {
        if (sPlayer.queueFrameCount == 0)
            break;
        decodeAudioFrame();
    }

    sPlayer.audioFramesProvided -= FV_AUDIO_START_OFFSET;

    int tmr = getAudioTimerValue(FV_AUDIO_RATE);
    SCHANNEL_SOURCE(FV_AUDIO_CH_LEFT) = (u32)sPlayer.audioRingL;
    SCHANNEL_REPEAT_POINT(FV_AUDIO_CH_LEFT) = 0;
    SCHANNEL_LENGTH(FV_AUDIO_CH_LEFT) = sizeof(sPlayer.audioRingL) >> 2;
    SCHANNEL_TIMER(FV_AUDIO_CH_LEFT) = -tmr;

    SCHANNEL_SOURCE(FV_AUDIO_CH_RIGHT) = (u32)sPlayer.audioRingR;
    SCHANNEL_REPEAT_POINT(FV_AUDIO_CH_RIGHT) = 0;
    SCHANNEL_LENGTH(FV_AUDIO_CH_RIGHT) = sizeof(sPlayer.audioRingR) >> 2;
    SCHANNEL_TIMER(FV_AUDIO_CH_RIGHT) = -tmr;

    TIMER_CR(0) = 0;
    TIMER_CR(1) = 0;
    TIMER_CR(2) = 0;
    TIMER_CR(3) = 0;

    TIMER_DATA(0) = -2;   // 1/2 clock divider
    TIMER_DATA(1) = -tmr; // sample rate
    TIMER_DATA(2) = -256; // length of audio frame
    TIMER_DATA(3) = 0;    // audio block counter

    TIMER_CR(3) = TIMER_CASCADE | TIMER_ENABLE;
    TIMER_CR(2) = TIMER_CASCADE | TIMER_ENABLE | TIMER_IRQ_REQ;
    TIMER_CR(1) = TIMER_CASCADE | TIMER_ENABLE;

    irqSet(IRQ_TIMER2, audioFrameIrq);
    irqEnable(IRQ_TIMER2);

    SCHANNEL_CR(FV_AUDIO_CH_LEFT) =
        SCHANNEL_ENABLE | SOUND_VOL(FV_AUDIO_VOLUME) | SOUND_PAN(0) | SOUND_FORMAT_16BIT | SOUND_REPEAT;
    SCHANNEL_CR(FV_AUDIO_CH_RIGHT) =
        SCHANNEL_ENABLE | SOUND_VOL(FV_AUDIO_VOLUME) | SOUND_PAN(0x7F) | SOUND_FORMAT_16BIT | SOUND_REPEAT;
    TIMER_CR(0) = TIMER_ENABLE;

    sPlayer.audioStarted = true;
}

static void stopAudio(void)
{
    if (!sPlayer.audioStarted)
        return;

    sPlayer.audioStarted = false;
    TIMER_CR(0) = 0;
    irqDisable(IRQ_TIMER2);
    SCHANNEL_CR(FV_AUDIO_CH_LEFT) = 0;
    SCHANNEL_CR(FV_AUDIO_CH_RIGHT) = 0;
    TIMER_CR(1) = 0;
    TIMER_CR(2) = 0;
    TIMER_CR(3) = 0;
    sPlayer.ringPos = 0;
    memset(&sPlayer.audioRingL[0][0], 0, sizeof(sPlayer.audioRingL));
    memset(&sPlayer.audioRingR[0][0], 0, sizeof(sPlayer.audioRingR));
}

static void updateAudio(void)
{
    if (!sPlayer.audioStarted)
        return;

    int audioBlocks = TIMER_DATA(3);
    int needed = audioBlocks - (sPlayer.audioFramesProvided & 0xFFFF);
    if (needed < 0)
        needed += 65536;

    while (needed > 0 && sPlayer.queueFrameCount > 0)
    {
        decodeAudioFrame();
        needed--;
    }
}

static void gotoKeyFrameDirect(const fv_keyframe_t* keyFrameData)
{
    f_lseek(&sPlayer.file, sVideoBase + keyFrameData->offset);

    // reset player state
    stopAudio();

    sPlayer.ringPos = 0;
    sPlayer.queueReadPtr = 0;
    sPlayer.queueWritePtr = 0;
    sPlayer.queueFrameCount = 0;
    sPlayer.audioFramesNeeded = 0;
    sPlayer.audioFramesProvided = 0;

    memset(&sPlayer.audioQueueL[0][0], 0, sizeof(sPlayer.audioQueueL));
    memset(&sPlayer.audioQueueR[0][0], 0, sizeof(sPlayer.audioQueueR));
}

static u32 gotoKeyFrame(u32 keyFrame)
{
    UINT br;
    fv_keyframe_t keyFrameData;

    f_lseek(&sPlayer.file, sVideoBase + sizeof(fv_header_t) + sizeof(fv_keyframe_t) * keyFrame);
    f_read(&sPlayer.file, &keyFrameData, sizeof(fv_keyframe_t), &br);

    gotoKeyFrameDirect(&keyFrameData);

    return keyFrameData.frame;
}

static u32 gotoNearestKeyFrame(u32 frame, u32* resultFrame)
{
    UINT br;
    fv_keyframe_t keyFrameData = { 0 };
    fv_keyframe_t newKeyFrameData;
    u32 keyFrameId = -1;

    f_lseek(&sPlayer.file, sVideoBase + sizeof(fv_header_t));

    for (int i = 0; i < sPlayer.nrKeyFrames; i++)
    {
        f_read(&sPlayer.file, &newKeyFrameData, sizeof(fv_keyframe_t), &br);
        if (newKeyFrameData.frame <= frame)
        {
            keyFrameData = newKeyFrameData;
            keyFrameId = i;
        }

        if (newKeyFrameData.frame >= frame)
            break;
    }

    gotoKeyFrameDirect(&keyFrameData);

    if (resultFrame)
        *resultFrame = keyFrameData.frame;

    return keyFrameId;
}

static void handleFifo(u32 value)
{
    UINT br;
    REG_IPC_SYNC = (REG_IPC_SYNC & ~0x0F00) | (9 << 8); // debug: got a command

    switch (value >> IPC_CMD_CMD_SHIFT)
    {
        case IPC_CMD_READ_FRAME:
        {
            u32 len;
            DBG_STAGE(10);
            if (f_read(&sPlayer.file, &len, 4, &br) != FR_OK || br != 4)
            {
                DBG_STAGE(14);
                fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_READ_FRAME, 0));
                break;
            }
            DBG_STAGE(11);
            f_read(&sPlayer.file, (void*)(value & IPC_CMD_ARG_MASK), len & 0x1FFFF, &br);
            DBG_STAGE(12);
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_READ_FRAME, len & 0x1FFFF));
            DBG_STAGE(13);
            // read audio frames
            u32 audioFrames = len >> 17;
            for (int i = 0; i < audioFrames; i++)
            {
                f_read(&sPlayer.file, sPlayer.audioQueueL[sPlayer.queueWritePtr], FV_AUDIO_FRAME_SIZE, &br);
                f_read(&sPlayer.file, sPlayer.audioQueueR[sPlayer.queueWritePtr], FV_AUDIO_FRAME_SIZE, &br);
                // sPlayer.queueVideoFrameIds[sPlayer.queueWritePtr] = sPlayer.curVideoFrame;
                if (++sPlayer.queueWritePtr == FV_AUDIO_QUEUE_FRAMES)
                    sPlayer.queueWritePtr = 0;

                sPlayer.queueFrameCount++;
            }

            // sPlayer.curVideoFrame++;
            break;
        }

        case IPC_CMD_OPEN_FILE:
        {
            DBG_STAGE(2);
            FRESULT result = f_open(&sPlayer.file, (const char*)(value & IPC_CMD_ARG_MASK), FA_OPEN_EXISTING | FA_READ);
            if (result == FR_OK)
                result = f_lseek(&sPlayer.file, sVideoBase);
            DBG_STAGE(3);
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_OPEN_FILE, result == FR_OK ? 1 : 0));
            break;
        }

        case IPC_CMD_VIDEO_BASE:
            sVideoBase = value & IPC_CMD_ARG_MASK;
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_VIDEO_BASE, 1));
            break;

        case IPC_CMD_READ_HEADER:
        {
            fv_header_t* header = (fv_header_t*)(value & IPC_CMD_ARG_MASK);
            f_read(&sPlayer.file, header, sizeof(fv_header_t), &br);
            DBG_STAGE(4);

            sPlayer.nrKeyFrames = header->nrKeyFrames;

            gotoKeyFrame(0);
            DBG_STAGE(5);

            u32 num = header->fpsNum;
            u32 den = header->fpsDen;

            int vblankCount = 1;
            while (num * (vblankCount + 1) / den < 62)
                vblankCount++;

            // safety
            if (num * vblankCount / den < 62)
            {
                fpsa_init(&sPlayer.fpsa);
                fpsa_setTargetFpsFraction(&sPlayer.fpsa, num * vblankCount, den);
                sPlayer.fpsa.targetCycles =
                    (double)sPlayer.fpsa.targetCycles * getAudioTimerValue(FV_AUDIO_RATE) * FV_AUDIO_RATE / 16756991.0;
                fpsa_start(&sPlayer.fpsa);
            }
            DBG_STAGE(7);

            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_READ_HEADER, vblankCount));
            break;
        }

        case IPC_CMD_GOTO_KEYFRAME:
        {
            u32 frame = gotoKeyFrame(value & IPC_CMD_ARG_MASK);
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_GOTO_KEYFRAME, frame));
            break;
        }

        case IPC_CMD_GOTO_NEAREST_KEYFRAME:
        {
            u32 frame = value & IPC_CMD_ARG_MASK;
            u32 resultFrame;
            u32 keyFrame = gotoNearestKeyFrame(frame, &resultFrame);
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_GOTO_NEAREST_KEYFRAME, keyFrame));
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_GOTO_KEYFRAME, resultFrame));
            break;
        }

        case IPC_CMD_CONTROL_AUDIO:
        {
            if ((value & IPC_CMD_ARG_MASK) == IPC_ARG_CONTROL_AUDIO_START)
                startAudio();
            else if ((value & IPC_CMD_ARG_MASK) == IPC_ARG_CONTROL_AUDIO_STOP)
                stopAudio();
            else if ((value & IPC_CMD_ARG_MASK) == IPC_ARG_CONTROL_AUDIO_STOP_CLEAR)
            {
                stopAudio();
                sPlayer.ringPos = 0;
                sPlayer.queueReadPtr = 0;
                sPlayer.queueWritePtr = 0;
                sPlayer.queueFrameCount = 0;
                sPlayer.audioFramesNeeded = 0;
                sPlayer.audioFramesProvided = 0;

                memset(&sPlayer.audioQueueL[0][0], 0, sizeof(sPlayer.audioQueueL));
                memset(&sPlayer.audioQueueR[0][0], 0, sizeof(sPlayer.audioQueueR));
            }
            break;
        }

        case IPC_CMD_SETUP_DLDI:
        {
            // arg = main RAM address of the DLDI driver, already relocated by the ARM9
            bool ok = fat_mountDldi((const void*)(value & IPC_CMD_ARG_MASK));
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_SETUP_DLDI, ok ? 1 : 0));
            break;
        }

        case IPC_CMD_PAK_OPEN:
        {
            // the asset pack is read straight from the .nds file (its own handle: video keeps sPlayer.file)
            if (sPakOpen)
                f_close(&sPak);
            sPakOpen = f_open(&sPak, (const char*)(value & IPC_CMD_ARG_MASK), FA_OPEN_EXISTING | FA_READ) == FR_OK;
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_PAK_OPEN, sPakOpen ? 1 : 0));
            break;
        }

        case IPC_CMD_PAK_READ:
        {
            const pak_read_t* req = (const pak_read_t*)(value & IPC_CMD_ARG_MASK);
            UINT got = 0;
            if (sPakOpen && f_lseek(&sPak, req->offset) == FR_OK)
                f_read(&sPak, req->buffer, req->size, &got);
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_PAK_READ, got));
            break;
        }

        case IPC_CMD_SAVE:
        {
            const save_req_t* req = (const save_req_t*)(value & IPC_CMD_ARG_MASK);
            FIL f;
            UINT n = 0;
            // written in place (like a cartridge save): an existing .sav needs no free space on the card
            BYTE mode = req->write ? FA_OPEN_ALWAYS | FA_WRITE : FA_OPEN_EXISTING | FA_READ;
            FRESULT ro = f_open(&f, req->path, mode), rw = FR_OK, rc = FR_OK;
            if (ro == FR_OK)
            {
                if (req->write)
                    rw = f_write(&f, req->buffer, req->size, &n);
                else
                    rw = f_read(&f, req->buffer, req->size, &n);
                rc = f_close(&f);
            }
            // bytes done, or (with nothing done) the FatFs error codes for the debug display
            u32 ret = n && rc == FR_OK ? n : 0x100000 | (ro << 12) | (rw << 6) | rc;
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_SAVE, ret));
            break;
        }

        case IPC_CMD_SETUP_PROXY:
        {
            bool ok = fat_mountProxy((void*)(value & IPC_CMD_ARG_MASK));
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_SETUP_PROXY, ok ? 1 : 0));
            break;
        }

        case IPC_CMD_HANDSHAKE:
        {
            bool canUseWram = false;
            if (isDSiMode())
                canUseWram = twr_isUnlocked();
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_HANDSHAKE, canUseWram ? 1 : 0));
            break;
        }
    }
}

void fv_main(void)
{
    if (((REG_IPC_SYNC >> 8) & 15) == 7)
        REG_IPC_SYNC = (REG_IPC_SYNC & ~0x0F00) | (8 << 8); // debug: main loop running
    if (!fifoCheckValue32(FIFO_USER_01))
        irq_wait(false, IRQ_TIMER2 | IRQ_FIFO_NOT_EMPTY);

    updateAudio();

    if (fifoCheckValue32(FIFO_USER_01))
        handleFifo(fifoGetValue32(FIFO_USER_01));
}
