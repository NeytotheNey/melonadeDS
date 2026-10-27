#ifndef FRAMEPACING_H
#define FRAMEPACING_H

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>

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
    void onVsync(int64_t vsyncTimeNs)
    {
        if (vsyncTimeNs <= lastWriterVsyncNs)
            return;

        if (lastWriterVsyncNs > 0)
            trackInterval((double) (vsyncTimeNs - lastWriterVsyncNs));

        lastWriterVsyncNs = vsyncTimeNs;
        lastVsyncNs.store(vsyncTimeNs, std::memory_order_release);
    }

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

    void trackInterval(double interval)
    {
        if (interval < MIN_INTERVAL_NS || interval > MAX_INTERVAL_NS)
        {
            candidateIntervalCount = 0;
            return;
        }

        if (smoothedPeriodNs <= 0.0)
        {
            smoothedPeriodNs = interval;
            publishPeriod();
            return;
        }

        if (std::fabs(interval - smoothedPeriodNs) <= smoothedPeriodNs * MATCHING_INTERVAL_TOLERANCE)
        {
            smoothedPeriodNs += (interval - smoothedPeriodNs) * SMOOTHING_FACTOR;
            candidateIntervalCount = 0;
            publishPeriod();
            return;
        }

        // Missed callbacks (gaps of 1.5 periods or more) and refresh rate changes end up here. Neither is averaged into the estimate
        if (candidateIntervalCount > 0 && std::fabs(interval - candidatePeriodNs) <= candidatePeriodNs * MATCHING_INTERVAL_TOLERANCE)
        {
            candidateIntervalCount++;
            candidatePeriodNs += (interval - candidatePeriodNs) / candidateIntervalCount;
        }
        else
        {
            candidateIntervalCount = 1;
            candidatePeriodNs = interval;
        }

        // All candidates match each other, so they are all on the same side of the estimate
        int reseedIntervalCount = candidatePeriodNs < smoothedPeriodNs ? RESEED_SHORTER_INTERVAL_COUNT : RESEED_LONGER_INTERVAL_COUNT;
        if (candidateIntervalCount >= reseedIntervalCount)
        {
            smoothedPeriodNs = candidatePeriodNs;
            candidateIntervalCount = 0;
            publishPeriod();
        }
    }

    void publishPeriod()
    {
        refreshPeriodNs.store(std::llround(smoothedPeriodNs), std::memory_order_relaxed);
    }

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

    /**
     * Returns the number of refreshes each 60 FPS frame should last (N, from 1 to [MAX_REFRESH_DIVISOR]) if the display's refresh rate is within
     * 0.5% of a multiple of 60Hz and vsyncs are recent, or 0 if frames can't be locked to the display.
     */
    static int getRefreshDivisor(int64_t nowNs, int64_t lastVsyncNs, int64_t refreshPeriodNs)
    {
        if (lastVsyncNs <= 0 || refreshPeriodNs <= 0 || nowNs - lastVsyncNs > MAX_VSYNC_AGE_NS)
            return 0;

        // refresh_hz / 60
        double refreshRatio = (1000000000.0 / 60.0) / (double) refreshPeriodNs;
        long divisor = std::lround(refreshRatio);
        if (divisor < 1 || divisor > MAX_REFRESH_DIVISOR)
            return 0;

        if (std::fabs(refreshRatio / (double) divisor - 1.0) > 0.005)
            return 0;

        return (int) divisor;
    }

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
    FrameSchedule scheduleNextFrame(int64_t nowNs, int64_t lastVsyncNs, int64_t refreshPeriodNs, int divisor, int64_t lastWorkNs, bool isBufferEnabled)
    {
        int64_t framePeriodNs = refreshPeriodNs * divisor;
        int64_t gridOriginNs = lastVsyncNs + VSYNC_OFFSET_NS;
        int64_t lateSlackNs = refreshPeriodNs / 4;
        int64_t dueOffsetNs = framePeriodNs * (isBufferEnabled ? 2 : 1);

        bool isOverBudget = lastWorkNs > framePeriodNs;
        overBudgetFrames = isOverBudget ? overBudgetFrames + 1 : 0;

        if (divisor != lockedDivisor)
        {
            // The refresh rate changed
            bool wasLocked = isLocked;
            isLocked = false;
            isOverloaded = false;
            lockedDivisor = divisor;

            // If each frame still lasts as long (for example from 60Hz to 120Hz, with twice the refreshes per frame), keep the chain of frame
            // starts, moved to the first point at or after it on the new grid. When the new grid contains the old one, the frames keep landing on
            // the vsyncs they were going to. Re-locking from the current time instead can start the frame up to half a frame early when the work
            // was short, and present two frames on back to back refreshes. When the new grid is on the other parity (for example from 120Hz to
            // 60Hz), the chain moves later by half a refresh. Moving it earlier would make two frames due on the same vsync, and skip one of them.
            // The small tolerance keeps an aligned chain from being pushed a whole refresh later because of the estimate error
            bool isSameFramePeriod = std::llabs(framePeriodNs - lastFramePeriodNs) <= (int64_t) ((double) lastFramePeriodNs * MATCHING_FRAME_PERIOD_TOLERANCE);
            if (wasLocked && overBudgetFrames < 2 && isSameFramePeriod)
            {
                int64_t targetNs = getNextGridPoint(lastTargetNs + framePeriodNs - refreshPeriodNs / 8, gridOriginNs, refreshPeriodNs);
                bool isTargetPlausible = nowNs - targetNs <= lateSlackNs && targetNs - nowNs <= framePeriodNs + refreshPeriodNs;
                if (isTargetPlausible)
                {
                    isLocked = true;
                    return scheduleLockedFrame(targetNs, dueOffsetNs, refreshPeriodNs, framePeriodNs);
                }
            }

            // Otherwise, start over on the new grid below
        }

        if (isOverloaded)
        {
            if (isOverBudget)
                return scheduleOverloadedFrame(nowNs, gridOriginNs, refreshPeriodNs, framePeriodNs);

            // Back on budget. Re-lock below
            isOverloaded = false;
        }
        else if (isLocked)
        {
            int64_t targetNs = lastTargetNs + framePeriodNs + getPhaseCorrection(lastTargetNs + framePeriodNs, gridOriginNs, refreshPeriodNs, framePeriodNs);
            int64_t lateNs = nowNs - targetNs;

            if (overBudgetFrames >= 2 || lateNs > framePeriodNs)
            {
                isLocked = false;
                isOverloaded = true;
                return scheduleOverloadedFrame(nowNs, gridOriginNs, refreshPeriodNs, framePeriodNs);
            }

            bool isTargetPlausible = targetNs - nowNs <= framePeriodNs + refreshPeriodNs;
            if (isTargetPlausible && (lateNs <= lateSlackNs || isBufferEnabled))
                return scheduleLockedFrame(targetNs, dueOffsetNs, refreshPeriodNs, framePeriodNs);

            // Behind without the buffer (the debt is dropped), or the grid moved. Re-lock below
            isLocked = false;
        }
        else if (overBudgetFrames >= 2)
        {
            isOverloaded = true;
            return scheduleOverloadedFrame(nowNs, gridOriginNs, refreshPeriodNs, framePeriodNs);
        }

        // (Re-)lock to the closest grid point. If the previous one was less than a quarter of a refresh ago, start right away on it instead of
        // waiting for the next one
        int64_t phaseNs = floorMod(nowNs - gridOriginNs, refreshPeriodNs);
        int64_t targetNs = phaseNs <= lateSlackNs ? nowNs - phaseNs : nowNs + (refreshPeriodNs - phaseNs);
        isLocked = true;
        return scheduleLockedFrame(targetNs, dueOffsetNs, refreshPeriodNs, framePeriodNs);
    }

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
    static int64_t getPhaseCorrection(int64_t targetNs, int64_t gridOriginNs, int64_t refreshPeriodNs, int64_t framePeriodNs)
    {
        // How far the target is after the closest grid point, from -period/2 to +period/2
        int64_t phaseNs = floorMod(targetNs - gridOriginNs, refreshPeriodNs);
        int64_t phaseErrorNs = phaseNs > refreshPeriodNs / 2 ? phaseNs - refreshPeriodNs : phaseNs;

        int64_t maxCorrectionNs = framePeriodNs * 2 / 100;
        int64_t correctionNs = -phaseErrorNs / 8;
        if (correctionNs > maxCorrectionNs)
            correctionNs = maxCorrectionNs;
        if (correctionNs < -maxCorrectionNs)
            correctionNs = -maxCorrectionNs;
        return correctionNs;
    }

    /**
     * First grid point at or after [nowNs]. Always less than one refresh period away.
     */
    static int64_t getNextGridPoint(int64_t nowNs, int64_t gridOriginNs, int64_t refreshPeriodNs)
    {
        int64_t phaseNs = floorMod(nowNs - gridOriginNs, refreshPeriodNs);
        return phaseNs == 0 ? nowNs : nowNs + (refreshPeriodNs - phaseNs);
    }

    FrameSchedule scheduleLockedFrame(int64_t targetNs, int64_t dueOffsetNs, int64_t refreshPeriodNs, int64_t framePeriodNs)
    {
        int64_t dueNs = targetNs + dueOffsetNs;
        // Keep the presentation order when switching modes (for example from overloaded, or when the buffer is turned off): a frame must be due
        // at least one refresh after the previous one, or the previous one would be dropped. When each frame lasts a single refresh, that would
        // keep the extra latency forever after turning the buffer off, so the frame is only kept from being due before the previous one (which
        // skips one frame at the switch)
        int64_t minDueNs = framePeriodNs == refreshPeriodNs ? lastDueNs : lastDueNs + refreshPeriodNs;
        if (lastDueNs != 0 && dueNs < minDueNs)
            dueNs = minDueNs;

        lastTargetNs = targetNs;
        lastDueNs = dueNs;
        lastFramePeriodNs = framePeriodNs;
        return FrameSchedule { .startNs = targetNs, .dueNs = dueNs, .isLocked = true };
    }

    FrameSchedule scheduleOverloadedFrame(int64_t nowNs, int64_t gridOriginNs, int64_t refreshPeriodNs, int64_t framePeriodNs)
    {
        // One frame period after the previous frame's due time, but not before the vsync after the next one. The chain falls behind the current
        // time while overloaded, in which case the frame is simply presented as soon as it's ready
        int64_t dueNs = 0;
        if (lastDueNs != 0)
        {
            dueNs = lastDueNs + framePeriodNs;
            int64_t earliestDueNs = getNextGridPoint(nowNs, gridOriginNs, refreshPeriodNs) + refreshPeriodNs;
            if (dueNs < earliestDueNs)
                dueNs = earliestDueNs;
        }

        lastDueNs = dueNs;
        lastFramePeriodNs = framePeriodNs;
        return FrameSchedule { .startNs = nowNs, .dueNs = dueNs, .isLocked = false };
    }

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
