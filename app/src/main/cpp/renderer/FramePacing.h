#ifndef FRAMEPACING_H
#define FRAMEPACING_H

#include <atomic>
#include <cstdint>

/**
 * Tracks the display's vsync timestamps and estimates its refresh period from the interval between them. Timestamps are published from the
 * Choreographer callback on the UI thread (the only writer) and read by the emulator and render threads. All times are CLOCK_MONOTONIC, in ns.
 */
class DisplayVsyncTracker
{
public:
    /**
     * Writer side. Must always be called from the same thread. Never allocates or blocks.
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
    // Intervals outside of this range (over 250Hz, or under 20Hz) are never used. Long intervals happen when the app goes to the background
    static constexpr double MIN_INTERVAL_NS = 4000000.0;
    static constexpr double MAX_INTERVAL_NS = 50000000.0;
    // Intervals within this fraction of the current estimate are averaged into it
    static constexpr double MATCHING_INTERVAL_TOLERANCE = 0.1;
    static constexpr double SMOOTHING_FACTOR = 1.0 / 16.0;
    // Number of consecutive matching intervals that don't match the current estimate after which the estimate is replaced. Allows the estimate
    // to follow refresh rate changes without reacting to isolated missed callbacks. Intervals shorter than the estimate can't be missed
    // callbacks, so a faster refresh rate (for example 60Hz to 120Hz) is followed after very few of them. Until then, the render thread's due
    // tolerance (half of the old period) is a whole new refresh, and the frame pacer sees the grid flip by half a period on every vsync. Longer
    // intervals (for example 120Hz to 60Hz) take a few more, since a missed callback looks the same as one of them
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
 * Schedules the start of emulator frames on the display's vsync grid, so that each frame finishes at a consistent point relative to the refresh it
 * is presented on. Only used from the emulator thread.
 */
class VsyncFramePacer
{
public:
    static constexpr int MAX_REFRESH_DIVISOR = 4;
    // Frames start this long after a vsync
    static constexpr int64_t VSYNC_OFFSET_NS = 500000;
    // Pacing is only used while vsyncs keep arriving
    static constexpr int64_t MAX_VSYNC_AGE_NS = 100000000;
    // When the refresh divisor changes, the chain of frame starts is kept if the frame period is within this fraction of the previous one. Each
    // refresh rate is within 0.5% of a multiple of 60Hz (see [getRefreshDivisor]), so this holds for any change between two paced rates
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
        // When the frame should start. If it isn't after the current time, the frame starts right away
        int64_t startNs;
        // Vsync the frame is due on, or 0 to present it as soon as it's ready
        int64_t dueNs;
        // Whether the frame's start is locked to the vsync grid
        bool isLocked;
    };

    /**
     * Forgets the current phase and the previous frame's due time. Used when the next frame isn't paced (after a pause, or when falling back to
     * the timer). The next scheduled frame re-syncs to the vsync grid.
     */
    void unlock()
    {
        isLocked = false;
        isOverloaded = false;
        overBudgetFrames = 0;
        lastDueNs = 0;
    }

    /**
     * Like [unlock], for when pacing stops while running (falling back to the timer). Returns the due time for the first unpaced frame: one frame
     * period after the last paced one, so that it isn't presented ahead of a buffered frame that is still queued. Returns 0 if the last frame
     * wasn't paced, so every frame after the first one is presented as soon as it's ready.
     */
    int64_t unlockFromRunning()
    {
        int64_t handoffDueNs = lastDueNs != 0 ? lastDueNs + lastFramePeriodNs : 0;
        unlock();
        return handoffDueNs;
    }

    /**
     * Schedules the next frame. [lastWorkNs] is how long the frame that just ran took to emulate.
     *
     * While locked, each frame starts one frame period (N refreshes) after the previous one, nudged towards the closest grid point. Being late by up
     * to a quarter of a refresh (wake-up latency) is ignored. With the buffer, being further behind is caught up as long as it's under one frame
     * period, since the buffered frame hides it. Without the buffer, the lost time is dropped instead (the audio skew absorbs it) and the pacer
     * re-locks, so a hitch costs a single stall instead of a run of short holds afterwards.
     *
     * After 2 frames in a row over budget, or once more than a frame period behind, the emulator is considered overloaded: frames start right away
     * so that heavy scenes never run slower than with the timer. They stay that way while the work is still over budget. Their due times keep
     * following the previous frame's, so they can never be presented ahead of a buffered frame that is still queued.
     *
     * When the refresh rate changes but the frame period doesn't (only the number of refreshes per frame changes), the chain of frame starts
     * carries over to the new grid instead of re-locking from the current time.
     */
    FrameSchedule scheduleNextFrame(int64_t nowNs, int64_t lastVsyncNs, int64_t refreshPeriodNs, int divisor, int64_t lastWorkNs, bool isBufferEnabled);

private:
    static int64_t floorMod(int64_t value, int64_t divisor)
    {
        int64_t result = value % divisor;
        return result < 0 ? result + divisor : result;
    }

    /**
     * Moves the target a fraction of the way back to the closest grid point. It only drifts away because the refresh period is an estimate. The
     * step is clamped so that the emulation speed never changes by more than 2%.
     */
    static int64_t getPhaseCorrection(int64_t targetNs, int64_t gridOriginNs, int64_t refreshPeriodNs, int64_t framePeriodNs);

    /**
     * First grid point at or after [nowNs]. Always less than one refresh period away.
     */
    static int64_t getNextGridPoint(int64_t nowNs, int64_t gridOriginNs, int64_t refreshPeriodNs);

    FrameSchedule scheduleLockedFrame(int64_t targetNs, int64_t dueOffsetNs, int64_t refreshPeriodNs, int64_t framePeriodNs);
    FrameSchedule scheduleOverloadedFrame(int64_t nowNs, int64_t gridOriginNs, int64_t refreshPeriodNs, int64_t framePeriodNs);

    bool isLocked = false;
    bool isOverloaded = false;
    int lockedDivisor = 0;
    int overBudgetFrames = 0;
    int64_t lastTargetNs = 0;
    // Due time and frame period of the last scheduled frame. The due time is 0 if it wasn't paced
    int64_t lastDueNs = 0;
    int64_t lastFramePeriodNs = 0;
};

#endif //FRAMEPACING_H
