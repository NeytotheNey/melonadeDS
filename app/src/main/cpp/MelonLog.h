#ifndef MELONLOG_H
#define MELONLOG_H

#ifdef __ANDROID__
#include <android/log.h>
#else
// The desktop's log (desktop/src/DesktopLog.cpp) with Android's priorities, so the tags and the queued macros (debug/LogQueue.h) work the same
enum
{
    ANDROID_LOG_DEBUG = 3,
    ANDROID_LOG_INFO = 4,
    ANDROID_LOG_WARN = 5,
    ANDROID_LOG_ERROR = 6,
};
void melonadeLogPrint(int priority, const char* tag, const char* format, ...) __attribute__((format(printf, 3, 4)));
#endif

// The logcat tags, one per area, with the core's messages under LOG_TAG, or LOG_TAG_CORE while the debug log also collapses repeated ones
// (both rate limited). Anything that logs every frame or every second goes through debug/LogQueue.h instead of these macros.
#define LOG_TAG "melonade"
#define LOG_TAG_CORE "melonade-core"
#define LOG_TAG_CPU "melonade-cpu"
#define LOG_TAG_AUDIO "melonade-audio"
#define LOG_TAG_LUA "melonade-lua"
#define LOG_TAG_GL "melonade-gl"
// Debug-only lines (debug builds' CAPWAIT, REWIND and CPU pinning, and the app's own debug log markers)
#define LOG_TAG_DEBUG "melonade-debug"

#ifdef __ANDROID__
#define LOG_DEBUG(tag, message, ...) __android_log_print(ANDROID_LOG_DEBUG, tag, message, ##__VA_ARGS__)
#define LOG_INFO(tag, message, ...) __android_log_print(ANDROID_LOG_INFO, tag, message, ##__VA_ARGS__)
#define LOG_WARN(tag, message, ...) __android_log_print(ANDROID_LOG_WARN, tag, message, ##__VA_ARGS__)
#define LOG_ERROR(tag, message, ...) __android_log_print(ANDROID_LOG_ERROR, tag, message, ##__VA_ARGS__)
#else
#define LOG_DEBUG(tag, message, ...) melonadeLogPrint(ANDROID_LOG_DEBUG, tag, message, ##__VA_ARGS__)
#define LOG_INFO(tag, message, ...) melonadeLogPrint(ANDROID_LOG_INFO, tag, message, ##__VA_ARGS__)
#define LOG_WARN(tag, message, ...) melonadeLogPrint(ANDROID_LOG_WARN, tag, message, ##__VA_ARGS__)
#define LOG_ERROR(tag, message, ...) melonadeLogPrint(ANDROID_LOG_ERROR, tag, message, ##__VA_ARGS__)
#endif

#endif //MELONLOG_H
