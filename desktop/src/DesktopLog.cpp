// The desktop's stand-in for the app's debug/ code (Android only: logcat, Oboe and system properties): MelonLog.h's log macros, the
// queued ones (debug/LogQueue.h) and the debug log (debug/DebugLog.h, never on here) all write to the core's log (PlatformBase.cpp), so
// the shared code logs the same lines as on the phone
#include <cstdarg>
#include <cstdio>

#include "MelonLog.h"
#include "Platform.h"
#include "debug/DebugLog.h"
#include "debug/LogQueue.h"

using namespace melonDS;

static void logLine(int priority, const char* tag, const char* text)
{
    Platform::LogLevel level = priority <= ANDROID_LOG_DEBUG ? Platform::LogLevel::Debug
                             : priority == ANDROID_LOG_INFO ? Platform::LogLevel::Info
                             : priority == ANDROID_LOG_WARN ? Platform::LogLevel::Warn : Platform::LogLevel::Error;
    Platform::Log(level, "%s: %s\n", tag, text);
}

static void logFormatted(int priority, const char* tag, const char* format, va_list args)
{
    char text[1024];
    vsnprintf(text, sizeof(text), format, args);
    logLine(priority, tag, text);
}

void melonadeLogPrint(int priority, const char* tag, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    logFormatted(priority, tag, format, args);
    va_end(args);
}

namespace LogQueue
{

void start(void (*reportCounts)(), bool (*hasPendingCounts)()) {}
void setStampingEnabled(bool isEnabled) {}
void nudge() {}
void flush() {}

void write(int priority, const char* tag, const char* text)
{
    logLine(priority, tag, text);
}

void print(int priority, const char* tag, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    logFormatted(priority, tag, format, args);
    va_end(args);
}

void vprint(int priority, const char* tag, const char* format, va_list args)
{
    logFormatted(priority, tag, format, args);
}

}

namespace DebugLog
{

std::atomic_bool enabled = false;

void setEnabled(bool isEnabled) {}
void startLogQueue() {}

void logCoreMessage(Platform::LogLevel level, const char* format, va_list args)
{
    char text[1024];
    vsnprintf(text, sizeof(text), format, args);
    Platform::Log(level, "%s", text);
}

bool takeLuaLogBudget()
{
    return true;
}

void onFrame(NDS& nds, bool isFastForward, float fps, int64_t lastFrameWorkNs) {}
void logGameStart(NDS& nds, const char* reason, bool hasRomCrc32, uint32_t romCrc32) {}

}
