#include "LogQueue.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <mutex>
#include <thread>
#include <type_traits>
#include <pthread.h>
#include <unistd.h>
#include "LogRing.h"
#include "MonotonicClock.h"

namespace LogQueue
{

// 256 KB: a 1 KB slot holds the longest line (the audio heartbeat, about 700-850 characters), and 256 of them cover a burst of a few hundred
// lines while the writer is descheduled
static constexpr uint32_t SLOT_COUNT = 256;
static constexpr std::size_t SLOT_SIZE = 1024;
// How long pending drop and repeat counts wait to be reported, so they come out about once a second
static constexpr int64_t REPORT_INTERVAL_NS = 1000000000LL;
// " [tid 4194304 at 23:59:59.999999]"
static constexpr std::size_t MAX_STAMP_LENGTH = 48;

using Ring = LogRing<SLOT_COUNT, SLOT_SIZE>;

// Nothing here may have a destructor: Runtime.exit() (the settings backup restore) runs static destructors while the writer thread still runs,
// and bionic aborts on a destroyed mutex. So the mutex is allocated and never freed, and everything else is trivially destructible.
static_assert(std::is_trivially_destructible_v<Ring>);
static_assert(std::is_trivially_destructible_v<std::once_flag>);
static Ring ring;
static std::atomic_bool isStarted = false;
static std::atomic_bool isStamping = false;
static std::once_flag startOnce;
// Taken by whoever consumes the ring (the writer thread, or flush()); producers never touch it
static std::mutex& drainMutex = *new std::mutex();
static void (*countReporter)() = nullptr;
static bool (*pendingCountsCheck)() = nullptr;
static std::atomic<uint32_t> fullDrops = 0;

// Writer side only: the local time of the last stamped second, so localtime_r runs once a second at most
static time_t stampSecond = -1;
static tm stampTime;

static void writeLine(int priority, const char* tag, int32_t tid, int64_t timeNs, const char* text)
{
    if (tid == 0)
    {
        __android_log_write(priority, tag, text);
        return;
    }

    time_t second = (time_t) (timeNs / 1000000000LL);
    if (second != stampSecond)
    {
        localtime_r(&second, &stampTime);
        stampSecond = second;
    }
    char line[Ring::TEXT_SIZE + MAX_STAMP_LENGTH];
    snprintf(line, sizeof(line), "%s [tid %d at %02d:%02d:%02d.%06ld]", text, tid, stampTime.tm_hour, stampTime.tm_min, stampTime.tm_sec,
            (long) (timeNs % 1000000000LL / 1000));
    __android_log_write(priority, tag, line);
}

static bool hasPendingCounts()
{
    return fullDrops.load(std::memory_order_relaxed) != 0 || (pendingCountsCheck != nullptr && pendingCountsCheck());
}

// Must be called with drainMutex held
static void reportLocked()
{
    // Straight to logcat, since the queue may still be full
    uint32_t dropped = fullDrops.exchange(0, std::memory_order_relaxed);
    if (dropped > 0)
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "(%u log lines dropped, the log queue was full)", dropped);
    if (countReporter != nullptr)
    {
        countReporter();
        ring.drain(writeLine);
    }
}

static void* writerMain(void*)
{
    // Left at the normal priority: lines still in the ring when the process dies never reach logd (or a tombstone), so the writer shouldn't lag
    pthread_setname_np(pthread_self(), "melonade-log");

    // When the pending counts get reported (a second after the writer first saw them, so a burst is summed up in one report), -1 if none are
    int64_t reportDueNs = -1;
    while (true)
    {
        int timeoutMs;
        {
            std::lock_guard<std::mutex> lock(drainMutex);
            ring.drain(writeLine);
            int64_t nowNs = monotonicNanos();
            if (reportDueNs >= 0 && nowNs >= reportDueNs)
            {
                reportLocked();
                reportDueNs = -1;
            }
            if (!ring.prepareToWait())
                continue;

            // Checked after prepareToWait()'s fence, so a count bumped from now on comes with a nudge. Nothing pending means nothing to wake up
            // for until the next push or nudge.
            if (hasPendingCounts())
            {
                if (reportDueNs < 0)
                    reportDueNs = nowNs + REPORT_INTERVAL_NS;
                timeoutMs = (int) ((reportDueNs - nowNs) / 1000000) + 1;
            }
            else
            {
                reportDueNs = -1;
                timeoutMs = -1;
            }
        }
        ring.wait(timeoutMs);
    }
    return nullptr;
}

void start(void (*reportCounts)(), bool (*hasPendingCounts)())
{
    std::call_once(startOnce, [reportCounts, hasPendingCounts]() {
        countReporter = reportCounts;
        pendingCountsCheck = hasPendingCounts;
        pthread_attr_t attributes;
        pthread_attr_init(&attributes);
        pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
        pthread_t thread;
        if (pthread_create(&thread, &attributes, writerMain, nullptr) == 0)
            isStarted.store(true, std::memory_order_release);
        else
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Couldn't start the log writer thread, logging straight to logcat");
        pthread_attr_destroy(&attributes);
    });
}

void setStampingEnabled(bool isEnabled)
{
    isStamping.store(isEnabled, std::memory_order_relaxed);
}

static int64_t realtimeNanos()
{
    timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return (int64_t) now.tv_sec * 1000000000LL + now.tv_nsec;
}

// bionic caches the tid, and CLOCK_REALTIME comes from the vDSO, so a stamp costs no syscall
#define TAKE_STAMP(tid, timeNs) \
    int32_t tid = 0; \
    int64_t timeNs = 0; \
    if (isStamping.load(std::memory_order_relaxed)) [[unlikely]] \
    { \
        tid = gettid(); \
        timeNs = realtimeNanos(); \
    }

static void countFullDrop()
{
    if (fullDrops.fetch_add(1, std::memory_order_relaxed) == 0)
        ring.wakeConsumer();
}

void write(int priority, const char* tag, const char* text)
{
    if (!isStarted.load(std::memory_order_acquire)) [[unlikely]]
    {
        __android_log_write(priority, tag, text);
        return;
    }

    TAKE_STAMP(tid, timeNs)
    if (!ring.pushText(priority, tag, tid, timeNs, text)) [[unlikely]]
        countFullDrop();
}

void vprint(int priority, const char* tag, const char* format, va_list args)
{
    if (!isStarted.load(std::memory_order_acquire)) [[unlikely]]
    {
        __android_log_vprint(priority, tag, format, args);
        return;
    }

    TAKE_STAMP(tid, timeNs)
    if (!ring.pushFormatted(priority, tag, tid, timeNs, format, args)) [[unlikely]]
        countFullDrop();
}

void print(int priority, const char* tag, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vprint(priority, tag, format, args);
    va_end(args);
}

void nudge()
{
    if (isStarted.load(std::memory_order_acquire))
        ring.wakeConsumer();
}

void flush()
{
    if (!isStarted.load(std::memory_order_acquire))
        return;

    std::lock_guard<std::mutex> lock(drainMutex);
    // A thread caught between claiming a slot and publishing it holds up everything after it, so it gets a moment to finish
    for (int attempt = 0; ; attempt++)
    {
        ring.drain(writeLine);
        if (!ring.hasPendingLines() || attempt >= 100)
            break;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    reportLocked();
}

}
