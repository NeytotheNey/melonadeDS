package me.magnum.melonds.impl.perf

import android.content.Context
import android.hardware.display.DisplayManager
import android.os.Build
import android.os.Debug
import android.os.PowerManager
import android.system.Os
import android.system.OsConstants
import android.util.Log
import android.view.Display
import androidx.core.content.getSystemService
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
import me.magnum.melonds.extensions.find60HzMode
import me.magnum.melonds.extensions.getModesAtCurrentResolution
import me.magnum.melonds.impl.debug.DebugLog
import me.magnum.melonds.impl.debug.LogDestination
import me.magnum.melonds.impl.debug.LogFolder
import me.magnum.melonds.impl.perf.PerfCsv.appendFlag
import me.magnum.melonds.impl.perf.PerfCsv.appendMillis
import me.magnum.melonds.impl.perf.PerfCsv.appendSeconds
import me.magnum.melonds.impl.perf.PerfCsv.formatRefreshRate
import me.magnum.melonds.impl.perf.PerfCsv.toCsvField
import java.io.BufferedWriter
import java.io.File
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.ConcurrentLinkedQueue
import kotlin.math.abs

/**
 * Opt-in logger used to compare builds. While a session is active, it drains the frame samples captured by the emulator thread and the display
 * samples captured by the render thread about once per second, writes them to CSV files together with a memory sample (plus a PSS breakdown every
 * 30 s) and the events reported through [logEvent] and [logStateChange], and keeps a summary with aggregated statistics up to date. All work happens
 * on a background thread.
 */
class PerformanceLogger(
    private val context: Context,
    private val settingsRepository: SettingsRepository,
    private val logFolder: LogFolder,
) {

    companion object {
        private const val TAG = "PerformanceLogger"
        private const val DRAIN_INTERVAL_MS = 1000L
        private const val PSS_SAMPLE_INTERVAL_NS = 30_000_000_000L
        // The summary is rewritten every few drains so that a session that is killed without being stopped still has one
        private const val SUMMARY_INTERVAL_DRAINS = 5
        private const val PREVIOUS_SESSION_TIMEOUT_MS = 5000L
        private const val FRAME_DRAIN_BATCH_SIZE = 4096
        private const val DISPLAY_DRAIN_BATCH_SIZE = 1024
        private const val MAX_SESSION_NAME_LENGTH = 48
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
        DebugLog.currentStates().forEach { (key, value) -> session.queueEvent(key, value, session.startNs) }
        val previousSessionJob = lastSessionJob
        currentSession = session
        lastSessionJob = loggerScope.launch {
            // All sessions share the same native capture buffer, so a new session can only start once the previous one has been finalized
            if (previousSessionJob != null) {
                val previousSessionFinished = withTimeoutOrNull(PREVIOUS_SESSION_TIMEOUT_MS) { previousSessionJob.join() } != null
                if (!previousSessionFinished) {
                    Log.w(TAG, "Previous performance log session is taking too long to finish. Skipping session for $sessionName")
                    // Forgotten right away, so that isSessionActive() lets the caller start a new one later
                    clearSession(session)
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

    @Synchronized
    private fun clearSession(session: Session) {
        if (currentSession === session) {
            currentSession = null
        }
    }

    /**
     * Records an event (for example the activity being paused) in the active session's events file. Does nothing if there's no active session.
     * Cheap and safe to call from any thread: the event is only queued here and written by the logger's background thread.
     */
    @Synchronized
    fun logEvent(event: String, detail: String = "") {
        currentSession?.queueEvent(event, detail, System.nanoTime())
    }

    /**
     * Like [logEvent], but only records [value] if it differs from the last value reported for [key], and also logs it as a [DebugLog] state
     * change. The current value of every key is also written at the start of each session.
     */
    @Synchronized
    fun logStateChange(key: String, value: String) {
        if (DebugLog.stateChange(key, value)) {
            currentSession?.queueEvent(key, value, System.nanoTime())
        }
    }

    internal inner class Session(val sessionName: String) {
        private val stopSignal = CompletableDeferred<Unit>()
        val startDate = Date()
        val startNs = System.nanoTime()
        private val filePrefix = buildFilePrefix()

        private val frameBuffer = LongArray(FRAME_DRAIN_BATCH_SIZE * MelonEmulator.PERF_FRAME_SAMPLE_FIELDS)
        private val displayBuffer = LongArray(DISPLAY_DRAIN_BATCH_SIZE * DisplayPerformanceCapture.FIELDS)
        private val lineBuilder = StringBuilder(64 * 1024)
        private val powerManager = context.getSystemService<PowerManager>()
        private val pageSizeKb = Os.sysconf(OsConstants._SC_PAGESIZE) / 1024
        private val pendingEvents = ConcurrentLinkedQueue<PendingEvent>()
        private val audioEventTimestamps = LongArray(MelonEmulator.AUDIO_MAX_STREAM_EVENTS)
        private val audioEventNames = arrayOfNulls<String>(MelonEmulator.AUDIO_MAX_STREAM_EVENTS)
        private val audioEventDetails = arrayOfNulls<String>(MelonEmulator.AUDIO_MAX_STREAM_EVENTS)
        val audioTotalsAtStart = LongArray(MelonEmulator.AUDIO_TOTAL_COUNT)
        // Kept for the final summary, by which time the stream is usually closed
        var lastAudioStreamInfo: String? = null
            private set
        var wasAudioStreamOpenAtStart = false
            private set
        // The open size of the first stream that opened during the session, or -1. Only used if no stream was open at the start
        var firstAudioBufferSizeAtOpen = -1L
            private set

        val stats = SessionStatistics()
        var settingsSnapshot = emptyList<Pair<String, String>>()
            private set
        // The rate the display reported at session start, used when no display sample was counted
        var displayRefreshRate: Float? = null
            private set
        // Re-read at session start and before every periodic summary. Not re-read for the final summary, since by then the emulator screen may be
        // gone and the display back at its normal rate
        var lastDisplayState: DisplayStateSnapshot? = null
            private set
        var startMemorySnapshot: PssSnapshot? = null
            private set
        var endMemorySnapshot: PssSnapshot? = null
            private set

        var destination: LogDestination? = null
            private set
        private var framesWriter: BufferedWriter? = null
        private var displayWriter: BufferedWriter? = null
        private var memoryWriter: BufferedWriter? = null
        private var eventsWriter: BufferedWriter? = null

        fun queueEvent(event: String, detail: String, timestampNs: Long) {
            pendingEvents.add(PendingEvent(timestampNs, event, detail))
        }

        fun requestStop() {
            stopSignal.complete(Unit)
        }

        fun isStopRequested(): Boolean {
            return stopSignal.isCompleted
        }

        suspend fun run() {
            // Full PSS breakdown. This reads /proc/self/smaps while holding the process' mmap lock, which can stall the emulator thread (the JIT
            // maps and protects memory). So apart from the samples before capture starts and after it ends, it's only taken every 30 s, and the
            // time each sample took is logged so that the frames it stalled can be told apart
            startMemorySnapshot = takePssSnapshot()

            // Start capturing straight away so that start-up frames are kept. Samples wait in the buffers until the first drain
            MelonEmulator.setPerformanceCaptureEnabled(true)
            DisplayPerformanceCapture.setEnabled(true)
            MelonEmulator.getAudioDiagnosticsTotals(audioTotalsAtStart)
            wasAudioStreamOpenAtStart = MelonEmulator.getAudioStreamInfo() != null
            firstAudioBufferSizeAtOpen = -1L

            try {
                settingsSnapshot = buildSettingsSnapshot()
                displayRefreshRate = getDefaultDisplay()?.refreshRate
                stats.setInitialRefreshRate(displayRefreshRate)
                sampleDisplayState()

                val logDestination = logFolder.resolve()
                destination = logDestination
                framesWriter = logDestination.openWriter("${filePrefix}_frames.csv").apply {
                    write("frame,t_ms,work_ms,sleep_ms,interval_ms,lines,fast_forward,after_pause,paced,audio_fill,audio_silence,audio_full,audio_skew_ppm,audio_ff_speed\n")
                }
                displayWriter = logDestination.openWriter("${filePrefix}_display.csv").apply {
                    write("t_ms,render_ms,valid,new_frame,skipped_frames,paused,ds_sleep,fast_forward,prev_hold_refreshes,refresh_period_ms\n")
                }
                memoryWriter = logDestination.openWriter("${filePrefix}_memory.csv").apply {
                    // The PSS columns are only set on the rows where a PSS sample was taken
                    write("t_s,rss_kb,native_heap_alloc_kb,java_heap_used_kb,thermal_status,")
                    write("pss_total_kb,pss_graphics_kb,pss_native_kb,pss_java_kb,pss_private_other_kb,pss_sample_ms\n")
                }
                eventsWriter = logDestination.openWriter("${filePrefix}_events.csv").apply {
                    write("t_s,event,detail\n")
                }
                // The first row carries the PSS sample taken just before capture started
                sampleMemory(startMemorySnapshot)

                var drainCount = 0
                var stopRequested = false
                var nextPssSampleNs = startNs + PSS_SAMPLE_INTERVAL_NS
                while (!stopRequested) {
                    stopRequested = withTimeoutOrNull(DRAIN_INTERVAL_MS) { stopSignal.await() } != null
                    if (stopRequested) {
                        // Stop capturing before the final drain so that nothing is left behind in the buffers
                        MelonEmulator.setPerformanceCaptureEnabled(false)
                        DisplayPerformanceCapture.setEnabled(false)
                    }

                    drainFrameSamples()
                    drainDisplaySamples()
                    drainEvents()
                    drainAudioEvents()
                    MelonEmulator.getAudioStreamInfo()?.let { lastAudioStreamInfo = it }
                    val now = System.nanoTime()
                    val pssSnapshot = when {
                        // Capture has already stopped, so this one can't stall a logged frame
                        stopRequested -> takePssSnapshot().also { endMemorySnapshot = it }
                        now >= nextPssSampleNs -> {
                            nextPssSampleNs = now + PSS_SAMPLE_INTERVAL_NS
                            takePssSnapshot()
                        }
                        else -> null
                    }
                    sampleMemory(pssSnapshot)
                    flushWriters()

                    drainCount++
                    if (!stopRequested && drainCount % SUMMARY_INTERVAL_DRAINS == 0) {
                        sampleDisplayState()
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

            // Already taken by the final drain, unless the session failed before it
            if (endMemorySnapshot == null) {
                endMemorySnapshot = takePssSnapshot()
            }
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
                    val isPacingLocked = (flags and MelonEmulator.PERF_FRAME_FLAG_PACING_LOCKED) != 0L
                    val pacingDivisor = ((flags shr MelonEmulator.PERF_FRAME_PACING_DIVISOR_SHIFT) and MelonEmulator.PERF_FRAME_PACING_DIVISOR_MASK).toInt()
                    val isAudioBufferFull = (flags and MelonEmulator.PERF_FRAME_FLAG_AUDIO_BUFFER_FULL) != 0L
                    val audioFill = frameBuffer[offset + 6]
                    val audioSilence = frameBuffer[offset + 7]
                    val audioSkewPpm = frameBuffer[offset + 8]
                    val audioFastForwardSpeedMilli = frameBuffer[offset + 9]

                    lineBuilder.append(stats.totalFrames).append(',')
                        .appendMillis(timestamp - startNs).append(',')
                        .appendMillis(workDuration).append(',')
                        .appendMillis(sleepDuration).append(',')
                        .appendMillis(interval).append(',')
                        .append(lines).append(',')
                        .appendFlag(isFastForward).append(',')
                        .appendFlag(isAfterPause).append(',')
                        .appendFlag(isPacingLocked).append(',')
                    // Left blank on frames that ran before the audio diagnostics turned on
                    if (audioFill >= 0) {
                        lineBuilder.append(audioFill)
                    }
                    lineBuilder.append(',')
                        .append(audioSilence).append(',')
                        .appendFlag(isAudioBufferFull).append(',')
                    if (audioFill >= 0) {
                        lineBuilder.append(audioSkewPpm)
                    }
                    lineBuilder.append(',')
                    // The speed paced fast-forward really reached, blank when unknown. Thousandths, so they're written like milliseconds
                    if (audioFill >= 0 && audioFastForwardSpeedMilli >= 0) {
                        lineBuilder.appendMillis(audioFastForwardSpeedMilli * 1000)
                    }
                    lineBuilder.append('\n')

                    stats.trackFrame(workDuration, interval, isFastForward, isAfterPause, isPacingLocked, pacingDivisor)
                    stats.trackAudioFrame(audioFill, audioSilence, isAudioBufferFull, audioSkewPpm, isFastForward, isFastForward || isAfterPause)
                    stats.trackAudioFastForwardSpeed(audioFastForwardSpeedMilli, isFastForward && audioFill >= 0)
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
                    val refreshPeriod = displayBuffer[offset + 4]
                    val isValid = (flags and DisplayPerformanceCapture.FLAG_VALID) != 0L
                    val isNewFrame = (flags and DisplayPerformanceCapture.FLAG_NEW_FRAME) != 0L
                    val isPaused = (flags and DisplayPerformanceCapture.FLAG_EMULATION_PAUSED) != 0L
                    val isDsSleeping = (flags and DisplayPerformanceCapture.FLAG_DS_SLEEP) != 0L
                    val isFastForward = (flags and DisplayPerformanceCapture.FLAG_FAST_FORWARD) != 0L

                    val previousHold = stats.trackDisplayFrame(timestamp, renderDuration, isValid, isNewFrame, skippedFrames, isPaused, isDsSleeping, isFastForward, refreshPeriod)
                    if (stats.refreshRateChangedOnLastSample) {
                        stats.currentRefreshRate?.let { queueEvent("display_refresh_rate", formatRefreshRate(it.refreshRate), timestamp) }
                    }

                    lineBuilder.appendMillis(timestamp - startNs).append(',')
                        .appendMillis(renderDuration).append(',')
                        .appendFlag(isValid).append(',')
                        .appendFlag(isNewFrame).append(',')
                        .append(skippedFrames).append(',')
                        .appendFlag(isPaused).append(',')
                        .appendFlag(isDsSleeping).append(',')
                        .appendFlag(isFastForward).append(',')
                    // Only set on new frames whose previous frame's hold was measured
                    if (previousHold > 0) {
                        lineBuilder.append(previousHold)
                    }
                    lineBuilder.append(',')
                    // The vsync period the sample was rendered at. Blank when unknown
                    if (refreshPeriod > 0) {
                        lineBuilder.appendMillis(refreshPeriod)
                    }
                    lineBuilder.append('\n')
                }
                writer.append(lineBuilder)
            } while (count == DISPLAY_DRAIN_BATCH_SIZE)
        }

        private fun drainEvents() {
            val writer = eventsWriter ?: return

            lineBuilder.setLength(0)
            while (true) {
                val event = pendingEvents.poll() ?: break
                lineBuilder.appendSeconds(maxOf(0L, event.timestampNs - startNs)).append(',')
                    .append(event.event.toCsvField()).append(',')
                    .append(event.detail.toCsvField()).append('\n')
            }
            writer.append(lineBuilder)
        }

        private fun drainAudioEvents() {
            val writer = eventsWriter ?: return

            val count = MelonEmulator.drainAudioStreamEvents(audioEventTimestamps, audioEventNames, audioEventDetails)
            lineBuilder.setLength(0)
            for (i in 0 until count) {
                lineBuilder.appendSeconds(maxOf(0L, audioEventTimestamps[i] - startNs)).append(',')
                    .append(audioEventNames[i].orEmpty().toCsvField()).append(',')
                    .append(audioEventDetails[i].orEmpty().toCsvField()).append('\n')
                // With no stream open at the start, the first stream's facts give the buffer size the session started with
                if (!wasAudioStreamOpenAtStart && firstAudioBufferSizeAtOpen < 0 && audioEventNames[i] == "audio_stream") {
                    firstAudioBufferSizeAtOpen = parseAudioStreamInfo(audioEventDetails[i])["buffer_size"]?.toLongOrNull() ?: -1L
                }
            }
            writer.append(lineBuilder)
        }

        /**
         * Writes a memory row. [pssSnapshot] fills the PSS columns, which are left empty if it's null.
         */
        private fun sampleMemory(pssSnapshot: PssSnapshot?) {
            val writer = memoryWriter ?: return

            // Only cheap sources here. /proc/self/statm reads memory counters without walking the memory mappings
            val rss = readResidentSetSizeKb()
            val nativeHeapAllocated = Debug.getNativeHeapAllocatedSize() / 1024
            val runtime = Runtime.getRuntime()
            val javaHeapUsed = (runtime.totalMemory() - runtime.freeMemory()) / 1024
            val thermalStatus = getThermalStatus()

            lineBuilder.setLength(0)
            lineBuilder.appendSeconds(System.nanoTime() - startNs).append(',')
                .append(rss).append(',')
                .append(nativeHeapAllocated).append(',')
                .append(javaHeapUsed).append(',')
                .append(thermalStatus).append(',')
            if (pssSnapshot != null) {
                lineBuilder.append(pssSnapshot.total).append(',')
                    .append(pssSnapshot.graphics).append(',')
                    .append(pssSnapshot.native).append(',')
                    .append(pssSnapshot.java).append(',')
                    .append(pssSnapshot.privateOther).append(',')
                    .appendMillis(pssSnapshot.durationNs)
            } else {
                lineBuilder.append(",,,,,")
            }
            lineBuilder.append('\n')
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

        /**
         * Takes a full PSS breakdown and adds it to the statistics. It can take tens of ms, so it must only run on the logger's thread.
         */
        private fun takePssSnapshot(): PssSnapshot {
            val sampleStartNs = System.nanoTime()
            val memoryInfo = Debug.MemoryInfo()
            Debug.getMemoryInfo(memoryInfo)
            val snapshot = PssSnapshot(
                total = memoryInfo.totalPss.toLong(),
                java = memoryInfo.getMemoryStat("summary.java-heap")?.toLongOrNull() ?: -1L,
                native = memoryInfo.getMemoryStat("summary.native-heap")?.toLongOrNull() ?: -1L,
                graphics = memoryInfo.getMemoryStat("summary.graphics")?.toLongOrNull() ?: -1L,
                privateOther = memoryInfo.getMemoryStat("summary.private-other")?.toLongOrNull() ?: -1L,
                durationNs = System.nanoTime() - sampleStartNs,
            )
            stats.trackPss(snapshot)
            return snapshot
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
            eventsWriter?.flush()
        }

        private fun closeWriters() {
            listOf(framesWriter, displayWriter, memoryWriter, eventsWriter).forEach {
                try {
                    it?.close()
                } catch (e: IOException) {
                    Log.w(TAG, "Failed to close performance log file", e)
                }
            }
            framesWriter = null
            displayWriter = null
            memoryWriter = null
            eventsWriter = null
        }

        private fun writeSummary(isComplete: Boolean) {
            val logDestination = destination ?: return
            try {
                logDestination.writeReplacing("${filePrefix}_summary.txt", buildSummary(context, isComplete))
            } catch (e: Exception) {
                Log.e(TAG, "Failed to write performance log summary", e)
            }
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
                    "ds_screen_colors" to rendererConfiguration.dsScreenColors.toString(),
                    "screen_shadow" to rendererConfiguration.screenShadow.toString(),
                    "threaded_rendering" to rendererConfiguration.threadedRendering.toString(),
                    "threaded_2d_rendering" to rendererConfiguration.threaded2dRendering.toString(),
                    "hires_capture" to rendererConfiguration.hiresCapture.toString(),
                    "jit_enabled" to settingsRepository.isJitEnabled().toString(),
                    "sustained_performance_mode" to settingsRepository.isSustainedPerformanceModeEnabled().toString(),
                    "frame_pacing_vsync" to settingsRepository.isFramePacingVsyncEnabled().toString(),
                    "frame_pacing_lock_60hz" to settingsRepository.isFramePacingLock60HzEnabled().toString(),
                    "frame_pacing_buffer" to settingsRepository.isFramePacingBufferEnabled().toString(),
                    "fast_forward_multiplier" to settingsRepository.getFastForwardSpeedMultiplier().toString(),
                    "default_console_type" to settingsRepository.getDefaultConsoleType().name,
                    "sound_enabled" to settingsRepository.isSoundEnabled().toString(),
                    "rewind_enabled" to settingsRepository.isRewindEnabled().toString(),
                )
            } catch (e: Exception) {
                Log.w(TAG, "Failed to read settings for the performance log summary", e)
                emptyList()
            }
        }

        /**
         * Splits MelonEmulator.getAudioStreamInfo()'s "key=value" pairs.
         */
        fun parseAudioStreamInfo(info: String?): Map<String, String> {
            if (info.isNullOrEmpty()) {
                return emptyMap()
            }
            return info.split(' ').mapNotNull { pair ->
                val separator = pair.indexOf('=')
                if (separator > 0) pair.substring(0, separator) to pair.substring(separator + 1) else null
            }.toMap()
        }

        private fun sampleDisplayState() {
            val display = getDefaultDisplay() ?: return
            lastDisplayState = DisplayStateSnapshot(
                refreshRate = display.refreshRate,
                refreshRatesAtCurrentResolution = display.getModesAtCurrentResolution().map { it.refreshRate }.distinct().sorted(),
                isLock60HzEnabled = settingsRepository.isFramePacingLock60HzEnabled(),
                lock60HzMode = display.find60HzMode(),
            )
        }

        private fun getDefaultDisplay(): Display? {
            return context.getSystemService<DisplayManager>()?.getDisplay(Display.DEFAULT_DISPLAY)
        }
    }

    private class PendingEvent(val timestampNs: Long, val event: String, val detail: String)

    internal class DisplayStateSnapshot(
        val refreshRate: Float,
        val refreshRatesAtCurrentResolution: List<Float>,
        val isLock60HzEnabled: Boolean,
        val lock60HzMode: Display.Mode?,
    )
}

/**
 * Formatting shared by the CSV writers and the summary.
 */
internal object PerfCsv {

    fun formatRefreshRate(refreshRate: Double): String {
        return String.format(Locale.US, "%.2f", refreshRate)
    }

    /**
     * Keeps a value in one CSV field. Event names and details are short labels, so separators are simply replaced.
     */
    fun String.toCsvField(): String {
        return replace(',', ';').replace('\n', ' ').replace('\r', ' ')
    }

    /**
     * Appends a nanosecond value as milliseconds with 3 decimal places. Avoids String.format, which is too slow for thousands of lines per second.
     */
    fun StringBuilder.appendMillis(nanoseconds: Long): StringBuilder {
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

    /**
     * Appends a nanosecond value as seconds with 3 decimal places: the elapsed microseconds formatted as "milliseconds".
     */
    fun StringBuilder.appendSeconds(nanoseconds: Long): StringBuilder {
        return appendMillis(nanoseconds / 1000)
    }

    fun StringBuilder.appendFlag(flag: Boolean): StringBuilder {
        return append(if (flag) '1' else '0')
    }
}
