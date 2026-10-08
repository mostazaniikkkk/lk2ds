#pragma once
#include <nds.h>
#include "pak_ids.h"

// On-demand assets from the pack inside the ROM's NitroFS (tools/make_pak.py). The ARM7 reads the
// .nds file itself (the same way it streams video), so nothing here stays in RAM unless loaded.

// rom_path: argv[0] given by the loader
bool asset_init(const char *rom_path);
// allocate + load; NULL on error. Free with asset_free().
void *asset_load(pak_id_t id, u32 *size);
void asset_free(void *data);
// path of the .nds and absolute byte offset of an entry in it (for data streamed by the ARM7, e.g. video)
const char *asset_rom_path(void);
u32 asset_rom_offset(pak_id_t id);
// load an LZ77 bitmap/band, decompress it into VRAM and free it again
bool asset_show_lz(pak_id_t id, void *vram);
