package me.magnum.melonds.impl.diagnostics

import android.content.Context
import me.magnum.melonds.BuildConfig
import me.magnum.melonds.MelonEmulator
import me.magnum.melonds.impl.diagnostics.PerfCsv.appendMillis
import java.text.SimpleDateFormat
import java.util.Locale
import kotlin.math.roundToLong

// The SPU output ring between the emulator and the audio callback (AudioDiagnostics.h)
private const val AUDIO_BUFFER_CAPACITY_FRAMES = 2048
private const val AUDIO_BUFFER_SAMPLE_RATE = 48000

/**
 * One "key: value" line of the summary, whose [description] is printed in the summary's key list.
 */
internal class SummaryEntry(val key: String, val value: Any?, val description: String?)

internal class SummarySection(val title: String) {
    val entries = ArrayList<SummaryEntry>()

    fun entry(key: String, value: Any?, description: String? = null) {
        entries.add(SummaryEntry(key, value, description))
    }
}

/**
 * The summary's lines: a "complete" flag, the described keys, then the sections.
 */
internal class SummaryTable(private val complete: SummaryEntry) {
    val sections = ArrayList<SummarySection>()

    fun section(title: String, fill: SummarySection.() -> Unit) {
        sections.add(SummarySection(title).apply(fill))
    }

    fun render(): String {
        val summary = StringBuilder()
        fun line(entry: SummaryEntry) {
            summary.append(entry.key).append(": ").append(entry.value ?: "n/a").append('\n')
        }

        line(complete)
        summary.append("\n# Keys\n")
        (listOf(complete) + sections.flatMap { it.entries }).forEach { entry ->
            if (entry.description != null) {
                summary.append("# ").append(entry.key).append(": ").append(entry.description).append('\n')
            }
        }
        sections.forEach { section ->
            summary.append("\n# ").append(section.title).append('\n')
            section.entries.forEach { line(it) }
        }
        return summary.toString()
    }
}

/**
 * Renders the summary text file of a performance log session from its statistics and the emulator's counters.
 */
internal fun PerformanceLogger.Session.buildSummary(context: Context, isComplete: Boolean): String {
    val durationNs = System.nanoTime() - startNs

    fun millis(ns: Long?): String {
        return if (ns == null) "n/a" else StringBuilder().appendMillis(ns).toString()
    }

    fun decimal(value: Double?, decimals: Int = 2): String {
        return if (value == null || value.isNaN()) "n/a" else String.format(Locale.US, "%.${decimals}f", value)
    }

    val table = SummaryTable(
        SummaryEntry("complete", isComplete, "false while the session is still running (or if the app was killed before the session stopped)"),
    )

    table.section("App") {
        DiagnosticsInfo.app(context).forEach { (key, value) -> entry(key, value) }
    }

    table.section("Device") {
        DiagnosticsInfo.device().forEach { (key, value) -> entry(key, value) }
        entry(
            "display_refresh_rate_hz", decimal(stats.averageRefreshRate() ?: displayRefreshRate?.toDouble()),
            "Time-weighted average of the vsync period the render loop ran at (from Choreographer), over the display samples counted in the " +
                "repeat stats outside fast-forward (which drops the 60Hz lock). The rate the display reported at session start stands in until " +
                "the samples carry one. The session's rate unless the rate changed mid-session (see display_refresh_rate_time_pct)",
        )
        entry(
            "display_refresh_rate_start_hz", decimal(stats.firstCountedRefreshRate?.refreshRate ?: displayRefreshRate?.toDouble()),
            "Rate of the first counted display sample, or the rate the display reported at session start",
        )
        entry(
            "display_refresh_rate_last_hz", decimal(stats.lastCountedRefreshRate?.refreshRate ?: lastDisplayState?.refreshRate?.toDouble()),
            "Rate of the last counted sample, so not the rate the display goes back to once the emulator is paused or fast-forwarded",
        )
        entry("display_refresh_rate_changes", stats.refreshRateChanges, "How many times the rate changed between counted samples")
        entry("display_refresh_rate_samples", stats.formatRefreshRateSamples(), "Counted samples at each refresh rate")
        entry("display_refresh_rate_time_pct", stats.formatRefreshRateTimeShares(), "Share of the counted samples' time at each refresh rate")
        entry("display_reported_refresh_rate_start_hz", decimal(displayRefreshRate?.toDouble()), "Display.getRefreshRate() at session start")
        entry("display_reported_refresh_rate_last_hz", decimal(lastDisplayState?.refreshRate?.toDouble()), "Display.getRefreshRate() at the last periodic check")
        entry(
            "display_modes_hz", lastDisplayState?.refreshRatesAtCurrentResolution?.joinToString(",") { decimal(it.toDouble()) },
            "Refresh rates the display supports at its current resolution",
        )
        entry(
            "display_lock_60hz_mode", lastDisplayState?.let { state ->
                when {
                    !state.isLock60HzEnabled -> "off"
                    state.lock60HzMode == null -> "none"
                    else -> "${state.lock60HzMode.modeId} (${decimal(state.lock60HzMode.refreshRate.toDouble())} Hz)"
                }
            },
            "The mode the 60Hz lock asks for (dropped while fast-forwarding). off: setting disabled. none: no 60Hz mode at the current resolution",
        )
    }

    table.section("Session") {
        entry("rom", sessionName)
        entry("session_start", SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(startDate))
        entry("session_duration_s", decimal(durationNs / 1e9))
        entry("log_location", destination?.description)
        val settingsMap = settingsSnapshot.toMap()
        entry("active_renderer", settingsMap["video_renderer"], "The renderer the session ran with: the render setting at session start (WatermelonDS's logger samples the native renderer once a second instead)")
        entry("active_internal_resolution", settingsMap["internal_resolution_scaling"], "Internal resolution multiplier the session ran with")
        if (BuildConfig.DEBUG) {
            entry(
                "debug_switches", debugSwitches?.ifEmpty { "none" },
                "Debug builds: the debug.melonade.* switches the game read that had a value, from the Debug switches setting unless marked (prop) " +
                    "for setprop; unread: lists setting entries nothing read (a typo, or a switch read later). As of the last drain before the " +
                    "session stopped",
            )
        }
    }

    table.section("Settings (at session start)") {
        settingsSnapshot.forEach { (key, value) -> entry(key, value) }
    }

    table.section("Emulation frames (stats exclude fast-forward and after-pause frames)") {
        val intervals = stats.frameIntervals
        val workDurations = stats.frameWorkDurations
        val hasFrames = intervals.count > 0
        entry("frames_total", stats.totalFrames)
        entry("frames_fast_forward", stats.fastForwardFrames)
        entry("frames_after_pause", stats.afterPauseFrames)
        entry("frames_dropped_from_log", MelonEmulator.getDroppedPerformanceFrameSampleCount())
        entry("frames_analyzed", intervals.count)
        entry(
            "percentile_resolution_ms", millis(DurationHistogram.BUCKET_WIDTH_NS),
            "Bucket width of the interval and work histograms, so the percentiles are only this precise",
        )
        entry("fps_avg", decimal(if (hasFrames && intervals.sum > 0) intervals.count * 1e9 / intervals.sum else null))
        val slowestMean = if (hasFrames) intervals.meanOfSlowest(maxOf(1L, intervals.count / 100)) else null
        entry("fps_1pct_low", decimal(slowestMean?.takeIf { it > 0 }?.let { 1e9 / it }), "FPS at the mean of the slowest 1% of the analyzed frame intervals")
        entry("interval_median_ms", millis(if (hasFrames) intervals.percentile(0.5) else null))
        entry("interval_p95_ms", millis(if (hasFrames) intervals.percentile(0.95) else null))
        entry("interval_p99_ms", millis(if (hasFrames) intervals.percentile(0.99) else null))
        entry("interval_max_ms", millis(if (hasFrames) intervals.max else null))
        entry("interval_stddev_ms", decimal(if (hasFrames) intervals.standardDeviation() / 1e6 else null, 3))
        entry("missed_frames_threshold_ms", millis(SessionStatistics.MISSED_FRAME_THRESHOLD_NS), "An analyzed frame whose interval is above this counts as missed")
        entry("missed_frames", stats.missedFrames)
        entry("missed_frames_pct", decimal(if (hasFrames) stats.missedFrames * 100.0 / intervals.count else null))
        entry("work_avg_ms", decimal(if (hasFrames) workDurations.mean() / 1e6 else null, 3))
        entry("work_p95_ms", millis(if (hasFrames) workDurations.percentile(0.95) else null))
        entry("work_max_ms", millis(if (hasFrames) workDurations.max else null))
        entry(
            "pacing_locked_frames_pct", decimal(if (hasFrames) stats.pacingLockedFrames * 100.0 / intervals.count else null),
            "Share of the analyzed frames whose start was locked to the display's vsync grid",
        )
        entry("pacing_divisor_last", stats.lastPacingDivisor.takeIf { it > 0 }, "Refreshes per frame (N) of the last locked frame")
    }

    table.section("Display (repeat stats exclude samples while paused or while the DS is asleep. Expected and unexpected repeats and hold stats also exclude fast-forward)") {
        val countedFrames = stats.cadenceCountedFrames
        val unexpectedRepeats = stats.unexpectedRepeats()
        entry("display_samples", stats.displaySamples)
        entry("display_samples_while_paused", stats.displayPausedSamples)
        entry("display_samples_while_ds_sleep", stats.displaySleepSamples)
        entry("display_samples_dropped_from_log", DisplayPerformanceCapture.getDroppedSampleCount())
        entry("display_new_frames", stats.displayNewFrames)
        entry("display_repeated_frames", stats.displayRepeatedFrames)
        entry(
            "display_expected_repeat_ratio", decimal(stats.expectedRepeatRatio(), 3),
            "Share of the counted samples expected to be repeats at their own refresh rate, so a rate change mid-session doesn't show up as repeats",
        )
        entry("display_unexpected_repeats", unexpectedRepeats, "Repeated frames beyond the expected ratio")
        entry("display_unexpected_repeats_pct", decimal(unexpectedRepeats?.takeIf { countedFrames > 0 }?.let { it * 100.0 / countedFrames }))
        entry("display_invalid_frames", stats.displayInvalidFrames)
        entry("display_longest_repeat_run", stats.displayLongestRepeatRun)
        entry(
            "display_longest_repeat_run_expected", stats.expectedLongestRepeatRun(),
            "The longest run explained by the fastest refresh rate the counted samples were at",
        )
        entry("display_repeat_runs_over_expected", stats.displayRepeatRunsOverExpected, "Runs longer than their own rate explains, counted once per run")
        entry("display_skipped_emulator_frames", stats.displaySkippedFrames)
        entry("display_samples_while_fast_forward", stats.displayFastForwardSamples)
        val hasHolds = stats.displayHoldFrames > 0
        val tracksCadence = stats.currentRefreshRate != null
        entry(
            "display_expected_hold_refreshes", decimal(stats.expectedHoldRefreshes()),
            "Hold = number of refreshes a frame stayed on screen, measured in the refresh period in effect when it ended (holds that span a " +
                "refresh rate change are skipped). If the rate changed, the expected hold is averaged over the measured holds",
        )
        entry("display_on_cadence_holds", stats.formatOnCadenceHolds(), "The holds that count as on-cadence, for each rate if the rate changed")
        entry("display_hold_frames", stats.displayHoldFrames.takeIf { tracksCadence }, "Frames whose hold was measured")
        entry("display_hold_histogram", if (hasHolds) stats.formatHoldHistogram() else null, "Frames per hold length")
        entry(
            "display_off_cadence_frames", stats.displayOffCadenceFrames.takeIf { tracksCadence },
            "Frames held more or fewer refreshes than expected. Judder shows up here even when the average matches (for example 1 + 3 instead of " +
                "2 + 2 at 120Hz)",
        )
        entry("display_off_cadence_pct", decimal(if (tracksCadence && hasHolds) stats.displayOffCadenceFrames * 100.0 / stats.displayHoldFrames else null))
        entry(
            "display_judder_window_min_frames", SessionStatistics.JUDDER_WINDOW_MIN_FRAMES,
            "Measured holds a 10 s window needs before it counts for display_judder_worst_10s_pct",
        )
        entry("display_judder_worst_10s_pct", decimal(stats.worstJudderWindowPct()), "Highest off-cadence percentage in any 10 s window")
        entry("render_avg_ms", decimal(stats.averageRenderDurationNs()?.div(1e6), 3))
        entry("render_max_ms", millis(stats.displayMaxRenderDuration.takeIf { stats.displaySamples > 0 }))
    }

    table.section("Audio (underrun and silence counts exclude fast-forward, muted or not, which is given apart as *_ff. Buffer stats exclude fast-forward and after-pause frames)") {
        val audioStreamInfo = parseAudioStreamInfo(MelonEmulator.getAudioStreamInfo() ?: lastAudioStreamInfo)
        entry(
            "audio_latency_setting", audioStreamInfo["requested_latency"],
            "The latency the stream was asked for (emulation always asks for LOW, the audio latency preference isn't used)",
        )
        entry("audio_stream_performance_mode", audioStreamInfo["performance"], "The performance mode the stream got")
        audioStreamInfo.forEach { (key, value) ->
            if (key != "requested_latency" && key != "performance") {
                entry("audio_stream_$key", value)
            }
        }
        val audioTotals = LongArray(MelonEmulator.AUDIO_TOTAL_COUNT)
        MelonEmulator.getAudioDiagnosticsTotals(audioTotals)
        fun audioDelta(index: Int) = audioTotals[index] - audioTotalsAtStart[index]
        val audioCallbacks = audioDelta(MelonEmulator.AUDIO_TOTAL_CALLBACKS)
        val audioSilenceFrames = audioDelta(MelonEmulator.AUDIO_TOTAL_SILENCE_FRAMES)
        val audioSampleRate = audioStreamInfo["rate"]?.toIntOrNull()?.takeIf { it > 0 } ?: AUDIO_BUFFER_SAMPLE_RATE
        val audioXRuns = audioTotals[MelonEmulator.AUDIO_TOTAL_XRUNS]
        val audioStreamOpens = audioDelta(MelonEmulator.AUDIO_TOTAL_STREAM_OPENS)
        entry(
            "audio_streams_used", audioStreamOpens + (if (wasAudioStreamOpenAtStart) 1 else 0),
            "Streams opened during the session, plus the one already open when it started",
        )
        entry("audio_stream_open_at_start", wasAudioStreamOpenAtStart)
        entry("audio_stream_opens_during_session", audioStreamOpens)
        entry("audio_stream_errors", audioDelta(MelonEmulator.AUDIO_TOTAL_STREAM_ERRORS))
        entry("audio_callbacks", audioCallbacks)
        entry("audio_frames_per_callback_avg", decimal(if (audioCallbacks > 0) audioDelta(MelonEmulator.AUDIO_TOTAL_CALLBACK_FRAMES).toDouble() / audioCallbacks else null, 1))
        entry("audio_frames_per_callback_max", audioTotals[MelonEmulator.AUDIO_TOTAL_MAX_CALLBACK_FRAMES].takeIf { audioCallbacks > 0 })
        entry("audio_callback_gap_max_ms", millis(audioTotals[MelonEmulator.AUDIO_TOTAL_MAX_CALLBACK_GAP_NS].takeIf { audioCallbacks > 0 }))
        entry(
            "audio_xruns", audioXRunsDuringSession(audioXRuns, audioTotalsAtStart[MelonEmulator.AUDIO_TOTAL_XRUNS], audioTotals[MelonEmulator.AUDIO_TOTAL_STREAM_OPENS] != 0L),
            "Underruns the audio device reported during the session, or unsupported when it doesn't report them",
        )
        val audioFastForwardUnderruns = audioDelta(MelonEmulator.AUDIO_TOTAL_FAST_FORWARD_UNDERRUN_CALLBACKS)
        val audioFastForwardSilenceFrames = audioDelta(MelonEmulator.AUDIO_TOTAL_FAST_FORWARD_SILENCE_FRAMES)
        val audioUnderruns = audioDelta(MelonEmulator.AUDIO_TOTAL_UNDERRUN_CALLBACKS) - audioFastForwardUnderruns
        val audioSilenceFramesOutsideFastForward = audioSilenceFrames - audioFastForwardSilenceFrames
        entry("audio_underrun_callbacks", audioUnderruns, "Callbacks that found fewer frames in the buffer than they needed")
        entry("audio_silence_frames", audioSilenceFramesOutsideFastForward, "Frames the underrun callbacks filled with silence or a repeated sample")
        entry("audio_silence_ms", decimal(audioSilenceFramesOutsideFastForward * 1000.0 / audioSampleRate, 1))
        entry("audio_emulator_frames_with_silence", stats.audioFramesWithSilenceOutsideFastForward)
        entry("audio_underrun_callbacks_ff", audioFastForwardUnderruns)
        entry("audio_silence_frames_ff", audioFastForwardSilenceFrames)
        entry("audio_silence_ff_ms", decimal(audioFastForwardSilenceFrames * 1000.0 / audioSampleRate, 1))
        entry("audio_emulator_frames_with_silence_ff", stats.audioFramesWithSilence - stats.audioFramesWithSilenceOutsideFastForward)
        val audioPrimeWaitFrames = audioTotals[MelonEmulator.AUDIO_TOTAL_SESSION_PRIME_WAIT_FRAMES]
        entry(
            "audio_prime_wait_frames", audioPrimeWaitFrames,
            "Silence while the callback waited for the buffer to fill (after a start or resume, or after it ran dry), not part of the underruns. " +
                "The priming counts cover the whole emulation session, including the startup prime from before the capture began",
        )
        entry("audio_prime_wait_ms", decimal(audioPrimeWaitFrames * 1000.0 / audioSampleRate, 1))
        entry("audio_reprimes", audioTotals[MelonEmulator.AUDIO_TOTAL_SESSION_REPRIMES], "How often the buffer ran dry and was primed again")
        entry(
            "audio_prime_trims", audioTotals[MelonEmulator.AUDIO_TOTAL_SESSION_PRIME_TRIMS],
            "Trims when priming ended (the emulator keeps filling the ring while a stream starts)",
        )
        entry("audio_prime_trim_frames", audioTotals[MelonEmulator.AUDIO_TOTAL_SESSION_PRIME_TRIM_FRAMES])
        entry("audio_buffer_capacity_frames", AUDIO_BUFFER_CAPACITY_FRAMES, "The SPU output ring between the emulator and the audio callback")
        val hasAudioFill = stats.audioFillSamples > 0
        entry("audio_buffer_fill_min_frames", stats.audioFillMin.takeIf { hasAudioFill }, "The fill is taken after each frame's sleep, so these are the low points")
        entry("audio_buffer_fill_min_ms", decimal(if (hasAudioFill) stats.audioFillMin * 1000.0 / AUDIO_BUFFER_SAMPLE_RATE else null, 1))
        entry("audio_buffer_fill_avg_frames", decimal(stats.averageAudioFill(), 0))
        entry("audio_buffer_full_frames", stats.audioBufferFullFrames, "Frames after which the buffer was full, so some of the emulator's samples were dropped")
        entry(
            "audio_skew_min_ppm", stats.audioSkewMinPpm.takeIf { hasAudioFill },
            "The drift control's skew, as parts per million off the base rate (positive: the ring was too full, so fewer frames were made)",
        )
        entry("audio_skew_avg_ppm", decimal(stats.averageAudioSkewPpm(), 0))
        entry("audio_skew_max_ppm", stats.audioSkewMaxPpm.takeIf { hasAudioFill })
        val hasFastForwardSpeed = stats.audioFastForwardSpeedSamples > 0
        entry(
            "audio_ff_speed_min", decimal(if (hasFastForwardSpeed) stats.audioFastForwardSpeedMinMilli / 1000.0 else null, 3),
            "The speed paced fast-forward really reached (smoothed over ~0.35 s), over the fast-forward frames where it was known. Its audio " +
                "plays at this speed when it falls short of the multiplier",
        )
        entry("audio_ff_speed_avg", decimal(stats.averageAudioFastForwardSpeed(), 3))
        val audioBufferSizeStart = if (wasAudioStreamOpenAtStart) audioTotalsAtStart[MelonEmulator.AUDIO_TOTAL_BUFFER_SIZE] else firstAudioBufferSizeAtOpen
        val audioBufferSizeFinal = audioTotals[MelonEmulator.AUDIO_TOTAL_BUFFER_SIZE]
        entry(
            "audio_output_buffer_start_frames", audioBufferSizeStart.takeIf { it >= 0 },
            "The AAudio buffer (between the callback and the device): its size when the session started, or when the first stream opened " +
                "during the session if none was open at the start",
        )
        entry("audio_output_buffer_start_ms", decimal(if (audioBufferSizeStart >= 0) audioBufferSizeStart * 1000.0 / audioSampleRate else null, 1))
        entry("audio_output_buffer_final_frames", audioBufferSizeFinal.takeIf { it >= 0 }, "The AAudio buffer's size now")
        entry("audio_output_buffer_final_ms", decimal(if (audioBufferSizeFinal >= 0) audioBufferSizeFinal * 1000.0 / audioSampleRate else null, 1))
        entry("audio_output_buffer_grows", audioDelta(MelonEmulator.AUDIO_TOTAL_BUFFER_GROWS), "How often the AAudio buffer grew after an xrun")
    }

    table.section("Memory") {
        entry("memory_samples", stats.memorySamples)
        entry("rss_peak_kb", stats.peakRss.takeIf { stats.memorySamples > 0 })
        entry("rss_avg_kb", stats.averageRss()?.roundToLong())
        entry("native_heap_alloc_peak_kb", stats.peakNativeHeap.takeIf { stats.memorySamples > 0 })
        entry("pss_total_start_kb", startMemorySnapshot?.total)
        entry("pss_java_start_kb", startMemorySnapshot?.java)
        entry("pss_native_start_kb", startMemorySnapshot?.native)
        entry("pss_graphics_start_kb", startMemorySnapshot?.graphics)
        entry("pss_total_end_kb", endMemorySnapshot?.total)
        entry("pss_java_end_kb", endMemorySnapshot?.java)
        entry("pss_native_end_kb", endMemorySnapshot?.native)
        entry("pss_graphics_end_kb", endMemorySnapshot?.graphics)
        entry("pss_samples", stats.pssSamples, "PSS is only sampled at the start and the end, since a sample can stall the emulator thread")
        entry("pss_graphics_min_kb", stats.minPssGraphics.takeIf { it >= 0 }, "Of the start and end samples")
        entry("pss_graphics_peak_kb", stats.peakPssGraphics.takeIf { it >= 0 }, "Of the start and end samples")
        entry(
            "pss_sample_max_ms", millis(stats.maxPssSampleDuration.takeIf { stats.pssSamples > 0 }),
            "Slowest Debug.getMemoryInfo call. Both run while capture is off, so they don't show up in the logged frames",
        )
        entry("thermal_status_final", stats.lastThermalStatus)
        entry("thermal_status_max", stats.maxThermalStatus)
    }

    table.section("System (CPU, GPU and battery, sampled once a second; sources the app can't read are left out)") {
        systemSampler.addSummaryEntries(this)
    }

    return table.render()
}

/**
 * The xruns since the session started: null before any stream was opened, "unsupported" when the device doesn't report them.
 */
private fun audioXRunsDuringSession(xRuns: Long, xRunsAtStart: Long, hasOpenedStream: Boolean): Any? {
    return when {
        !hasOpenedStream -> null
        xRuns < 0 -> "unsupported"
        else -> xRuns - maxOf(0L, xRunsAtStart)
    }
}
