package me.magnum.melonds.impl.debug

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.content.SharedPreferences
import android.os.Build
import android.util.Log
import androidx.annotation.RequiresApi
import androidx.core.content.edit
import androidx.core.content.getSystemService
import me.magnum.melonds.common.PreferenceKeys
import java.io.InputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Writes a report for each crash, ANR or foreground kill of this app that happened since the last check, using the exit records the system
 * keeps (Android 11 and up). Each one goes to melonade_exit_<timestamp>.txt in the log folder, with the system's trace when it has one: the thread
 * dump for an ANR, or the native tombstone (Android 12 and up), which is saved next to it as melonade_exit_<timestamp>_tombstone.pb.
 *
 * [DebugLogRecorder] runs it while the debug log is on. The first time, it looks back [FIRST_RUN_LOOKBACK_MS], since the debug log is usually
 * turned on right after a problem.
 */
class ExitReasonReporter(
    private val context: Context,
    private val preferences: SharedPreferences,
    private val logFolder: LogFolder,
) {

    private companion object {
        const val TAG = "ExitReasonReporter"
        const val MAX_REPORTS_PER_RUN = 5
        const val FIRST_RUN_LOOKBACK_MS = 24 * 60 * 60 * 1000L
    }

    @Synchronized
    fun reportNewExits() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            return
        }

        try {
            reportNewExitsApi30()
        } catch (e: Exception) {
            Log.w(TAG, "Failed to write process exit reports", e)
        }
    }

    @RequiresApi(Build.VERSION_CODES.R)
    private fun reportNewExitsApi30() {
        val activityManager = context.getSystemService<ActivityManager>() ?: return
        // A null package name means this app
        val exits = activityManager.getHistoricalProcessExitReasons(null, 0, 0)
        val newestTimestamp = exits.maxOfOrNull { it.timestamp } ?: return
        val reportAfterTimestamp = if (preferences.contains(PreferenceKeys.DEBUG_LAST_REPORTED_EXIT_MS)) {
            preferences.getLong(PreferenceKeys.DEBUG_LAST_REPORTED_EXIT_MS, 0)
        } else {
            System.currentTimeMillis() - FIRST_RUN_LOOKBACK_MS
        }
        if (newestTimestamp <= reportAfterTimestamp) {
            return
        }

        exits.filter { it.timestamp > reportAfterTimestamp && isWorthReporting(it) }
            .sortedBy { it.timestamp }
            .takeLast(MAX_REPORTS_PER_RUN)
            .forEach {
                try {
                    writeReport(it)
                } catch (e: Exception) {
                    Log.w(TAG, "Failed to write the report for the ${getReasonName(it.reason)} exit at ${it.timestamp}", e)
                }
                // Saved after each exit, even if its report failed, so that a later run never writes the same files twice
                saveLastReportedTimestamp(it.timestamp)
            }

        // Also covers the exits that weren't worth a report
        saveLastReportedTimestamp(newestTimestamp)
    }

    private fun saveLastReportedTimestamp(timestamp: Long) {
        preferences.edit {
            putLong(PreferenceKeys.DEBUG_LAST_REPORTED_EXIT_MS, timestamp)
        }
    }

    @RequiresApi(Build.VERSION_CODES.R)
    private fun isWorthReporting(exit: ApplicationExitInfo): Boolean {
        return when (exit.reason) {
            ApplicationExitInfo.REASON_CRASH,
            ApplicationExitInfo.REASON_CRASH_NATIVE,
            ApplicationExitInfo.REASON_ANR,
            ApplicationExitInfo.REASON_INITIALIZATION_FAILURE,
            ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE -> true
            // The system kills cached apps for memory all the time. Only kills while the app was on screen are interesting
            ApplicationExitInfo.REASON_LOW_MEMORY,
            ApplicationExitInfo.REASON_SIGNALED -> exit.importance <= ActivityManager.RunningAppProcessInfo.IMPORTANCE_VISIBLE
            else -> false
        }
    }

    @RequiresApi(Build.VERSION_CODES.R)
    private fun writeReport(exit: ApplicationExitInfo) {
        // The trace stream is closed however the report ends
        val trace = runCatching { exit.traceInputStream }.getOrNull()
        if (trace != null) {
            trace.use { writeReport(exit, it) }
        } else {
            writeReport(exit, null)
        }
    }

    @RequiresApi(Build.VERSION_CODES.R)
    private fun writeReport(exit: ApplicationExitInfo, trace: InputStream?) {
        val filePrefix = "melonade_exit_${SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date(exit.timestamp))}"
        // From Android 12, the trace of a native crash is the tombstone in protobuf form. Anything else (ANR traces) is text
        val isTombstone = trace != null && exit.reason == ApplicationExitInfo.REASON_CRASH_NATIVE && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
        val tombstoneFile = if (isTombstone) "${filePrefix}_tombstone.pb" else null

        val dateFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS Z", Locale.US)
        val report = buildString {
            append("melonadeDS process exit report\n")
            append("written: ").append(dateFormat.format(Date())).append('\n')
            append("exit time: ").append(dateFormat.format(Date(exit.timestamp))).append('\n')
            append("reason: ").append(getReasonName(exit.reason)).append(" (").append(exit.reason).append(")\n")
            append("description: ").append(exit.description ?: "n/a").append('\n')
            // The signal number for native crashes and kills, the exit code for normal exits
            append("status: ").append(exit.status).append('\n')
            append("importance: ").append(exit.importance).append(" (").append(getImportanceName(exit.importance)).append(")\n")
            append("process: ").append(exit.processName).append(", pid ").append(exit.pid).append('\n')
            append("pss_kb: ").append(exit.pss).append(", rss_kb: ").append(exit.rss).append('\n')
            when {
                tombstoneFile != null -> append("trace: native tombstone saved as ").append(tombstoneFile).append('\n')
                trace == null && exit.reason == ApplicationExitInfo.REASON_CRASH -> append("trace: none (a Java crash's stack trace is in the debug log)\n")
                trace == null -> append("trace: none\n")
                else -> append("trace:\n")
            }
        }

        logFolder.createFile("$filePrefix.txt").bufferedWriter(Charsets.UTF_8).use { writer ->
            writer.write(report)
            if (trace != null && !isTombstone) {
                trace.bufferedReader(Charsets.UTF_8).copyTo(writer)
            }
        }
        if (trace != null && tombstoneFile != null) {
            logFolder.createFile(tombstoneFile, LogFolder.GENERIC_MIME_TYPE).use { trace.copyTo(it) }
        }
        Log.i(TAG, "Wrote $filePrefix.txt for the ${getReasonName(exit.reason)} exit")
    }

    private fun getReasonName(reason: Int): String {
        return when (reason) {
            ApplicationExitInfo.REASON_EXIT_SELF -> "EXIT_SELF"
            ApplicationExitInfo.REASON_SIGNALED -> "SIGNALED"
            ApplicationExitInfo.REASON_LOW_MEMORY -> "LOW_MEMORY"
            ApplicationExitInfo.REASON_CRASH -> "CRASH"
            ApplicationExitInfo.REASON_CRASH_NATIVE -> "CRASH_NATIVE"
            ApplicationExitInfo.REASON_ANR -> "ANR"
            ApplicationExitInfo.REASON_INITIALIZATION_FAILURE -> "INITIALIZATION_FAILURE"
            ApplicationExitInfo.REASON_PERMISSION_CHANGE -> "PERMISSION_CHANGE"
            ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE -> "EXCESSIVE_RESOURCE_USAGE"
            ApplicationExitInfo.REASON_USER_REQUESTED -> "USER_REQUESTED"
            ApplicationExitInfo.REASON_USER_STOPPED -> "USER_STOPPED"
            ApplicationExitInfo.REASON_DEPENDENCY_DIED -> "DEPENDENCY_DIED"
            ApplicationExitInfo.REASON_OTHER -> "OTHER"
            else -> "UNKNOWN"
        }
    }

    private fun getImportanceName(importance: Int): String {
        return when {
            importance <= ActivityManager.RunningAppProcessInfo.IMPORTANCE_FOREGROUND -> "foreground"
            importance <= ActivityManager.RunningAppProcessInfo.IMPORTANCE_FOREGROUND_SERVICE -> "foreground service"
            importance <= ActivityManager.RunningAppProcessInfo.IMPORTANCE_VISIBLE -> "visible"
            importance <= ActivityManager.RunningAppProcessInfo.IMPORTANCE_PERCEPTIBLE -> "perceptible"
            importance < ActivityManager.RunningAppProcessInfo.IMPORTANCE_CACHED -> "service or background"
            importance < ActivityManager.RunningAppProcessInfo.IMPORTANCE_GONE -> "cached"
            else -> "gone"
        }
    }
}
