#include "FramePacing.h"

#include <cmath>
#include <cstdlib>
#include "Configuration.h"

void DisplayVsyncTracker::onVsync(int64_t vsyncTimeNs)
{
    if (vsyncTimeNs <= lastWriterVsyncNs)
        return;

    if (lastWriterVsyncNs > 0)
        trackInterval((double) (vsyncTimeNs - lastWriterVsyncNs));

    lastWriterVsyncNs = vsyncTimeNs;
    lastVsyncNs.store(vsyncTimeNs, std::memory_order_release);
}

void DisplayVsyncTracker::trackInterval(double interval)
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

    // Missed callbacks (gaps of 1.5 periods or more) and refresh rate changes end up here, and neither is averaged into the estimate
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

void DisplayVsyncTracker::publishPeriod()
{
    refreshPeriodNs.store(std::llround(smoothedPeriodNs), std::memory_order_relaxed);
}

int VsyncFramePacer::getRefreshDivisor(int64_t nowNs, int64_t lastVsyncNs, int64_t refreshPeriodNs)
{
    if (lastVsyncNs <= 0 || refreshPeriodNs <= 0 || nowNs - lastVsyncNs > MAX_VSYNC_AGE_NS)
        return 0;

    // refresh_hz / 60
    double refreshRatio = (1000000000.0 / EMULATOR_TARGET_FPS) / (double) refreshPeriodNs;
    long divisor = std::lround(refreshRatio);
    if (divisor < 1 || divisor > MAX_REFRESH_DIVISOR)
        return 0;

    if (std::fabs(refreshRatio / (double) divisor - 1.0) > REFRESH_RATIO_TOLERANCE)
        return 0;

    return (int) divisor;
}

VsyncFramePacer::FrameSchedule VsyncFramePacer::scheduleNextFrame(int64_t nowNs, int64_t lastVsyncNs, int64_t refreshPeriodNs, int divisor, int64_t lastWorkNs, bool isBufferEnabled)
{
    int64_t framePeriodNs = refreshPeriodNs * divisor;
    int64_t gridOriginNs = lastVsyncNs + VSYNC_OFFSET_NS;
    int64_t lateSlackNs = refreshPeriodNs / LATE_SLACK_REFRESH_DENOMINATOR;
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

        // If each frame still lasts as long (for example from 60Hz to 120Hz), keep the chain of frame starts, moved to the first point at or after it
        // on the new grid (the small tolerance keeps an aligned chain from being pushed a whole refresh later by the estimate error)
        bool isSameFramePeriod = std::llabs(framePeriodNs - lastFramePeriodNs) <= (int64_t) ((double) lastFramePeriodNs * MATCHING_FRAME_PERIOD_TOLERANCE);
        if (wasLocked && overBudgetFrames < OVERLOADED_AFTER_FRAMES && isSameFramePeriod)
        {
            int64_t targetNs = getNextGridPoint(lastTargetNs + framePeriodNs - refreshPeriodNs / CARRY_OVER_TOLERANCE_REFRESH_DENOMINATOR, gridOriginNs, refreshPeriodNs);
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

        // Back on budget, so re-lock below
        isOverloaded = false;
    }
    else if (isLocked)
    {
        int64_t targetNs = lastTargetNs + framePeriodNs + getPhaseCorrection(lastTargetNs + framePeriodNs, gridOriginNs, refreshPeriodNs, framePeriodNs);
        int64_t lateNs = nowNs - targetNs;

        if (overBudgetFrames >= OVERLOADED_AFTER_FRAMES || lateNs > framePeriodNs)
        {
            isLocked = false;
            isOverloaded = true;
            return scheduleOverloadedFrame(nowNs, gridOriginNs, refreshPeriodNs, framePeriodNs);
        }

        bool isTargetPlausible = targetNs - nowNs <= framePeriodNs + refreshPeriodNs;
        if (isTargetPlausible && (lateNs <= lateSlackNs || isBufferEnabled))
            return scheduleLockedFrame(targetNs, dueOffsetNs, refreshPeriodNs, framePeriodNs);

        // Behind without the buffer (the debt is dropped), or the grid moved: re-lock below
        isLocked = false;
    }
    else if (overBudgetFrames >= OVERLOADED_AFTER_FRAMES)
    {
        isOverloaded = true;
        return scheduleOverloadedFrame(nowNs, gridOriginNs, refreshPeriodNs, framePeriodNs);
    }

    // (Re-)lock to the closest grid point, starting right away on the previous one if it was less than a quarter of a refresh ago
    int64_t phaseNs = floorMod(nowNs - gridOriginNs, refreshPeriodNs);
    int64_t targetNs = phaseNs <= lateSlackNs ? nowNs - phaseNs : nowNs + (refreshPeriodNs - phaseNs);
    isLocked = true;
    return scheduleLockedFrame(targetNs, dueOffsetNs, refreshPeriodNs, framePeriodNs);
}

int64_t VsyncFramePacer::getPhaseCorrection(int64_t targetNs, int64_t gridOriginNs, int64_t refreshPeriodNs, int64_t framePeriodNs)
{
    // How far the target is after the closest grid point, from -period/2 to +period/2
    int64_t phaseNs = floorMod(targetNs - gridOriginNs, refreshPeriodNs);
    int64_t phaseErrorNs = phaseNs > refreshPeriodNs / 2 ? phaseNs - refreshPeriodNs : phaseNs;

    int64_t maxCorrectionNs = framePeriodNs * MAX_PHASE_CORRECTION_PERCENT / 100;
    int64_t correctionNs = -phaseErrorNs / PHASE_CORRECTION_GAIN_DENOMINATOR;
    if (correctionNs > maxCorrectionNs)
        correctionNs = maxCorrectionNs;
    if (correctionNs < -maxCorrectionNs)
        correctionNs = -maxCorrectionNs;
    return correctionNs;
}

int64_t VsyncFramePacer::getNextGridPoint(int64_t nowNs, int64_t gridOriginNs, int64_t refreshPeriodNs)
{
    int64_t phaseNs = floorMod(nowNs - gridOriginNs, refreshPeriodNs);
    return phaseNs == 0 ? nowNs : nowNs + (refreshPeriodNs - phaseNs);
}

VsyncFramePacer::FrameSchedule VsyncFramePacer::scheduleLockedFrame(int64_t targetNs, int64_t dueOffsetNs, int64_t refreshPeriodNs, int64_t framePeriodNs)
{
    int64_t dueNs = targetNs + dueOffsetNs;
    // Keep the presentation order when switching modes (from overloaded, or when the buffer is turned off): a frame must be due at least one refresh
    // after the previous one, except with one refresh per frame, where it's only kept from being due before it, so the extra latency doesn't stay
    // forever after turning the buffer off
    int64_t minDueNs = framePeriodNs == refreshPeriodNs ? lastDueNs : lastDueNs + refreshPeriodNs;
    if (lastDueNs != 0 && dueNs < minDueNs)
        dueNs = minDueNs;

    lastTargetNs = targetNs;
    lastDueNs = dueNs;
    lastFramePeriodNs = framePeriodNs;
    return FrameSchedule { .startNs = targetNs, .dueNs = dueNs, .isLocked = true };
}

VsyncFramePacer::FrameSchedule VsyncFramePacer::scheduleOverloadedFrame(int64_t nowNs, int64_t gridOriginNs, int64_t refreshPeriodNs, int64_t framePeriodNs)
{
    // One frame period after the previous frame's due time, but not before the vsync after the next one; while overloaded the chain falls behind the
    // current time and the frame is just presented when it's ready
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
