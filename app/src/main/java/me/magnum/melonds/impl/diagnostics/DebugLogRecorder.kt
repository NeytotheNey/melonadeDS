package me.magnum.melonds.impl.diagnostics

import android.content.Context
import android.os.Process
import android.os.SystemClock
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import me.magnum.melonds.domain.repositories.SettingsRepository
import java.io.BufferedWriter
import java.io.IOException
import java.io.OutputStreamWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.UUID
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.ScheduledThreadPoolExecutor
import java.util.concurrent.ThreadFactory
import java.util.concurrent.TimeUnit

/**
 * Records this process' logcat to melonade_debug_<timestamp>.txt in the log folder while the debug log setting is on, flushing whenever logcat
 * catches up or sends an error line (at most every [MIN_FLUSH_GAP_MS]) so that a hang, crash or kill keeps the lines so far without a file write
 * per line. Its threads (and the logcat process) run at a slightly lower priority than normal.
 */
class DebugLogRecorder(
    private val context: Context,
    private val settingsRepository: SettingsRepository,
    private val logFolder: LogFolder,
    private val exitReasonReporter: ExitReasonReporter,
) {

    private companion object {
        const val TAG = "DebugLogRecorder"
        // The longest lines wait to be flushed while logcat keeps sending
        const val FLUSH_INTERVAL_MS = 500L
        const val MIN_FLUSH_GAP_MS = 75L
        const val STOP_TIMEOUT_MS = 500L
        // Nice 4: lower than the UI and emulator threads, but under THREAD_PRIORITY_BACKGROUND (10), from which setThreadPriority also moves the
        // thread (and so the logcat process it starts) into the background group, where logcat can fall behind and logd prune lines
        const val RECORDER_THREAD_PRIORITY = Process.THREAD_PRIORITY_DEFAULT + 4 * Process.THREAD_PRIORITY_LESS_FAVORABLE
        // The buffers an app can read its own lines from
        const val LOGCAT_BUFFERS = "main,system,crash"
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var session: Session? = null

    /**
     * Starts recording if the setting is on and follows it from then on; call once from the main thread at app start.
     */
    fun start() {
        setRecording(settingsRepository.isDebugLogEnabled())
        // The flow is created here because the settings repository's flow map isn't thread safe
        val enabledFlow = settingsRepository.observeDebugLogEnabled()
        scope.launch {
            enabledFlow.distinctUntilChanged().collect { setRecording(it) }
        }
    }

    @Synchronized
    private fun setRecording(enabled: Boolean) {
        if (enabled == (session != null)) {
            return
        }

        if (enabled) {
            DebugLog.setEnabled(true)
            session = Session().also { it.start() }
            // Covers both a crash in the previous run and the debug log being turned on right after a problem
            scope.launch { exitReasonReporter.reportNewExits() }
        } else {
            DebugLog.event { "Debug log turned off" }
            // Turned off first, since the native side writes out its queued lines and drop counts then; the session then stops once logcat
            // has passed its stop marker, logged after them
            DebugLog.setEnabled(false)
            session?.requestStop()
            session = null
        }
    }

    private inner class Session : Thread("DebugLogRecorder") {
        private var logcatProcess: java.lang.Process? = null
        private var isStopRequested = false
        // Logged once the native side has written out its last lines, so that the read loop knows it has everything
        @Volatile
        private var stopMarker: String? = null

        /**
         * Logs a stop marker and lets the read loop run until logcat passes it, so that the lines logged before this call make it to the file;
         * logcat is stopped after [STOP_TIMEOUT_MS] if the marker never shows up.
         */
        fun requestStop() {
            val marker = "Debug log recording stops here (${UUID.randomUUID()})"
            val isLogcatRunning = synchronized(this) {
                isStopRequested = true
                stopMarker = marker
                logcatProcess != null
            }
            // Otherwise recordLogcat() stops it as soon as it starts
            if (!isLogcatRunning) {
                return
            }

            Log.i(TAG, marker)
            scope.launch {
                delay(STOP_TIMEOUT_MS)
                synchronized(this@Session) {
                    // Ends the read loop, since the process' output closes
                    logcatProcess?.destroy()
                }
            }
        }

        override fun run() {
            // Set before logcat starts, so that the process inherits it
            Process.setThreadPriority(RECORDER_THREAD_PRIORITY)
            val startDate = Date()
            val fileName = "melonade_debug_${SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(startDate)}.txt"
            val logFile = try {
                logFolder.createFile(fileName)
            } catch (e: Exception) {
                Log.e(TAG, "Could not create the debug log file", e)
                return
            }

            try {
                BufferedWriter(OutputStreamWriter(logFile, Charsets.UTF_8), 32 * 1024).use { writer ->
                    val command = listOf("logcat", "-v", "threadtime", "--pid=${Process.myPid()}", "-b", LOGCAT_BUFFERS)
                    writer.write(buildHeader(startDate, command.joinToString(" ")))
                    writer.flush()
                    recordLogcat(command, writer)
                    writer.write("--- recording stopped at ${formatDate(Date())} ---\n")
                }
                Log.i(TAG, "Debug log written to $fileName")
            } catch (e: IOException) {
                Log.e(TAG, "Failed to write the debug log", e)
            }
        }

        private fun recordLogcat(command: List<String>, writer: BufferedWriter) {
            val process = try {
                ProcessBuilder(command).redirectErrorStream(true).start()
            } catch (e: IOException) {
                writer.write("Could not start logcat: $e\n")
                return
            }

            synchronized(this) {
                logcatProcess = process
                if (isStopRequested) {
                    process.destroy()
                }
            }

            // Every write to the file and these three hold the writer's lock, which the flusher thread takes too
            var hasUnflushedLines = false
            var lastFlushMs = SystemClock.uptimeMillis()
            var deferredFlush: ScheduledFuture<*>? = null
            val flusher = ScheduledThreadPoolExecutor(1, ThreadFactory { task ->
                Thread({
                    Process.setThreadPriority(RECORDER_THREAD_PRIORITY)
                    task.run()
                }, "DebugLogFlusher")
            }).apply {
                // A deferred flush still waiting when the session ends is dropped, since closing the file flushes anyway
                executeExistingDelayedTasksAfterShutdownPolicy = false
            }

            fun flushLocked() {
                if (hasUnflushedLines) {
                    hasUnflushedLines = false
                    writer.flush()
                }
                lastFlushMs = SystemClock.uptimeMillis()
                deferredFlush = null
            }

            try {
                // logcat first prints what the buffers still hold for this process (usually everything since the app started), then follows it
                val reader = process.inputStream.bufferedReader()
                while (true) {
                    val line = reader.readLine() ?: break
                    val marker = stopMarker
                    if (marker != null && line.contains(marker)) {
                        break
                    }

                    synchronized(writer) {
                        writer.write(line)
                        writer.write("\n")
                        hasUnflushedLines = true

                        // Flushes once logcat has caught up (a crash or kill keeps everything up to then) and after an error or fatal line (a
                        // crash's stack trace), at most every MIN_FLUSH_GAP_MS so that spam isn't a file write per line, with a flush on the
                        // flusher thread for one that comes too soon; and every FLUSH_INTERVAL_MS while logcat keeps sending
                        val now = SystemClock.uptimeMillis()
                        if (!reader.ready() || isErrorOrFatal(line) || now - lastFlushMs >= FLUSH_INTERVAL_MS) {
                            if (now - lastFlushMs >= MIN_FLUSH_GAP_MS) {
                                flushLocked()
                            } else if (deferredFlush == null) {
                                deferredFlush = flusher.schedule(Runnable {
                                    synchronized(writer) {
                                        // A failed flush shows up again on the reader's next write
                                        try {
                                            flushLocked()
                                        } catch (e: IOException) {
                                        }
                                    }
                                }, lastFlushMs + MIN_FLUSH_GAP_MS - now, TimeUnit.MILLISECONDS)
                            }
                        }
                    }
                }
            } catch (e: IOException) {
                // Destroying the process can end the read with an exception instead of EOF
                if (!synchronized(this) { isStopRequested }) {
                    synchronized(writer) {
                        writer.write("logcat output failed: $e\n")
                    }
                }
            } finally {
                // Waits for a flush in progress, so the caller has the writer to itself after this
                flusher.shutdown()
                flusher.awaitTermination(1, TimeUnit.SECONDS)
                process.destroy()
            }
        }

        /**
         * Whether a threadtime line ("MM-DD HH:MM:SS.mmm  PID  TID P TAG: message") has the E or F priority.
         */
        private fun isErrorOrFatal(line: String): Boolean {
            var index = 0
            repeat(4) {
                while (index < line.length && line[index] == ' ') index++
                while (index < line.length && line[index] != ' ') index++
            }
            while (index < line.length && line[index] == ' ') index++
            return index + 1 < line.length && line[index + 1] == ' ' && (line[index] == 'E' || line[index] == 'F')
        }
    }

    private fun buildHeader(startDate: Date, logcatCommand: String): String {
        return buildString {
            append("melonadeDS debug log\n")
            append("started: ").append(formatDate(startDate)).append('\n')
            (DiagnosticsInfo.app(context) + DiagnosticsInfo.device()).forEach { (key, value) ->
                append(key).append(": ").append(value ?: "n/a").append('\n')
            }
            append("pid: ").append(Process.myPid()).append('\n')
            append("command: ").append(logcatCommand).append('\n')
            append("tags: ").append(LogTags.described.joinToString(", ") { (tag, contents) -> "$tag ($contents)" }).append('\n')
            append("---\n")
        }
    }

    private fun formatDate(date: Date): String {
        return SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS Z", Locale.US).format(date)
    }
}
