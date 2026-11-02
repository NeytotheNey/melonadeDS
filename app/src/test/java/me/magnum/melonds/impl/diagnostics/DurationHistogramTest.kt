package me.magnum.melonds.impl.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Test

class DurationHistogramTest {

    private val millisecondNs = 1_000_000L

    private fun histogramOf(vararg values: Long): DurationHistogram {
        return DurationHistogram().apply { values.forEach { add(it) } }
    }

    @Test
    fun percentileIsTheNearestRankValueAtItsBucketMidpoint() {
        val histogram = histogramOf(1 * millisecondNs, 2 * millisecondNs, 3 * millisecondNs, 4 * millisecondNs, 5 * millisecondNs)

        // Rank ceil(0.5 * 5) = 3, the 3 ms value, in the 0.05 ms bucket starting at 3 ms
        assertEquals(3_025_000L, histogram.percentile(0.5))
        // Rank 4 exactly
        assertEquals(4_025_000L, histogram.percentile(0.8))
        // Rank 0 is clamped to the first value
        assertEquals(1_025_000L, histogram.percentile(0.0))
    }

    @Test
    fun meanOfSlowestAveragesTheLargestBucketsAndClampsTheCount() {
        val histogram = histogramOf(1 * millisecondNs, 2 * millisecondNs, 3 * millisecondNs, 4 * millisecondNs, 5 * millisecondNs)

        // The 5 ms bucket's midpoint is above the maximum, so it counts as 5 ms; the 4 ms one as 4.025 ms
        assertEquals(4_512_500.0, histogram.meanOfSlowest(2), 0.0)
        // More samples than values averages them all
        assertEquals(3_020_000.0, histogram.meanOfSlowest(100), 0.0)
    }

    @Test
    fun valuesAbove250MsShareTheOverflowBucketAtTheirAverage() {
        val histogram = histogramOf(10 * millisecondNs, 300 * millisecondNs, 500 * millisecondNs)

        assertEquals(400 * millisecondNs, histogram.percentile(0.5))
        assertEquals(400 * millisecondNs, histogram.percentile(1.0))
        assertEquals(400.0 * millisecondNs, histogram.meanOfSlowest(1), 0.0)
        assertEquals(500 * millisecondNs, histogram.max)
    }

    @Test
    fun bucketMidpointsAreKeptBetweenTheMinimumAndTheMaximum() {
        // A single value: its bucket's midpoint (1.225 ms) is below the minimum, so the value itself is reported
        val single = histogramOf(1_234_567L)
        assertEquals(1_234_567L, single.percentile(0.5))
        assertEquals(1_234_567.0, single.meanOfSlowest(1), 0.0)

        // Two values in one bucket whose midpoint (1.025 ms) is above the maximum
        val pair = histogramOf(1_000_000L, 1_010_000L)
        assertEquals(1_010_000L, pair.percentile(0.5))
        assertEquals(1_010_000L, pair.percentile(1.0))
        assertEquals(1_010_000.0, pair.meanOfSlowest(2), 0.0)
    }
}
