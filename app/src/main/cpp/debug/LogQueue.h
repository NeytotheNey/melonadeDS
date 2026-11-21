#ifndef LOGQUEUE_H
#define LOGQUEUE_H

#include <cstdarg>
#include "MelonLog.h"

/**
 * Logcat lines from the emulator, render and audio threads, queued in a lock-free ring and written to logcat by one writer thread, so that
 * logging costs a caller a format and a copy instead of a logd write. A full queue drops lines (counted and reported about once a second), and
 * before start() the lines go straight to logcat. Tags must be string literals, since the queue keeps the pointer.
 */
namespace LogQueue
{

/**
 * Starts the writer thread (once; later calls do nothing). [reportCounts] is called on it a second after [hasPendingCounts] first says there's
 * something to report (so about once a second at most), and on flush(), after a drain, to queue its own counts, which are written right after it
 * returns. Whoever bumps such a count from zero calls nudge(), since the writer sleeps without a timeout while nothing is pending.
 */
void start(void (*reportCounts)(), bool (*hasPendingCounts)());

/**
 * While on (the debug log is recording), each queued line gets the producer's thread id and push time (CLOCK_REALTIME, in local time to the
 * microsecond, like logcat's threadtime column) appended as " [tid <tid> at HH:MM:SS.uuuuuu]", since logcat only sees the writer thread and the
 * time it wrote the line. It's a suffix, not a prefix, so that scripts matching "melonade-audio: audio frame=" keep working.
 */
void setStampingEnabled(bool isEnabled);

void write(int priority, const char* tag, const char* text);

void print(int priority, const char* tag, const char* format, ...) __attribute__((format(MELONADE_PRINTF_FORMAT, 3, 4)));

void vprint(int priority, const char* tag, const char* format, va_list args);

/**
 * Wakes the writer, if it sleeps, to look at the pending counts.
 */
void nudge();

/**
 * Writes everything queued so far to logcat from the calling thread, plus the pending drop counts, for the stop and exit paths; waits up to
 * about 10 ms for a line another thread is still writing into the queue.
 */
void flush();

}

/**
 * LOG_INFO and friends through the queue, for anything that can log every frame or every second
 */
#define LOG_INFO_QUEUED(tag, message, ...) ::LogQueue::print(ANDROID_LOG_INFO, tag, message, ##__VA_ARGS__)
#define LOG_WARN_QUEUED(tag, message, ...) ::LogQueue::print(ANDROID_LOG_WARN, tag, message, ##__VA_ARGS__)
#define LOG_ERROR_QUEUED(tag, message, ...) ::LogQueue::print(ANDROID_LOG_ERROR, tag, message, ##__VA_ARGS__)

#endif //LOGQUEUE_H
