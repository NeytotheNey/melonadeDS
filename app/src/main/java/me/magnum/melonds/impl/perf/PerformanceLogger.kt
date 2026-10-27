package me.magnum.melonds.impl.perf

import android.content.Context
import android.content.pm.ApplicationInfo
import android.hardware.display.DisplayManager
import android.net.Uri
import android.os.Build
import android.os.Debug
import android.os.PowerManager
import android.system.Os
import android.system.OsConstants
import android.util.Log
import android.view.Display
import androidx.core.content.getSystemService
import androidx.core.content.pm.PackageInfoCompat
import androidx.documentfile.provider.DocumentFile
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import me.magnum.melonds.MelonEmulator
import me.magnum.melonds.domain.repositories.SettingsRepository
import me.magnum.melonds.utils.PackageManagerCompat
import java.io.BufferedWriter
import java.io.File
import java.io.IOException
import java.io.OutputStream
import java.io.OutputStreamWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.roundToLong
import kotlin.math.sqrt

/**
 * Opt-in logger used to compare builds. While a session is active, it drains the frame samples captured by the emulator thread and the display
 * samples captured by the render thread about once per second, writes them to CSV files together with a memory sample, and keeps a summary with
 * aggregated statistics up to date. All work happens on a background thread.
 */
class PerformanceLogger(
    private val context: Context,
    private val settingsRepository: SettingsRepository,
) {

    companion object {
        private const val TAG = "PerformanceLogger"
        private const val LOG_DIRECTORY_NAME = "perf_logs"
        private const val DRAIN_INTERVAL_MS = 1000L
        // The summary is rewritten every few drains so that a session that is killed without being stopped still has one
        private const val SUMMARY_INTERVAL_DRAINS = 5
        private const val PREVIOUS_SESSION_TIMEOUT_MS = 5000L
        private const val FRAME_DRAIN_BATCH_SIZE = 4096
        private const val DISPLAY_DRAIN_BATCH_SIZE = 1024
        private const val TARGET_FPS = 60.0
        private const val TARGET_FRAME_INTERVAL_NS = 16_666_667L
        private const val MISSED_FRAME_THRESHOLD_NS = TARGET_FRAME_INTERVAL_NS * 3 / 2
        // Holds from 1 to 31 refreshes get their own bucket. The last bucket counts 32 and above
        private const val HOLD_HISTOGRAM_SIZE = 33
        // If refresh_hz / 60 is within this of a whole number, every frame is expected to be held exactly that many refreshes
        private const val HOLD_WHOLE_NUMBER_TOLERANCE = 0.05
        private const val JUDDER_WINDOW_SECONDS = 10
        // A window is only evaluated once it holds at least 80% of the holds a full window should have at 60 FPS (480)
        private const val JUDDER_WINDOW_MIN_FRAMES = JUDDER_WINDOW_SECONDS * 60 * 4 / 5
        private const val MAX_SESSION_NAME_LENGTH = 48
        private const val LOG_FILE_MIME_TYPE = "application/octet-stream"

        /**
         * Directory used when the user hasn't picked a log folder.
         */
        fun getDefaultLogDirectory(context: Context): File {
            return context.getExternalFilesDir(LOG_DIRECTORY_NAME) ?: File(context.filesDir, LOG_DIRECTORY_NAME)
        }
    }

    private val loggerScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var currentSession: Session? = null
    private var lastSessionJob: Job? = null

    /**
     * Starts a new logging session if performance logging is enabled. Any active session is stopped first.
     *
     * @param sessionName Name of the ROM (or firmware) being run. Used in the file names and in the summary
     */
    @Synchronized
    fun startSession(sessionName: String) {
        stopSession()
        if (!settingsRepository.isPerformanceLoggingEnabled()) {
            return
        }

        val session = Session(sessionName)
        val previousSessionJob = lastSessionJob
        currentSession = session
        lastSessionJob = loggerScope.launch {
            // All sessions share the same native capture buffer, so a new session can only start once the previous one has been finalized
            if (previousSessionJob != null) {
                val previousSessionFinished = withTimeoutOrNull(PREVIOUS_SESSION_TIMEOUT_MS) { previousSessionJob.join() } != null
                if (!previousSessionFinished) {
                    Log.w(TAG, "Previous performance log session is taking too long to finish. Skipping session for $sessionName")
                    // Keep waiting so that the next session doesn't overlap with the one that is still running
                    previousSessionJob.join()
                    return@launch
                }
            }

            if (session.isStopRequested()) {
                return@launch
            }

            session.run()
        }
    }

    /**
     * Stops the active session, if any. The remaining samples are written and the summary is finalized in the background.
     */
    @Synchronized
    fun stopSession() {
        currentSession?.requestStop()
        currentSession = null
    }

    @Synchronized
    fun isSessionActive(): Boolean {
        return currentSession != null
    }

    private inner class Session(private val sessionName: String) {
        private val stopSignal = CompletableDeferred<Unit>()
        private val startDate = Date()
        private val startNs = System.nanoTime()
        private val filePrefix = buildFilePrefix()

        private val frameBuffer = LongArray(FRAME_DRAIN_BATCH_SIZE * MelonEmulator.PERF_FRAME_SAMPLE_FIELDS)
        private val displayBuffer = LongArray(DISPLAY_DRAIN_BATCH_SIZE * DisplayPerformanceCapture.FIELDS)
        private val lineBuilder = StringBuilder(64 * 1024)
        private val powerManager = context.getSystemService<PowerManager>()
        private val pageSizeKb = Os.sysconf(OsConstants._SC_PAGESIZE) / 1024

        private val stats = SessionStatistics()
        private var settingsSnapshot = emptyList<Pair<String, String>>()
        private var displayRefreshRate: Float? = null
        private var startMemorySnapshot: PssSnapshot? = null
        private var endMemorySnapshot: PssSnapshot? = null

        private var destination: LogDestination? = null
        private var framesWriter: BufferedWriter? = null
        private var displayWriter: BufferedWriter? = null
        private var memoryWriter: BufferedWriter? = null

        fun requestStop() {
            stopSignal.complete(Unit)
        }

        fun isStopRequested(): Boolean {
            return stopSignal.isCompleted
        }

        suspend fun run() {
            // Full PSS breakdown. This reads /proc/self/smaps while holding the process' mmap lock, which can stall the emulator thread (the JIT
            // maps and protects memory), so it's only done before capture starts and after it ends
            startMemorySnapshot = takePssSnapshot()

            // Start capturing straight away so that start-up frames are kept. Samples wait in the buffers until the first drain
            MelonEmulator.setPerformanceCaptureEnabled(true)
            DisplayPerformanceCapture.setEnabled(true)

            try {
                settingsSnapshot = buildSettingsSnapshot()
                displayRefreshRate = getDisplayRefreshRate()
                stats.configureDisplayCadence(displayRefreshRate)

                val logDestination = resolveDestination()
                destination = logDestination
                framesWriter = logDestination.openWriter("${filePrefix}_frames.csv").apply {
                    write("frame,t_ms,work_ms,sleep_ms,interval_ms,lines,fast_forward,after_pause\n")
                }
                displayWriter = logDestination.openWriter("${filePrefix}_display.csv").apply {
                    write("t_ms,render_ms,valid,new_frame,skipped_frames,paused,ds_sleep,fast_forward,prev_hold_refreshes\n")
                }
                memoryWriter = logDestination.openWriter("${filePrefix}_memory.csv").apply {
                    write("t_s,rss_kb,native_heap_alloc_kb,java_heap_used_kb,thermal_status\n")
                }

                var drainCount = 0
                var stopRequested = false
                while (!stopRequested) {
                    stopRequested = withTimeoutOrNull(DRAIN_INTERVAL_MS) { stopSignal.await() } != null
                    if (stopRequested) {
                        // Stop capturing before the final drain so that nothing is left behind in the buffers
                        MelonEmulator.setPerformanceCaptureEnabled(false)
                        DisplayPerformanceCapture.setEnabled(false)
                    }

                    drainFrameSamples()
                    drainDisplaySamples()
                    sampleMemory()
                    flushWriters()

                    drainCount++
                    if (!stopRequested && drainCount % SUMMARY_INTERVAL_DRAINS == 0) {
                        writeSummary(isComplete = false)
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "Failed to write performance logs", e)
            } finally {
                MelonEmulator.setPerformanceCaptureEnabled(false)
                DisplayPerformanceCapture.setEnabled(false)
                closeWriters()
            }

            if (destination == null) {
                return
            }

            endMemorySnapshot = takePssSnapshot()
            writeSummary(isComplete = true)
            Log.i(TAG, "Performance logs written to ${destination?.description}")
        }

        private fun drainFrameSamples() {
            val writer = framesWriter ?: return
            val fields = MelonEmulator.PERF_FRAME_SAMPLE_FIELDS

            do {
                val count = MelonEmulator.drainPerformanceFrameSamples(frameBuffer)
                lineBuilder.setLength(0)
                for (i in 0 until count) {
                    val offset = i * fields
                    val timestamp = frameBuffer[offset]
                    val workDuration = frameBuffer[offset + 1]
                    val sleepDuration = frameBuffer[offset + 2]
                    val interval = frameBuffer[offset + 3]
                    val lines = frameBuffer[offset + 4]
                    val flags = frameBuffer[offset + 5]
                    val isFastForward = (flags and MelonEmulator.PERF_FRAME_FLAG_FAST_FORWARD) != 0L
                    val isAfterPause = (flags and MelonEmulator.PERF_FRAME_FLAG_AFTER_PAUSE) != 0L

                    lineBuilder.append(stats.totalFrames).append(',')
                        .appendMillis(timestamp - startNs).append(',')
                        .appendMillis(workDuration).append(',')
                        .appendMillis(sleepDuration).append(',')
                        .appendMillis(interval).append(',')
                        .append(lines).append(',')
                        .append(if (isFastForward) '1' else '0').append(',')
                        .append(if (isAfterPause) '1' else '0').append('\n')

                    stats.trackFrame(workDuration, interval, isFastForward, isAfterPause)
                }
                writer.append(lineBuilder)
            } while (count == FRAME_DRAIN_BATCH_SIZE)
        }

        private fun drainDisplaySamples() {
            val writer = displayWriter ?: return
            val fields = DisplayPerformanceCapture.FIELDS

            do {
                val count = DisplayPerformanceCapture.drain(displayBuffer)
                lineBuilder.setLength(0)
                for (i in 0 until count) {
                    val offset = i * fields
                    val timestamp = displayBuffer[offset]
                    val renderDuration = displayBuffer[offset + 1]
                    val flags = displayBuffer[offset + 2]
                    val skippedFrames = displayBuffer[offset + 3]
                    val isValid = (flags and DisplayPerformanceCapture.FLAG_VALID) != 0L
                    val isNewFrame = (flags and DisplayPerformanceCapture.FLAG_NEW_FRAME) != 0L
                    val isPaused = (flags and DisplayPerformanceCapture.FLAG_EMULATION_PAUSED) != 0L
                    val isDsSleeping = (flags and DisplayPerformanceCapture.FLAG_DS_SLEEP) != 0L
                    val isFastForward = (flags and DisplayPerformanceCapture.FLAG_FAST_FORWARD) != 0L

                    val previousHold = stats.trackDisplayFrame(timestamp, renderDuration, isValid, isNewFrame, skippedFrames, isPaused, isDsSleeping, isFastForward)

                    lineBuilder.appendMillis(timestamp - startNs).append(',')
                        .appendMillis(renderDuration).append(',')
                        .append(if (isValid) '1' else '0').append(',')
                        .append(if (isNewFrame) '1' else '0').append(',')
                        .append(skippedFrames).append(',')
                        .append(if (isPaused) '1' else '0').append(',')
                        .append(if (isDsSleeping) '1' else '0').append(',')
                        .append(if (isFastForward) '1' else '0').append(',')
                    // Only set on new frames whose previous frame's hold was measured
                    if (previousHold > 0) {
                        lineBuilder.append(previousHold)
                    }
                    lineBuilder.append('\n')
                }
                writer.append(lineBuilder)
            } while (count == DISPLAY_DRAIN_BATCH_SIZE)
        }

        private fun sampleMemory() {
            val writer = memoryWriter ?: return

            // Only cheap sources here. /proc/self/statm reads memory counters without walking the memory mappings
            val rss = readResidentSetSizeKb()
            val nativeHeapAllocated = Debug.getNativeHeapAllocatedSize() / 1024
            val runtime = Runtime.getRuntime()
            val javaHeapUsed = (runtime.totalMemory() - runtime.freeMemory()) / 1024
            val thermalStatus = getThermalStatus()

            lineBuilder.setLength(0)
            // Elapsed microseconds formatted as "milliseconds" gives seconds with 3 decimal places
            lineBuilder.appendMillis((System.nanoTime() - startNs) / 1000).append(',')
                .append(rss).append(',')
                .append(nativeHeapAllocated).append(',')
                .append(javaHeapUsed).append(',')
                .append(thermalStatus).append('\n')
            writer.append(lineBuilder)

            stats.trackMemory(rss, nativeHeapAllocated, thermalStatus)
        }

        private fun readResidentSetSizeKb(): Long {
            return try {
                // Format: size resident shared text lib data dt (in pages)
                val residentPages = File("/proc/self/statm").readText().split(' ').getOrNull(1)?.trim()?.toLongOrNull()
                residentPages?.let { it * pageSizeKb } ?: -1L
            } catch (e: IOException) {
                -1L
            }
        }

        private fun takePssSnapshot(): PssSnapshot {
            val memoryInfo = Debug.MemoryInfo()
            Debug.getMemoryInfo(memoryInfo)
            return PssSnapshot(
                total = memoryInfo.totalPss.toLong(),
                java = memoryInfo.getMemoryStat("summary.java-heap")?.toLongOrNull() ?: -1L,
                native = memoryInfo.getMemoryStat("summary.native-heap")?.toLongOrNull() ?: -1L,
                graphics = memoryInfo.getMemoryStat("summary.graphics")?.toLongOrNull() ?: -1L,
            )
        }

        private fun getThermalStatus(): Int {
            return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                powerManager?.currentThermalStatus ?: -1
            } else {
                -1
            }
        }

        private fun flushWriters() {
            framesWriter?.flush()
            displayWriter?.flush()
            memoryWriter?.flush()
        }

        private fun closeWriters() {
            listOf(framesWriter, displayWriter, memoryWriter).forEach {
                try {
                    it?.close()
                } catch (e: IOException) {
                    Log.w(TAG, "Failed to close performance log file", e)
                }
            }
            framesWriter = null
            displayWriter = null
            memoryWriter = null
        }

        private fun writeSummary(isComplete: Boolean) {
            val logDestination = destination ?: return
            try {
                logDestination.writeReplacing("${filePrefix}_summary.txt", buildSummary(isComplete))
            } catch (e: Exception) {
                Log.e(TAG, "Failed to write performance log summary", e)
            }
        }

        private fun resolveDestination(): LogDestination {
            val selectedDirectory = settingsRepository.getPerformanceLogDirectory()
            if (selectedDirectory != null) {
                val directoryDocument = try {
                    DocumentFile.fromTreeUri(context, selectedDirectory)?.takeIf { it.isDirectory && it.canWrite() }
                } catch (e: Exception) {
                    null
                }

                if (directoryDocument != null) {
                    return DocumentTreeDestination(directoryDocument)
                } else {
                    Log.w(TAG, "Cannot write to $selectedDirectory. Using the default log directory instead")
                }
            }

            val defaultDirectory = getDefaultLogDirectory(context)
            if (!defaultDirectory.isDirectory && !defaultDirectory.mkdirs()) {
                throw IOException("Could not create ${defaultDirectory.absolutePath}")
            }
            return FileDestination(defaultDirectory)
        }

        private fun buildFilePrefix(): String {
            val timestamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(startDate)
            val sanitizedName = sessionName
                .replace(Regex("[^A-Za-z0-9._-]+"), "_")
                .trim('_', '.')
                .take(MAX_SESSION_NAME_LENGTH)
                .ifEmpty { "session" }
            return "perf_${timestamp}_$sanitizedName"
        }

        private suspend fun buildSettingsSnapshot(): List<Pair<String, String>> {
            return try {
                val rendererConfiguration = settingsRepository.observeRenderConfiguration().first()
                listOf(
                    "video_renderer" to rendererConfiguration.renderer.name,
                    "internal_resolution_scaling" to rendererConfiguration.resolutionScaling.toString(),
                    "video_filtering" to rendererConfiguration.videoFiltering.name,
                    "threaded_rendering" to rendererConfiguration.threadedRendering.toString(),
                    "render_strategy" to rendererConfiguration.renderStrategy.name,
                    "jit_enabled" to settingsRepository.isJitEnabled().toString(),
                    "sustained_performance_mode" to settingsRepository.isSustainedPerformanceModeEnabled().toString(),
                    "fast_forward_multiplier" to settingsRepository.getFastForwardSpeedMultiplier().toString(),
                    "default_console_type" to settingsRepository.getDefaultConsoleType().name,
                    "sound_enabled" to settingsRepository.isSoundEnabled().toString(),
                    "audio_latency" to settingsRepository.getAudioLatency().name,
                    "rewind_enabled" to settingsRepository.isRewindEnabled().toString(),
                )
            } catch (e: Exception) {
                Log.w(TAG, "Failed to read settings for the performance log summary", e)
                emptyList()
            }
        }

        private fun buildSummary(isComplete: Boolean): String {
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
            line("display_refresh_rate_hz", decimal(displayRefreshRate?.toDouble()))

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
            line("missed_frames_threshold_ms", millis(MISSED_FRAME_THRESHOLD_NS))
            line("missed_frames", stats.missedFrames)
            line("missed_frames_pct", decimal(if (hasFrames) stats.missedFrames * 100.0 / intervals.count else null))
            line("work_avg_ms", decimal(if (hasFrames) workDurations.mean() / 1e6 else null, 3))
            line("work_p95_ms", millis(if (hasFrames) workDurations.percentile(0.95) else null))
            line("work_max_ms", millis(if (hasFrames) workDurations.max else null))

            summary.append("\n# Display (repeat stats exclude samples while paused or while the DS is asleep. Hold stats also exclude fast-forward)\n")
            val refreshRate = displayRefreshRate?.toDouble()?.takeIf { it > 0 }
            // A display faster than 60Hz has to show some frames twice even with perfect pacing
            val expectedRepeatRatio = refreshRate?.let { maxOf(0.0, 1.0 - TARGET_FPS / it) }
            val countedFrames = stats.displayNewFrames + stats.displayRepeatedFrames
            val unexpectedRepeats = expectedRepeatRatio?.let { maxOf(0L, stats.displayRepeatedFrames - (it * countedFrames).roundToLong()) }
            val expectedLongestRepeatRun = refreshRate?.let { maxOf(0L, ceil(it / TARGET_FPS - 1e-3).toLong() - 1) }
            line("display_samples", stats.displaySamples)
            line("display_samples_while_paused", stats.displayPausedSamples)
            line("display_samples_while_ds_sleep", stats.displaySleepSamples)
            line("display_samples_dropped_from_log", DisplayPerformanceCapture.getDroppedSampleCount())
            line("display_new_frames", stats.displayNewFrames)
            line("display_repeated_frames", stats.displayRepeatedFrames)
            line("display_expected_repeat_ratio", decimal(expectedRepeatRatio, 3))
            line("display_unexpected_repeats", unexpectedRepeats)
            line("display_unexpected_repeats_pct", decimal(unexpectedRepeats?.takeIf { countedFrames > 0 }?.let { it * 100.0 / countedFrames }))
            line("display_invalid_frames", stats.displayInvalidFrames)
            line("display_longest_repeat_run", stats.displayLongestRepeatRun)
            line("display_longest_repeat_run_expected", expectedLongestRepeatRun)
            line("display_skipped_emulator_frames", stats.displaySkippedFrames)
            line("display_samples_while_fast_forward", stats.displayFastForwardSamples)
            // Hold = number of refreshes a frame stayed on screen. Judder shows up as frames held more or fewer refreshes than expected, even when
            // the average matches (for example 1 + 3 instead of 2 + 2 at 120Hz)
            val hasHolds = stats.displayHoldFrames > 0
            val tracksCadence = stats.expectedHoldRefreshes != null
            line("display_expected_hold_refreshes", decimal(stats.expectedHoldRefreshes))
            line("display_on_cadence_holds", when {
                !tracksCadence -> null
                stats.onCadenceMinHold == stats.onCadenceMaxHold -> "${stats.onCadenceMinHold}"
                // For example 90Hz: frames alternate between 1 and 2 refreshes, so both count as on-cadence
                else -> "${stats.onCadenceMinHold} or ${stats.onCadenceMaxHold} (expected hold is not a whole number, so the cadence alternates)"
            })
            line("display_hold_frames", stats.displayHoldFrames.takeIf { tracksCadence })
            line("display_hold_histogram", if (hasHolds) stats.formatHoldHistogram() else null)
            line("display_off_cadence_frames", stats.displayOffCadenceFrames.takeIf { tracksCadence })
            line("display_off_cadence_pct", decimal(if (tracksCadence && hasHolds) stats.displayOffCadenceFrames * 100.0 / stats.displayHoldFrames else null))
            line("display_judder_window_min_frames", JUDDER_WINDOW_MIN_FRAMES)
            line("display_judder_worst_10s_pct", decimal(stats.worstJudderWindowPct()))
            line("render_avg_ms", decimal(stats.averageRenderDurationNs()?.div(1e6), 3))
            line("render_max_ms", millis(stats.displayMaxRenderDuration.takeIf { stats.displaySamples > 0 }))

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
            line("thermal_status_final", stats.lastThermalStatus)
            line("thermal_status_max", stats.maxThermalStatus)

            return summary.toString()
        }

        private fun getDisplayRefreshRate(): Float? {
            return context.getSystemService<DisplayManager>()?.getDisplay(Display.DEFAULT_DISPLAY)?.refreshRate
        }
    }

    private class PssSnapshot(val total: Long, val java: Long, val native: Long, val graphics: Long)

    private interface LogDestination {
        val description: String
        fun openStream(fileName: String): OutputStream

        /**
         * Writes [content] to [fileName], replacing the previous contents if the file was already written by this destination.
         */
        fun writeReplacing(fileName: String, content: String)

        fun openWriter(fileName: String): BufferedWriter {
            return BufferedWriter(OutputStreamWriter(openStream(fileName), Charsets.UTF_8), 64 * 1024)
        }
    }

    private inner class DocumentTreeDestination(private val directory: DocumentFile) : LogDestination {
        override val description: String = directory.uri.toString()
        private val createdDocuments = mutableMapOf<String, Uri>()

        override fun openStream(fileName: String): OutputStream {
            val documentUri = createDocument(fileName)
            return context.contentResolver.openOutputStream(documentUri, "w") ?: throw IOException("Could not open $fileName")
        }

        override fun writeReplacing(fileName: String, content: String) {
            val existingDocument = createdDocuments[fileName]
            if (existingDocument != null) {
                // "wt" truncates the file. Some providers don't support it, in which case the document is recreated
                val outputStream = runCatching { context.contentResolver.openOutputStream(existingDocument, "wt") }.getOrNull()
                if (outputStream != null) {
                    outputStream.use { it.write(content.toByteArray(Charsets.UTF_8)) }
                    return
                }

                DocumentFile.fromSingleUri(context, existingDocument)?.delete()
                createdDocuments.remove(fileName)
            }

            openStream(fileName).use { it.write(content.toByteArray(Charsets.UTF_8)) }
        }

        private fun createDocument(fileName: String): Uri {
            // A generic MIME type prevents providers from changing the extension of the file
            val document = directory.createFile(LOG_FILE_MIME_TYPE, fileName) ?: throw IOException("Could not create $fileName")
            createdDocuments[fileName] = document.uri
            return document.uri
        }
    }

    private class FileDestination(private val directory: File) : LogDestination {
        override val description: String = directory.absolutePath

        override fun openStream(fileName: String): OutputStream {
            return File(directory, fileName).outputStream()
        }

        override fun writeReplacing(fileName: String, content: String) {
            File(directory, fileName).writeText(content, Charsets.UTF_8)
        }
    }

    private class SessionStatistics {
        var totalFrames = 0L
            private set
        var fastForwardFrames = 0L
            private set
        var afterPauseFrames = 0L
            private set
        var missedFrames = 0L
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
        var expectedHoldRefreshes: Double? = null
            private set
        // 0 when the refresh rate is unknown, in which case off-cadence frames aren't tracked
        var onCadenceMinHold = 0L
            private set
        var onCadenceMaxHold = 0L
            private set
        private val displayHoldHistogram = LongArray(HOLD_HISTOGRAM_SIZE)
        // 0 when the refresh rate is unknown, in which case holds aren't measured
        private var refreshPeriodNs = 0.0
        // Timestamp of the current frame's first sample. Only valid when its start was seen (not after a pause, fast-forward or an invalid frame)
        private var currentFrameStartNs = 0L
        private var hasCurrentFrameStart = false

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

        fun trackFrame(workDuration: Long, interval: Long, isFastForward: Boolean, isAfterPause: Boolean) {
            totalFrames++
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

        fun configureDisplayCadence(refreshRate: Float?) {
            val rate = refreshRate?.toDouble()?.takeIf { it > 0 } ?: return
            val expected = rate / TARGET_FPS
            refreshPeriodNs = 1e9 / rate
            expectedHoldRefreshes = expected
            val nearest = expected.roundToLong()
            if (abs(expected - nearest) <= HOLD_WHOLE_NUMBER_TOLERANCE) {
                onCadenceMinHold = maxOf(1L, nearest)
                onCadenceMaxHold = onCadenceMinHold
            } else {
                onCadenceMinHold = maxOf(1L, floor(expected).toLong())
                onCadenceMaxHold = maxOf(1L, ceil(expected).toLong())
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
        ): Long {
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
                    displaySkippedFrames += skippedFrames
                    currentRepeatRun = 0
                    if (hasCurrentFrameStart && !isFastForward && refreshPeriodNs > 0) {
                        previousHold = maxOf(1L, Math.round((timestamp - currentFrameStartNs) / refreshPeriodNs))
                        trackHold(previousHold, timestamp)
                    }
                    currentFrameStartNs = timestamp
                    hasCurrentFrameStart = true
                }
                else -> {
                    displayRepeatedFrames++
                    currentRepeatRun++
                    if (currentRepeatRun > displayLongestRepeatRun) {
                        displayLongestRepeatRun = currentRepeatRun
                    }
                }
            }

            // Frames shown during fast-forward don't follow the normal cadence, so holds that touch it aren't counted
            if (isFastForward) {
                hasCurrentFrameStart = false
            }
            return previousHold
        }

        private fun trackHold(hold: Long, timestamp: Long) {
            displayHoldFrames++
            displayHoldHistogram[minOf(hold, HOLD_HISTOGRAM_SIZE - 1L).toInt()]++

            if (onCadenceMinHold == 0L) {
                return
            }

            val isOffCadence = hold < onCadenceMinHold || hold > onCadenceMaxHold
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
            val lastListed = maxOf(4L, onCadenceMaxHold + 2).coerceAtMost(HOLD_HISTOGRAM_SIZE - 2L).toInt()
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

        fun averageRenderDurationNs(): Double? {
            return if (displaySamples > 0) displayRenderDurationSum.toDouble() / displaySamples else null
        }

        fun averageRss(): Double? {
            return if (memorySamples > 0) rssSum.toDouble() / memorySamples else null
        }
    }

    /**
     * Fixed-size histogram of durations, so that memory use doesn't grow with the session length. Values from 0 to 250 ms go into 0.05 ms buckets
     * and anything above goes into an overflow bucket. Count, sum, sum of squares, min and max are tracked exactly.
     */
    private class DurationHistogram {
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
}

/**
 * Appends a nanosecond value as milliseconds with 3 decimal places. Avoids String.format, which is too slow for thousands of lines per second.
 */
private fun StringBuilder.appendMillis(nanoseconds: Long): StringBuilder {
    val microseconds = (abs(nanoseconds) + 500) / 1000
    if (nanoseconds < 0 && microseconds > 0) {
        append('-')
    }
    append(microseconds / 1000).append('.')
    val fraction = (microseconds % 1000).toInt()
    if (fraction < 100) append('0')
    if (fraction < 10) append('0')
    append(fraction)
    return this
}
