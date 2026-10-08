#include <nds.h>
#include "check.h"
#include <stdio.h>
#include <malloc.h>
#include "ipc.h"
#include "twlwram.h"
#include "fvMcData.h"
#include "fvPlayer.h"

// LK2 DS changes (vs. upstream FastVideoDSPlayer):
// - frame buffers rotate through VRAM banks A, B, D (bank C is left free for the sub engine)
// - P-frame residuals live in main RAM instead of VRAM_D
// - the bitmap BG wraps and is shifted down by FV_TOP_OFFSET lines; rows 256-FV_TOP_OFFSET..255
//   of every frame bank hold a static header image (see fv_setHeader)

// vram block index -> bank index (0 = A, 1 = B, 3 = D)
static const u8 sBankOfBlock[3] = { 0, 1, 3 };
#define BLOCK_VRAM(b)   ((u32)VRAM_A + 0x20000 * sBankOfBlock[b])
#define BLOCK_CR(b)     (*(vu8*)(0x04000240 + sBankOfBlock[b]))

static u8* sResidual;


static void dbgWaitReply(void)
{
    CHECK(65);
    while (!fifoCheckValue32(FIFO_USER_01))
        ;
}

static ITCM_CODE void requestDataBuffer(fv_player_t* player)
{
    int irq = enterCriticalSection();
    do
    {
        if (player->isRequesting || player->endOfStream)
            break;
        // after the last frame the next pack entry follows in the .nds: never read it as video
        if (player->framesRequested >= player->fvHeader->nrFrames)
        {
            player->endOfStream = true;
            break;
        }
        player->framesRequested++;
        player->isRequesting = TRUE;

        if (isDSiMode())
        {
            // map wram block to arm7
            int slot = (player->dataBufferWriteIdx * 2) & 7;
            if (player->dataBufferWriteIdx < 4)
            {
                twr_mapWramBSlot(slot, TWR_WRAM_B_SLOT_MASTER_ARM7, slot, true);
                twr_mapWramBSlot(slot + 1, TWR_WRAM_B_SLOT_MASTER_ARM7, slot + 1, true);
            }
            else
            {
                twr_mapWramCSlot(slot, TWR_WRAM_C_SLOT_MASTER_ARM7, slot, true);
                twr_mapWramCSlot(slot + 1, TWR_WRAM_C_SLOT_MASTER_ARM7, slot + 1, true);
            }
        }
        fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_READ_FRAME,
                                                   (u32)fv_getPlayerDataBuffer(player, player->dataBufferWriteIdx)));
    } while (0);
    leaveCriticalSection(irq);
}

static ITCM_CODE void fifoHandler(u32 value, void* arg)
{
    fv_player_t* player = (fv_player_t*)arg;
    u32 cmd = value >> IPC_CMD_CMD_SHIFT;
    if (cmd == IPC_CMD_READ_FRAME)
    {
        if (isDSiMode())
        {
            // map wram block to arm9
            int slot = (player->dataBufferWriteIdx * 2) & 7;
            if (player->dataBufferWriteIdx < 4)
            {
                twr_mapWramBSlot(slot, TWR_WRAM_B_SLOT_MASTER_ARM9, slot, true);
                twr_mapWramBSlot(slot + 1, TWR_WRAM_B_SLOT_MASTER_ARM9, slot + 1, true);
            }
            else
            {
                twr_mapWramCSlot(slot, TWR_WRAM_C_SLOT_MASTER_ARM9, slot, true);
                twr_mapWramCSlot(slot + 1, TWR_WRAM_C_SLOT_MASTER_ARM9, slot + 1, true);
            }
        }
        player->isRequesting = FALSE;
        u32 length = value & IPC_CMD_ARG_MASK;
        if (length == 0)
        {
            player->endOfStream = true;
            return;
        }
        player->frameDataSizes[player->dataBufferWriteIdx] = length;
        player->frameHasAudio[player->dataBufferWriteIdx] = true;
        if (++player->dataBufferWriteIdx == FV_PLAYER_DATA_BUFFER_COUNT)
            player->dataBufferWriteIdx = 0;
        player->validDataBufferCount++;
        if (--player->freeDataBufferCount != 0 && player->isPlaying)
            requestDataBuffer(player);
    }
    else if (cmd == IPC_CMD_GOTO_KEYFRAME)
    {
        player->curFrame = value & IPC_CMD_ARG_MASK;
        player->seekComplete = true;
    }
    else if (cmd == IPC_CMD_GOTO_NEAREST_KEYFRAME)
    {
        player->lastKeyFrame = value & IPC_CMD_ARG_MASK;
    }
    // else if ((value >> IPC_CMD_CMD_SHIFT) == 0xE)
    // {
    //     // iprintf("%d\n", value & 0xFFFF);
    // }
}

static volatile DTCM_BSS int sVBlankCount;

void (*fv_vblankHook)(void);

static ITCM_CODE void vblankHandler()
{
    sVBlankCount++;
    if (fv_vblankHook)
        fv_vblankHook();
}

static void setupProjectionMtx()
{
    glMatrixMode(GL_PROJECTION);
    MATRIX_LOAD4x3 = divf32(inttof32(2), (256 << 6) - 0);
    MATRIX_LOAD4x3 = 0;
    MATRIX_LOAD4x3 = 0;

    MATRIX_LOAD4x3 = 0;
    MATRIX_LOAD4x3 = -21845;
    MATRIX_LOAD4x3 = 0;

    MATRIX_LOAD4x3 = 0;
    MATRIX_LOAD4x3 = 0;
    MATRIX_LOAD4x3 = 4096 >> 5;

    MATRIX_LOAD4x3 = -divf32((256 << 6), (256 << 6));
    MATRIX_LOAD4x3 = -divf32((192 << 6), -(192 << 6));
    MATRIX_LOAD4x3 = 0;
}

static void setupTextureMtx()
{
    glMatrixMode(GL_TEXTURE);
    MATRIX_LOAD4x3 = 4096 * 64 * 16;
    MATRIX_LOAD4x3 = 0;
    MATRIX_LOAD4x3 = 0;

    MATRIX_LOAD4x3 = 0;
    MATRIX_LOAD4x3 = 4096 * 64 * 8 * 16;
    MATRIX_LOAD4x3 = 0;

    MATRIX_LOAD4x3 = 0;
    MATRIX_LOAD4x3 = 0;
    MATRIX_LOAD4x3 = 0;

    MATRIX_LOAD4x3 = 0;
    MATRIX_LOAD4x3 = 0;
    MATRIX_LOAD4x3 = 0;
}

static void setup3DEngine()
{
    powerOn(POWER_3D_CORE | POWER_MATRIX);
    while (GFX_BUSY)
        ;
    GFX_STATUS |= 1 << 29; // clear fifo
    glResetMatrixStack();
    glFlush(1);
    GFX_CONTROL = 0x9;
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(0x7FFF);
    glFlush(0);
    setupProjectionMtx();
    setupTextureMtx();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glViewport(0, 0, 255, 191);
}

static ITCM_CODE void executeDisplayList(const fv_player_t* player)
{
    DMA3_SRC = (u32)fv_gMcGxCmdBuf;
    DMA3_DEST = (u32)&GFX_FIFO;
    DMA3_CR = DMA_FIFO | (player->dlLength >> 2);
}

static ITCM_CODE void startCapture(int dst)
{
    REG_DISPCAPCNT = DCAP_ENABLE | DCAP_MODE(DCAP_MODE_A) | DCAP_BANK(sBankOfBlock[dst]) | DCAP_SIZE(DCAP_SIZE_256x192) |
                     DCAP_SRC_A(DCAP_SRC_A_3DONLY);
}

#define BLOCK_VTX_PACK(x, y, z) (((x)&0x3FF) | ((((y) >> 3) & 0x3FF) << 10) | ((z) << 20))

static u32* makeMbRenderDl(u32* displayList, int height, u32 texParam)
{
    int y = 0;
    for (; y < height / 8 / 2; y++)
    {
        for (int x = 0; x < 256 / 8; x++)
        {
            *displayList++ = FIFO_COMMAND_PACK(FIFO_TEX_COORD, FIFO_VERTEX10, FIFO_VERTEX10, FIFO_VERTEX10);
            *displayList++ = 0; // texcoord placeholder
            *displayList++ = BLOCK_VTX_PACK(x * 8, y * 8 - 8, 33 - x);
            *displayList++ = BLOCK_VTX_PACK(x * 8, y * 8 + 8, 32 - x);
            *displayList++ = BLOCK_VTX_PACK(x * 8 + 16, y * 8 + 8, 32 - x);
        }
    }
    *displayList++ = FIFO_COMMAND_PACK(FIFO_END, FIFO_TEX_FORMAT, FIFO_BEGIN, FIFO_NOP);
    *displayList++ = texParam;
    *displayList++ = GL_TRIANGLES;
    for (; y < height / 8; y++)
    {
        for (int x = 0; x < 256 / 8; x++)
        {
            *displayList++ = FIFO_COMMAND_PACK(FIFO_TEX_COORD, FIFO_VERTEX10, FIFO_VERTEX10, FIFO_VERTEX10);
            *displayList++ = 0; // texcoord placeholder
            *displayList++ = BLOCK_VTX_PACK(x * 8, y * 8 - 8, 33 - x);
            *displayList++ = BLOCK_VTX_PACK(x * 8, y * 8 + 8, 32 - x);
            *displayList++ = BLOCK_VTX_PACK(x * 8 + 16, y * 8 + 8, 32 - x);
        }
    }
    return displayList;
}

bool fv_initPlayer(fv_player_t* player, const char* filePath, bool useWram)
{
    memset(player, 0, sizeof(fv_player_t));
    if (isDSiMode() && twr_isUnlocked() && useWram)
        player->dataBuffer = (u8*)twr_getBlockAddress(TWR_WRAM_BLOCK_B);
    else
        player->dataBuffer = memalign(32, FV_PLAYER_DATA_BUFFER_SIZE * FV_PLAYER_DATA_BUFFER_COUNT);
    player->fvHeader = memalign(32, sizeof(fv_header_t));
    player->dataBufferReadIdx = 0;
    player->dataBufferWriteIdx = 0;
    player->validDataBufferCount = 0;
    player->stage1Buffer = -1;
    player->stage2Buffer = -1;
    player->shownBlock = -1;
    fv_initDecoderAsm(&player->decoder);
    vramSetBankA(VRAM_A_LCD);
    vramSetBankB(VRAM_B_LCD);
    vramSetBankD(VRAM_D_LCD);
    if (!sResidual)
        sResidual = memalign(32, 32 * 24 * 192); // up to 256x192: 8x8 blocks * 192 bytes

    // open file
    fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_OPEN_FILE, filePath));
    dbgWaitReply();
    if ((fifoGetValue32(FIFO_USER_01) & IPC_CMD_ARG_MASK) == 0)
        return false;

    // open file and read header
    DC_InvalidateRange(player->fvHeader, sizeof(fv_header_t));
    fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_READ_HEADER, (u32)player->fvHeader));
    dbgWaitReply();
    player->vblankPerFrame = fifoGetValue32(FIFO_USER_01) & IPC_CMD_ARG_MASK;
    if (player->fvHeader->signature != FV_SIGNATURE)
        return false;
    if (player->vblankPerFrame == 0)
        return false;

    int height = player->fvHeader->height;

    player->decoder.yBlocks = height / 8;
    player->decoder.texYOffset = (256 - height) << 1;
    u32 texParam = 0xDED00000 + (((-(256 - height) * 256 * 2) >> 3) & 0xFFFF);

    // make display list
    u32* displayList = (u32*)fv_gMcGxCmdBufA;
    displayList = makeMbRenderDl(displayList, height, texParam);
    *displayList++ = FIFO_COMMAND_PACK(FIFO_END, FIFO_POLY_FORMAT, FIFO_TEX_FORMAT, FIFO_BEGIN);
    *displayList++ = 0x0F4080;
    *displayList++ = 0xDED00000;
    *displayList++ = GL_TRIANGLES;
    player->decoder.dlB = displayList + 1;
    displayList = makeMbRenderDl(displayList, height, texParam);
    *displayList++ = FIFO_COMMAND_PACK(FIFO_END, FIFO_FLUSH, FIFO_NOP, FIFO_NOP);
    *displayList++ = 0;

    player->dlLength = (u32)displayList - (u32)fv_gMcGxCmdBuf;

    DC_FlushRange(fv_gMcGxCmdBuf, player->dlLength);

    // int irq = enterCriticalSection();
    // iprintf("fps %d/%d\n", player->fvHeader->fpsNum, player->fvHeader->fpsDen);
    // iprintf("lcd %d/%d\n", player->fvHeader->fpsNum * player->vblankPerFrame, player->fvHeader->fpsDen);
    // leaveCriticalSection(irq);

    fifoSetValue32Handler(FIFO_USER_01, fifoHandler, player);
    setup3DEngine();

    player->lastKeyFrame = 0;
    player->curFrame = 0;

    return true;
}

static void stopAudioClearQueue(void);

// after the last frame of a clip: keep only the bank last shown as the BG. During playback the previous bank
// stays mapped until it is reused for decoding, which never happens once the clip is over, so two banks
// would remain mapped together and the still picture would show both mixed.
void fv_holdLastFrame(const fv_player_t* player)
{
    if (player->shownBlock < 0)
        return;
    for (int b = 0; b < 3; b++)
        if (b != player->shownBlock && BLOCK_CR(b) == 0x81)
            BLOCK_CR(b) = 0x80;
}

void fv_destroyPlayer(fv_player_t* player)
{
    player->isPlaying = false;
    CHECK(64);
    while (player->isRequesting)
        ;
    CHECK(67);
    stopAudioClearQueue();
    fifoSetValue32Handler(FIFO_USER_01, NULL, NULL);
    if (!isDSiMode())
        free(player->dataBuffer);
    free(player->fvHeader);
    player->dataBuffer = NULL;
    player->fvHeader = NULL;
}

ITCM_CODE void fv_startPlayer(fv_player_t* player)
{
    if (BLOCK_CR(0) == 0x81)
    {
        player->nextVramBlock = 1;
        BLOCK_CR(1) = 0x80;
        BLOCK_CR(2) = 0x80;
    }
    else if (BLOCK_CR(1) == 0x81)
    {
        player->nextVramBlock = 2;
        BLOCK_CR(0) = 0x80;
        BLOCK_CR(2) = 0x80;
    }
    else
    {
        player->nextVramBlock = 0;
        BLOCK_CR(0) = 0x80;
        BLOCK_CR(1) = 0x80;
    }

    player->stage3VramBlock = -1;
    player->stage2Buffer = -1;
    player->stage1Buffer = -1;
    player->freeDataBufferCount = FV_PLAYER_DATA_BUFFER_COUNT;
    player->validDataBufferCount = 0;
    player->dataBufferReadIdx = 0;
    player->dataBufferWriteIdx = 0;
    player->isPlaying = true;
    // start by loading the first blocks of data
    requestDataBuffer(player);
    CHECK(63);
    while (player->validDataBufferCount < 3)
        ;
    BG_PALETTE[0] = 0;
    swiWaitForVBlank();
    sVBlankCount = 0;
    irqSet(IRQ_VBLANK, vblankHandler);
    player->lateCount = 0;
    player->audioStarted = false;
    player->firstKeyFrame = true;

    // keep the sprite configuration (enable, 1D mapping, boundary, ext palette) set up by the game
    REG_DISPCNT = MODE_5_2D | DISPLAY_BG2_ACTIVE | (REG_DISPCNT & 0x80F01070);
    REG_BG2CNT = (u16)BgSize_B16_256x256 | BG_WRAP_ON | BG_PRIORITY_3;
    REG_BG2PA = 256;
    REG_BG2PB = 0;
    REG_BG2PC = 0;
    REG_BG2PD = 256;
    REG_BG2X = 0;
    REG_BG2Y = -(FV_TOP_OFFSET << 8);
}

static void startAudio(void)
{
    fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_CONTROL_AUDIO, IPC_ARG_CONTROL_AUDIO_START));
}

static void stopAudio(void)
{
    fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_CONTROL_AUDIO, IPC_ARG_CONTROL_AUDIO_STOP));
}

static void stopAudioClearQueue(void)
{
    fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_CONTROL_AUDIO, IPC_ARG_CONTROL_AUDIO_STOP_CLEAR));
}

ITCM_CODE void fv_updatePlayer(fv_player_t* player)
{
    CHECK(60);
    if (player->validDataBufferCount > 0)
    {
        player->stage1Buffer = player->dataBufferReadIdx;
        if (++player->dataBufferReadIdx == FV_PLAYER_DATA_BUFFER_COUNT)
            player->dataBufferReadIdx = 0;
        int irq = enterCriticalSection();
        {
            player->validDataBufferCount--;
        }
        leaveCriticalSection(irq);
    }
    const u16* nextStage2DataPtr = NULL;
    int nextStage2IsPFrame = FALSE;
    int doStartCapture = FALSE;
    int stage1VramBlock = -1;
    if (player->stage1Buffer != -1)
    {
        stage1VramBlock = player->nextVramBlock;
        if (++player->nextVramBlock == 3)
            player->nextVramBlock = 0;
        const u16* dataBuf = fv_getPlayerDataBuffer(player, player->stage1Buffer);
        DC_FlushAll();
        // DC_InvalidateRange(dataBuf, player->frameDataSizes[player->stage1Buffer]);
        int isPFrame = fv_frameIsP(dataBuf);
        if (isPFrame)
        {
            dataBuf = fv_decodePFrameVectors(&player->decoder, dataBuf);
            executeDisplayList(player);
            doStartCapture = TRUE;
        }
        nextStage2DataPtr = dataBuf;
        nextStage2IsPFrame = isPFrame;
    }
    bool nextStage3HasAudio = false;
    if (player->stage2Buffer != -1)
    {
        if (player->stage2IsPFrame)
        {
            fv_decodePFrameDcts(&player->decoder, player->stage2DataPtr, sResidual);
            // DC_InvalidateRange((u16*)((u32)VRAM_A + 0x20000 * player->stage2VramBlock), 256 *
            // player->fvHeader->height * 2);
            DC_FlushAll();
            CHECK(66);
            while (sVBlankCount == 0 && REG_VCOUNT >= 192)
                ;
            fv_finishPBlock(&player->decoder, sResidual, (u16*)BLOCK_VRAM(player->stage2VramBlock));
        }
        else
        {
            fv_decodeFrame_asm(&player->decoder, player->stage2DataPtr,
                               (u8*)BLOCK_VRAM(player->stage2VramBlock));
        }
        nextStage3HasAudio = player->frameHasAudio[player->stage2Buffer];
        int irq = enterCriticalSection();
        {
            player->freeDataBufferCount++;
        }
        leaveCriticalSection(irq);
        if (player->isPlaying)
            requestDataBuffer(player);
        DC_FlushAll();
        // DC_FlushRange((u16*)((u32)VRAM_A + 0x20000 * player->stage2VramBlock), 256 * player->fvHeader->height * 2);
    }
    if ((sVBlankCount == player->vblankPerFrame && (REG_VCOUNT >= 245 || REG_VCOUNT < 192)) ||
        sVBlankCount > player->vblankPerFrame)
    {
        player->lateCount++;
        swiWaitForVBlank();
    }
    else
    {
        CHECK(61);
        while (sVBlankCount < player->vblankPerFrame)
        {
            // halt to save energy, if a vblank irq already happened this will return immediately
            swiIntrWait(0, IRQ_VBLANK);
        }
    }
    sVBlankCount = 0;
    CHECK(62);

    // iprintf("\n%d", GFX_RDLINES_COUNT);

    if (player->stage1Buffer != -1)
    {
        BLOCK_CR(stage1VramBlock) = 0x80; // map to lcdc
    }
    if (player->stage3VramBlock != -1)
    {
        BLOCK_CR(player->stage3VramBlock) = 0x81; // map to bg
        player->shownBlock = player->stage3VramBlock;
        player->stage3VramBlock = -1;
        if (!player->audioStarted && player->stage3HasAudio)
        {
            player->audioStarted = true;
            startAudio();
        }

        if (player->firstKeyFrame)
            player->firstKeyFrame = false;
        else
        {
            player->curFrame++;
            if (!player->stage3IsPFrame)
                player->lastKeyFrame++;
        }
    }
    if (player->stage2Buffer != -1)
    {
        BLOCK_CR(player->stage2VramBlock) = 0x83; // map to tex
        player->stage3VramBlock = player->stage2VramBlock;
        player->stage2VramBlock = -1;
        player->stage3IsPFrame = player->stage2IsPFrame;
        player->stage3HasAudio = nextStage3HasAudio;
    }
    player->stage2Buffer = player->stage1Buffer;
    player->stage1Buffer = -1;
    player->stage2DataPtr = nextStage2DataPtr;
    player->stage2IsPFrame = nextStage2IsPFrame;
    player->stage2VramBlock = stage1VramBlock;
    if (doStartCapture)
        startCapture(stage1VramBlock);
}

void fv_pausePlayer(fv_player_t* player)
{
    player->isPlaying = false;
    while (player->isRequesting)
        ;
    stopAudioClearQueue();
    player->audioStarted = false;
    for (int i = 0; i < FV_PLAYER_DATA_BUFFER_COUNT; i++)
        player->frameHasAudio[i] = false;
    player->stage3HasAudio = false;
}

void fv_resumePlayer(fv_player_t* player)
{
    sVBlankCount = 0;
    player->isPlaying = true;
}

void fv_gotoKeyFrame(fv_player_t* player, u32 keyFrame)
{
    if (keyFrame >= player->fvHeader->nrKeyFrames)
        keyFrame = player->fvHeader->nrKeyFrames - 1;

    player->isPlaying = false;
    player->seekComplete = false;
    while (player->isRequesting)
        ;
    stopAudio();
    fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_GOTO_KEYFRAME, keyFrame));
    while (!player->seekComplete)
        ;
    while (REG_DISPCAPCNT & DCAP_ENABLE)
        ;
    player->lastKeyFrame = keyFrame;
}

void fv_gotoNearestKeyFrame(fv_player_t* player, u32 frame)
{
    if (frame >= player->fvHeader->nrFrames)
        frame = player->fvHeader->nrFrames - 1;

    player->isPlaying = false;
    player->seekComplete = false;
    while (player->isRequesting)
        ;
    stopAudio();
    fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_GOTO_NEAREST_KEYFRAME, frame));
    while (!player->seekComplete)
        ;
    while (REG_DISPCAPCNT & DCAP_ENABLE)
        ;
}

void fv_setHeader(const u16* header)
{
    // copy the header into the wrapped-around rows of all three frame banks
    for (int b = 0; b < 3; b++)
    {
        vu8* cr = &BLOCK_CR(b);
        u8 old = *cr;
        *cr = 0x80;
        dmaCopy(header, (u16*)(BLOCK_VRAM(b) + (256 - FV_TOP_OFFSET) * 512), FV_TOP_OFFSET * 512);
        *cr = old;
    }
}

void fv_clearFrames(u16 color)
{
    for (int b = 0; b < 3; b++)
    {
        vu8* cr = &BLOCK_CR(b);
        u8 old = *cr;
        *cr = 0x80;
        dmaFillHalfWords(color, (u16*)BLOCK_VRAM(b), (256 - FV_TOP_OFFSET) * 512);
        *cr = old;
    }
}
