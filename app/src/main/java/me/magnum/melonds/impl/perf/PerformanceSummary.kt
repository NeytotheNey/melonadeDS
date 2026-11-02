package me.magnum.melonds.impl.perf

import android.content.Context
import android.content.pm.ApplicationInfo
import android.os.Build
import androidx.core.content.pm.PackageInfoCompat
import me.magnum.melonds.MelonEmulator
import me.magnum.melonds.impl.perf.PerfCsv.appendMillis
import me.magnum.melonds.utils.PackageManagerCompat
import java.text.SimpleDateFormat
import java.util.Locale
import kotlin.math.roundToLong

// The SPU output ring between the emulator and the audio callback (AudioDiagnostics.h)
private const val AUDIO_BUFFER_CAPACITY_FRAMES = 2048
private const val AUDIO_BUFFER_SAMPLE_RATE = 48000

/**
 * Renders the summary text file of a performance log session from its statistics and the emulator's counters.
 */
internal fun PerformanceLogger.Session.buildSummary(context: Context, isComplete: Boolean): String {
    val durationNs = System.nanoTime() - startNs
    val summary = StringBuilder()

    fun line(key: String, value: Any?) {
        summary.append(key).append(": ").append(value ?: "n/a").append('\n')
    }

    fun millis(ns: Long?): String {
        return if (ns == null) "n/a" else StringBuilder().appendMillis(ns).toString()
    }

    fun decimal(value: Double?, decimals: Int = 2): String {
        return if (value == null || value.isNaN()) "n/a" else String.format(Locale.US, "%.${decimals}f", value)
    }

    // false while the session is still running (or if the app was killed before the session stopped)
    line("complete", isComplete)

    summary.append("\n# App\n")
    val packageInfo = runCatching { PackageManagerCompat.getPackageInfo(context.packageManager, context.packageName, 0) }.getOrNull()
    val isDebuggable = (context.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0
    line("app_version_name", packageInfo?.versionName)
    line("app_version_code", packageInfo?.let { PackageInfoCompat.getLongVersionCode(it) })
    line("app_package", context.packageName)
    line("build_type", if (isDebuggable) "debug" else "release")

    summary.append("\n# Device\n")
    line("device_manufacturer", Build.MANUFACTURER)
    line("device_model", Build.MODEL)
    line("device_name", Build.DEVICE)
    line("device_hardware", Build.HARDWARE)
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        line("device_soc", "${Build.SOC_MANUFACTURER} ${Build.SOC_MODEL}")
    }
    line("android_version", Build.VERSION.RELEASE)
    line("android_sdk", Build.VERSION.SDK_INT)
    // The refresh rates below are the vsync period the render loop ran at (from Choreographer), over the display samples counted in the
    // repeat stats outside fast-forward (which drops the 60Hz lock). The rate the display reported at session start stands in until the samples carry one. display_refresh_rate_hz is their
    // time-weighted average, so it's the session's rate unless the rate changed mid-session (see display_refresh_rate_time_pct)
    line("display_refresh_rate_hz", decimal(stats.averageRefreshRate() ?: displayRefreshRate?.toDouble()))
    line("display_refresh_rate_start_hz", decimal(stats.firstCountedRefreshRate?.refreshRate ?: displayRefreshRate?.toDouble()))
    // Of the last counted sample, so not the rate the display goes back to once the emulator is paused or fast-forwarded
    line("display_refresh_rate_last_hz", decimal(stats.lastCountedRefreshRate?.refreshRate ?: lastDisplayState?.refreshRate?.toDouble()))
    line("display_refresh_rate_changes", stats.refreshRateChanges)
    // Counted samples, and share of their time, at each refresh rate
    line("display_refresh_rate_samples", stats.formatRefreshRateSamples())
    line("display_refresh_rate_time_pct", stats.formatRefreshRateTimeShares())
    // Display.getRefreshRate() at session start and at the last periodic check
    line("display_reported_refresh_rate_start_hz", decimal(displayRefreshRate?.toDouble()))
    line("display_reported_refresh_rate_last_hz", decimal(lastDisplayState?.refreshRate?.toDouble()))
    // Refresh rates the display supports at its current resolution
    line("display_modes_hz", lastDisplayState?.refreshRatesAtCurrentResolution?.joinToString(",") { decimal(it.toDouble()) })
    // The mode the 60Hz lock asks for (dropped while fast-forwarding). off: setting disabled. none: no 60Hz mode at the current resolution
    line("display_lock_60hz_mode", lastDisplayState?.let { state ->
        when {
            !state.isLock60HzEnabled -> "off"
            state.lock60HzMode == null -> "none"
            else -> "${state.lock60HzMode.modeId} (${decimal(state.lock60HzMode.refreshRate.toDouble())} Hz)"
        }
    })

    summary.append("\n# Session\n")
    line("rom", sessionName)
    line("session_start", SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(startDate))
    line("session_duration_s", decimal(durationNs / 1e9))
    line("log_location", destination?.description)

    summary.append("\n# Settings (at session start)\n")
    settingsSnapshot.forEach { (key, value) -> line(key, value) }

    summary.append("\n# Emulation frames (stats exclude fast-forward and after-pause frames)\n")
    val intervals = stats.frameIntervals
    val workDurations = stats.frameWorkDurations
    val hasFrames = intervals.count > 0
    line("frames_total", stats.totalFrames)
    line("frames_fast_forward", stats.fastForwardFrames)
    line("frames_after_pause", stats.afterPauseFrames)
    line("frames_dropped_from_log", MelonEmulator.getDroppedPerformanceFrameSampleCount())
    line("frames_analyzed", intervals.count)
    line("percentile_resolution_ms", millis(DurationHistogram.BUCKET_WIDTH_NS))
    line("fps_avg", decimal(if (hasFrames && intervals.sum > 0) intervals.count * 1e9 / intervals.sum else null))
    val slowestMean = if (hasFrames) intervals.meanOfSlowest(maxOf(1L, intervals.count / 100)) else null
    line("fps_1pct_low", decimal(slowestMean?.takeIf { it > 0 }?.let { 1e9 / it }))
    line("interval_median_ms", millis(if (hasFrames) intervals.percentile(0.5) else null))
    line("interval_p95_ms", millis(if (hasFrames) intervals.percentile(0.95) else null))
    line("interval_p99_ms", millis(if (hasFrames) intervals.percentile(0.99) else null))
    line("interval_max_ms", millis(if (hasFrames) intervals.max else null))
    line("interval_stddev_ms", decimal(if (hasFrames) intervals.standardDeviation() / 1e6 else null, 3))
    line("missed_frames_threshold_ms", millis(SessionStatistics.MISSED_FRAME_THRESHOLD_NS))
    line("missed_frames", stats.missedFrames)
    line("missed_frames_pct", decimal(if (hasFrames) stats.missedFrames * 100.0 / intervals.count else null))
    line("work_avg_ms", decimal(if (hasFrames) workDurations.mean() / 1e6 else null, 3))
    line("work_p95_ms", millis(if (hasFrames) workDurations.percentile(0.95) else null))
    line("work_max_ms", millis(if (hasFrames) workDurations.max else null))
    // Share of the analyzed frames whose start was locked to the display's vsync grid, and the refreshes per frame (N) of the last one
    line("pacing_locked_frames_pct", decimal(if (hasFrames) stats.pacingLockedFrames * 100.0 / intervals.count else null))
    line("pacing_divisor_last", stats.lastPacingDivisor.takeIf { it > 0 })

    summary.append("\n# Display (repeat stats exclude samples while paused or while the DS is asleep. Expected and unexpected repeats and hold stats also exclude fast-forward)\n")
    // Each counted sample's expected repeats come from its own refresh rate, so a rate change mid-session doesn't show up as repeats
    val countedFrames = stats.cadenceCountedFrames
    val unexpectedRepeats = stats.unexpectedRepeats()
    line("display_samples", stats.displaySamples)
    line("display_samples_while_paused", stats.displayPausedSamples)
    line("display_samples_while_ds_sleep", stats.displaySleepSamples)
    line("display_samples_dropped_from_log", DisplayPerformanceCapture.getDroppedSampleCount())
    line("display_new_frames", stats.displayNewFrames)
    line("display_repeated_frames", stats.displayRepeatedFrames)
    line("display_expected_repeat_ratio", decimal(stats.expectedRepeatRatio(), 3))
    line("display_unexpected_repeats", unexpectedRepeats)
    line("display_unexpected_repeats_pct", decimal(unexpectedRepeats?.takeIf { countedFrames > 0 }?.let { it * 100.0 / countedFrames }))
    line("display_invalid_frames", stats.displayInvalidFrames)
    line("display_longest_repeat_run", stats.displayLongestRepeatRun)
    // The longest run explained by the fastest refresh rate the counted samples were at. Runs longer than their own rate explains are counted
    // apart
    line("display_longest_repeat_run_expected", stats.expectedLongestRepeatRun())
    line("display_repeat_runs_over_expected", stats.displayRepeatRunsOverExpected)
    line("display_skipped_emulator_frames", stats.displaySkippedFrames)
    line("display_samples_while_fast_forward", stats.displayFastForwardSamples)
    // Hold = number of refreshes a frame stayed on screen. Judder shows up as frames held more or fewer refreshes than expected, even when
    // the average matches (for example 1 + 3 instead of 2 + 2 at 120Hz)
    val hasHolds = stats.displayHoldFrames > 0
    // Each hold is measured in the refresh period in effect when it ended, and holds that span a refresh rate change are skipped. If the rate
    // changed, the expected hold is averaged over the measured holds and the on-cadence holds are given for each rate
    val tracksCadence = stats.currentRefreshRate != null
    line("display_expected_hold_refreshes", decimal(stats.expectedHoldRefreshes()))
    line("display_on_cadence_holds", stats.formatOnCadenceHolds())
    line("display_hold_frames", stats.displayHoldFrames.takeIf { tracksCadence })
    line("display_hold_histogram", if (hasHolds) stats.formatHoldHistogram() else null)
    line("display_off_cadence_frames", stats.displayOffCadenceFrames.takeIf { tracksCadence })
    line("display_off_cadence_pct", decimal(if (tracksCadence && hasHolds) stats.displayOffCadenceFrames * 100.0 / stats.displayHoldFrames else null))
    line("display_judder_window_min_frames", SessionStatistics.JUDDER_WINDOW_MIN_FRAMES)
    line("display_judder_worst_10s_pct", decimal(stats.worstJudderWindowPct()))
    line("render_avg_ms", decimal(stats.averageRenderDurationNs()?.div(1e6), 3))
    line("render_max_ms", millis(stats.displayMaxRenderDuration.takeIf { stats.displaySamples > 0 }))

    summary.append("\n# Audio (underrun and silence counts exclude fast-forward, muted or not, which is given apart as *_ff. Buffer stats exclude fast-forward and after-pause frames)\n")
    // The latency the stream was asked for (emulation always asks for LOW, the audio latency preference isn't used) and what it got
    val audioStreamInfo = parseAudioStreamInfo(MelonEmulator.getAudioStreamInfo() ?: lastAudioStreamInfo)
    line("audio_latency_setting", audioStreamInfo["requested_latency"])
    line("audio_stream_performance_mode", audioStreamInfo["performance"])
    audioStreamInfo.forEach { (key, value) ->
        if (key != "requested_latency" && key != "performance") {
            line("audio_stream_$key", value)
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
    // The stream already open when the session started counts as used
    line("audio_streams_used", audioStreamOpens + if (wasAudioStreamOpenAtStart) 1 else 0)
    line("audio_stream_open_at_start", wasAudioStreamOpenAtStart)
    line("audio_stream_opens_during_session", audioStreamOpens)
    line("audio_stream_errors", audioDelta(MelonEmulator.AUDIO_TOTAL_STREAM_ERRORS))
    line("audio_callbacks", audioCallbacks)
    line("audio_frames_per_callback_avg", decimal(if (audioCallbacks > 0) audioDelta(MelonEmulator.AUDIO_TOTAL_CALLBACK_FRAMES).toDouble() / audioCallbacks else null, 1))
    line("audio_frames_per_callback_max", audioTotals[MelonEmulator.AUDIO_TOTAL_MAX_CALLBACK_FRAMES].takeIf { audioCallbacks > 0 })
    line("audio_callback_gap_max_ms", millis(audioTotals[MelonEmulator.AUDIO_TOTAL_MAX_CALLBACK_GAP_NS].takeIf { audioCallbacks > 0 }))
    line("audio_xruns", if (audioTotals[MelonEmulator.AUDIO_TOTAL_STREAM_OPENS] == 0L) null else if (audioXRuns < 0) "unsupported" else audioXRuns - maxOf(0L, audioTotalsAtStart[MelonEmulator.AUDIO_TOTAL_XRUNS]))
    // Callbacks that found fewer frames in the buffer than they needed, and the frames they filled with silence or a repeated sample
    val audioFastForwardUnderruns = audioDelta(MelonEmulator.AUDIO_TOTAL_FAST_FORWARD_UNDERRUN_CALLBACKS)
    val audioFastForwardSilenceFrames = audioDelta(MelonEmulator.AUDIO_TOTAL_FAST_FORWARD_SILENCE_FRAMES)
    val audioUnderruns = audioDelta(MelonEmulator.AUDIO_TOTAL_UNDERRUN_CALLBACKS) - audioFastForwardUnderruns
    val audioSilenceFramesOutsideFastForward = audioSilenceFrames - audioFastForwardSilenceFrames
    line("audio_underrun_callbacks", audioUnderruns)
    line("audio_silence_frames", audioSilenceFramesOutsideFastForward)
    line("audio_silence_ms", decimal(audioSilenceFramesOutsideFastForward * 1000.0 / audioSampleRate, 1))
    line("audio_emulator_frames_with_silence", stats.audioFramesWithSilenceOutsideFastForward)
    line("audio_underrun_callbacks_ff", audioFastForwardUnderruns)
    line("audio_silence_frames_ff", audioFastForwardSilenceFrames)
    line("audio_silence_ff_ms", decimal(audioFastForwardSilenceFrames * 1000.0 / audioSampleRate, 1))
    line("audio_emulator_frames_with_silence_ff", stats.audioFramesWithSilence - stats.audioFramesWithSilenceOutsideFastForward)
    // Priming, over the whole emulation session (so including the startup prime from before the capture began): silence while the
    // callback waited for the buffer to fill (after a start or resume, or after it ran dry), not part of the underruns, how often it
    // ran dry, and the trims when priming ended (the emulator keeps filling the ring while a stream starts)
    val audioPrimeWaitFrames = audioTotals[MelonEmulator.AUDIO_TOTAL_SESSION_PRIME_WAIT_FRAMES]
    line("audio_prime_wait_frames", audioPrimeWaitFrames)
    line("audio_prime_wait_ms", decimal(audioPrimeWaitFrames * 1000.0 / audioSampleRate, 1))
    line("audio_reprimes", audioTotals[MelonEmulator.AUDIO_TOTAL_SESSION_REPRIMES])
    line("audio_prime_trims", audioTotals[MelonEmulator.AUDIO_TOTAL_SESSION_PRIME_TRIMS])
    line("audio_prime_trim_frames", audioTotals[MelonEmulator.AUDIO_TOTAL_SESSION_PRIME_TRIM_FRAMES])
    line("audio_buffer_capacity_frames", AUDIO_BUFFER_CAPACITY_FRAMES)
    // Taken after each frame's sleep, so these are the low points
    val hasAudioFill = stats.audioFillSamples > 0
    line("audio_buffer_fill_min_frames", stats.audioFillMin.takeIf { hasAudioFill })
    line("audio_buffer_fill_min_ms", decimal(if (hasAudioFill) stats.audioFillMin * 1000.0 / AUDIO_BUFFER_SAMPLE_RATE else null, 1))
    line("audio_buffer_fill_avg_frames", decimal(stats.averageAudioFill(), 0))
    // Frames after which the buffer was full, so some of the emulator's samples were dropped
    line("audio_buffer_full_frames", stats.audioBufferFullFrames)
    // The drift control's skew, as parts per million off the base rate (positive: the ring was too full, so fewer frames were made)
    line("audio_skew_min_ppm", stats.audioSkewMinPpm.takeIf { hasAudioFill })
    line("audio_skew_avg_ppm", decimal(stats.averageAudioSkewPpm(), 0))
    line("audio_skew_max_ppm", stats.audioSkewMaxPpm.takeIf { hasAudioFill })
    // The speed paced fast-forward really reached (smoothed over ~0.35 s), over the fast-forward frames where it was known. Its audio
    // plays at this speed when it falls short of the multiplier
    val hasFastForwardSpeed = stats.audioFastForwardSpeedSamples > 0
    line("audio_ff_speed_min", decimal(if (hasFastForwardSpeed) stats.audioFastForwardSpeedMinMilli / 1000.0 else null, 3))
    line("audio_ff_speed_avg", decimal(stats.averageAudioFastForwardSpeed(), 3))
    // The AAudio buffer (between the callback and the device): its size when the session started (or when the first stream opened during
    // the session started, if none was open at the start), now, and how often it grew after an xrun
    val audioBufferSizeStart = if (wasAudioStreamOpenAtStart) audioTotalsAtStart[MelonEmulator.AUDIO_TOTAL_BUFFER_SIZE] else firstAudioBufferSizeAtOpen
    val audioBufferSizeFinal = audioTotals[MelonEmulator.AUDIO_TOTAL_BUFFER_SIZE]
    line("audio_output_buffer_start_frames", audioBufferSizeStart.takeIf { it >= 0 })
    line("audio_output_buffer_start_ms", decimal(if (audioBufferSizeStart >= 0) audioBufferSizeStart * 1000.0 / audioSampleRate else null, 1))
    line("audio_output_buffer_final_frames", audioBufferSizeFinal.takeIf { it >= 0 })
    line("audio_output_buffer_final_ms", decimal(if (audioBufferSizeFinal >= 0) audioBufferSizeFinal * 1000.0 / audioSampleRate else null, 1))
    line("audio_output_buffer_grows", audioDelta(MelonEmulator.AUDIO_TOTAL_BUFFER_GROWS))

    summary.append("\n# Memory\n")
    line("memory_samples", stats.memorySamples)
    line("rss_peak_kb", stats.peakRss.takeIf { stats.memorySamples > 0 })
    line("rss_avg_kb", stats.averageRss()?.roundToLong())
    line("native_heap_alloc_peak_kb", stats.peakNativeHeap.takeIf { stats.memorySamples > 0 })
    line("pss_total_start_kb", startMemorySnapshot?.total)
    line("pss_java_start_kb", startMemorySnapshot?.java)
    line("pss_native_start_kb", startMemorySnapshot?.native)
    line("pss_graphics_start_kb", startMemorySnapshot?.graphics)
    line("pss_total_end_kb", endMemorySnapshot?.total)
    line("pss_java_end_kb", endMemorySnapshot?.java)
    line("pss_native_end_kb", endMemorySnapshot?.native)
    line("pss_graphics_end_kb", endMemorySnapshot?.graphics)
    // Over every PSS sample (start, every 30 s and end)
    line("pss_samples", stats.pssSamples)
    line("pss_graphics_min_kb", stats.minPssGraphics.takeIf { it >= 0 })
    line("pss_graphics_peak_kb", stats.peakPssGraphics.takeIf { it >= 0 })
    // Slowest Debug.getMemoryInfo call. Samples taken during capture can stall the emulator thread for up to about this long
    line("pss_sample_max_ms", millis(stats.maxPssSampleDuration.takeIf { stats.pssSamples > 0 }))
    line("thermal_status_final", stats.lastThermalStatus)
    line("thermal_status_max", stats.maxThermalStatus)

    return summary.toString()
}
