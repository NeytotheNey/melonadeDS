// What the desktop frontend and tools/jitverify share for scripted runs: the --press parser, the
// --set switch table, file reading and the --bios setup. Header-only, no SDL or GL
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Args.h"
#include "GPU.h"
#include "Platform.h"
#include "SPI_Firmware.h"
#include "types.h"

namespace Frontend
{

// --press FRAME[-LAST]:key[+key...]: keys held over frames FROM..TO
struct KeyPress { melonDS::u32 From, To, Mask; };

// keys a b x y l r start select up down left right; held for FRAME..LAST, or 6 frames without a
// LAST. frames count from 0 after the reset (or --state)
inline bool ParsePress(const std::string& spec, KeyPress& press)
{
    static const struct { const char* Name; melonDS::u32 Bit; } kKeys[] = {
        {"a", 0}, {"b", 1}, {"select", 2}, {"start", 3}, {"right", 4}, {"left", 5}, {"up", 6},
        {"down", 7}, {"r", 8}, {"l", 9}, {"x", 10}, {"y", 11},
    };
    size_t colon = spec.find(':');
    if (colon == std::string::npos) return false;
    std::string range = spec.substr(0, colon);
    size_t dash = range.find('-');
    press.From = strtoul(range.c_str(), nullptr, 10);
    press.To = dash == std::string::npos ? press.From + 5 : strtoul(range.c_str() + dash + 1, nullptr, 10);
    press.Mask = 0;
    std::string keys = spec.substr(colon + 1);
    size_t pos = 0;
    for (;;)
    {
        size_t plus = keys.find('+', pos);
        std::string key = keys.substr(pos, plus == std::string::npos ? std::string::npos : plus - pos);
        bool found = false;
        for (const auto& k : kKeys)
        {
            if (key == k.Name) { press.Mask |= 1u << k.Bit; found = true; }
        }
        if (!found) return false;
        if (plus == std::string::npos) break;
        pos = plus + 1;
    }
    return press.To >= press.From;
}

// the keys the presses hold on a frame
inline melonDS::u32 PressMask(const std::vector<KeyPress>& presses, melonDS::u32 frame)
{
    melonDS::u32 mask = 0;
    for (const KeyPress& press : presses)
    {
        if (frame >= press.From && frame <= press.To)
            mask |= press.Mask;
    }
    return mask;
}

// the core's debug switches that --set can change, all on GPU. they're read as the frame runs
// or when a renderer is made, and are applied before the first frame
struct BoolSwitch { const char* Name; bool melonDS::GPU::* Field; };
struct IntSwitch { const char* Name; int melonDS::GPU::* Field; };

inline const BoolSwitch kBoolSwitches[] = {
    {"DeferCapture2D", &melonDS::GPU::DeferCapture2D},
    {"EngineBNoHold", &melonDS::GPU::EngineBNoHold},
    {"EarlyFlushCapture", &melonDS::GPU::EarlyFlushCapture},
    {"ShadowMaskLineClear", &melonDS::GPU::ShadowMaskLineClear},
    {"ShadowMaskDepthTest", &melonDS::GPU::ShadowMaskDepthTest},
    {"EqualDepthMargin", &melonDS::GPU::EqualDepthMargin},
    {"ComputeDirectDispatch", &melonDS::GPU::ComputeDirectDispatch},
    {"ComputeGrow", &melonDS::GPU::ComputeGrow},
    {"ComputeRooms", &melonDS::GPU::ComputeRooms},
    {"ComputeWarm", &melonDS::GPU::ComputeWarm},
    {"ShaderCache", &melonDS::GPU::ShaderCache},
    {"ComputeTimers", &melonDS::GPU::ComputeTimers},
    {"ComputePeaks", &melonDS::GPU::ComputePeaks},
    {"ComputeSync", &melonDS::GPU::ComputeSync},
    {"ComputePaceLog", &melonDS::GPU::ComputePaceLog},
    {"ComputeWhy", &melonDS::GPU::ComputeWhy},
    {"BlitFlushCapture", &melonDS::GPU::BlitFlushCapture},
    {"LateBlit", &melonDS::GPU::LateBlit},
    {"BlitAfterReadback", &melonDS::GPU::BlitAfterReadback},
    {"Filter2DLogging", &melonDS::GPU::Filter2DLogging},
    {"Hash2DLogging", &melonDS::GPU::Hash2DLogging},
    {"Hash3DLogging", &melonDS::GPU::Hash3DLogging},
    {"ComputeSSBO", &melonDS::GPU::ComputeSSBO},
    {"WidePolys", &melonDS::GPU::WidePolys},
    {"WideY", &melonDS::GPU::WideY},
    // the frontend's deband flag (GPU::OutputCap): the window doesn't deband, so this stands in for the app's
    {"OutputCapDeband", &melonDS::GPU::OutputCapDeband},
};

inline const IntSwitch kIntSwitches[] = {
    {"CaptureSpinUs", &melonDS::GPU::CaptureSpinUs},
    {"HiresCaptureDebug", &melonDS::GPU::HiresCaptureDebug},
    {"GLTransSkip", &melonDS::GPU::GLTransSkip},
    {"GLEdgeSkip", &melonDS::GPU::GLEdgeSkip},
    {"GLEdgeMark", &melonDS::GPU::GLEdgeMark},
    {"GLEdgeFast", &melonDS::GPU::GLEdgeFast},
    {"GLClearFast", &melonDS::GPU::GLClearFast},
    {"GLNoDiscard", &melonDS::GPU::GLNoDiscard},
    {"GLTexProbe", &melonDS::GPU::GLTexProbe},
    {"GLTexCache", &melonDS::GPU::GLTexCache},
    {"CompClear", &melonDS::GPU::CompClear},
    {"Comp1x", &melonDS::GPU::Comp1x},
    {"Filter2DRows", &melonDS::GPU::Filter2DRows},
    {"OutputCap", &melonDS::GPU::OutputCap},
    {"OutputCapNeed", &melonDS::GPU::OutputCapNeed},
    {"OutputCap3D", &melonDS::GPU::OutputCap3D},
    {"GLTimers", &melonDS::GPU::GLTimers},
    {"SceneStats", &melonDS::GPU::SceneStats},
    {"ComputeCheck", &melonDS::GPU::ComputeCheck},
    {"ComputeBin", &melonDS::GPU::ComputeBin},
    {"ComputeCalc", &melonDS::GPU::ComputeCalc},
    {"ComputeFlat", &melonDS::GPU::ComputeFlat},
    {"HeldTo2DThread", &melonDS::GPU::HeldTo2DThread},
};

inline bool ApplySet(melonDS::GPU& gpu, const std::string& set)
{
    size_t eq = set.find('=');
    if (eq == std::string::npos)
        return false;
    std::string name = set.substr(0, eq);
    int value = (int)strtol(set.c_str() + eq + 1, nullptr, 0);
    for (const BoolSwitch& s : kBoolSwitches)
    {
        if (name == s.Name)
        {
            gpu.*s.Field = value != 0;
            return true;
        }
    }
    for (const IntSwitch& s : kIntSwitches)
    {
        if (name == s.Name)
        {
            gpu.*s.Field = value;
            return true;
        }
    }
    return false;
}

inline void ListSets()
{
    for (const BoolSwitch& s : kBoolSwitches) printf("%s (0/1)\n", s.Name);
    for (const IntSwitch& s : kIntSwitches) printf("%s (number)\n", s.Name);
}

inline bool ReadFile(const std::string& path, std::vector<melonDS::u8>& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    fseeko64(f, 0, SEEK_END);
    melonDS::s64 len = ftello64(f);
    fseeko64(f, 0, SEEK_SET);
    out.resize((size_t)std::max<melonDS::s64>(len, 0));
    bool ok = len >= 0 && fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

// --bios DIR: the official bios9.bin and bios7.bin into args, and firmware.bin if DIR has one.
// returns false when the BIOS can't be read; haveFirmware says whether a firmware dump was loaded
// (otherwise the caller generates one). an empty DIR leaves FreeBIOS in args
inline bool LoadBiosDir(const std::string& dir, melonDS::NDSArgs& args, bool& haveFirmware)
{
    haveFirmware = false;
    if (dir.empty())
        return true;
    std::vector<melonDS::u8> bios9, bios7;
    if (!ReadFile(dir + "/bios9.bin", bios9) || bios9.size() != melonDS::ARM9BIOSSize
        || !ReadFile(dir + "/bios7.bin", bios7) || bios7.size() != melonDS::ARM7BIOSSize)
    {
        fprintf(stderr, "can't read bios9.bin and bios7.bin in %s\n", dir.c_str());
        return false;
    }
    memcpy(args.ARM9BIOS->data(), bios9.data(), melonDS::ARM9BIOSSize);
    memcpy(args.ARM7BIOS->data(), bios7.data(), melonDS::ARM7BIOSSize);

    std::string fwPath = dir + "/firmware.bin";
    if (melonDS::Platform::FileHandle* f = melonDS::Platform::OpenFile(fwPath, melonDS::Platform::FileMode::Read))
    {
        args.Firmware = melonDS::Firmware(f);
        melonDS::Platform::CloseFile(f);
        haveFirmware = args.Firmware.Buffer() != nullptr;
        if (haveFirmware)
            args.Firmware.UpdateChecksums();
    }
    return true;
}

}
