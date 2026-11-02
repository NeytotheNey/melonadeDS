package me.magnum.melonds.impl.debug

import android.content.Context
import android.content.SharedPreferences
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
 * Opt-in recorder for this process' logcat, so that logs can be collected without adb. While the debug log setting is on, it runs
 * `logcat --pid=<own pid>` and writes its output to melonade_debug_<timestamp>.txt in the log folder, after a header that describes the app
 * build and the device. The file is flushed whenever logcat goes quiet (and at least every [FLUSH_INTERVAL_MS]), so a hang or a kill still
 * leaves the lines up to that point. It also turns on [DebugLog], and when recording starts it writes reports of recent crashes (see
 * [ExitReasonReporter]).
 */
class DebugLogRecorder(
    private val context: Context,
    private val settingsRepository: SettingsRepository,
    private val preferences: SharedPreferences,
    private val logFolder: LogFolder,
) {

    private companion object {
        const val TAG = "DebugLogRecorder"
        const val FLUSH_INTERVAL_MS = 250L
        // The buffers an app can read its own lines from
        const val LOGCAT_BUFFERS = "main,system,crash"
    }

    private val exitReasonReporter = ExitReasonReporter(context, preferences, logFolder)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var session: Session? = null

    /**
     * Starts recording if the setting is on, and follows the setting from then on. Must be called once, on the main thread, when the app starts.
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
            Log.i(DebugLog.TAG, "Debug log turned off")
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
            append("tags: ").append(DebugLog.TAG).append(" (markers, game configuration, fold and display changes), melonade-cpu (heartbeat, exceptions, GPU state), ")
                .append("melonade-audio (audio stream, dropouts, buffer fill), melonade-lua (scripts), melonade-core (emulator core messages)\n")
            append("---\n")
        }
    }

    private fun formatDate(date: Date): String {
        return SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS Z", Locale.US).format(date)
    }
}
