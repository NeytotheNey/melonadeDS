package me.magnum.melonds.impl.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class FrameAndAudioStatisticsTest {

    private val frameNs = 16_666_667L

    private fun SessionStatistics.trackTestFrame(
        interval: Long,
        workDuration: Long = 4_000_000L,
        isFastForward: Boolean = false,
        isAfterPause: Boolean = false,
        isPacingLocked: Boolean = false,
        pacingDivisor: Int = 0,
    ) {
        trackFrame(workDuration, interval, isFastForward, isAfterPause, isPacingLocked, pacingDivisor)
    }

    @Test
    fun fastForwardAndAfterPauseFramesAreCountedButNotAnalyzed() {
        val stats = SessionStatistics()

        stats.trackTestFrame(frameNs, workDuration = 3_000_000L)
        stats.trackTestFrame(100_000_000L, isFastForward = true)
        stats.trackTestFrame(200_000_000L, isAfterPause = true)
        stats.trackTestFrame(300_000_000L, isFastForward = true, isAfterPause = true)

        assertEquals(4L, stats.totalFrames)
        assertEquals(2L, stats.fastForwardFrames)
        assertEquals(2L, stats.afterPauseFrames)
        assertEquals(1L, stats.frameIntervals.count)
        assertEquals(frameNs, stats.frameIntervals.max)
        assertEquals(1L, stats.frameWorkDurations.count)
        assertEquals(3_000_000L, stats.frameWorkDurations.max)
        // The long intervals were all left out
        assertEquals(0L, stats.missedFrames)
    }

    @Test
    fun onlyIntervalsAboveOneAndAHalfFramesAreMissed() {
        val stats = SessionStatistics()

        stats.trackTestFrame(SessionStatistics.MISSED_FRAME_THRESHOLD_NS)
        stats.trackTestFrame(SessionStatistics.MISSED_FRAME_THRESHOLD_NS + 1)

        assertEquals(25_000_000L, SessionStatistics.MISSED_FRAME_THRESHOLD_NS)
        assertEquals(1L, stats.missedFrames)
    }

    @Test
    fun pacingDivisorFollowsEveryLockedFrameButOnlyAnalyzedOnesCountAsLocked() {
        val stats = SessionStatistics()
        assertEquals(0, stats.lastPacingDivisor)

        stats.trackTestFrame(frameNs, isPacingLocked = true, pacingDivisor = 2)
        stats.trackTestFrame(frameNs, isFastForward = true, isPacingLocked = true, pacingDivisor = 1)
        stats.trackTestFrame(frameNs, isAfterPause = true, isPacingLocked = true, pacingDivisor = 3)

        assertEquals(1L, stats.pacingLockedFrames)
        assertEquals(3, stats.lastPacingDivisor)

        // An unlocked frame keeps the last divisor, whatever divisor it passes
        stats.trackTestFrame(frameNs, pacingDivisor = 4)
        assertEquals(3, stats.lastPacingDivisor)
        assertEquals(1L, stats.pacingLockedFrames)
    }

    @Test
    fun audioStatisticsStartAtTheirSentinels() {
        val stats = SessionStatistics()

        assertEquals(Long.MAX_VALUE, stats.audioFillMin)
        assertEquals(Long.MAX_VALUE, stats.audioSkewMinPpm)
        assertEquals(Long.MIN_VALUE, stats.audioSkewMaxPpm)
        assertEquals(Long.MAX_VALUE, stats.audioFastForwardSpeedMinMilli)
        assertNull(stats.averageAudioFill())
        assertNull(stats.averageAudioSkewPpm())
        assertNull(stats.averageAudioFastForwardSpeed())
    }

    @Test
    fun silenceIsCountedOnEveryFrameAndTheBufferOnlyOnIncludedOnes() {
        val stats = SessionStatistics()

        stats.trackAudioFrame(fill = 1000, silenceFrames = 0, isBufferFull = false, skewPpm = -20, isFastForward = false, isExcluded = false)
        stats.trackAudioFrame(fill = 3000, silenceFrames = 5, isBufferFull = true, skewPpm = 40, isFastForward = false, isExcluded = false)
        // Excluded, or no fill reading: only the silence counts
        stats.trackAudioFrame(fill = 10, silenceFrames = 1, isBufferFull = true, skewPpm = 900, isFastForward = true, isExcluded = true)
        stats.trackAudioFrame(fill = -1, silenceFrames = 1, isBufferFull = true, skewPpm = -900, isFastForward = false, isExcluded = false)

        assertEquals(3L, stats.audioFramesWithSilence)
        assertEquals(2L, stats.audioFramesWithSilenceOutsideFastForward)
        assertEquals(2L, stats.audioFillSamples)
        assertEquals(1000L, stats.audioFillMin)
        assertEquals(2000.0, stats.averageAudioFill()!!, 0.0)
        assertEquals(-20L, stats.audioSkewMinPpm)
        assertEquals(40L, stats.audioSkewMaxPpm)
        assertEquals(10.0, stats.averageAudioSkewPpm()!!, 0.0)
        assertEquals(1L, stats.audioBufferFullFrames)
    }

    @Test
    fun fastForwardSpeedIsOnlyTrackedWhileFastForwardingAndKnown() {
        val stats = SessionStatistics()

        stats.trackAudioFastForwardSpeed(speedMilli = 2500, isFastForward = false)
        stats.trackAudioFastForwardSpeed(speedMilli = -1, isFastForward = true)
        stats.trackAudioFastForwardSpeed(speedMilli = 2000, isFastForward = true)
        stats.trackAudioFastForwardSpeed(speedMilli = 3000, isFastForward = true)

        assertEquals(2L, stats.audioFastForwardSpeedSamples)
        assertEquals(2000L, stats.audioFastForwardSpeedMinMilli)
        assertEquals(2.5, stats.averageAudioFastForwardSpeed()!!, 1e-9)
    }
}
