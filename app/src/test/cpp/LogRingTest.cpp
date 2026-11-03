/*
 * Host test for debug/LogRing.h (the log queue's lock-free ring and budgets) that prints each failed check and exits with 1 if any failed; it
 * isn't part of the Gradle build. From app/, with any C++20 compiler (MSYS2's MinGW, say):
 *
 *   g++ -std=c++20 -O2 -pthread -Isrc/main/cpp src/test/cpp/LogRingTest.cpp -o log-ring-test && ./log-ring-test
 *
 * On Linux the consumer sleeps on the futex the app uses; elsewhere it polls, so there the wakeups are only checked for lost ones.
 */

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include "debug/LogRing.h"

static int failures = 0;

#define CHECK(condition) \
    do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); failures++; } } while (0)

template<typename Ring>
static bool pushf(Ring& ring, int priority, const char* tag, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    bool pushed = ring.pushFormatted(priority, tag, 0, 0, format, args);
    va_end(args);
    return pushed;
}

static void testOrderAndContent()
{
    static LogRing<16, 128> ring;
    for (int i = 0; i < 10; i++)
        CHECK(pushf(ring, i % 7, "tag", "line %d", i));

    CHECK(ring.pushText(3, "tag2", 1234, 5678, "stamped"));

    int next = 0;
    uint32_t count = ring.drain([&](int priority, const char* tag, int32_t tid, int64_t timeNs, const char* text) {
        if (next == 10)
        {
            CHECK(priority == 3 && std::strcmp(tag, "tag2") == 0 && tid == 1234 && timeNs == 5678 && std::strcmp(text, "stamped") == 0);
            next++;
            return;
        }
        CHECK(tid == 0 && timeNs == 0);
        char expected[32];
        std::snprintf(expected, sizeof(expected), "line %d", next);
        CHECK(priority == next % 7);
        CHECK(std::strcmp(tag, "tag") == 0);
        CHECK(std::strcmp(text, expected) == 0);
        next++;
    });
    CHECK(count == 11);
    CHECK(!ring.hasPendingLines());
    CHECK(ring.drain([](int, const char*, int32_t, int64_t, const char*) {}) == 0);
}

static void testFullAndWrap()
{
    static LogRing<8, 128> ring;
    // Several laps, so the sequence numbers wrap around the slots
    for (int lap = 0; lap < 5; lap++)
    {
        int pushed = 0;
        for (int i = 0; i < 12; i++)
            pushed += pushf(ring, 4, "t", "lap %d line %d", lap, i) ? 1 : 0;
        CHECK(pushed == 8);

        int next = 0;
        ring.drain([&](int, const char*, int32_t, int64_t, const char* text) {
            char expected[32];
            std::snprintf(expected, sizeof(expected), "lap %d line %d", lap, next);
            CHECK(std::strcmp(text, expected) == 0);
            next++;
        });
        CHECK(next == 8);
    }
}

static void testTruncation()
{
    static LogRing<4, 64> ring;
    std::string longText(500, 'x');
    CHECK(ring.pushText(4, "t", 0, 0, longText.c_str()));
    CHECK(pushf(ring, 4, "t", "%s", longText.c_str()));
    int lines = 0;
    ring.drain([&](int, const char*, int32_t, int64_t, const char* text) {
        CHECK(std::strlen(text) == decltype(ring)::TEXT_SIZE - 1);
        lines++;
    });
    CHECK(lines == 2);
}

/**
 * [producers] threads push [linesEach] lines each while one consumer drains (sleeping without a timeout like the app's idle writer when
 * [sleepingConsumer], woken by the pushes and, once all producers are done, by a wakeConsumer() nudge); checks that each thread's lines arrive in
 * order and that every line either arrives or was counted as dropped.
 */
template<uint32_t Slots>
static void testConcurrent(int producers, int linesEach, bool sleepingConsumer, bool expectNoDrops)
{
    static LogRing<Slots, 128> ring;
    std::atomic<int> producersDone = 0;
    std::vector<int> dropped(producers, 0);
    std::vector<int> received(producers, 0);
    std::vector<int> lastSeen(producers, -1);
    int outOfOrder = 0;
    int garbled = 0;

    std::thread consumer([&]() {
        auto take = [&](int, const char*, int32_t, int64_t, const char* text) {
            int thread, index;
            if (std::sscanf(text, "thread %d line %d", &thread, &index) != 2 || thread < 0 || thread >= producers)
            {
                garbled++;
                return;
            }
            if (index <= lastSeen[thread])
                outOfOrder++;
            lastSeen[thread] = index;
            received[thread]++;
        };
        while (true)
        {
            bool wereAllDone = producersDone.load() == producers;
            ring.drain(take);
            if (wereAllDone && !ring.hasPendingLines())
                break;
            if (sleepingConsumer && ring.prepareToWait())
            {
                // Checked after prepareToWait(), like the writer's pending counts, so the done flag either shows up here or comes with a nudge
                if (producersDone.load() == producers)
                    ring.cancelWait();
                else
                    ring.wait(-1);
            }
        }
    });

    std::vector<std::thread> threads;
    for (int t = 0; t < producers; t++)
    {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < linesEach; i++)
            {
                if (!pushf(ring, 4, "t", "thread %d line %d", t, i))
                    dropped[t]++;
                // Some threads pause now and then, so the consumer goes to sleep with lines still to come
                if (t % 2 == 1 && i % 64 == 0)
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            producersDone.fetch_add(1);
            ring.wakeConsumer();
        });
    }
    for (auto& thread : threads)
        thread.join();
    consumer.join();

    CHECK(outOfOrder == 0);
    CHECK(garbled == 0);
    int totalDropped = 0;
    for (int t = 0; t < producers; t++)
    {
        CHECK(received[t] + dropped[t] == linesEach);
        totalDropped += dropped[t];
    }
    if (expectNoDrops)
        CHECK(totalDropped == 0);
    std::printf("concurrent: %d producers x %d lines, %u slots, %s consumer: %d dropped\n", producers, linesEach, Slots,
            sleepingConsumer ? "sleeping" : "spinning", totalDropped);
}

/**
 * A consumer sleeping without a timeout, with nothing queued, wakes for a wakeConsumer() nudge alone.
 */
static void testNudge()
{
    static LogRing<8, 64> ring;
    std::atomic<bool> hasCount = false;
    std::atomic<bool> sawCount = false;
    std::thread consumer([&]() {
        while (true)
        {
            ring.drain([](int, const char*, int32_t, int64_t, const char*) {});
            if (!ring.prepareToWait())
                continue;
            if (hasCount.load())
            {
                ring.cancelWait();
                sawCount.store(true);
                return;
            }
            ring.wait(-1);
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    hasCount.store(true);
    ring.wakeConsumer();
    consumer.join();
    CHECK(sawCount.load());
}

static void testBudget()
{
    std::atomic<uint64_t> window = 0;
    int taken = 0;
    for (int i = 0; i < 100; i++)
        taken += takeLogBudget(window, 7, 30) ? 1 : 0;
    CHECK(taken == 30);
    // A new second starts over
    CHECK(takeLogBudget(window, 8, 30));
    taken = 1;
    for (int i = 0; i < 100; i++)
        taken += takeLogBudget(window, 8, 30) ? 1 : 0;
    CHECK(taken == 30);

    // Several threads at once never take more than the budget
    std::atomic<uint64_t> sharedWindow = 0;
    std::atomic<int> sharedTaken = 0;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++)
    {
        threads.emplace_back([&]() {
            for (int i = 0; i < 10000; i++)
                if (takeLogBudget(sharedWindow, 3, 200))
                    sharedTaken.fetch_add(1);
        });
    }
    for (auto& thread : threads)
        thread.join();
    CHECK(sharedTaken.load() == 200);
}

static void testSiteBudgets()
{
    static LogSiteBudgets<8> sites;
    static const char formats[32][8] = {};
    auto& first = sites.find(formats[0]);
    CHECK(&sites.find(formats[0]) == &first);
    CHECK(first.format.load() == formats[0]);
    CHECK(&sites.find(formats[1]) != &first);

    // More formats than entries: the rest share the overflow entry
    int overflowed = 0;
    for (auto& format : formats)
        overflowed += &sites.find(format) == sites.getOverflowSite() ? 1 : 0;
    CHECK(overflowed == 32 - 8);

    first.overBudget.fetch_add(3);
    sites.find(formats[1]).overBudget.fetch_add(9);
    uint32_t count;
    const auto* worst = sites.takeWorstSite(count);
    CHECK(worst == &sites.find(formats[1]));
    CHECK(count == 9);
    CHECK(sites.takeWorstSite(count) == nullptr);
    CHECK(count == 0);
}

int main()
{
    // A lost wakeup would hang an untimed wait forever, so a hang fails the test instead
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::seconds(20));
        std::printf("FAIL: timed out, probably a lost wakeup\n");
        std::fflush(stdout);
        std::_Exit(1);
    }).detach();

    testOrderAndContent();
    testFullAndWrap();
    testTruncation();
    // Fewer lines than slots: nothing may be lost, however the threads interleave
    testConcurrent<1024>(4, 200, false, true);
    testConcurrent<1024>(4, 200, true, true);
    // Far more lines than slots: drops are fine, but every line is accounted for and each thread's lines stay in order
    testConcurrent<64>(6, 20000, false, false);
    testConcurrent<64>(6, 20000, true, false);
    testNudge();
    testBudget();
    testSiteBudgets();

    if (failures == 0)
        std::printf("All checks passed\n");
    return failures == 0 ? 0 : 1;
}
