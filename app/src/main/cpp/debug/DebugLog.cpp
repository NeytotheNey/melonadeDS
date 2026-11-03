#include "DebugLog.h"

#include "AudioDiagnostics.h"
#include <cstdio>
#include <cstring>
#include "LogQueue.h"
#include "LogRing.h"
#include "MelonLog.h"
#include "MonotonicClock.h"
#include "NDS.h"
#include "NDSCart.h"

using namespace melonDS;

namespace DebugLog
{

std::atomic_bool enabled = false;
// Set when the debug log is turned on, so that the emulator thread starts its per-frame checks from scratch on the next frame
static std::atomic_bool frameStateResetPending = false;

static const int64_t ONE_SECOND_NS = 1000000000LL;
// Core lines allowed per second from one call site (one format string) and in all, checked before the line is formatted, with anything above
// that dropped and counted; the debug log's dedupe only sees the lines within budget. Warnings and errors only have the per call site budget, so
// that unknown IO spam can't starve a line like "Stopping emulated console".
static const uint32_t MAX_CORE_LINES_PER_SITE_PER_SECOND = 50;
static const uint32_t MAX_CORE_LINES_PER_SECOND = 200;
// Lua log() lines allowed per second, in all
static const uint32_t MAX_LUA_LINES_PER_SECOND = 120;
// Immediate reports allowed per second on top of the heartbeat, with separate budgets for exceptions and GPU changes so a game that changes its
// display setup every frame can't hide an exception
static const uint32_t MAX_EXCEPTION_REPORTS_PER_SECOND = 10;
static const uint32_t MAX_GPU_CHANGE_REPORTS_PER_SECOND = 5;
static const std::size_t MAX_LINE_LENGTH = 1024;
// How much of an over-budget call site's format string the drop report shows
static const std::size_t MAX_REPORTED_FORMAT_LENGTH = 80;

// Core line budgets, lock-free since the core logs from several threads
static LogSiteBudgets<256> coreSiteBudgets;
static std::atomic<uint64_t> coreBudgetWindow = 0;
static std::atomic<uint32_t> coreOverBudget = 0;
static std::atomic<uint64_t> luaBudgetWindow = 0;
static std::atomic<uint32_t> luaOverBudget = 0;

// Core message dedupe while the debug log is on: a hash of the last line in the high bits and its priority in the low 4, and how many times it
// came again since it was written. Two threads logging at once can pin a repeat on the wrong line, which is fine for a debug log.
static std::atomic<uint64_t> lastCoreLineKey = 0;
static std::atomic<uint32_t> coreRepeatCount = 0;

static int toAndroidPriority(Platform::LogLevel level)
{
    switch (level)
    {
        case Platform::LogLevel::Debug: return ANDROID_LOG_DEBUG;
        case Platform::LogLevel::Info: return ANDROID_LOG_INFO;
        case Platform::LogLevel::Warn: return ANDROID_LOG_WARN;
        case Platform::LogLevel::Error: return ANDROID_LOG_ERROR;
    }
    return ANDROID_LOG_INFO;
}

// FNV-1a, shifted up to leave room for the priority
static uint64_t makeCoreLineKey(const char* text, int priority)
{
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (const char* c = text; *c != '\0'; c++)
        hash = (hash ^ (uint8_t) *c) * 0x100000001B3ULL;
    return (hash << 4) | (uint64_t) (priority & 0xF);
}

// The priority stored in a core line key, or INFO for no line (liblog drops a line with priority 0)
static int getCoreLineKeyPriority(uint64_t key)
{
    int priority = (int) (key & 0xF);
    return priority >= ANDROID_LOG_VERBOSE ? priority : ANDROID_LOG_INFO;
}

// Counts a dropped or repeated line, waking the log writer if it's the first since its last report (it sleeps while there's nothing to report)
static void addPendingCount(std::atomic<uint32_t>& count, std::memory_order order = std::memory_order_relaxed)
{
    if (count.fetch_add(1, order) == 0)
        LogQueue::nudge();
}

// Writer thread: queues the last core line's repeat count, so a line that keeps repeating still shows up once a second. The acquire pairs with the
// release in logCoreMessage, so the count's line was queued before and comes out first.
static void reportCoreRepeats()
{
    uint32_t repeats = coreRepeatCount.exchange(0, std::memory_order_acquire);
    if (repeats > 0)
    {
        int priority = getCoreLineKeyPriority(lastCoreLineKey.load(std::memory_order_relaxed));
        LogQueue::print(priority, LOG_TAG_CORE, "(previous line repeated %u more times)", repeats);
    }
}

static bool hasPendingLogCounts()
{
    return coreRepeatCount.load(std::memory_order_relaxed) != 0 || coreOverBudget.load(std::memory_order_relaxed) != 0
            || luaOverBudget.load(std::memory_order_relaxed) != 0;
}

// Writer thread, about once a second and on LogQueue::flush(): queues reports of what the budgets dropped
static void reportLogSecond()
{
    reportCoreRepeats();

    uint32_t coreDropped = coreOverBudget.exchange(0, std::memory_order_relaxed);
    if (coreDropped > 0)
    {
        uint32_t siteDropped;
        const auto* site = coreSiteBudgets.takeWorstSite(siteDropped);
        const char* format = site != nullptr && site != coreSiteBudgets.getOverflowSite() ? site->format.load(std::memory_order_relaxed) : nullptr;
        char shownFormat[MAX_REPORTED_FORMAT_LENGTH + 1] = "several call sites";
        if (format != nullptr)
        {
            snprintf(shownFormat, sizeof(shownFormat), "%s", format);
            // Drops the core's trailing newline and keeps the report on one line
            std::size_t end = strlen(shownFormat);
            while (end > 0 && (shownFormat[end - 1] == '\n' || shownFormat[end - 1] == '\r'))
                shownFormat[--end] = '\0';
            for (char* c = shownFormat; *c != '\0'; c++)
            {
                if (*c == '\n' || *c == '\r')
                    *c = ' ';
            }
        }
        LogQueue::print(ANDROID_LOG_WARN, isEnabled() ? LOG_TAG_CORE : LOG_TAG,
                "(%u core lines dropped, over %u per second from one call site or %u in all for debug and info lines; most from \"%s\": %u)", coreDropped,
                MAX_CORE_LINES_PER_SITE_PER_SECOND, MAX_CORE_LINES_PER_SECOND, shownFormat, siteDropped);
    }

    uint32_t luaDropped = luaOverBudget.exchange(0, std::memory_order_relaxed);
    if (luaDropped > 0)
        LogQueue::print(ANDROID_LOG_WARN, LOG_TAG_LUA, "(%u Lua log lines dropped, over %u per second)", luaDropped, MAX_LUA_LINES_PER_SECOND);
}

void startLogQueue()
{
    LogQueue::start(reportLogSecond, hasPendingLogCounts);
}

void setEnabled(bool isEnabled)
{
    startLogQueue();
    AudioDiagnostics::setDebugLogEnabled(isEnabled);

    if (isEnabled)
    {
        lastCoreLineKey.store(0, std::memory_order_relaxed);
        coreRepeatCount.store(0, std::memory_order_relaxed);
        frameStateResetPending.store(true, std::memory_order_relaxed);
        LogQueue::setStampingEnabled(true);
        enabled.store(true, std::memory_order_relaxed);
        return;
    }

    enabled.store(false, std::memory_order_relaxed);
    // Gets the last lines and the pending counts out while the recorder still runs, with the last line's key still there for its repeat count
    LogQueue::flush();
    LogQueue::setStampingEnabled(false);
    lastCoreLineKey.store(0, std::memory_order_relaxed);
}

void logCoreMessage(Platform::LogLevel level, const char* format, va_list args)
{
    // The budgets come first, so a line over budget costs a few atomics: no formatting, no clock read beyond the coarse one, no lock
    uint32_t second = coarseMonotonicSecond();
    auto& site = coreSiteBudgets.find(format);
    bool isWarningOrError = level == Platform::LogLevel::Warn || level == Platform::LogLevel::Error;
    if (!takeLogBudget(site.window, second, MAX_CORE_LINES_PER_SITE_PER_SECOND)
            || (!isWarningOrError && !takeLogBudget(coreBudgetWindow, second, MAX_CORE_LINES_PER_SECOND)))
    {
        site.overBudget.fetch_add(1, std::memory_order_relaxed);
        addPendingCount(coreOverBudget);
        return;
    }

    int priority = toAndroidPriority(level);
    if (!isEnabled())
    {
        LogQueue::vprint(priority, LOG_TAG, format, args);
        return;
    }

    char message[MAX_LINE_LENGTH];
    int length = vsnprintf(message, sizeof(message), format, args);
    if (length < 0)
        return;

    // The core ends most lines with a newline, which logcat doesn't need
    std::size_t end = strnlen(message, sizeof(message));
    while (end > 0 && (message[end - 1] == '\n' || message[end - 1] == '\r'))
        message[--end] = '\0';

    uint64_t key = makeCoreLineKey(message, priority);
    uint64_t previousKey = lastCoreLineKey.exchange(key, std::memory_order_relaxed);
    if (previousKey == key)
    {
        addPendingCount(coreRepeatCount, std::memory_order_release);
        return;
    }

    uint32_t repeats = coreRepeatCount.exchange(0, std::memory_order_relaxed);
    if (repeats > 0)
        LogQueue::print(getCoreLineKeyPriority(previousKey), LOG_TAG_CORE, "(previous line repeated %u more times)", repeats);
    LogQueue::write(priority, LOG_TAG_CORE, message);
}

bool takeLuaLogBudget()
{
    if (takeLogBudget(luaBudgetWindow, coarseMonotonicSecond(), MAX_LUA_LINES_PER_SECOND))
        return true;
    addPendingCount(luaOverBudget);
    return false;
}

// Everything below is only used from the emulator thread

struct CpuWatch
{
    bool wasFlagged = false;
    u32 lastMode = 0;
    int64_t lastReportNs = 0;
    bool wasAtLowAddress = false;
    int64_t lastLowAddressReportNs = 0;
};

struct GpuSnapshot
{
    u16 powerControl = 0;
    u32 dispCntA = 0;
    u32 dispCntB = 0;
    u8 vramCnt[9] = {};

    bool operator==(const GpuSnapshot& other) const
    {
        return powerControl == other.powerControl && dispCntA == other.dispCntA && dispCntB == other.dispCntB && memcmp(vramCnt, other.vramCnt, sizeof(vramCnt)) == 0;
    }
};

class ReportRateLimiter
{
public:
    ReportRateLimiter(const char* label, uint32_t maxPerSecond) : label(label), maxPerSecond(maxPerSecond) {}

    bool take(int64_t nowNs)
    {
        if (nowNs - windowStartNs >= ONE_SECOND_NS)
        {
            if (dropped > 0)
                LOG_WARN_QUEUED(LOG_TAG_CPU, "(%u %s reports dropped)", dropped, label);

            windowStartNs = nowNs;
            count = 0;
            dropped = 0;
        }

        if (count >= maxPerSecond)
        {
            dropped++;
            return false;
        }

        count++;
        return true;
    }

private:
    const char* label;
    uint32_t maxPerSecond;
    int64_t windowStartNs = 0;
    uint32_t count = 0;
    uint32_t dropped = 0;
};

static int64_t nextHeartbeatNs = 0;
static ReportRateLimiter exceptionReportLimiter("exception", MAX_EXCEPTION_REPORTS_PER_SECOND);
static ReportRateLimiter gpuChangeReportLimiter("gpu change", MAX_GPU_CHANGE_REPORTS_PER_SECOND);
static CpuWatch arm9Watch;
static CpuWatch arm7Watch;
static GpuSnapshot lastGpuSnapshot;
static bool hasGpuSnapshot = false;

static const char* getModeName(u32 cpsr)
{
    switch (cpsr & 0x1F)
    {
        case 0x10: return "USR";
        case 0x11: return "FIQ";
        case 0x12: return "IRQ";
        case 0x13: return "SVC";
        case 0x17: return "ABT";
        case 0x1B: return "UND";
        case 0x1F: return "SYS";
        default: return "???";
    }
}

// Which exception vector the CPU is about to run, if any (the next instruction is at R15 - 4, since exceptions always switch to ARM state)
static const char* getExceptionVectorName(const ARM& cpu)
{
    if (cpu.CPSR & 0x20)
        return nullptr;

    switch (cpu.R[15] - 4 - cpu.ExceptionBase)
    {
        case 0x04: return "undefined";
        case 0x0C: return "prefetch abort";
        case 0x10: return "data abort";
        default: return nullptr;
    }
}

static void checkCpuExceptionState(const ARM& cpu, CpuWatch& watch, u32 frame, int64_t nowNs)
{
    u32 mode = cpu.CPSR & 0x1F;
    bool isExceptionMode = mode == 0x17 || mode == 0x1B;
    const char* vector = getExceptionVectorName(cpu);
    bool isFlagged = isExceptionMode || vector != nullptr;

    if (isFlagged)
    {
        bool isNewState = !watch.wasFlagged || mode != watch.lastMode;
        // A new state is reported right away (within the budget), and a CPU that stays in it again once per second
        bool shouldReport = isNewState ? exceptionReportLimiter.take(nowNs) : nowNs - watch.lastReportNs >= ONE_SECOND_NS;
        if (shouldReport)
        {
            // In ABT/UND mode, R13/R14 are that mode's banked registers and the third banked slot holds its SPSR
            u32 spsr = mode == 0x17 ? cpu.R_ABT[2] : (mode == 0x1B ? cpu.R_UND[2] : 0);
            LOG_WARN_QUEUED(LOG_TAG_CPU, "ARM%d %s: frame=%u mode=%s vector=%s r15=%08X r14=%08X r13=%08X cpsr=%08X spsr=%08X exception_base=%08X",
                    cpu.Num ? 7 : 9, isNewState ? "entered exception state" : "still in exception state", frame, getModeName(cpu.CPSR), vector ? vector : "none",
                    cpu.R[15], cpu.R[14], cpu.R[13], cpu.CPSR, spsr, cpu.ExceptionBase);
            watch.lastReportNs = nowNs;
        }
    }
    else if (watch.wasFlagged)
    {
        LOG_INFO_QUEUED(LOG_TAG_CPU, "ARM%d left exception state: frame=%u mode=%s r15=%08X", cpu.Num ? 7 : 9, frame, getModeName(cpu.CPSR), cpu.R[15]);
    }

    watch.wasFlagged = isFlagged;
    watch.lastMode = mode;
}

/**
 * Whether the CPU is about to run code below 0x20 that isn't one of its exception vectors (usually a null pointer call), with [nextAddress] set to
 * the next instruction's address (R15 - 4 in ARM state, R15 - 2 in Thumb).
 */
static bool isAtSuspiciousLowAddress(const ARM& cpu, u32& nextAddress)
{
    bool isThumb = cpu.CPSR & 0x20;
    nextAddress = cpu.R[15] - (isThumb ? 2 : 4);
    if (nextAddress >= 0x20)
        return false;

    // The vectors are only down here when the exception base is 0 (always on the ARM7; the ARM9 normally uses 0xFFFF0000), and they're ARM code
    if (cpu.ExceptionBase != 0 || isThumb)
        return true;

    // The core jumps to 0x04 (undefined), 0x08 (SWI), 0x0C and 0x10 (aborts, ARM9 only) and 0x18 (IRQ), while 0x1C (FIQ) is unused but harmless and
    // 0x00 (reset) and 0x14 (reserved) are never jumped to on purpose
    switch (nextAddress)
    {
        case 0x04: case 0x08: case 0x0C: case 0x10: case 0x18: case 0x1C:
            return false;
        default:
            return true;
    }
}

static void checkCpuLowAddress(const ARM& cpu, CpuWatch& watch, u32 frame, int64_t nowNs)
{
    u32 nextAddress;
    bool isAtLowAddress = isAtSuspiciousLowAddress(cpu, nextAddress);
    if (isAtLowAddress)
    {
        bool isNew = !watch.wasAtLowAddress;
        bool shouldReport = isNew ? exceptionReportLimiter.take(nowNs) : nowNs - watch.lastLowAddressReportNs >= ONE_SECOND_NS;
        if (shouldReport)
        {
            LOG_WARN_QUEUED(LOG_TAG_CPU, "ARM%d %s: frame=%u next_pc=%08X r15=%08X r14=%08X r13=%08X cpsr=%08X mode=%s%s exception_base=%08X",
                    cpu.Num ? 7 : 9, isNew ? "jumped to low memory" : "still in low memory", frame, nextAddress, cpu.R[15], cpu.R[14], cpu.R[13], cpu.CPSR,
                    getModeName(cpu.CPSR), (cpu.CPSR & 0x20) ? " thumb" : "", cpu.ExceptionBase);
            watch.lastLowAddressReportNs = nowNs;
        }
    }

    watch.wasAtLowAddress = isAtLowAddress;
}

static void resetFrameState()
{
    nextHeartbeatNs = 0;
    arm9Watch = CpuWatch();
    arm7Watch = CpuWatch();
    hasGpuSnapshot = false;
    AudioDiagnostics::resetForDebugLog();
}

static GpuSnapshot takeGpuSnapshot(NDS& nds)
{
    GpuSnapshot snapshot;
    snapshot.powerControl = nds.PowerControl9;
    snapshot.dispCntA = nds.GPU.GPU2D_A.DispCnt;
    snapshot.dispCntB = nds.GPU.GPU2D_B.DispCnt;
    memcpy(snapshot.vramCnt, nds.GPU.VRAMCNT, sizeof(snapshot.vramCnt));
    return snapshot;
}

static void logGpuState(NDS& nds, const GpuSnapshot& snapshot, const char* label, u32 frame)
{
    const auto& a = nds.GPU.GPU2D_A;
    const auto& b = nds.GPU.GPU2D_B;
    const u8* v = snapshot.vramCnt;
    // POWCNT1 bit 15 set means engine A is on the top screen
    LOG_INFO_QUEUED(LOG_TAG_CPU,
            "%s frame=%u powcnt1=%04X dispcnt_a=%08X dispcnt_b=%08X bgcnt_a=%04X,%04X,%04X,%04X bgcnt_b=%04X,%04X,%04X,%04X vramcnt=%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X",
            label, frame, snapshot.powerControl, snapshot.dispCntA, snapshot.dispCntB, a.BGCnt[0], a.BGCnt[1], a.BGCnt[2], a.BGCnt[3],
            b.BGCnt[0], b.BGCnt[1], b.BGCnt[2], b.BGCnt[3], v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8]);
}

void onFrame(NDS& nds, bool isFastForward, float fps, int64_t lastFrameWorkNs)
{
    if (frameStateResetPending.exchange(false, std::memory_order_relaxed)) [[unlikely]]
        resetFrameState();

    int64_t nowNs = monotonicNanos();
    u32 frame = nds.NumFrames;

    checkCpuExceptionState(nds.ARM9, arm9Watch, frame, nowNs);
    checkCpuExceptionState(nds.ARM7, arm7Watch, frame, nowNs);
    checkCpuLowAddress(nds.ARM9, arm9Watch, frame, nowNs);
    checkCpuLowAddress(nds.ARM7, arm7Watch, frame, nowNs);

    GpuSnapshot gpuSnapshot = takeGpuSnapshot(nds);
    if (hasGpuSnapshot && !(gpuSnapshot == lastGpuSnapshot) && gpuChangeReportLimiter.take(nowNs))
        logGpuState(nds, gpuSnapshot, "gpu change", frame);

    lastGpuSnapshot = gpuSnapshot;
    hasGpuSnapshot = true;

    if (nowNs < nextHeartbeatNs)
        return;

    nextHeartbeatNs = nowNs + ONE_SECOND_NS;
    const ARM& arm9 = nds.ARM9;
    const ARM& arm7 = nds.ARM7;
    LOG_INFO_QUEUED(LOG_TAG_CPU,
            "heartbeat frame=%u fps=%.1f work_ms=%.2f jit=%d ff=%d | arm9 r15=%08X cpsr=%08X %s%s halted=%u | arm7 r15=%08X cpsr=%08X %s%s halted=%u | "
            "ime9=%u ie9=%08X if9=%08X ime7=%u ie7=%08X if7=%08X cpustop=%08X",
            frame, fps, lastFrameWorkNs / 1000000.0, nds.IsJITEnabled() ? 1 : 0, isFastForward ? 1 : 0,
            arm9.R[15], arm9.CPSR, getModeName(arm9.CPSR), (arm9.CPSR & 0x20) ? " thumb" : "", arm9.Halted,
            arm7.R[15], arm7.CPSR, getModeName(arm7.CPSR), (arm7.CPSR & 0x20) ? " thumb" : "", arm7.Halted,
            nds.IME[0], nds.IE[0], nds.IF[0], nds.IME[1], nds.IE[1], nds.IF[1], nds.CPUStop);
    logGpuState(nds, gpuSnapshot, "gpu", frame);
    AudioDiagnostics::logSecond(frame);
}

// Replaces anything outside printable ASCII with '?', so that a header field is safe to log
static void sanitizeAscii(char* text)
{
    for (char* c = text; *c != '\0'; c++)
    {
        if (*c < 0x20 || *c > 0x7E)
            *c = '?';
    }
}

void logGameStart(NDS& nds, const char* reason, bool hasRomCrc32, uint32_t romCrc32)
{
    resetFrameState();

    LOG_INFO_QUEUED(LOG_TAG_CPU, "core %s: console=%s jit=%d arm9_bios=%s arm7_bios=%s arm9_exception_base=%08X",
            reason, nds.ConsoleType == 1 ? "DSi" : "DS", nds.IsJITEnabled() ? 1 : 0,
            nds.IsLoadedARM9BIOSKnownNative() ? "native" : "not native (FreeBIOS or unknown)",
            nds.IsLoadedARM7BIOSKnownNative() ? "native" : "not native (FreeBIOS or unknown)", nds.ARM9.ExceptionBase);

    const NDSCart::CartCommon* cart = nds.GetNDSCart();
    if (cart == nullptr)
    {
        LOG_INFO_QUEUED(LOG_TAG_CPU, "core %s: no cart inserted", reason);
        return;
    }

    const NDSHeader& header = cart->GetHeader();
    char title[sizeof(header.GameTitle) + 1] = {};
    char gameCode[sizeof(header.GameCode) + 1] = {};
    memcpy(title, header.GameTitle, sizeof(header.GameTitle));
    memcpy(gameCode, header.GameCode, sizeof(header.GameCode));
    sanitizeAscii(title);
    sanitizeAscii(gameCode);

    char crcText[16] = "n/a";
    if (hasRomCrc32)
        snprintf(crcText, sizeof(crcText), "%08X", romCrc32);

    LOG_INFO_QUEUED(LOG_TAG_CPU, "core %s: cart title=\"%s\" game_code=%s rom_version=%u header_crc16=%04X rom_size=%u rom_file_crc32=%s dsi_ware=%d",
            reason, title, gameCode, header.ROMVersion, header.HeaderCRC16, cart->GetROMLength(), crcText, header.IsDSiWare() ? 1 : 0);
}

}
