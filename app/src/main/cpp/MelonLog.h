#ifndef MELONLOG_H
#define MELONLOG_H

#include <android/log.h>

// The logcat tags, one per area, with the core's messages under LOG_TAG, or LOG_TAG_CORE while the debug log collapses and rate limits them
#define LOG_TAG "melonade"
#define LOG_TAG_CORE "melonade-core"
#define LOG_TAG_CPU "melonade-cpu"
#define LOG_TAG_AUDIO "melonade-audio"
#define LOG_TAG_LUA "melonade-lua"
#define LOG_TAG_GL "melonade-gl"
// Debug-only lines (debug builds' CAPWAIT, REWIND and CPU pinning, and the app's own debug log markers)
#define LOG_TAG_DEBUG "melonade-debug"

#define LOG_DEBUG(tag, message, ...) __android_log_print(ANDROID_LOG_DEBUG, tag, message, ##__VA_ARGS__)
#define LOG_INFO(tag, message, ...) __android_log_print(ANDROID_LOG_INFO, tag, message, ##__VA_ARGS__)
#define LOG_WARN(tag, message, ...) __android_log_print(ANDROID_LOG_WARN, tag, message, ##__VA_ARGS__)
#define LOG_ERROR(tag, message, ...) __android_log_print(ANDROID_LOG_ERROR, tag, message, ##__VA_ARGS__)

#endif //MELONLOG_H
