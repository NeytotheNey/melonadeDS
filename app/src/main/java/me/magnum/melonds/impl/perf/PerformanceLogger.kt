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
import androidx.annotation.VisibleForTesting
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
import me.magnum.melonds.domain.model.EMULATOR_TARGET_FPS
import me.magnum.melonds.domain.repositories.SettingsRepository
import me.magnum.melonds.extensions.find60HzMode
import me.magnum.melonds.extensions.getModesAtCurrentResolution
import me.magnum.melonds.utils.PackageManagerCompat
import java.io.BufferedWriter
import java.io.File
import java.io.IOException
import java.io.OutputStream
import java.io.OutputStreamWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.ConcurrentLinkedQueue
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.roundToLong
import kotlin.math.sqrt

/**
 * Opt-in logger used to compare builds. While a session is active, it drains the frame samples captured by the emulator thread and the display
 * samples captured by the render thread about once per second, writes them to CSV files together with a memory sample (plus a PSS breakdown every
 * 30 s) and the events reported through [logEvent] and [logStateChange], and keeps a summary with aggregated statistics up to date. All work happens
 * on a background thread.
 */
class PerformanceLogger(
    private val context: Context,
    private val settingsRepository: SettingsRepository,
) {

    companion object {
        private const val TAG = "PerformanceLogger"
        private const val LOG_DIRECTORY_NAME = "perf_logs"
        private const val DRAIN_INTERVAL_MS = 1000L
        private const val PSS_SAMPLE_INTERVAL_NS = 30_000_000_000L
        // The summary is rewritten every few drains so that a session that is killed without being stopped still has one
        private const val SUMMARY_INTERVAL_DRAINS = 5
        private const val PREVIOUS_SESSION_TIMEOUT_MS = 5000L
        private const val FRAME_DRAIN_BATCH_SIZE = 4096
        private const val DISPLAY_DRAIN_BATCH_SIZE = 1024
        private const val TARGET_FPS = EMULATOR_TARGET_FPS.toDouble()
        // Rounded to the nearest ns
        private const val TARGET_FRAME_INTERVAL_NS = (1_000_000_000L + EMULATOR_TARGET_FPS / 2) / EMULATOR_TARGET_FPS
        private const val MISSED_FRAME_THRESHOLD_NS = TARGET_FRAME_INTERVAL_NS * 3 / 2
        // Holds from 1 to 31 refreshes get their own bucket. The last bucket counts 32 and above
        private const val HOLD_HISTOGRAM_SIZE = 33
        // If refresh_hz / 60 is within this of a whole number, every frame is expected to be held exactly that many refreshes
        private const val HOLD_WHOLE_NUMBER_TOLERANCE = 0.05
        // Refresh rates within this fraction of each other are taken as the same rate, so that small changes in the vsync period aren't a change
        private const val REFRESH_RATE_MATCH_TOLERANCE = 0.03
        private const val JUDDER_WINDOW_SECONDS = 10
        // A window is only evaluated once it holds at least 80% of the holds a full window should have at 60 FPS (480)
        private const val JUDDER_WINDOW_MIN_FRAMES = JUDDER_WINDOW_SECONDS * EMULATOR_TARGET_FPS * 4 / 5
        private const val MAX_SESSION_NAME_LENGTH = 48
        // The SPU output ring between the emulator and the audio callback (AudioDiagnostics.h)
        private const val AUDIO_BUFFER_CAPACITY_FRAMES = 2048
        private const val AUDIO_BUFFER_SAMPLE_RATE = 48000
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
    // Last value reported for each key through logStateChange, kept across sessions so that a new session can start with the current state
    private val lastStates = LinkedHashMap<String, String>()

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
        lastStates.forEach { (key, value) -> session.queueEvent(key, value, session.startNs) }
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

    /**
     * Records an event (for example the activity being paused) in the active session's events file. Does nothing if there's no active session.
     * Cheap and safe to call from any thread: the event is only queued here and written by the logger's background thread.
     */
    @Synchronized
    fun logEvent(event: String, detail: String = "") {
        currentSession?.queueEvent(event, detail, System.nanoTime())
    }

    /**
     * Like [logEvent], but only records [value] if it differs from the last value reported for [key]. The current value of every key is also
     * written at the start of each session.
     */
    @Synchronized
    fun logStateChange(key: String, value: String) {
        if (lastStates[key] == value) {
            return
        }
        lastStates[key] = value
        currentSession?.queueEvent(key, value, System.nanoTime())
    }

    private inner class Session(private val sessionName: String) {
        private val stopSignal = CompletableDeferred<Unit>()
        private val startDate = Date()
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
        private val audioTotalsAtStart = LongArray(MelonEmulator.AUDIO_TOTAL_COUNT)
        // Kept for the final summary, by which time the stream is usually closed
        private var lastAudioStreamInfo: String? = null
        private var wasAudioStreamOpenAtStart = false
        // The open size of the first stream that opened during the session, or -1. Only used if no stream was open at the start
        private var firstAudioBufferSizeAtOpen = -1L

        private val stats = SessionStatistics()
        private var settingsSnapshot = emptyList<Pair<String, String>>()
        // The rate the display reported at session start. Only used for the display stats until the display samples carry their own vsync period
        private var displayRefreshRate: Float? = null
        // Re-read at session start and before every periodic summary. Not re-read for the final summary, since by then the emulator screen may be
        // gone and the display back at its normal rate
        private var lastDisplayState: DisplayStateSnapshot? = null
        private var startMemorySnapshot: PssSnapshot? = null
        private var endMemorySnapshot: PssSnapshot? = null

        private var destination: LogDestination? = null
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

                val logDestination = resolveDestination()
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
                        .append(if (isFastForward) '1' else '0').append(',')
                        .append(if (isAfterPause) '1' else '0').append(',')
                        .append(if (isPacingLocked) '1' else '0').append(',')
                    // Left blank on frames that ran before the audio diagnostics turned on
                    if (audioFill >= 0) {
                        lineBuilder.append(audioFill)
                    }
                    lineBuilder.append(',')
                        .append(audioSilence).append(',')
                        .append(if (isAudioBufferFull) '1' else '0').append(',')
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
                // Elapsed microseconds formatted as "milliseconds" gives seconds with 3 decimal places
                lineBuilder.appendMillis(maxOf(0L, event.timestampNs - startNs) / 1000).append(',')
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
                // Elapsed microseconds formatted as "milliseconds" gives seconds with 3 decimal places
                lineBuilder.appendMillis(maxOf(0L, audioEventTimestamps[i] - startNs) / 1000).append(',')
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
            // Elapsed microseconds formatted as "milliseconds" gives seconds with 3 decimal places
            lineBuilder.appendMillis((System.nanoTime() - startNs) / 1000).append(',')
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
            line("missed_frames_threshold_ms", millis(MISSED_FRAME_THRESHOLD_NS))
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
            line("display_judder_window_min_frames", JUDDER_WINDOW_MIN_FRAMES)
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

        /**
         * Splits MelonEmulator.getAudioStreamInfo()'s "key=value" pairs.
         */
        private fun parseAudioStreamInfo(info: String?): Map<String, String> {
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

    internal class PssSnapshot(val total: Long, val java: Long, val native: Long, val graphics: Long, val privateOther: Long, val durationNs: Long)

    private class PendingEvent(val timestampNs: Long, val event: String, val detail: String)

    private class DisplayStateSnapshot(
        val refreshRate: Float,
        val refreshRatesAtCurrentResolution: List<Float>,
        val isLock60HzEnabled: Boolean,
        val lock60HzMode: Display.Mode?,
    )

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

    @VisibleForTesting
    internal class SessionStatistics {
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
        val expectedHold = refreshRate / TARGET_FPS
        // A display faster than 60Hz has to show some frames twice even with perfect pacing
        val expectedRepeatRatio = maxOf(0.0, 1.0 - TARGET_FPS / refreshRate)
        val expectedLongestRepeatRun = maxOf(0L, ceil(expectedHold - 1e-3).toLong() - 1)
        // If the expected hold isn't a whole number (for example 90Hz), frames alternate between the two nearest holds, so both are on-cadence
        val onCadenceMinHold: Long
        val onCadenceMaxHold: Long
        var countedSamples = 0L
        var holdFrames = 0L

        init {
            val nearest = expectedHold.roundToLong()
            if (abs(expectedHold - nearest) <= HOLD_WHOLE_NUMBER_TOLERANCE) {
                onCadenceMinHold = maxOf(1L, nearest)
                onCadenceMaxHold = onCadenceMinHold
            } else {
                onCadenceMinHold = maxOf(1L, floor(expectedHold).toLong())
                onCadenceMaxHold = maxOf(1L, ceil(expectedHold).toLong())
            }
        }

        fun matches(rate: Double): Boolean {
            return abs(rate - refreshRate) <= refreshRate * REFRESH_RATE_MATCH_TOLERANCE
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
}

private fun formatRefreshRate(refreshRate: Double): String {
    return String.format(Locale.US, "%.2f", refreshRate)
}

/**
 * Keeps a value in one CSV field. Event names and details are short labels, so separators are simply replaced.
 */
private fun String.toCsvField(): String {
    return replace(',', ';').replace('\n', ' ').replace('\r', ' ')
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
