package me.magnum.melonds.impl.perf

import androidx.annotation.VisibleForTesting
import me.magnum.melonds.domain.model.EMULATOR_TARGET_FPS
import me.magnum.melonds.impl.perf.PerfCsv.formatRefreshRate
import java.util.Locale
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.roundToLong
import kotlin.math.sqrt

internal class PssSnapshot(val total: Long, val java: Long, val native: Long, val graphics: Long, val privateOther: Long, val durationNs: Long)

@VisibleForTesting
internal class SessionStatistics {
    companion object {
        const val TARGET_FPS = EMULATOR_TARGET_FPS.toDouble()
        // Rounded to the nearest ns
        private const val TARGET_FRAME_INTERVAL_NS = (1_000_000_000L + EMULATOR_TARGET_FPS / 2) / EMULATOR_TARGET_FPS
        const val MISSED_FRAME_THRESHOLD_NS = TARGET_FRAME_INTERVAL_NS * 3 / 2
        // Holds from 1 to 31 refreshes get their own bucket. The last bucket counts 32 and above
        private const val HOLD_HISTOGRAM_SIZE = 33
        // If refresh_hz / 60 is within this of a whole number, every frame is expected to be held exactly that many refreshes
        const val HOLD_WHOLE_NUMBER_TOLERANCE = 0.05
        // Refresh rates within this fraction of each other are taken as the same rate, so that small changes in the vsync period aren't a change
        const val REFRESH_RATE_MATCH_TOLERANCE = 0.03
        private const val JUDDER_WINDOW_SECONDS = 10
        // A window is only evaluated once it holds at least 80% of the holds a full window should have at 60 FPS (480)
        const val JUDDER_WINDOW_MIN_FRAMES = JUDDER_WINDOW_SECONDS * EMULATOR_TARGET_FPS * 4 / 5
    }

    var totalFrames = 0L
        private set
    var fastForwardFrames = 0L
        private set
    var afterPauseFrames = 0L
        private set
    var missedFrames = 0L
        private set
    // Analyzed frames (same filter as frameIntervals) whose start was locked to the display's vsync grid
    var pacingLockedFrames = 0L
        private set
    // Refresh divisor of the last locked frame (of any kind), or 0 if no frame was locked
    var lastPacingDivisor = 0
        private set
    // Only frames that represent normal emulation pacing (not fast-forwarded and not right after a pause)
    val frameIntervals = DurationHistogram()
    val frameWorkDurations = DurationHistogram()

    var displaySamples = 0L
        private set
    var displayPausedSamples = 0L
        private set
    var displaySleepSamples = 0L
        private set
    var displayNewFrames = 0L
        private set
    var displayRepeatedFrames = 0L
        private set
    var displayInvalidFrames = 0L
        private set
    var displayLongestRepeatRun = 0L
        private set
    var displaySkippedFrames = 0L
        private set
    var displayMaxRenderDuration = 0L
        private set
    private var displayRenderDurationSum = 0L
    private var currentRepeatRun = 0L

    // Frame hold cadence. A hold is the time from one new frame to the next, in refresh periods, measured when the next one arrives. Time is used
    // instead of counting samples because render callbacks can be merged or dropped, so there isn't always one sample per refresh
    var displayFastForwardSamples = 0L
        private set
    var displayHoldFrames = 0L
        private set
    var displayOffCadenceFrames = 0L
        private set
    // Repeat runs longer than the refresh rate they happened at explains, counted once per run
    var displayRepeatRunsOverExpected = 0L
        private set
    private val displayHoldHistogram = LongArray(HOLD_HISTOGRAM_SIZE)
    // Timestamp of the current frame's first sample. Only valid when its start was seen (not after a pause, fast-forward, an invalid frame or a
    // refresh rate change)
    private var currentFrameStartNs = 0L
    private var hasCurrentFrameStart = false

    // Refresh rates the display samples were rendered at, in the order they were first seen. The rate can change mid-session (the 60Hz lock
    // landing late, fast-forward dropping it, a fold), so every sample is measured against the rate in effect for it
    private val refreshRates = ArrayList<RefreshRateStatistics>()
    // Rate of the latest sample that carried one, or the one given to setInitialRefreshRate before that. null while unknown, in which case
    // holds aren't measured and no repeats are expected
    var currentRefreshRate: RefreshRateStatistics? = null
        private set
    // Whether the latest sample's rate differs from the previous sample's (or is the first one known)
    var refreshRateChangedOnLastSample = false
        private set
    private var previousSampleRefreshRate: RefreshRateStatistics? = null
    // The rate-weighted stats below only use the samples counted in the repeat stats that aren't fast-forwarded. Fast-forward drops the 60Hz
    // lock and shows a new frame on almost every refresh, so its samples would expect repeats that never happen and offset real ones
    var cadenceCountedFrames = 0L
        private set
    var cadenceRepeatedFrames = 0L
        private set
    // Rates of the first and last of those samples, and how many times the rate changed between them
    var firstCountedRefreshRate: RefreshRateStatistics? = null
        private set
    var lastCountedRefreshRate: RefreshRateStatistics? = null
        private set
    var refreshRateChanges = 0L
        private set
    // Repeats expected at each of those samples' rate, summed over the ones whose rate was known
    private var expectedRepeatSum = 0.0
    private var ratedCountedSamples = 0L
    // Whether the current repeat run has already been counted in displayRepeatRunsOverExpected
    private var isRepeatRunOverExpected = false

    // Sliding judder window in 1 second steps. Each slot holds the counts of one second
    private val judderWindowFrames = LongArray(JUDDER_WINDOW_SECONDS)
    private val judderWindowOffCadence = LongArray(JUDDER_WINDOW_SECONDS)
    private var judderWindowSecond = 0L
    private var hasJudderWindow = false
    private var worstJudderPct = Double.NaN

    var memorySamples = 0L
        private set
    var peakRss = 0L
        private set
    var peakNativeHeap = 0L
        private set
    var lastThermalStatus = -1
        private set
    var maxThermalStatus = -1
        private set
    private var rssSum = 0L

    var pssSamples = 0L
        private set
    // -1 until a sample reports graphics memory
    var minPssGraphics = -1L
        private set
    var peakPssGraphics = -1L
        private set
    var maxPssSampleDuration = 0L
        private set

    fun trackFrame(workDuration: Long, interval: Long, isFastForward: Boolean, isAfterPause: Boolean, isPacingLocked: Boolean, pacingDivisor: Int) {
        totalFrames++
        if (isPacingLocked) {
            lastPacingDivisor = pacingDivisor
            if (!isFastForward && !isAfterPause) {
                pacingLockedFrames++
            }
        }
        if (isFastForward) {
            fastForwardFrames++
        }
        if (isAfterPause) {
            afterPauseFrames++
        }
        if (!isFastForward && !isAfterPause) {
            frameIntervals.add(interval)
            frameWorkDurations.add(workDuration)
            if (interval > MISSED_FRAME_THRESHOLD_NS) {
                missedFrames++
            }
        }
    }

    // Every frame counts towards audioFramesWithSilence. The buffer stats leave out fast-forward and after-pause frames
    var audioFramesWithSilence = 0L
        private set
    var audioFramesWithSilenceOutsideFastForward = 0L
        private set
    var audioFillSamples = 0L
        private set
    var audioFillMin = Long.MAX_VALUE
        private set
    private var audioFillSum = 0L
    var audioBufferFullFrames = 0L
        private set
    var audioSkewMinPpm = Long.MAX_VALUE
        private set
    var audioSkewMaxPpm = Long.MIN_VALUE
        private set
    private var audioSkewSumPpm = 0L
    var audioFastForwardSpeedSamples = 0L
        private set
    var audioFastForwardSpeedMinMilli = Long.MAX_VALUE
        private set
    private var audioFastForwardSpeedSumMilli = 0L

    fun trackAudioFrame(fill: Long, silenceFrames: Long, isBufferFull: Boolean, skewPpm: Long, isFastForward: Boolean, isExcluded: Boolean) {
        if (silenceFrames > 0) {
            audioFramesWithSilence++
            if (!isFastForward) {
                audioFramesWithSilenceOutsideFastForward++
            }
        }
        if (isExcluded || fill < 0) {
            return
        }
        audioFillSamples++
        audioFillSum += fill
        if (fill < audioFillMin) {
            audioFillMin = fill
        }
        audioSkewSumPpm += skewPpm
        audioSkewMinPpm = minOf(audioSkewMinPpm, skewPpm)
        audioSkewMaxPpm = maxOf(audioSkewMaxPpm, skewPpm)
        if (isBufferFull) {
            audioBufferFullFrames++
        }
    }

    // [speedMilli] is -1 while the speed isn't known
    fun trackAudioFastForwardSpeed(speedMilli: Long, isFastForward: Boolean) {
        if (!isFastForward || speedMilli < 0) {
            return
        }
        audioFastForwardSpeedSamples++
        audioFastForwardSpeedSumMilli += speedMilli
        audioFastForwardSpeedMinMilli = minOf(audioFastForwardSpeedMinMilli, speedMilli)
    }

    fun averageAudioFastForwardSpeed(): Double? {
        return if (audioFastForwardSpeedSamples > 0) audioFastForwardSpeedSumMilli / 1000.0 / audioFastForwardSpeedSamples else null
    }

    fun averageAudioFill(): Double? {
        return if (audioFillSamples > 0) audioFillSum.toDouble() / audioFillSamples else null
    }

    // Sampled on the same frames as the fill
    fun averageAudioSkewPpm(): Double? {
        return if (audioFillSamples > 0) audioSkewSumPpm.toDouble() / audioFillSamples else null
    }

    /**
     * Sets the rate used until the display samples carry their own vsync period.
     */
    fun setInitialRefreshRate(refreshRate: Float?) {
        val rate = refreshRate?.toDouble()?.takeIf { it > 0 } ?: return
        currentRefreshRate = findRefreshRate(rate)
    }

    private fun findRefreshRate(refreshRate: Double): RefreshRateStatistics {
        return refreshRates.firstOrNull { it.matches(refreshRate) } ?: RefreshRateStatistics(refreshRate).also { refreshRates.add(it) }
    }

    /**
     * Moves to the refresh rate given by [refreshPeriodNs] (0 if unknown, which keeps the current one) and returns the rate in effect.
     */
    private fun updateRefreshRate(refreshPeriodNs: Long): RefreshRateStatistics? {
        val current = currentRefreshRate
        val rate = when {
            refreshPeriodNs <= 0 -> current
            current != null && current.matches(1e9 / refreshPeriodNs) -> current
            else -> findRefreshRate(1e9 / refreshPeriodNs)
        }
        if (rate !== current) {
            // A hold that spans a rate change can't be measured in either period, so it's skipped
            hasCurrentFrameStart = false
            currentRefreshRate = rate
        }
        refreshRateChangedOnLastSample = rate != null && rate !== previousSampleRefreshRate
        previousSampleRefreshRate = rate
        return rate
    }

    private fun trackCountedRefreshRate(rate: RefreshRateStatistics?, isRepeat: Boolean, isFastForward: Boolean) {
        if (isFastForward) {
            return
        }
        cadenceCountedFrames++
        if (isRepeat) {
            cadenceRepeatedFrames++
        }
        rate ?: return
        rate.countedSamples++
        ratedCountedSamples++
        expectedRepeatSum += rate.expectedRepeatRatio
        if (lastCountedRefreshRate != null && lastCountedRefreshRate !== rate) {
            refreshRateChanges++
        }
        lastCountedRefreshRate = rate
        if (firstCountedRefreshRate == null) {
            firstCountedRefreshRate = rate
        }
    }

    /**
     * Returns how many refreshes the previous frame was held if this sample is a new frame and that hold could be measured, or 0 otherwise.
     */
    fun trackDisplayFrame(
        timestamp: Long,
        renderDuration: Long,
        isValid: Boolean,
        isNewFrame: Boolean,
        skippedFrames: Long,
        isPaused: Boolean,
        isDsSleeping: Boolean,
        isFastForward: Boolean,
        refreshPeriodNs: Long,
    ): Long {
        val refreshRate = updateRefreshRate(refreshPeriodNs)
        displaySamples++
        displayRenderDurationSum += renderDuration
        if (renderDuration > displayMaxRenderDuration) {
            displayMaxRenderDuration = renderDuration
        }

        // No new frames are produced while the emulator is paused or while the DS is asleep, so those don't count towards frame pacing
        if (isPaused || isDsSleeping) {
            if (isPaused) {
                displayPausedSamples++
            } else {
                displaySleepSamples++
            }
            currentRepeatRun = 0
            isRepeatRunOverExpected = false
            hasCurrentFrameStart = false
            return 0
        }

        if (isFastForward) {
            displayFastForwardSamples++
        }

        var previousHold = 0L
        when {
            !isValid -> {
                displayInvalidFrames++
                hasCurrentFrameStart = false
            }
            isNewFrame -> {
                displayNewFrames++
                trackCountedRefreshRate(refreshRate, isRepeat = false, isFastForward)
                displaySkippedFrames += skippedFrames
                currentRepeatRun = 0
                isRepeatRunOverExpected = false
                if (hasCurrentFrameStart && !isFastForward && refreshRate != null) {
                    previousHold = maxOf(1L, Math.round((timestamp - currentFrameStartNs) / refreshRate.periodNs))
                    trackHold(previousHold, timestamp, refreshRate)
                }
                currentFrameStartNs = timestamp
                hasCurrentFrameStart = true
            }
            else -> {
                displayRepeatedFrames++
                trackCountedRefreshRate(refreshRate, isRepeat = true, isFastForward)
                currentRepeatRun++
                if (currentRepeatRun > displayLongestRepeatRun) {
                    displayLongestRepeatRun = currentRepeatRun
                }
                // Checked against the rate in effect now, since a run can span a rate change. Counted once per run
                if (refreshRate != null && !isRepeatRunOverExpected && currentRepeatRun > refreshRate.expectedLongestRepeatRun) {
                    isRepeatRunOverExpected = true
                    displayRepeatRunsOverExpected++
                }
            }
        }

        // Frames shown during fast-forward don't follow the normal cadence, so holds that touch it aren't counted
        if (isFastForward) {
            hasCurrentFrameStart = false
        }
        return previousHold
    }

    private fun trackHold(hold: Long, timestamp: Long, refreshRate: RefreshRateStatistics) {
        displayHoldFrames++
        displayHoldHistogram[minOf(hold, HOLD_HISTOGRAM_SIZE - 1L).toInt()]++
        refreshRate.holdFrames++

        val isOffCadence = hold < refreshRate.onCadenceMinHold || hold > refreshRate.onCadenceMaxHold
        if (isOffCadence) {
            displayOffCadenceFrames++
        }

        advanceJudderWindow(Math.floorDiv(timestamp, 1_000_000_000L))
        val slot = Math.floorMod(judderWindowSecond, JUDDER_WINDOW_SECONDS.toLong()).toInt()
        judderWindowFrames[slot]++
        if (isOffCadence) {
            judderWindowOffCadence[slot]++
        }
    }

    private fun advanceJudderWindow(second: Long) {
        if (!hasJudderWindow) {
            hasJudderWindow = true
            judderWindowSecond = second
            return
        }

        // Every window that ends at a completed second is checked before its oldest second is dropped. After a full window of steps every slot
        // is empty, so longer gaps don't need more steps
        var steps = 0
        while (judderWindowSecond < second && steps < JUDDER_WINDOW_SECONDS) {
            checkJudderWindow()
            judderWindowSecond++
            val slot = Math.floorMod(judderWindowSecond, JUDDER_WINDOW_SECONDS.toLong()).toInt()
            judderWindowFrames[slot] = 0
            judderWindowOffCadence[slot] = 0
            steps++
        }
        judderWindowSecond = maxOf(judderWindowSecond, second)
    }

    private fun checkJudderWindow() {
        val pct = currentJudderWindowPct() ?: return
        if (worstJudderPct.isNaN() || pct > worstJudderPct) {
            worstJudderPct = pct
        }
    }

    private fun currentJudderWindowPct(): Double? {
        val frames = judderWindowFrames.sum()
        if (frames < JUDDER_WINDOW_MIN_FRAMES) {
            return null
        }
        return judderWindowOffCadence.sum() * 100.0 / frames
    }

    /**
     * Highest off-cadence percentage in any window of [JUDDER_WINDOW_SECONDS] seconds (sliding in 1 second steps) with at least
     * [JUDDER_WINDOW_MIN_FRAMES] measured holds, including the window that is still in progress.
     */
    fun worstJudderWindowPct(): Double? {
        val current = currentJudderWindowPct()
        val worst = worstJudderPct.takeUnless { it.isNaN() }
        return when {
            current == null -> worst
            worst == null -> current
            else -> maxOf(worst, current)
        }
    }

    /**
     * Compact hold histogram, for example "1:2918 2:17640 3:2873 4:37 5+:6". Lists every hold up to 2 above the longest on-cadence one (at
     * least up to 4) and groups the rest.
     */
    fun formatHoldHistogram(): String {
        val longestOnCadenceHold = refreshRates.filter { it.holdFrames > 0 }.maxOfOrNull { it.onCadenceMaxHold } ?: currentRefreshRate?.onCadenceMaxHold ?: 0L
        val lastListed = maxOf(4L, longestOnCadenceHold + 2).coerceAtMost(HOLD_HISTOGRAM_SIZE - 2L).toInt()
        val builder = StringBuilder()
        for (hold in 1..lastListed) {
            builder.append(hold).append(':').append(displayHoldHistogram[hold]).append(' ')
        }
        var rest = 0L
        for (hold in lastListed + 1 until HOLD_HISTOGRAM_SIZE) {
            rest += displayHoldHistogram[hold]
        }
        builder.append(lastListed + 1).append("+:").append(rest)
        return builder.toString()
    }

    /**
     * Time-weighted average of the refresh rates of the counted samples outside fast-forward (each sample lasts one refresh period), or null if
     * none had a known rate.
     */
    fun averageRefreshRate(): Double? {
        val countedTimeNs = refreshRates.sumOf { it.countedSamples * it.periodNs }
        return if (countedTimeNs > 0) ratedCountedSamples * 1e9 / countedTimeNs else null
    }

    /**
     * Share of the counted samples outside fast-forward expected to be repeats, from each sample's refresh rate. Before any sample is counted,
     * the current rate's.
     */
    fun expectedRepeatRatio(): Double? {
        return if (ratedCountedSamples > 0) expectedRepeatSum / ratedCountedSamples else currentRefreshRate?.expectedRepeatRatio
    }

    fun unexpectedRepeats(): Long? {
        // Scaled to every counted sample outside fast-forward, in case some came before any rate was known
        val expectedRepeats = expectedRepeatRatio()?.let { it * cadenceCountedFrames } ?: return null
        return maxOf(0L, cadenceRepeatedFrames - expectedRepeats.roundToLong())
    }

    fun expectedLongestRepeatRun(): Long? {
        return refreshRates.filter { it.countedSamples > 0 }.maxOfOrNull { it.expectedLongestRepeatRun } ?: currentRefreshRate?.expectedLongestRepeatRun
    }

    /**
     * Expected hold averaged over the measured holds, or the current rate's if none was measured.
     */
    fun expectedHoldRefreshes(): Double? {
        if (displayHoldFrames == 0L) {
            return currentRefreshRate?.expectedHold
        }
        return refreshRates.sumOf { it.holdFrames * it.expectedHold } / displayHoldFrames
    }

    /**
     * The on-cadence holds, for example "2". If holds were measured at more than one refresh rate, those of each rate, most holds first, for
     * example "1 at 60.00 Hz, 2 at 120.00 Hz".
     */
    fun formatOnCadenceHolds(): String? {
        val ratesWithHolds = refreshRates.filter { it.holdFrames > 0 }.sortedByDescending { it.holdFrames }
        if (ratesWithHolds.size > 1) {
            return ratesWithHolds.joinToString(", ") { rate ->
                val holds = if (rate.onCadenceMinHold == rate.onCadenceMaxHold) "${rate.onCadenceMinHold}" else "${rate.onCadenceMinHold} or ${rate.onCadenceMaxHold}"
                "$holds at ${formatRefreshRate(rate.refreshRate)} Hz"
            }
        }

        val rate = ratesWithHolds.firstOrNull() ?: currentRefreshRate ?: return null
        return if (rate.onCadenceMinHold == rate.onCadenceMaxHold) {
            "${rate.onCadenceMinHold}"
        } else {
            // For example 90Hz: frames alternate between 1 and 2 refreshes, so both count as on-cadence
            "${rate.onCadenceMinHold} or ${rate.onCadenceMaxHold} (expected hold is not a whole number, so the cadence alternates)"
        }
    }

    /**
     * Counted samples outside fast-forward at each refresh rate, in the order the rates were first seen, for example "120.00:12 60.00:2400".
     */
    fun formatRefreshRateSamples(): String? {
        return refreshRates.filter { it.countedSamples > 0 }.takeIf { it.isNotEmpty() }?.joinToString(" ") {
            "${formatRefreshRate(it.refreshRate)}:${it.countedSamples}"
        }
    }

    /**
     * Share of the time (in %) of the counted samples outside fast-forward at each refresh rate, for example "120.00:0.2 60.00:99.8".
     */
    fun formatRefreshRateTimeShares(): String? {
        val counted = refreshRates.filter { it.countedSamples > 0 }
        val countedTimeNs = counted.sumOf { it.countedSamples * it.periodNs }
        if (countedTimeNs <= 0) {
            return null
        }
        return counted.joinToString(" ") {
            "${formatRefreshRate(it.refreshRate)}:${String.format(Locale.US, "%.1f", it.countedSamples * it.periodNs * 100.0 / countedTimeNs)}"
        }
    }

    fun trackMemory(rss: Long, nativeHeapAllocated: Long, thermalStatus: Int) {
        memorySamples++
        rssSum += rss
        if (rss > peakRss) {
            peakRss = rss
        }
        if (nativeHeapAllocated > peakNativeHeap) {
            peakNativeHeap = nativeHeapAllocated
        }
        lastThermalStatus = thermalStatus
        if (thermalStatus > maxThermalStatus) {
            maxThermalStatus = thermalStatus
        }
    }

    fun trackPss(snapshot: PssSnapshot) {
        pssSamples++
        if (snapshot.graphics >= 0) {
            if (minPssGraphics < 0 || snapshot.graphics < minPssGraphics) {
                minPssGraphics = snapshot.graphics
            }
            if (snapshot.graphics > peakPssGraphics) {
                peakPssGraphics = snapshot.graphics
            }
        }
        if (snapshot.durationNs > maxPssSampleDuration) {
            maxPssSampleDuration = snapshot.durationNs
        }
    }

    fun averageRenderDurationNs(): Double? {
        return if (displaySamples > 0) displayRenderDurationSum.toDouble() / displaySamples else null
    }

    fun averageRss(): Double? {
        return if (memorySamples > 0) rssSum.toDouble() / memorySamples else null
    }
}

/**
 * What the display cadence is expected to be at one refresh rate, and the counted samples and measured holds at it.
 */
internal class RefreshRateStatistics(val refreshRate: Double) {
    val periodNs = 1e9 / refreshRate
    // Refreshes each frame is expected to stay on screen at 60 FPS
    val expectedHold = refreshRate / SessionStatistics.TARGET_FPS
    // A display faster than 60Hz has to show some frames twice even with perfect pacing
    val expectedRepeatRatio = maxOf(0.0, 1.0 - SessionStatistics.TARGET_FPS / refreshRate)
    val expectedLongestRepeatRun = maxOf(0L, ceil(expectedHold - 1e-3).toLong() - 1)
    // If the expected hold isn't a whole number (for example 90Hz), frames alternate between the two nearest holds, so both are on-cadence
    val onCadenceMinHold: Long
    val onCadenceMaxHold: Long
    var countedSamples = 0L
    var holdFrames = 0L

    init {
        val nearest = expectedHold.roundToLong()
        if (abs(expectedHold - nearest) <= SessionStatistics.HOLD_WHOLE_NUMBER_TOLERANCE) {
            onCadenceMinHold = maxOf(1L, nearest)
            onCadenceMaxHold = onCadenceMinHold
        } else {
            onCadenceMinHold = maxOf(1L, floor(expectedHold).toLong())
            onCadenceMaxHold = maxOf(1L, ceil(expectedHold).toLong())
        }
    }

    fun matches(rate: Double): Boolean {
        return abs(rate - refreshRate) <= refreshRate * SessionStatistics.REFRESH_RATE_MATCH_TOLERANCE
    }
}

/**
 * Fixed-size histogram of durations, so that memory use doesn't grow with the session length. Values from 0 to 250 ms go into 0.05 ms buckets
 * and anything above goes into an overflow bucket. Count, sum, sum of squares, min and max are tracked exactly.
 */
internal class DurationHistogram {
    companion object {
        const val BUCKET_WIDTH_NS = 50_000L
        const val BUCKET_COUNT = 5000
    }

    // The last bucket is the overflow bucket
    private val buckets = LongArray(BUCKET_COUNT + 1)
    private var overflowSum = 0L
    var count = 0L
        private set
    var sum = 0L
        private set
    private var sumOfSquares = 0.0
    var min = Long.MAX_VALUE
        private set
    var max = 0L
        private set

    fun add(value: Long) {
        val duration = maxOf(0L, value)
        val bucket = minOf(duration / BUCKET_WIDTH_NS, BUCKET_COUNT.toLong()).toInt()
        buckets[bucket]++
        if (bucket == BUCKET_COUNT) {
            overflowSum += duration
        }

        count++
        sum += duration
        sumOfSquares += duration.toDouble() * duration
        if (duration < min) min = duration
        if (duration > max) max = duration
    }

    fun mean(): Double {
        return if (count > 0) sum.toDouble() / count else Double.NaN
    }

    fun standardDeviation(): Double {
        if (count == 0L) return Double.NaN
        val mean = mean()
        return sqrt(maxOf(0.0, sumOfSquares / count - mean * mean))
    }

    /**
     * Nearest-rank percentile. Values inside a bucket are approximated by the bucket's midpoint (and by the average for the overflow bucket).
     */
    fun percentile(percentile: Double): Long {
        val rank = ceil(percentile * count).toLong().coerceIn(1L, count)
        var cumulative = 0L
        for (i in buckets.indices) {
            cumulative += buckets[i]
            if (cumulative >= rank) {
                return bucketValue(i).coerceIn(min, max)
            }
        }
        return max
    }

    /**
     * Average of the [sampleCount] largest values, approximated in the same way as [percentile].
     */
    fun meanOfSlowest(sampleCount: Long): Double {
        val target = sampleCount.coerceIn(1L, count)
        var remaining = target
        var total = 0.0
        for (i in buckets.indices.reversed()) {
            if (buckets[i] == 0L) continue
            val taken = minOf(remaining, buckets[i])
            total += taken * bucketValue(i).coerceIn(min, max).toDouble()
            remaining -= taken
            if (remaining == 0L) break
        }
        return total / target
    }

    private fun bucketValue(bucket: Int): Long {
        return if (bucket == BUCKET_COUNT) {
            overflowSum / buckets[BUCKET_COUNT]
        } else {
            bucket * BUCKET_WIDTH_NS + BUCKET_WIDTH_NS / 2
        }
    }
}
