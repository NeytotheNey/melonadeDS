package me.magnum.melonds.impl.perf

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DisplayCadenceStatisticsTest {

    private val period120HzNs = 8_333_333L
    private val period60HzNs = 16_666_667L

    private fun SessionStatistics.trackSample(
        timestampNs: Long,
        isNewFrame: Boolean,
        refreshPeriodNs: Long,
        isPaused: Boolean = false,
        isFastForward: Boolean = false,
    ): Long {
        return trackDisplayFrame(timestampNs, 1_000_000L, true, isNewFrame, 0L, isPaused, false, isFastForward, refreshPeriodNs)
    }

    @Test
    fun refreshRateDroppingTo60HzMidSessionIsNotCountedAsRepeatsOrJudder() {
        val stats = SessionStatistics()
        // The display reported 120Hz when the session started, like when the 60Hz lock lands just after
        stats.setInitialRefreshRate(120f)

        var timestamp = 0L
        // 120Hz with every frame held 2 refreshes
        repeat(12) {
            stats.trackSample(timestamp, isNewFrame = it % 2 == 0, refreshPeriodNs = period120HzNs)
            timestamp += period120HzNs
        }
        timestamp += period60HzNs - period120HzNs
        // Then 60Hz with a new frame on every refresh
        repeat(120) {
            stats.trackSample(timestamp, isNewFrame = true, refreshPeriodNs = period60HzNs)
            timestamp += period60HzNs
        }

        assertEquals(0L, stats.unexpectedRepeats())
        assertEquals(0L, stats.displayOffCadenceFrames)
        assertEquals(0L, stats.displayRepeatRunsOverExpected)
        // The hold that spans the change is skipped. Every hold at 60Hz is 1 refresh
        assertEquals(5L + 119L, stats.displayHoldFrames)
        assertEquals("1:119 2:5 3:0 4:0 5+:0", stats.formatHoldHistogram())
        assertEquals("1 at 60.00 Hz, 2 at 120.00 Hz", stats.formatOnCadenceHolds())
        assertEquals(1L, stats.refreshRateChanges)
        assertEquals(120.0, stats.firstCountedRefreshRate!!.refreshRate, 0.01)
        assertEquals(60.0, stats.lastCountedRefreshRate!!.refreshRate, 0.01)
        assertEquals("120.00:12 60.00:120", stats.formatRefreshRateSamples())
        // 132 samples over 0.1 s at 120Hz and 2 s at 60Hz
        assertEquals(132 / 2.1, stats.averageRefreshRate()!!, 0.01)
    }

    @Test
    fun refreshRateChangeWhilePausedIsNotCountedButIsReported() {
        val stats = SessionStatistics()
        stats.setInitialRefreshRate(60f)

        var timestamp = 0L
        repeat(60) {
            stats.trackSample(timestamp, isNewFrame = true, refreshPeriodNs = period60HzNs)
            timestamp += period60HzNs
        }
        // The display goes back to 120Hz while the emulator is paused
        stats.trackSample(timestamp, isNewFrame = false, refreshPeriodNs = period120HzNs, isPaused = true)
        assertTrue(stats.refreshRateChangedOnLastSample)
        timestamp += period120HzNs
        repeat(10) {
            stats.trackSample(timestamp, isNewFrame = false, refreshPeriodNs = period120HzNs, isPaused = true)
            assertFalse(stats.refreshRateChangedOnLastSample)
            timestamp += period120HzNs
        }
        repeat(60) {
            stats.trackSample(timestamp, isNewFrame = true, refreshPeriodNs = period60HzNs)
            timestamp += period60HzNs
        }

        assertEquals(0L, stats.refreshRateChanges)
        assertEquals(0L, stats.unexpectedRepeats())
        assertEquals(0L, stats.displayOffCadenceFrames)
        assertEquals(59L + 59L, stats.displayHoldFrames)
        assertEquals("1", stats.formatOnCadenceHolds())
        assertEquals(60.0, stats.averageRefreshRate()!!, 0.01)
    }

    @Test
    fun displayReportedAt60HzWhileRunningAt120HzExpectsEveryFrameShownTwice() {
        val stats = SessionStatistics()
        stats.setInitialRefreshRate(60f)

        var timestamp = 0L
        repeat(240) {
            stats.trackSample(timestamp, isNewFrame = it % 2 == 0, refreshPeriodNs = period120HzNs)
            timestamp += period120HzNs
        }

        assertEquals(0L, stats.unexpectedRepeats())
        assertEquals(0L, stats.displayOffCadenceFrames)
        assertEquals(0L, stats.displayRepeatRunsOverExpected)
        assertEquals("1:0 2:119 3:0 4:0 5+:0", stats.formatHoldHistogram())
        assertEquals("2", stats.formatOnCadenceHolds())
        assertEquals(120.0, stats.averageRefreshRate()!!, 0.01)
    }

    @Test
    fun missingVsyncPeriodFallsBackToTheStartRate() {
        val stats = SessionStatistics()
        stats.setInitialRefreshRate(120f)

        var timestamp = 0L
        repeat(120) {
            stats.trackSample(timestamp, isNewFrame = it % 2 == 0, refreshPeriodNs = 0L)
            timestamp += period120HzNs
        }

        assertEquals(0L, stats.unexpectedRepeats())
        assertEquals(0L, stats.displayOffCadenceFrames)
        assertEquals("1:0 2:59 3:0 4:0 5+:0", stats.formatHoldHistogram())
        assertEquals(120.0, stats.averageRefreshRate()!!, 0.01)
        assertEquals(0L, stats.refreshRateChanges)
    }

    @Test
    fun unknownRefreshRateDoesNotMeasureHoldsOrRepeats() {
        val stats = SessionStatistics()
        stats.setInitialRefreshRate(null)

        var timestamp = 0L
        repeat(60) {
            stats.trackSample(timestamp, isNewFrame = it % 2 == 0, refreshPeriodNs = 0L)
            timestamp += period60HzNs
        }

        assertNull(stats.unexpectedRepeats())
        assertNull(stats.averageRefreshRate())
        assertNull(stats.formatOnCadenceHolds())
        assertNull(stats.formatRefreshRateSamples())
        assertEquals(0L, stats.displayHoldFrames)
        assertFalse(stats.refreshRateChangedOnLastSample)
    }

    @Test
    fun fastForwardAt120HzDoesNotOffsetRepeatsAt60Hz() {
        val stats = SessionStatistics()
        stats.setInitialRefreshRate(60f)

        var timestamp = 0L
        // Normal play at 60Hz with a repeat every 10 refreshes
        repeat(600) {
            stats.trackSample(timestamp, isNewFrame = it % 10 != 9, refreshPeriodNs = period60HzNs)
            timestamp += period60HzNs
        }
        // Fast-forward drops the 60Hz lock and shows a new frame on every refresh
        repeat(600) {
            stats.trackSample(timestamp, isNewFrame = true, refreshPeriodNs = period120HzNs, isFastForward = true)
            timestamp += period120HzNs
        }
        repeat(600) {
            stats.trackSample(timestamp, isNewFrame = it % 10 != 9, refreshPeriodNs = period60HzNs)
            timestamp += period60HzNs
        }

        assertEquals(120L, stats.unexpectedRepeats())
        assertEquals(0.0, stats.expectedRepeatRatio()!!, 1e-9)
        assertEquals(1200L, stats.cadenceCountedFrames)
        assertEquals(0L, stats.refreshRateChanges)
        assertEquals("60.00:1200", stats.formatRefreshRateSamples())
        assertEquals(60.0, stats.averageRefreshRate()!!, 0.01)
    }

    @Test
    fun repeatRunSpanningARefreshRateChangeIsCountedOnce() {
        val stats = SessionStatistics()
        stats.setInitialRefreshRate(120f)

        var timestamp = 0L
        stats.trackSample(timestamp, isNewFrame = true, refreshPeriodNs = period120HzNs)
        timestamp += period120HzNs
        // One repeat is expected at 120Hz
        stats.trackSample(timestamp, isNewFrame = false, refreshPeriodNs = period120HzNs)
        assertEquals(0L, stats.displayRepeatRunsOverExpected)
        timestamp += period60HzNs
        // The run goes on at 60Hz, where no repeat is expected
        stats.trackSample(timestamp, isNewFrame = false, refreshPeriodNs = period60HzNs)
        timestamp += period60HzNs
        stats.trackSample(timestamp, isNewFrame = false, refreshPeriodNs = period60HzNs)
        timestamp += period60HzNs
        assertEquals(1L, stats.displayRepeatRunsOverExpected)

        stats.trackSample(timestamp, isNewFrame = true, refreshPeriodNs = period60HzNs)
        timestamp += period60HzNs
        stats.trackSample(timestamp, isNewFrame = false, refreshPeriodNs = period60HzNs)
        assertEquals(2L, stats.displayRepeatRunsOverExpected)
    }
}
