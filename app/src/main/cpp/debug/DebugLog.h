#ifndef DEBUGLOG_H
#define DEBUGLOG_H

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include "NDS.h"
#include "Platform.h"

/**
 * Opt-in debug logging, turned on from Kotlin while the debug log recorder runs. While it's off, callers only pay for the [isEnabled] check.
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
 * Writes a message from the core (melonDS::Platform::Log) to logcat with the melonade-core tag. Identical lines in a row are collapsed into a
 * "repeated N times" line, and new lines are capped per second, so a core that spams the log can't flood logcat or slow the emulator down.
 */
void logCoreMessage(melonDS::Platform::LogLevel level, const char* format, va_list args);

/**
 * Called on the emulator thread after every frame while the debug log is on. Logs a CPU heartbeat, the GPU state and the audio stats (see
 * AudioDiagnostics) once per second, and logs
 * right away (rate limited) when a CPU enters an abort/undefined mode, stops at an exception vector or is about to run code below 0x20 that isn't
 * a vector (a null pointer call), or when the VRAM or display setup changes.
 */
void onFrame(melonDS::NDS& nds, bool isFastForward, float fps, int64_t lastFrameWorkNs);

/**
 * Logs what the core was set up with when a game (or the firmware) starts or is reset. Must be called on the emulator thread, or while it's parked (reset).
 */
void logGameStart(melonDS::NDS& nds, const char* reason, bool hasRomCrc32, uint32_t romCrc32);

}

#endif //DEBUGLOG_H
