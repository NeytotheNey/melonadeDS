#ifndef DEBUGLOG_H
#define DEBUGLOG_H

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include "MelonLog.h"
#include "Platform.h"

namespace melonDS { class NDS; }

/**
 * Opt-in debug logging, turned on from Kotlin while the debug log recorder runs, which costs callers only the [isEnabled] check while it's off.
 */
namespace DebugLog
{

extern std::atomic_bool enabled;

inline bool isEnabled()
{
    return enabled.load(std::memory_order_relaxed);
}

void setEnabled(bool isEnabled);

/**
 * LOG_INFO([tag], ...) while the debug log is on, and nothing otherwise
 */
#define LOG_DEBUGLOG(tag, ...) \
    do \
    { \
        if (::DebugLog::isEnabled()) [[unlikely]] \
            LOG_INFO(tag, __VA_ARGS__); \
    } while (0)

/**
 * Writes a message from the core (melonDS::Platform::Log) to logcat under the melonade-core tag, collapsing identical lines in a row and capping new
 * lines per second, so a core that spams the log can't flood logcat or slow the emulator down.
 */
void logCoreMessage(melonDS::Platform::LogLevel level, const char* format, va_list args);

/**
 * Emulator thread, after every frame while the debug log is on: logs a CPU heartbeat, the GPU state and the audio stats once a second, and reports
 * exception modes and vectors, null pointer calls and VRAM or display setup changes right away (rate limited).
 */
void onFrame(melonDS::NDS& nds, bool isFastForward, float fps, int64_t lastFrameWorkNs);

/**
 * Logs what the core was set up with when a game (or the firmware) starts or is reset, on the emulator thread or while it's parked (reset).
 */
void logGameStart(melonDS::NDS& nds, const char* reason, bool hasRomCrc32, uint32_t romCrc32);

}

#endif //DEBUGLOG_H
