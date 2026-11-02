#ifndef MONOTONICCLOCK_H
#define MONOTONICCLOCK_H

#include <cstdint>
#include <ctime>

/**
 * CLOCK_MONOTONIC now, in ns, the clock every frontend timestamp (frame pacing, vsyncs, diagnostics) is on.
 */
inline int64_t monotonicNanos()
{
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t) now.tv_sec * 1000000000LL + now.tv_nsec;
}

#endif //MONOTONICCLOCK_H
