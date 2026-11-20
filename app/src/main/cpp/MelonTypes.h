#ifndef MELONTYPES_H
#define MELONTYPES_H

#include <cstdint>
#include <vector>
#include "types.h"

// Types MelonInstance shares with the frontends (MelonDS.h on Android), kept apart so the shared native code doesn't need MelonDS.h
namespace MelonDSAndroid
{

typedef struct {
    std::vector<melonDS::u32> code;
} Cheat;

#ifdef MELONADE_DEBUG_TOOLS
/**
 * Debug builds only: the capture readback stats summed since the last call (compute renderer only): readback maps, ns waited in the
 * map, ns spent polling the capspin fence first, and the ns from each RenderFrame start to its map's return
 */
struct CaptureStats {
    int64_t maps = 0;
    int64_t waitNs = 0;
    int64_t spinNs = 0;
    int64_t spanNs = 0;
};
#endif

}

#endif //MELONTYPES_H
