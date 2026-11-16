// jitverify: runs a DS game headless and deterministically from boot (or a savestate) with scripted
// input, and writes one line per frame with hashes of both screens, the frame's audio, the CPU
// timestamps, the ARM9/ARM7 registers, their control state and memory. Two runs (the same backend
// before and after a change, or a switch flipped) are then diffed with compare.py to find the first
// frame that differs. Software 3D and 2D on the emulator thread, no window, no audio output.
// See tools/jitverify/README.md
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "NDS.h"
#include "NDSCart.h"
#include "Args.h"
#include "SPI_Firmware.h"
#include "GPU.h"
#include "GPU3D_Soft.h"
#include "Platform.h"
#include "Savestate.h"
#include "xxhash/xxhash.h"

#include "Frontend.h"
#include "RunOptions.h"

using namespace melonDS;
namespace fs = std::filesystem;

namespace
{

// the core keeps the system timestamp protected: a subclass reads it without touching the core
class VerifyNDS : public NDS
{
public:
    using NDS::NDS;
    u64 SysTime() const { return SysTimestamp; }
};

struct Options
{
    std::string RomPath;
    std::string BiosDir;
    std::string SavePath;   // read once as the starting save, never written
    std::string StatePath;
    std::string OutPath;
    std::string LogPath;
    u32 Frames = 3000;
    bool Jit = true;
    bool FastMem = true;
    int LdmDirect = -1;     // -1 keeps the core's default
    int LdmFast = -1;
    int JitDispatch = -1;
    bool LdmStats = false;
    int BlockSize = 32;
    bool LiteralOpt = true;
    bool BranchOpt = true;
    bool Regs = false;      // raw registers too
    bool Mem = true;        // the memory fields
    int Rtc[6] = {2026, 1, 1, 12, 0, 0};
    u32 FlushEvery = 1;
    std::vector<std::string> Sets;
    std::vector<Frontend::KeyPress> Presses;
    std::vector<std::string> PressSpecs; // as given, for the header
    struct At { u32 Frame; std::string Action; };
    std::vector<At> Ats;
};

void Usage()
{
    printf(
        "usage: jitverify [options] game.nds\n"
        "\n"
        "  --out FILE            per-frame lines (default: stdout)\n"
        "  --frames N            frames to run, decimal (default 3000)\n"
        "  --jit / --no-jit      JIT or interpreter (the JIT is on by default)\n"
        "  --fastmem             fast memory, as the app runs it (the default); doesn't turn the JIT on\n"
        "  --no-fastmem          JIT without fast memory\n"
        "  --ldmdirect B         slow block transfers in one plain region straight in host memory (core default 1)\n"
        "  --ldmfast B           ARM64: multi-register loads through fastmem (core default 1)\n"
        "  --jitdispatch B       ARM64: block exits jump to the next block in generated code (core default 0)\n"
        "  --ldmstats            log the LDMSTATS lines (to --log)\n"
        "  --block-size N        JIT max block size, 1-32 (default 32, the app's)\n"
        "  --literal-opt B       JIT literal optimizations (default 1)\n"
        "  --branch-opt B        JIT branch optimizations (default 1)\n"
        "                        B is 0/1, true/false or on/off; for every option, the last one given wins\n"
        "  --set NAME=VALUE      a core debug switch on GPU, as the desktop's --set (see --list-sets)\n"
        "  --list-sets           list the switches --set knows\n"
        "  --bios DIR            official bios9.bin, bios7.bin (and firmware.bin) in DIR; FreeBIOS and a\n"
        "                        generated firmware otherwise\n"
        "  --sav FILE            starting save, read only: the run's save writes stay in memory\n"
        "  --state FILE          load a savestate after the reset (frames count from there)\n"
        "  --press F[-L]:KEYS    hold keys (a b x y l r start select up down left right, joined by +)\n"
        "                        over frames F..L (6 frames without L)\n"
        "  --at F:reset          reset (and boot again) before frame F\n"
        "  --at F:state          load --state again before frame F; --at can be given more than once\n"
        "  --rtc Y,M,D,h,m,s     RTC start (default 2026,1,1,12,0,0; the clock is always pinned)\n"
        "  --fixed-time          accepted for the desktop's command lines; the time is always fixed\n"
        "  --regs                raw R0-R15 and CPSR of both CPUs too (a9r=, a7r=)\n"
        "  --no-mem              leave out the memory fields (main wram tcm vram save)\n"
        "  --flush-every N       write and flush --out every N frames (default 1); lines are never split\n"
        "  --log FILE            core log (default: none; errors still go to stderr)\n"
        "\n"
        "line: F <n> top= bot= aud= a9ts= a7ts= sys= a9= a7= ctl= main= wram= tcm= vram= save=\n"
        "  top/bot  the two screens          aud   the frame's audio samples\n"
        "  a9/a7    R0-R15, CPSR and every banked register and SPSR\n"
        "  ctl      ARM9 CP15 (control, DTCM/ITCM, protection unit), both CPUs' Halted, IME/IE/IF of both\n"
        "  main     main RAM                 wram  shared WRAM + ARM7 WRAM    tcm  ITCM + DTCM\n"
        "  vram     VRAM A-I, palette, OAM   save  cart save memory\n"
        "all hashes are XXH3-64\n");
}

bool ParseBool(const std::string& s, bool& out)
{
    if (s == "1" || s == "true" || s == "on") { out = true; return true; }
    if (s == "0" || s == "false" || s == "off") { out = false; return true; }
    return false;
}

bool ParseDecimal(const std::string& s, u32& out)
{
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
        return false;
    out = (u32)strtoul(s.c_str(), nullptr, 10);
    return true;
}

bool ParseArgs(int argc, char** argv, Options& opts)
{
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        auto next = [&]() -> std::string
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "%s needs a value\n", arg.c_str());
                exit(1);
            }
            return argv[++i];
        };
        auto flag = [&]() -> bool
        {
            std::string v = next();
            bool b;
            if (!ParseBool(v, b))
            {
                fprintf(stderr, "%s needs 0/1, true/false or on/off, not %s\n", arg.c_str(), v.c_str());
                exit(1);
            }
            return b;
        };
        auto number = [&]() -> u32
        {
            std::string v = next();
            u32 n;
            if (!ParseDecimal(v, n))
            {
                fprintf(stderr, "%s needs a decimal number, not %s\n", arg.c_str(), v.c_str());
                exit(1);
            }
            return n;
        };

        if (arg == "--out") opts.OutPath = next();
        else if (arg == "--frames") opts.Frames = number();
        else if (arg == "--jit") opts.Jit = true;
        else if (arg == "--no-jit") opts.Jit = false;
        else if (arg == "--fastmem") opts.FastMem = true;
        else if (arg == "--no-fastmem") opts.FastMem = false;
        else if (arg == "--ldmdirect") opts.LdmDirect = flag() ? 1 : 0;
        else if (arg == "--ldmfast") opts.LdmFast = flag() ? 1 : 0;
        else if (arg == "--jitdispatch") opts.JitDispatch = flag() ? 1 : 0;
        else if (arg == "--ldmstats") opts.LdmStats = true;
        else if (arg == "--block-size") opts.BlockSize = std::clamp((int)number(), 1, 32);
        else if (arg == "--literal-opt") opts.LiteralOpt = flag();
        else if (arg == "--branch-opt") opts.BranchOpt = flag();
        else if (arg == "--set") opts.Sets.push_back(next());
        else if (arg == "--list-sets") { Frontend::ListSets(); exit(0); }
        else if (arg == "--bios") opts.BiosDir = next();
        else if (arg == "--sav") opts.SavePath = next();
        else if (arg == "--state") opts.StatePath = next();
        else if (arg == "--press")
        {
            Frontend::KeyPress press;
            std::string spec = next();
            if (!Frontend::ParsePress(spec, press)) { fprintf(stderr, "bad --press %s\n", spec.c_str()); return false; }
            opts.Presses.push_back(press);
            opts.PressSpecs.push_back(spec);
        }
        else if (arg == "--at")
        {
            std::string spec = next();
            size_t colon = spec.find(':');
            u32 frame;
            std::string action = colon == std::string::npos ? "" : spec.substr(colon + 1);
            if (colon == std::string::npos || !ParseDecimal(spec.substr(0, colon), frame)
                || (action != "reset" && action != "state"))
            {
                fprintf(stderr, "bad --at %s (F:reset or F:state)\n", spec.c_str());
                return false;
            }
            opts.Ats.push_back({frame, action});
        }
        else if (arg == "--rtc")
        {
            std::string spec = next();
            int r[6];
            if (sscanf(spec.c_str(), "%d,%d,%d,%d,%d,%d", &r[0], &r[1], &r[2], &r[3], &r[4], &r[5]) != 6)
            {
                fprintf(stderr, "bad --rtc %s\n", spec.c_str());
                return false;
            }
            memcpy(opts.Rtc, r, sizeof(r));
        }
        else if (arg == "--fixed-time") {}
        else if (arg == "--regs") opts.Regs = true;
        else if (arg == "--mem") opts.Mem = true;
        else if (arg == "--no-mem") opts.Mem = false;
        else if (arg == "--flush-every") opts.FlushEvery = std::max(1u, number());
        else if (arg == "--log") opts.LogPath = next();
        else if (arg == "-h" || arg == "--help") { Usage(); exit(0); }
        else if (arg[0] == '-') { fprintf(stderr, "unknown option %s\n", arg.c_str()); return false; }
        else opts.RomPath = arg;
    }
    for (const Options::At& at : opts.Ats)
    {
        if (at.Action == "state" && opts.StatePath.empty())
        {
            fprintf(stderr, "--at %u:state needs --state\n", at.Frame);
            return false;
        }
    }
    return !opts.RomPath.empty();
}

// what the desktop's Reset does: the reset, the pinned clock, direct boot, start
void Boot(NDS& nds, const Options& opts)
{
    nds.Reset();
    nds.RTC.SetDateTime(opts.Rtc[0], opts.Rtc[1], opts.Rtc[2], opts.Rtc[3], opts.Rtc[4], opts.Rtc[5]);
    // the file name only, so the same ROM at another path boots the same
    nds.SetupDirectBoot(fs::path(opts.RomPath).filename().string());
    nds.Start();
}

bool LoadState(NDS& nds, const std::string& path)
{
    std::vector<u8> data;
    if (!Frontend::ReadFile(path, data) || data.empty())
    {
        fprintf(stderr, "can't read savestate %s\n", path.c_str());
        return false;
    }
    Savestate state(data.data(), (u32)data.size(), false);
    if (state.Error || !nds.DoSavestate(&state) || state.Error)
    {
        fprintf(stderr, "savestate %s didn't load\n", path.c_str());
        return false;
    }
    return true;
}

std::unique_ptr<VerifyNDS> CreateNDS(const Options& opts)
{
    NDSArgs args {};
    args.JIT = std::nullopt;
#ifdef JIT_ENABLED
    if (opts.Jit)
    {
        JITArgs jit;
        jit.MaxBlockSize = (unsigned)opts.BlockSize;
        jit.LiteralOptimizations = opts.LiteralOpt;
        jit.BranchOptimizations = opts.BranchOpt;
        jit.FastMemory = opts.FastMem;
        args.JIT = jit;
    }
#else
    if (opts.Jit)
        fprintf(stderr, "this build has no JIT, interpreting\n");
#endif
    // the app's rate: the core resamples to it, which is part of the frame's work
    args.OutputSampleRate = 48000.0;

    bool haveFirmware = false;
    if (!Frontend::LoadBiosDir(opts.BiosDir, args, haveFirmware))
        return nullptr;
    if (!haveFirmware)
        args.Firmware = Firmware(0); // generated, with the core's fixed default MAC

    auto nds = std::make_unique<VerifyNDS>(std::move(args), &Frontend::App);
    for (const std::string& set : opts.Sets)
    {
        if (!Frontend::ApplySet(nds->GPU, set))
            fprintf(stderr, "unknown --set %s (see --list-sets)\n", set.c_str());
    }
#ifdef JIT_ENABLED
    if (opts.LdmDirect >= 0)
        nds->JIT.SetDirectBlockTransfers(opts.LdmDirect == 1);
    if (opts.LdmFast >= 0)
        nds->JIT.SetFastBlockLoads(opts.LdmFast == 1);
    if (opts.JitDispatch >= 0)
        nds->JIT.SetJitDispatch(opts.JitDispatch == 1);
    if (opts.LdmStats)
        nds->JIT.SetLdmStats(true);
#endif

    std::vector<u8> rom;
    if (!Frontend::ReadFile(opts.RomPath, rom) || rom.empty())
    {
        fprintf(stderr, "can't read ROM %s\n", opts.RomPath.c_str());
        return nullptr;
    }
    auto romData = std::make_unique<u8[]>(rom.size());
    memcpy(romData.get(), rom.data(), rom.size());
    u32 romLen = (u32)rom.size();
    rom.clear();
    rom.shrink_to_fit();

    // the save path stays empty, so Platform's FlushSaves never writes one anywhere
    NDSCart::NDSCartArgs cartArgs {};
    if (!opts.SavePath.empty())
    {
        std::vector<u8> sav;
        if (!Frontend::ReadFile(opts.SavePath, sav) || sav.empty())
        {
            fprintf(stderr, "can't read save %s\n", opts.SavePath.c_str());
            return nullptr;
        }
        cartArgs.SRAM = std::make_unique<u8[]>(sav.size());
        memcpy(cartArgs.SRAM.get(), sav.data(), sav.size());
        cartArgs.SRAMLength = (u32)sav.size();
    }
    auto cart = NDSCart::ParseROM(std::move(romData), romLen, &Frontend::App, std::move(cartArgs));
    if (!cart)
    {
        fprintf(stderr, "%s isn't a DS ROM the core can parse\n", opts.RomPath.c_str());
        return nullptr;
    }
    nds->SetNDSCart(std::move(cart));

    // software 3D and 2D on the emulator thread: nothing depends on another thread's timing
    static_cast<SoftRenderer&>(nds->GPU.GetRenderer3D()).SetThreaded(false, nds->GPU);
    nds->GPU.SetThreaded2D(false);

    Boot(*nds, opts);
    if (!opts.StatePath.empty() && !LoadState(*nds, opts.StatePath))
        return nullptr;
    return nds;
}

// a running XXH3 over several areas
struct Hasher
{
    XXH3_state_t* St = XXH3_createState();
    Hasher() { XXH3_64bits_reset(St); }
    ~Hasher() { XXH3_freeState(St); }
    void Add(const void* data, size_t len) { if (data && len) XXH3_64bits_update(St, data, len); }
    template <typename T> void Add(const T& value) { Add(&value, sizeof(value)); }
    u64 Done() { return XXH3_64bits_digest(St); }
};

u64 HashCpu(const ARM& cpu)
{
    Hasher h;
    h.Add(cpu.R, sizeof(cpu.R));
    h.Add(cpu.CPSR);
    h.Add(cpu.R_FIQ, sizeof(cpu.R_FIQ));
    h.Add(cpu.R_SVC, sizeof(cpu.R_SVC));
    h.Add(cpu.R_ABT, sizeof(cpu.R_ABT));
    h.Add(cpu.R_IRQ, sizeof(cpu.R_IRQ));
    h.Add(cpu.R_UND, sizeof(cpu.R_UND));
    return h.Done();
}

// the state around the registers that decides what runs next
u64 HashControl(const NDS& nds)
{
    const ARMv5& a9 = nds.ARM9;
    Hasher h;
    h.Add(a9.CP15Control);
    h.Add(a9.DTCMSetting);
    h.Add(a9.ITCMSetting);
    h.Add(a9.PU_CodeCacheable);
    h.Add(a9.PU_DataCacheable);
    h.Add(a9.PU_DataCacheWrite);
    h.Add(a9.PU_CodeRW);
    h.Add(a9.PU_DataRW);
    h.Add(a9.PU_Region, sizeof(a9.PU_Region));
    h.Add(nds.ARM9.Halted);
    h.Add(nds.ARM7.Halted);
    h.Add(nds.IME, sizeof(nds.IME));
    h.Add(nds.IE, sizeof(nds.IE));
    h.Add(nds.IF, sizeof(nds.IF));
    return h.Done();
}

u64 HashVram(const GPU& gpu)
{
    Hasher h;
    h.Add(gpu.VRAM_A, sizeof(gpu.VRAM_A));
    h.Add(gpu.VRAM_B, sizeof(gpu.VRAM_B));
    h.Add(gpu.VRAM_C, sizeof(gpu.VRAM_C));
    h.Add(gpu.VRAM_D, sizeof(gpu.VRAM_D));
    h.Add(gpu.VRAM_E, sizeof(gpu.VRAM_E));
    h.Add(gpu.VRAM_F, sizeof(gpu.VRAM_F));
    h.Add(gpu.VRAM_G, sizeof(gpu.VRAM_G));
    h.Add(gpu.VRAM_H, sizeof(gpu.VRAM_H));
    h.Add(gpu.VRAM_I, sizeof(gpu.VRAM_I));
    h.Add(gpu.Palette, sizeof(gpu.Palette));
    h.Add(gpu.OAM, sizeof(gpu.OAM));
    return h.Done();
}

void AddHash(std::string& line, const char* name, u64 hash)
{
    char buf[48];
    snprintf(buf, sizeof(buf), " %s=%016" PRIx64, name, hash);
    line += buf;
}

void AddRegs(std::string& line, const char* name, const ARM& cpu)
{
    char buf[16];
    line += ' ';
    line += name;
    line += '=';
    for (int i = 0; i < 16; i++)
    {
        snprintf(buf, sizeof(buf), "%08x,", cpu.R[i]);
        line += buf;
    }
    snprintf(buf, sizeof(buf), "cpsr:%08x", cpu.CPSR);
    line += buf;
}

long HostPageSize()
{
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (long)info.dwPageSize;
#else
    return sysconf(_SC_PAGESIZE);
#endif
}

// what actually runs, after the core had its say (fast memory can be unsupported, for one)
std::string Header(const Options& opts, VerifyNDS& nds)
{
    bool jit = nds.IsJITEnabled();
    int fastmem = 0, block = 0, literal = 0, branch = 0, ldmfast = 0, ldmdirect = 0, jitdispatch = 0;
#ifdef JIT_ENABLED
    if (jit)
    {
        fastmem = nds.JIT.FastMemoryEnabled();
        block = nds.JIT.GetMaxBlockSize();
        literal = nds.JIT.LiteralOptimizationsEnabled();
        branch = nds.JIT.BranchOptimizationsEnabled();
        ldmfast = nds.JIT.FastBlockLoadsEnabled();
        ldmdirect = nds.JIT.DirectBlockTransfersEnabled();
#if defined(__aarch64__)
        jitdispatch = nds.JIT.JitDispatchEnabled();
#endif // only the A64 backend has the stub; x64 always reports 0
    }
#endif
#if defined(__aarch64__)
    const char* host = "arm64";
#elif defined(__x86_64__)
    const char* host = "x64";
#else
    const char* host = "other";
#endif
    char buf[512];
    snprintf(buf, sizeof(buf),
        "# jitverify rom=%s host=%s page=%ld frames=%u jit=%d fastmem=%d block=%d literal=%d branch=%d ldmfast=%d ldmdirect=%d jitdispatch=%d mem=%d",
        fs::path(opts.RomPath).filename().string().c_str(), host, HostPageSize(), opts.Frames, jit ? 1 : 0,
        fastmem, block, literal, branch, ldmfast, ldmdirect, jitdispatch, opts.Mem ? 1 : 0);
    std::string line = buf;
    line += " state=" + (opts.StatePath.empty() ? std::string("-") : fs::path(opts.StatePath).filename().string());
    line += " bios=" + std::string(opts.BiosDir.empty() ? "free" : "dir");
    line += " press=";
    for (size_t i = 0; i < opts.PressSpecs.size(); i++)
        line += (i ? "," : "") + opts.PressSpecs[i];
    if (opts.PressSpecs.empty()) line += "-";
    line += " at=";
    for (size_t i = 0; i < opts.Ats.size(); i++)
        line += (i ? "," : "") + std::to_string(opts.Ats[i].Frame) + ":" + opts.Ats[i].Action;
    if (opts.Ats.empty()) line += "-";
    line += " set=";
    for (size_t i = 0; i < opts.Sets.size(); i++)
        line += (i ? "," : "") + opts.Sets[i];
    if (opts.Sets.empty()) line += "-";
    line += '\n';
    return line;
}

}

int main(int argc, char** argv)
{
    Options opts;
    if (!ParseArgs(argc, argv, opts))
    {
        Usage();
        return 1;
    }

#ifdef _WIN32
    // Windows 11 slows down background processes (EcoQoS): opt out, as the desktop does
    PROCESS_POWER_THROTTLING_STATE throttling {};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));
#endif

    if (!opts.LogPath.empty())
        Frontend::LogFile = fopen(opts.LogPath.c_str(), "w");

    FILE* out = stdout;
    if (!opts.OutPath.empty())
    {
        out = fopen(opts.OutPath.c_str(), "wb");
        if (!out)
        {
            fprintf(stderr, "can't write %s\n", opts.OutPath.c_str());
            return 1;
        }
    }

    std::unique_ptr<VerifyNDS> nds = CreateNDS(opts);
    if (!nds)
        return 1;

    // whole lines only reach the file: they collect here and go out in one write, then a flush
    std::string pending = Header(opts, *nds);
    auto writeOut = [&]()
    {
        fwrite(pending.data(), 1, pending.size(), out);
        fflush(out);
        pending.clear();
    };
    writeOut();

    std::vector<s16> audioBuf(2 * 1024);
    const size_t screenBytes = 256 * 192 * 4;
    auto start = std::chrono::steady_clock::now();
    u32 frame = 0;
    bool failed = false;
    std::string line;
    for (; frame < opts.Frames && !Frontend::App.StopRequested; frame++)
    {
        for (const Options::At& at : opts.Ats)
        {
            if (at.Frame != frame)
                continue;
            if (at.Action == "reset")
                Boot(*nds, opts);
            else if (!LoadState(*nds, opts.StatePath))
            {
                fprintf(stderr, "--at %u:state: the state didn't load, stopping\n", frame);
                failed = true;
            }
        }
        if (failed)
            break;

        nds->SetKeyMask(~Frontend::PressMask(opts.Presses, frame) & 0xFFF);
        nds->RunFrame();

        // the core's audio is made and drained as in the app, and hashed instead of played
        Hasher aud;
        int got;
        while ((got = nds->SPU.ReadOutput(audioBuf.data(), (int)audioBuf.size() / 2)) > 0)
            aud.Add(audioBuf.data(), (size_t)got * 2 * sizeof(s16));

        int front = nds->GPU.FrontBuffer;
        char buf[160];
        snprintf(buf, sizeof(buf), "F %u", frame);
        line = buf;
        AddHash(line, "top", XXH3_64bits(nds->GPU.Framebuffer[front][0].get(), screenBytes));
        AddHash(line, "bot", XXH3_64bits(nds->GPU.Framebuffer[front][1].get(), screenBytes));
        AddHash(line, "aud", aud.Done());
        snprintf(buf, sizeof(buf), " a9ts=%" PRIu64 " a7ts=%" PRIu64 " sys=%" PRIu64,
            nds->ARM9Timestamp, nds->ARM7Timestamp, nds->SysTime());
        line += buf;
        AddHash(line, "a9", HashCpu(nds->ARM9));
        AddHash(line, "a7", HashCpu(nds->ARM7));
        AddHash(line, "ctl", HashControl(*nds));
        if (opts.Mem)
        {
            AddHash(line, "main", XXH3_64bits(nds->MainRAM, nds->MainRAMMask + 1));
            Hasher wram;
            wram.Add(nds->SharedWRAM, nds->SharedWRAMSize);
            wram.Add(nds->ARM7WRAM, nds->ARM7WRAMSize);
            AddHash(line, "wram", wram.Done());
            Hasher tcm;
            tcm.Add(nds->ARM9.ITCM, ITCMPhysicalSize);
            tcm.Add(nds->ARM9.DTCM, DTCMPhysicalSize);
            AddHash(line, "tcm", tcm.Done());
            AddHash(line, "vram", HashVram(nds->GPU));
            Hasher save;
            save.Add(nds->GetNDSSave(), nds->GetNDSSaveLength());
            AddHash(line, "save", save.Done());
        }
        if (opts.Regs)
        {
            AddRegs(line, "a9r", nds->ARM9);
            AddRegs(line, "a7r", nds->ARM7);
        }
        line += '\n';
        pending += line;
        if ((frame + 1) % opts.FlushEvery == 0)
            writeOut();
    }
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    writeOut();
    bool stoppedEarly = Frontend::App.StopRequested;

    // on stderr when the lines go to stdout
    fprintf(out == stdout ? stderr : stdout, "%u frames in %.2f s (%.1f fps)%s\n", frame, wall, wall > 0 ? frame / wall : 0.0,
        failed ? ", stopped: a state didn't load" : stoppedEarly ? ", stopped early by the core" : "");

    if (out != stdout)
        fclose(out);
    nds->Stop();
    nds.reset();
    if (Frontend::LogFile)
        fclose(Frontend::LogFile);
    return failed || stoppedEarly ? 2 : 0;
}
