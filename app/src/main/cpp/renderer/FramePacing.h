#ifndef FRAMEPACING_H
#define FRAMEPACING_H

#include <atomic>
#include <cstdint>

/**
 * Tracks the display's vsync timestamps and estimates its refresh period from the interval between them, all CLOCK_MONOTONIC ns. The Choreographer
 * callback on the UI thread is the only writer; the emulator and render threads read.
 */
class DisplayVsyncTracker
{
public:
    /**
     * Writer side: must always be called from the same thread, and never allocates or blocks.
     */
    void onVsync(int64_t vsyncTimeNs);

    /**
     * Timestamp of the latest vsync, or 0 if none has been seen.
     */
    int64_t getLastVsyncNs() const
    {
        return lastVsyncNs.load(std::memory_order_acquire);
    }

    /**
     * Estimated refresh period, or 0 if it's not known yet.
     */
    int64_t getRefreshPeriodNs() const
    {
        return refreshPeriodNs.load(std::memory_order_relaxed);
    }

private:
    // Intervals outside of this range (over 250Hz, or under 20Hz, as when the app goes to the background) are never used
    static constexpr double MIN_INTERVAL_NS = 4000000.0;
    static constexpr double MAX_INTERVAL_NS = 50000000.0;
    // Intervals within this fraction of the current estimate are averaged into it
    static constexpr double MATCHING_INTERVAL_TOLERANCE = 0.1;
    static constexpr double SMOOTHING_FACTOR = 1.0 / 16.0;
    // Number of consecutive matching intervals off the current estimate after which it's replaced, so it follows refresh rate changes but not
    // isolated missed callbacks; shorter intervals can't be missed callbacks, so they need fewer
    static constexpr int RESEED_SHORTER_INTERVAL_COUNT = 3;
    static constexpr int RESEED_LONGER_INTERVAL_COUNT = 4;

    void trackInterval(double interval);
    void publishPeriod();

    std::atomic<int64_t> lastVsyncNs{0};
    std::atomic<int64_t> refreshPeriodNs{0};

    // Only accessed by the writer
    int64_t lastWriterVsyncNs = 0;
    double smoothedPeriodNs = 0.0;
    double candidatePeriodNs = 0.0;
    int candidateIntervalCount = 0;
};

/**
 * Schedules the start of emulator frames on the display's vsync grid so each one finishes at a consistent point relative to the refresh it's
 * presented on, from the emulator thread only. While locked each frame starts N refreshes after the previous one, nudged towards the closest grid
 * point, and once overloaded frames start right away, with due times that never get ahead of a queued buffered frame.
 */
class VsyncFramePacer
{
public:
    static constexpr int MAX_REFRESH_DIVISOR = 4;
    // Frames start this long after a vsync
    static constexpr int64_t VSYNC_OFFSET_NS = 500000;
    // Pacing is only used while vsyncs keep arriving
    static constexpr int64_t MAX_VSYNC_AGE_NS = 100000000;
    // When the refresh divisor changes, the chain of frame starts is kept if the frame period is within this fraction of the previous one, which
    // holds for any change between two paced rates (each is within 0.5% of a multiple of 60Hz)
    static constexpr double MATCHING_FRAME_PERIOD_TOLERANCE = 0.02;
    // Frames are only locked to the display if refresh_hz / 60 is within this fraction of a whole number
    static constexpr double REFRESH_RATIO_TOLERANCE = 0.005;
    // Being late by up to this fraction of a refresh (1/4, wake-up latency) is ignored
    static constexpr int64_t LATE_SLACK_REFRESH_DENOMINATOR = 4;
    // When the chain of frame starts carries over to a new grid, a target up to this fraction of a refresh (1/8) past a grid point still goes
    // to that point instead of the next one
    static constexpr int64_t CARRY_OVER_TOLERANCE_REFRESH_DENOMINATOR = 8;
    // The emulator is considered overloaded after this many frames in a row over budget
    static constexpr int OVERLOADED_AFTER_FRAMES = 2;
    // Each frame, the phase correction moves the target this fraction (1/8) of the way back to the closest grid point...
    static constexpr int64_t PHASE_CORRECTION_GAIN_DENOMINATOR = 8;
    // ...but never by more than this percentage of a frame period, so that the emulation speed never changes by more than that
    static constexpr int64_t MAX_PHASE_CORRECTION_PERCENT = 2;

    /**
     * Returns the number of refreshes each 60 FPS frame should last (N, from 1 to [MAX_REFRESH_DIVISOR]) if the display's refresh rate is within
     * 0.5% of a multiple of 60Hz and vsyncs are recent, or 0 if frames can't be locked to the display.
     */
    static int getRefreshDivisor(int64_t nowNs, int64_t lastVsyncNs, int64_t refreshPeriodNs);

    struct FrameSchedule
    {
        // When the frame should start, right away if that isn't after the current time
        int64_t startNs;
        // Vsync the frame is due on, or 0 to present it as soon as it's ready
        int64_t dueNs;
        // Whether the frame's start is locked to the vsync grid
        bool isLocked;
    };

    /**
     * Forgets the current phase and the previous frame's due time when the next frame isn't paced (after a pause, or when falling back to the timer),
     * so the next scheduled frame re-syncs to the vsync grid.
     */
    void unlock()
    {
        isLocked = false;
        isOverloaded = false;
        overBudgetFrames = 0;
        lastDueNs = 0;
    }

    /**
     * Like [unlock], for when pacing stops while running: returns the due time for the first unpaced frame, one frame period after the last paced one
     * so it isn't presented ahead of a queued buffered frame, or 0 if the last frame wasn't paced.
     */
    int64_t unlockFromRunning()
    {
        int64_t handoffDueNs = lastDueNs != 0 ? lastDueNs + lastFramePeriodNs : 0;
        unlock();
        return handoffDueNs;
    }

    /**
     * Schedules the next frame, where [lastWorkNs] is how long the frame that just ran took to emulate.
     */
    FrameSchedule scheduleNextFrame(int64_t nowNs, int64_t lastVsyncNs, int64_t refreshPeriodNs, int divisor, int64_t lastWorkNs, bool isBufferEnabled);

private:
    static int64_t floorMod(int64_t value, int64_t divisor)
    {
        int64_t result = value % divisor;
        return result < 0 ? result + divisor : result;
    }

    /**
     * Moves the target a fraction of the way back to the closest grid point (it only drifts because the refresh period is an estimate), clamped so
     * the emulation speed never changes by more than 2%.
     */
    static int64_t getPhaseCorrection(int64_t targetNs, int64_t gridOriginNs, int64_t refreshPeriodNs, int64_t framePeriodNs);

    /**
     * First grid point at or after [nowNs], always less than one refresh period away.
     */
    static int64_t getNextGridPoint(int64_t nowNs, int64_t gridOriginNs, int64_t refreshPeriodNs);

    FrameSchedule scheduleLockedFrame(int64_t targetNs, int64_t dueOffsetNs, int64_t refreshPeriodNs, int64_t framePeriodNs);
    FrameSchedule scheduleOverloadedFrame(int64_t nowNs, int64_t gridOriginNs, int64_t refreshPeriodNs, int64_t framePeriodNs);

    bool isLocked = false;
    bool isOverloaded = false;
    int lockedDivisor = 0;
    int overBudgetFrames = 0;
    int64_t lastTargetNs = 0;
    // Due time (0 if it wasn't paced) and frame period of the last scheduled frame
    int64_t lastDueNs = 0;
    int64_t lastFramePeriodNs = 0;
};

#endif //FRAMEPACING_H
