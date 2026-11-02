package me.magnum.melonds.impl.diagnostics

import android.content.Context
import android.os.Process
import android.os.SystemClock
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import me.magnum.melonds.domain.repositories.SettingsRepository
import java.io.BufferedWriter
import java.io.IOException
import java.io.OutputStreamWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Records this process' logcat to melonade_debug_<timestamp>.txt in the log folder while the debug log setting is on, flushing often enough
 * that a hang or kill keeps the lines so far.
 */
class DebugLogRecorder(
    private val context: Context,
    private val settingsRepository: SettingsRepository,
    private val logFolder: LogFolder,
    private val exitReasonReporter: ExitReasonReporter,
) {

    private companion object {
        const val TAG = "DebugLogRecorder"
        const val FLUSH_INTERVAL_MS = 250L
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
            session?.requestStop()
            session = null
            DebugLog.setEnabled(false)
        }
    }

    private inner class Session : Thread("DebugLogRecorder") {
        private var logcatProcess: java.lang.Process? = null
        private var isStopRequested = false

        fun requestStop() {
            synchronized(this) {
                isStopRequested = true
                // Ends the read loop, since the process' output closes
                logcatProcess?.destroy()
            }
        }

        override fun run() {
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

            try {
                // logcat first prints what the buffers still hold for this process (usually everything since the app started), then follows it
                val reader = process.inputStream.bufferedReader()
                var lastFlushMs = SystemClock.elapsedRealtime()
                while (true) {
                    val line = reader.readLine() ?: break
                    writer.write(line)
                    writer.write("\n")

                    val now = SystemClock.elapsedRealtime()
                    if (!reader.ready() || now - lastFlushMs >= FLUSH_INTERVAL_MS) {
                        writer.flush()
                        lastFlushMs = now
                    }
                }
            } catch (e: IOException) {
                // Destroying the process can end the read with an exception instead of EOF
                if (!synchronized(this) { isStopRequested }) {
                    writer.write("logcat output failed: $e\n")
                }
            } finally {
                process.destroy()
            }
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
