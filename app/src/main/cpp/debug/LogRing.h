#ifndef LOGRING_H
#define LOGRING_H

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#if defined(__linux__)
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#else
#include <chrono>
#include <thread>
#endif

/**
 * The seconds count of a cheap, coarse monotonic clock (CLOCK_MONOTONIC_COARSE, read from the vDSO without a syscall), for per-second budgets.
 */
inline uint32_t coarseMonotonicSecond()
{
#if defined(__linux__)
    timespec now;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
    return (uint32_t) now.tv_sec;
#else
    return (uint32_t) std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

/**
 * Takes one line from a lock-free per-second budget, [window] holding the second it counts in (high 32 bits) and the lines taken in it (low 32
 * bits); a line over budget costs one atomic load.
 */
inline bool takeLogBudget(std::atomic<uint64_t>& window, uint32_t second, uint32_t maxPerSecond)
{
    uint64_t value = window.load(std::memory_order_relaxed);
    while (true)
    {
        uint64_t next;
        if ((uint32_t) (value >> 32) != second)
            next = ((uint64_t) second << 32) | 1;
        else if ((uint32_t) value >= maxPerSecond)
            return false;
        else
            next = value + 1;

        if (window.compare_exchange_weak(value, next, std::memory_order_relaxed))
            return true;
    }
}

/**
 * Per call site budgets keyed on the format string's address, in a fixed open-addressed table that never allocates or locks; call sites that
 * don't fit share one last entry.
 */
template<std::size_t Capacity>
class LogSiteBudgets
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr std::size_t MAX_PROBES = 8;

public:
    struct Site
    {
        std::atomic<const char*> format = nullptr;
        std::atomic<uint64_t> window = 0;
        // Lines dropped over budget since the last report
        std::atomic<uint32_t> overBudget = 0;
    };

    Site& find(const char* format)
    {
        // Fibonacci hashing of the address, with the low bits (alignment) dropped
        std::size_t index = (std::size_t) ((((uint64_t) (uintptr_t) format >> 2) * 0x9E3779B97F4A7C15ULL) >> 40) & (Capacity - 1);
        for (std::size_t probe = 0; probe < MAX_PROBES; probe++)
        {
            Site& site = sites[(index + probe) & (Capacity - 1)];
            const char* siteFormat = site.format.load(std::memory_order_relaxed);
            if (siteFormat == format)
                return site;
            if (siteFormat == nullptr)
            {
                // Lost the race means another thread took the entry, maybe for the same format
                if (site.format.compare_exchange_strong(siteFormat, format, std::memory_order_relaxed) || siteFormat == format)
                    return site;
            }
        }
        return overflowSite;
    }

    /**
     * Resets every site's over-budget count, returning the site that had the most (nullptr if none), with its count in [count].
     */
    const Site* takeWorstSite(uint32_t& count)
    {
        const Site* worst = nullptr;
        count = 0;
        for (Site& site : sites)
            takeIfWorse(site, worst, count);
        takeIfWorse(overflowSite, worst, count);
        return worst;
    }

    const Site* getOverflowSite() const { return &overflowSite; }

private:
    static void takeIfWorse(Site& site, const Site*& worst, uint32_t& count)
    {
        uint32_t siteCount = site.overBudget.exchange(0, std::memory_order_relaxed);
        if (siteCount > count)
        {
            worst = &site;
            count = siteCount;
        }
    }

    Site sites[Capacity];
    Site overflowSite;
};

/**
 * Lock-free multi-producer, single-consumer ring of fixed-size log lines (Vyukov's bounded queue, with each slot's sequence number saying whose
 * turn it is). A push claims the next position with a CAS, formats the line into that slot and publishes it, and drops the line if the ring is
 * full, so producers never block, lock, allocate or make a syscall (other than the futex wake when the consumer sleeps). The consumer reads the
 * slots in position order, so each thread's lines come out in the order it pushed them. Only one thread at a time may consume (the caller locks).
 * It has no destructor to run, so a process exit can't pull it out from under a thread still using it.
 */
template<uint32_t SlotCount, std::size_t SlotSize>
class LogRing
{
    static_assert((SlotCount & (SlotCount - 1)) == 0, "SlotCount must be a power of 2");

    struct alignas(64) Slot
    {
        // pos while free for the producer claiming pos, pos + 1 once that line is published, pos + SlotCount once it's consumed
        std::atomic<uint32_t> sequence;
        int32_t priority;
        // Must point at a string that lives forever (a literal)
        const char* tag;
        // The producer's thread and push time, when it stamped the line (0 otherwise)
        int32_t tid;
        int64_t timeNs;
        char text[SlotSize - 32];
    };
    static_assert(sizeof(Slot) == SlotSize, "SlotSize must be a multiple of 64");

public:
    static constexpr std::size_t TEXT_SIZE = SlotSize - 32;

    LogRing()
    {
        for (uint32_t i = 0; i < SlotCount; i++)
            slots[i].sequence.store(i, std::memory_order_relaxed);
    }

    /**
     * Producer side: formats a line into the next slot (truncated to TEXT_SIZE - 1 characters); false if the ring is full.
     */
    bool pushFormatted(int priority, const char* tag, int32_t tid, int64_t timeNs, const char* format, va_list args)
    {
        uint32_t position;
        Slot* slot = claim(position);
        if (slot == nullptr)
            return false;

        fillHeader(*slot, priority, tag, tid, timeNs);
        if (vsnprintf(slot->text, TEXT_SIZE, format, args) < 0)
            slot->text[0] = '\0';
        publish(*slot, position);
        return true;
    }

    /**
     * Producer side: copies [text] into the next slot (truncated to TEXT_SIZE - 1 characters); false if the ring is full.
     */
    bool pushText(int priority, const char* tag, int32_t tid, int64_t timeNs, const char* text)
    {
        uint32_t position;
        Slot* slot = claim(position);
        if (slot == nullptr)
            return false;

        fillHeader(*slot, priority, tag, tid, timeNs);
        std::size_t length = strnlen(text, TEXT_SIZE - 1);
        memcpy(slot->text, text, length);
        slot->text[length] = '\0';
        publish(*slot, position);
        return true;
    }

    /**
     * Consumer side: hands every published line to [sink] (priority, tag, tid, timeNs, text) in order, stopping at the first slot that is still
     * being written, and returns how many it took.
     */
    template<typename Sink>
    uint32_t drain(Sink&& sink)
    {
        uint32_t count = 0;
        while (true)
        {
            Slot& slot = slots[readPosition & (SlotCount - 1)];
            if (slot.sequence.load(std::memory_order_acquire) != readPosition + 1)
                return count;

            sink(slot.priority, slot.tag, slot.tid, slot.timeNs, slot.text);
            slot.sequence.store(readPosition + SlotCount, std::memory_order_release);
            readPosition++;
            count++;
        }
    }

    /**
     * Consumer side: whether a producer has claimed a slot it hasn't published yet (or published one since the last drain).
     */
    bool hasPendingLines() const
    {
        return claimPosition.load(std::memory_order_acquire) != readPosition;
    }

    /**
     * Consumer side, before sleeping: marks the consumer as waiting and returns false (unmarking it) if a line was published meanwhile. The
     * fence pairs with the one in wakeConsumer(), so either the consumer sees the line (or anything else the caller checks after this returns
     * true) or its producer sees the mark and wakes it.
     */
    bool prepareToWait()
    {
        consumerWaiting.store(1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (slots[readPosition & (SlotCount - 1)].sequence.load(std::memory_order_acquire) == readPosition + 1)
        {
            cancelWait();
            return false;
        }
        return true;
    }

    /**
     * Consumer side: unmarks the consumer after prepareToWait() returned true, when it finds a reason not to sleep after all.
     */
    void cancelWait()
    {
        consumerWaiting.store(0, std::memory_order_relaxed);
    }

    /**
     * Consumer side, after prepareToWait() returned true: sleeps until a producer publishes a line or calls wakeConsumer(), or until [timeoutMs]
     * passes (no timeout if it's negative).
     */
    void wait(int timeoutMs)
    {
#if defined(__linux__)
        timespec timeout;
        timeout.tv_sec = timeoutMs / 1000;
        timeout.tv_nsec = (long) (timeoutMs % 1000) * 1000000L;
        // Returns straight away if a producer already cleared the mark
        syscall(SYS_futex, reinterpret_cast<int32_t*>(&consumerWaiting), FUTEX_WAIT_PRIVATE, 1, timeoutMs >= 0 ? &timeout : nullptr, nullptr, 0);
#else
        // Host tests only: poll
        for (int waitedMs = 0; (timeoutMs < 0 || waitedMs < timeoutMs) && consumerWaiting.load(std::memory_order_relaxed) != 0; waitedMs++)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
        cancelWait();
    }

    /**
     * Producer side: wakes the consumer if it sleeps, after a push or after changing anything else the consumer checks before sleeping. Only one
     * caller gets to clear the mark, so a sleeping consumer costs one futex wake and an awake one none.
     */
    void wakeConsumer()
    {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (consumerWaiting.load(std::memory_order_relaxed) != 0 && consumerWaiting.exchange(0, std::memory_order_relaxed) != 0)
        {
#if defined(__linux__)
            syscall(SYS_futex, reinterpret_cast<int32_t*>(&consumerWaiting), FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
#endif
        }
    }

private:
    Slot* claim(uint32_t& position)
    {
        position = claimPosition.load(std::memory_order_relaxed);
        while (true)
        {
            Slot& slot = slots[position & (SlotCount - 1)];
            int32_t difference = (int32_t) (slot.sequence.load(std::memory_order_acquire) - position);
            if (difference == 0)
            {
                if (claimPosition.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
                    return &slot;
            }
            else if (difference < 0)
            {
                // The slot still holds a line from the previous lap that the consumer hasn't taken
                return nullptr;
            }
            else
            {
                position = claimPosition.load(std::memory_order_relaxed);
            }
        }
    }

    static void fillHeader(Slot& slot, int priority, const char* tag, int32_t tid, int64_t timeNs)
    {
        slot.priority = priority;
        slot.tag = tag;
        slot.tid = tid;
        slot.timeNs = timeNs;
    }

    void publish(Slot& slot, uint32_t position)
    {
        slot.sequence.store(position + 1, std::memory_order_release);
        wakeConsumer();
    }

    Slot slots[SlotCount];
    alignas(64) std::atomic<uint32_t> claimPosition = 0;
    alignas(64) std::atomic<int32_t> consumerWaiting = 0;
    // Consumer only
    alignas(64) uint32_t readPosition = 0;
};

#endif //LOGRING_H
