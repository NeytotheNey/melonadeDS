#include <cstring>
#include <type_traits>
#include "LuaEngineInternal.h"

using namespace melonDS;

namespace MelonDSAndroid
{

template <typename T>
static bool readTcm(ARMv5& arm9, u32 address, T& value)
{
    if (address < arm9.ITCMSize)
    {
        memcpy(&value, &arm9.ITCM[address & (ITCMPhysicalSize - 1)], sizeof(T));
        return true;
    }
    if ((address & arm9.DTCMMask) == arm9.DTCMBase)
    {
        memcpy(&value, &arm9.DTCM[address & (DTCMPhysicalSize - 1)], sizeof(T));
        return true;
    }
    return false;
}

template <typename T>
static bool writeTcm(NDS& nds, u32 address, T value)
{
    ARMv5& arm9 = nds.ARM9;
    if (address < arm9.ITCMSize)
    {
        memcpy(&arm9.ITCM[address & (ITCMPhysicalSize - 1)], &value, sizeof(T));
        // Code can run from ITCM, so drop any JIT block compiled from it (like ARMv5::DataWrite does)
        nds.JIT.CheckAndInvalidate<0, ARMJIT_Memory::memregion_ITCM>(address);
        return true;
    }
    if ((address & arm9.DTCMMask) == arm9.DTCMBase)
    {
        memcpy(&arm9.DTCM[address & (DTCMPhysicalSize - 1)], &value, sizeof(T));
        return true;
    }
    return false;
}

template <typename T>
static T busRead(NDS& nds, u32 address, bool isArm9)
{
    if constexpr (sizeof(T) == 1)
        return isArm9 ? nds.ARM9Read8(address) : nds.ARM7Read8(address);
    else if constexpr (sizeof(T) == 2)
        return isArm9 ? nds.ARM9Read16(address) : nds.ARM7Read16(address);
    else
        return isArm9 ? nds.ARM9Read32(address) : nds.ARM7Read32(address);
}

template <typename T>
static void busWrite(NDS& nds, u32 address, T value, bool isArm9)
{
    // Through the bus, like AR cheats. Main RAM and shared WRAM writes invalidate the JIT there
    if constexpr (sizeof(T) == 1)
        isArm9 ? nds.ARM9Write8(address, value) : nds.ARM7Write8(address, value);
    else if constexpr (sizeof(T) == 2)
        isArm9 ? nds.ARM9Write16(address, value) : nds.ARM7Write16(address, value);
    else
        isArm9 ? nds.ARM9Write32(address, value) : nds.ARM7Write32(address, value);
}

template <typename T, bool isSigned, bool isArm9>
int LuaMemoryApi::read(lua_State* L)
{
    u32 address = (u32) luaL_checkinteger(L, 1) & ~(u32) (sizeof(T) - 1);
    NDS& nds = *getScript(L).engine->nds;

    T value;
    bool isTcm = false;
    if constexpr (isArm9)
        isTcm = readTcm(nds.ARM9, address, value);

    if (!isTcm)
    {
        // Reading some I/O registers has side effects (like popping the IPC FIFO), so they are off limits
        if ((address & 0xFF000000) == 0x04000000)
            return luaL_error(L, "can't read I/O register 0x%08X: reads can change the emulated state", (unsigned int) address);

        value = busRead<T>(nds, address, isArm9);
    }

    if constexpr (isSigned)
        lua_pushinteger(L, (std::make_signed_t<T>) value);
    else
        lua_pushinteger(L, value);
    return 1;
}

template <typename T, bool isArm9>
int LuaMemoryApi::write(lua_State* L)
{
    u32 address = (u32) luaL_checkinteger(L, 1) & ~(u32) (sizeof(T) - 1);
    T value = (T) luaL_checkinteger(L, 2);
    NDS& nds = *getScript(L).engine->nds;

    if constexpr (isArm9)
    {
        if (writeTcm(nds, address, value))
            return 0;
    }

    busWrite<T>(nds, address, value, isArm9);
    return 0;
}

const luaL_Reg LuaMemoryApi::arm9Functions[] = {
    { "read8", read<u8, false, true> },
    { "read16", read<u16, false, true> },
    { "read32", read<u32, false, true> },
    { "read_s8", read<u8, true, true> },
    { "read_s16", read<u16, true, true> },
    { "read_s32", read<u32, true, true> },
    { "write8", write<u8, true> },
    { "write16", write<u16, true> },
    { "write32", write<u32, true> },
    { nullptr, nullptr },
};

const luaL_Reg LuaMemoryApi::arm7Functions[] = {
    { "read8", read<u8, false, false> },
    { "read16", read<u16, false, false> },
    { "read32", read<u32, false, false> },
    { "read_s8", read<u8, true, false> },
    { "read_s16", read<u16, true, false> },
    { "read_s32", read<u32, true, false> },
    { "write8", write<u8, false> },
    { "write16", write<u16, false> },
    { "write32", write<u32, false> },
    { nullptr, nullptr },
};

}
