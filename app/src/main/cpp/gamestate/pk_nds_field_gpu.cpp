// Copied from PokeDaisy's app/src/main/cpp/nds/pk_nds_field_gpu.cpp (https://github.com/lidor30/pokedaisy, commit 69f5b1ea), GNU GPL 3.0.
// PokeDaisy, Copyright (C) 2026 Lidor Itzhari. Unchanged below this header: melonadeDS's core has the same GPU2D fields.
// Kept as close to verbatim as possible so fixes can be diffed both ways; melonadeDS adapts it in GameState.cpp.

// pk_nds_field.h's one read of melonDS itself (the rest of the field read is RAM only: pk_nds_field.cpp).

#include "pk_nds_field.h"

#include <NDS.h>

int pk_nds_bg3_tiles(melonDS::NDS* nds, int bg)
{
    uint32_t disp = nds->GPU.GPU2D_A.DispCnt;
    if (!(disp & (1 << (8 + bg)))) return 0;   // the BG is off
    if (bg == 3 && (disp & 7) > 1) return 0;   // not a text BG in this mode
    if (bg == 2 && (disp & 7) > 3) return 0;
    uint16_t cnt = nds->GPU.GPU2D_A.BGCnt[bg];
    uint32_t base = 0x06000000u + ((disp >> 27) & 7) * 0x10000 + ((cnt >> 8) & 0x1F) * 0x800;
    int n = 0;
    for (uint32_t i = 0; i < 32 * 24; i++)
        if (nds->ARM9Read16(base + i * 2) & 0x3FF) n++;
    return n;
}
