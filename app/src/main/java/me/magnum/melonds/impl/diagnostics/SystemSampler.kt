package me.magnum.melonds.impl.diagnostics

import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.os.Build
import android.os.PowerManager
import android.os.SystemClock
import android.system.Os
import android.system.OsConstants
import android.util.Log
import androidx.annotation.VisibleForTesting
import androidx.core.content.getSystemService
import me.magnum.melonds.impl.diagnostics.PerfCsv.appendMillis
import me.magnum.melonds.impl.diagnostics.PerfCsv.appendSeconds
import me.magnum.melonds.impl.diagnostics.PerfCsv.toCsvField
import java.io.File
import java.io.FileInputStream
import java.util.Locale

/**
 * Samples the process's per-thread CPU time, the CPU and GPU clocks, the GPU's busy time and the battery about once a second, on the performance
 * logger's thread. Everything is read from /proc, sysfs and the battery service; sources the app isn't allowed to read are probed once at the
 * start and left out of the columns and the summary.
 */
internal class SystemSampler(private val context: Context) {

    companion object {
        private const val TAG = "SystemSampler"
        // Rows per second in the threads CSV, busiest first
        const val TOP_THREADS_PER_SAMPLE = 10
        // Threads (grouped by name) listed in the summary
        const val SUMMARY_TOP_THREADS = 12
        private const val KGSL_DIR = "/sys/class/kgsl/kgsl-3d0"
        private const val MALI_DIR = "/sys/class/misc/mali0/device"
        private const val CPUFREQ_DIR = "/sys/devices/system/cpu/cpufreq"
        private const val MAX_GPU_LEVELS = 32

        /**
         * utime + stime (in clock ticks) of a /proc stat line in [buffer], or -1 if it can't be parsed. The name in parentheses may contain spaces
         * and parentheses, so the fields are counted from the last ')'.
         */
        @VisibleForTesting
        fun parseStatCpuTicks(buffer: ByteArray, length: Int): Long {
            val fields = LongArray(2)
            return if (parseStatFields(buffer, length, fields, needsStartTime = false)) fields[0] else -1L
        }

        /**
         * Fills [out] with utime + stime (out[0]) and, if [needsStartTime], the start time since boot (out[1], field 22), both in clock ticks;
         * returns false if the line can't be parsed.
         */
        @VisibleForTesting
        fun parseStatFields(buffer: ByteArray, length: Int, out: LongArray, needsStartTime: Boolean = true): Boolean {
            var close = -1
            for (i in length - 1 downTo 0) {
                if (buffer[i] == ')'.code.toByte()) {
                    close = i
                    break
                }
            }
            if (close < 0) {
                return false
            }

            // Field 3 (state) is the first one after ") "; utime and stime are fields 14 and 15, starttime is 22
            var position = close + 1
            var field = 2
            var utime = -1L
            while (position < length) {
                while (position < length && isSeparator(buffer[position])) position++
                if (position >= length) break
                val start = position
                while (position < length && !isSeparator(buffer[position])) position++
                field++
                if (field == 14) {
                    utime = parseLong(buffer, start, position)
                } else if (field == 15) {
                    val stime = parseLong(buffer, start, position)
                    if (utime < 0 || stime < 0) return false
                    out[0] = utime + stime
                    if (!needsStartTime) return true
                } else if (field == 22) {
                    out[1] = parseLong(buffer, start, position)
                    return out[1] >= 0
                }
            }
            return false
        }

        /**
         * The name between the first '(' and the last ')' of a /proc stat line, as [start, end) offsets packed in a long, or -1.
         */
        @VisibleForTesting
        fun findStatName(buffer: ByteArray, length: Int): Long {
            var open = -1
            for (i in 0 until length) {
                if (buffer[i] == '('.code.toByte()) {
                    open = i
                    break
                }
            }
            var close = -1
            for (i in length - 1 downTo 0) {
                if (buffer[i] == ')'.code.toByte()) {
                    close = i
                    break
                }
            }
            if (open < 0 || close <= open) {
                return -1L
            }
            return ((open + 1).toLong() shl 32) or close.toLong()
        }

        private fun isSeparator(byte: Byte): Boolean {
            return byte == ' '.code.toByte() || byte == '\n'.code.toByte() || byte == '\t'.code.toByte()
        }

        /**
         * The non-negative decimal number in [start, end) (stopping at the first non-digit), or -1 if it doesn't start with a digit.
         */
        @VisibleForTesting
        fun parseLong(buffer: ByteArray, start: Int, end: Int): Long {
            var value = 0L
            var digits = 0
            var i = start
            while (i < end) {
                val digit = buffer[i] - '0'.code.toByte()
                if (digit !in 0..9) break
                value = value * 10 + digit
                digits++
                i++
            }
            return if (digits > 0) value else -1L
        }

        /**
         * Splits the whitespace-separated numbers in [buffer] into [out], returning how many were read (up to out.size).
         */
        @VisibleForTesting
        fun parseLongs(buffer: ByteArray, length: Int, out: LongArray): Int {
            var count = 0
            var position = 0
            while (position < length && count < out.size) {
                while (position < length && (isSeparator(buffer[position]) || buffer[position] == ','.code.toByte())) position++
                if (position >= length) break
                val start = position
                while (position < length && !isSeparator(buffer[position]) && buffer[position] != ','.code.toByte()) position++
                val value = parseLong(buffer, start, position)
                if (value < 0) break
                out[count++] = value
            }
            return count
        }
    }

    private val clockTicksPerSecond = Os.sysconf(OsConstants._SC_CLK_TCK).takeIf { it > 0 } ?: 100L
    private val readBuffer = ByteArray(4096)
    private val powerManager = context.getSystemService<PowerManager>()
    private val batteryManager = context.getSystemService<BatteryManager>()
    private val batteryChangedFilter = IntentFilter(Intent.ACTION_BATTERY_CHANGED)

    // Probed once in start()
    var isThreadCpuAvailable = false
        private set
    private var isProcessCpuAvailable = false
    private val cpuPolicies = ArrayList<CpuPolicy>()
    private var gpuBusySource: GpuBusySource? = null
    private var gpuBusyPath: String? = null
    private var gpuClockPath: String? = null
    // gpuClockPath's unit: Hz for kgsl and devfreq, kHz or MHz on some Mali drivers (normalized by value)
    private var gpuClockStatsFrequenciesMhz: LongArray? = null
    private var isBatteryCurrentAvailable = false
    private var isThermalHeadroomAvailable = false
    private val unavailableSources = ArrayList<String>()
    val gpuSourceDescription: String?
        get() = listOfNotNull(gpuBusySource?.description, gpuClockPath?.let { "clock $it" }, gpuClockStatsFrequenciesMhz?.let { "$KGSL_DIR/gpu_clock_stats" })
            .takeIf { it.isNotEmpty() }?.joinToString(", ")

    // Per-sample state
    private var hasBaseline = false
    private var lastSampleNs = 0L
    private var lastProcessTicks = -1L
    private var lastProcessTicksNs = 0L
    private var lastGpuClockStatsNs = 0L
    private var previousSampleBootNs = 0L
    private val statFields = LongArray(2)
    // The sampled window (first to last sample) and the emulated frames that ended inside it, which the per-frame values are divided by
    private var windowStartNs = 0L
    var windowFrames = 0L
        private set
    private var pendingWindowFrames = 0L
    private val threads = HashMap<Int, ThreadCpu>()
    private val threadsByName = HashMap<String, ThreadTotal>()
    private val busyThreads = ArrayList<ThreadCpu>()
    private val busyThreadComparator = Comparator<ThreadCpu> { a, b -> b.deltaTicks.compareTo(a.deltaTicks) }
    private var sampleGeneration = 0
    private val gpuNumbers = LongArray(MAX_GPU_LEVELS)
    private var lastGpuClockStats: LongArray? = null
    private val currentGpuClockStats = LongArray(MAX_GPU_LEVELS)

    // Session totals for the summary
    var cpuSamples = 0L
        private set
    private var sampledWallNs = 0L
    private var processTicksTotal = 0L
    private var processCoresMax = 0.0
    private var gpuBusySamples = 0L
    private var gpuBusySum = 0.0
    private var gpuBusyMax = 0.0
    private var gpuClockSamples = 0L
    private var gpuClockSumMhz = 0.0
    private var gpuClockStatsBusyUsTotal = 0L
    private var gpuClockStatsCyclesTotal = 0.0
    private var batteryCurrentSamples = 0L
    private var batteryCurrentSumMa = 0.0
    private var batterySamples = 0L
    private var batteryPluggedSamples = 0L
    private var batteryTempStart = Double.NaN
    private var batteryTempMax = Double.NaN
    private var batteryTempLast = Double.NaN
    private var batteryPluggedStart: String? = null
    private var batteryPluggedLast: String? = null
    private var batteryPluggedChanges = 0L
    private var thermalHeadroomSamples = 0L
    private var thermalHeadroomMax = Double.NaN
    private var thermalHeadroomLast = Double.NaN

    /**
     * Finds the readable sources and takes the baselines; returns the sources that couldn't be read, for a one-time note in the logs.
     */
    fun start(): List<String> {
        isProcessCpuAvailable = readProcessTicks() >= 0
        isThreadCpuAvailable = File("/proc/self/task").list()?.isNotEmpty() == true
        if (!isProcessCpuAvailable) unavailableSources.add("/proc/self/stat")
        if (!isThreadCpuAvailable) unavailableSources.add("/proc/self/task")
        probeCpuPolicies()
        probeGpu()
        isBatteryCurrentAvailable = readBatteryCurrentMa() != null
        if (!isBatteryCurrentAvailable) unavailableSources.add("BatteryManager current")
        isThermalHeadroomAvailable = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && powerManager != null
        if (!isThermalHeadroomAvailable) unavailableSources.add("PowerManager thermal headroom")
        if (unavailableSources.isNotEmpty()) {
            Log.i(TAG, "Not readable, left out of the performance log: ${unavailableSources.joinToString(", ")}")
        }
        return unavailableSources.toList()
    }

    /**
     * The columns this sampler adds to each per-second row, each with a leading comma.
     */
    fun csvHeader(): String {
        val header = StringBuilder()
        if (isProcessCpuAvailable) header.append(",process_cpu_cores")
        cpuPolicies.forEach { header.append(",cpu_").append(it.name).append("_mhz") }
        if (gpuBusySource != null) header.append(",gpu_busy_pct")
        if (gpuClockPath != null) header.append(",gpu_clock_mhz")
        if (gpuClockStatsFrequenciesMhz != null) header.append(",gpu_clock_stats_busy_pct,gpu_busy_weighted_mhz")
        header.append(",battery_current_ma,battery_voltage_mv,battery_temp_c,battery_plugged")
        if (isThermalHeadroomAvailable) header.append(",thermal_headroom")
        return header.toString()
    }

    /**
     * Takes the baselines the first [sample] measures from, without writing anything; call it right when frame capture starts, so that the
     * CPU and GPU totals cover the same time as the frames counted by [trackFrame].
     */
    fun takeBaseline(nowNs: Long) {
        sample(nowNs, nowNs, StringBuilder(), StringBuilder(), isBaselineOnly = true)
    }

    /**
     * Counts an emulated frame (by its end timestamp, System.nanoTime() clock) towards the per-frame values if it falls inside the sampled
     * window: after the baseline and at or before the last sample. Frames are drained in time order after each sample.
     */
    fun trackFrame(timestampNs: Long) {
        if (!hasBaseline || timestampNs < windowStartNs) {
            return
        }
        if (timestampNs <= lastSampleNs) windowFrames++ else pendingWindowFrames++
    }

    /**
     * Takes a sample: appends this sampler's columns (with leading commas, matching [csvHeader]) to [row] and the busiest threads' rows to
     * [threadRows]. The first call (normally [takeBaseline]) only takes the baselines.
     */
    fun sample(nowNs: Long, sessionStartNs: Long, row: StringBuilder, threadRows: StringBuilder, isBaselineOnly: Boolean = false) {
        val intervalNs = if (hasBaseline) nowNs - lastSampleNs else 0L
        val hasInterval = intervalNs > 0
        if (!hasBaseline) {
            windowStartNs = nowNs
        }
        // Frames drained since the last sample that ended after it are now inside the window
        windowFrames += pendingWindowFrames
        pendingWindowFrames = 0
        lastSampleNs = nowNs
        val bootNs = SystemClock.elapsedRealtimeNanos()

        if (isProcessCpuAvailable) {
            val ticks = readProcessTicks()
            row.append(',')
            if (ticks >= 0) {
                // Measured from the last successful read, so a failed read doesn't put two intervals' time over one
                val elapsedNs = nowNs - lastProcessTicksNs
                if (lastProcessTicks >= 0 && elapsedNs > 0) {
                    val deltaTicks = ticks - lastProcessTicks
                    val cores = deltaTicks.toDouble() / clockTicksPerSecond / (elapsedNs / 1e9)
                    appendDecimal(row, cores, 3)
                    processTicksTotal += deltaTicks
                    processCoresMax = maxOf(processCoresMax, cores)
                }
                lastProcessTicks = ticks
                lastProcessTicksNs = nowNs
            }
        }
        if (hasInterval) {
            cpuSamples++
            sampledWallNs += intervalNs
        }

        if (isThreadCpuAvailable) {
            sampleThreads(hasBaseline, hasInterval, nowNs - sessionStartNs, previousSampleBootNs, threadRows)
        }
        previousSampleBootNs = bootNs

        if (isBaselineOnly) {
            // Only the counters measured as deltas need a baseline; clocks, battery and headroom are read as they are on each sample
            sampleGpuClockStats(nowNs, row)
            hasBaseline = true
            return
        }

        cpuPolicies.forEach { policy ->
            row.append(',')
            val length = readFile(policy.curFreqPath)
            val khz = if (length > 0) parseLong(readBuffer, 0, length) else -1L
            if (khz > 0) {
                row.append(khz / 1000)
                policy.samples++
                policy.sumMhz += khz / 1000.0
                policy.maxMhz = maxOf(policy.maxMhz, khz / 1000)
            }
        }

        gpuBusySource?.let { source ->
            row.append(',')
            val busy = readGpuBusyPct(source)
            if (busy != null) {
                appendDecimal(row, busy, 1)
                gpuBusySamples++
                gpuBusySum += busy
                gpuBusyMax = maxOf(gpuBusyMax, busy)
            }
        }

        gpuClockPath?.let { path ->
            row.append(',')
            val mhz = readGpuClockMhz(path)
            if (mhz != null) {
                row.append(mhz)
                gpuClockSamples++
                gpuClockSumMhz += mhz
            }
        }

        sampleGpuClockStats(nowNs, row)

        sampleBattery(row)

        if (isThermalHeadroomAvailable) {
            row.append(',')
            val headroom = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) powerManager?.getThermalHeadroom(0)?.toDouble() ?: Double.NaN else Double.NaN
            // NaN when called more than once a second or when the device can't tell
            if (!headroom.isNaN()) {
                appendDecimal(row, headroom, 3)
                thermalHeadroomSamples++
                thermalHeadroomLast = headroom
                thermalHeadroomMax = if (thermalHeadroomMax.isNaN()) headroom else maxOf(thermalHeadroomMax, headroom)
            }
        }

        hasBaseline = true
    }

    private fun sampleGpuClockStats(nowNs: Long, row: StringBuilder) {
        gpuClockStatsFrequenciesMhz?.let { frequencies ->
            row.append(',')
            val length = readFile("$KGSL_DIR/gpu_clock_stats")
            val count = if (length > 0) parseLongs(readBuffer, length, currentGpuClockStats) else 0
            val previous = lastGpuClockStats
            var busyUs = 0L
            var cycles = 0.0
            val isValid = count == frequencies.size
            // Measured from the last successful read, so a failed read doesn't put two intervals' busy time over one
            val elapsedNs = nowNs - lastGpuClockStatsNs
            if (isValid && previous != null && elapsedNs > 0) {
                for (i in 0 until count) {
                    val delta = maxOf(0L, currentGpuClockStats[i] - previous[i])
                    busyUs += delta
                    cycles += delta.toDouble() * frequencies[i]
                }
                appendDecimal(row, busyUs * 100.0 / (elapsedNs / 1000.0), 1)
                row.append(',')
                if (busyUs > 0) row.append(Math.round(cycles / busyUs))
                gpuClockStatsBusyUsTotal += busyUs
                gpuClockStatsCyclesTotal += cycles
            } else {
                row.append(',')
            }
            if (isValid) {
                val stats = previous ?: LongArray(count).also { lastGpuClockStats = it }
                System.arraycopy(currentGpuClockStats, 0, stats, 0, count)
                lastGpuClockStatsNs = nowNs
            }
        }
    }

    private fun sampleThreads(hasBaseline: Boolean, hasInterval: Boolean, elapsedNs: Long, previousSampleBootNs: Long, threadRows: StringBuilder) {
        val taskIds = File("/proc/self/task").list() ?: return
        sampleGeneration++
        busyThreads.clear()
        for (taskId in taskIds) {
            val tid = taskId.toIntOrNull() ?: continue
            var thread = threads[tid]
            if (thread == null) {
                thread = ThreadCpu(tid, "/proc/self/task/$tid/stat")
                threads[tid] = thread
            }
            // Still listed, so kept (with its baseline) even if this read fails
            thread.seenGeneration = sampleGeneration
            thread.deltaTicks = 0L
            val length = readFile(thread.statPath)
            if (length <= 0) continue
            if (!parseStatFields(readBuffer, length, statFields)) continue
            val ticks = statFields[0]
            val startTime = statFields[1]
            thread.updateName(readBuffer, length)

            // A different start time means the tid was reused by a new thread, which needs its own baseline
            if (thread.startTime != startTime) {
                thread.startTime = startTime
                thread.lastTicks = -1L
            }
            thread.deltaTicks = when {
                thread.lastTicks >= 0 -> maxOf(0L, ticks - thread.lastTicks)
                !hasBaseline -> 0L
                // No baseline yet: a thread that started after the last sample used all of its CPU time in this interval, an older one
                // only gets its baseline now
                ticksToNs(startTime) >= previousSampleBootNs -> ticks
                else -> 0L
            }
            thread.lastTicks = ticks
            if (thread.deltaTicks > 0 && hasInterval) {
                busyThreads.add(thread)
                val total = threadsByName.getOrPut(thread.name) { ThreadTotal(thread.name) }
                total.ticks += thread.deltaTicks
                total.tids.add(tid)
            }
        }
        // Forget threads that exited
        threads.values.removeAll { it.seenGeneration != sampleGeneration }

        if (busyThreads.isEmpty()) return
        busyThreads.sortWith(busyThreadComparator)
        val count = minOf(TOP_THREADS_PER_SAMPLE, busyThreads.size)
        for (i in 0 until count) {
            val thread = busyThreads[i]
            threadRows.appendSeconds(elapsedNs).append(',')
                .append(thread.name.toCsvField()).append(',')
                .append(thread.tid).append(',')
                .appendMillis(ticksToNs(thread.deltaTicks)).append('\n')
        }
    }

    private fun sampleBattery(row: StringBuilder) {
        val current = if (isBatteryCurrentAvailable) readBatteryCurrentMa() else null
        val batteryIntent = try {
            context.registerReceiver(null, batteryChangedFilter)
        } catch (e: Exception) {
            null
        }
        row.append(',')
        if (current != null) {
            row.append(current)
            batteryCurrentSamples++
            batteryCurrentSumMa += current
        }
        row.append(',')
        if (batteryIntent == null) {
            row.append(",,")
            return
        }

        val voltage = batteryIntent.getIntExtra(BatteryManager.EXTRA_VOLTAGE, -1)
        if (voltage > 0) row.append(voltage)
        row.append(',')
        val tempTenths = batteryIntent.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Int.MIN_VALUE)
        if (tempTenths != Int.MIN_VALUE) {
            val temp = tempTenths / 10.0
            appendDecimal(row, temp, 1)
            if (batteryTempStart.isNaN()) batteryTempStart = temp
            batteryTempMax = if (batteryTempMax.isNaN()) temp else maxOf(batteryTempMax, temp)
            batteryTempLast = temp
        }
        row.append(',')
        val plugged = pluggedName(batteryIntent.getIntExtra(BatteryManager.EXTRA_PLUGGED, -1))
        row.append(plugged)
        batterySamples++
        if (plugged != "none" && plugged != "unknown") batteryPluggedSamples++
        if (batteryPluggedStart == null) batteryPluggedStart = plugged
        if (batteryPluggedLast != null && batteryPluggedLast != plugged) batteryPluggedChanges++
        batteryPluggedLast = plugged
    }

    private fun pluggedName(plugged: Int): String {
        return when (plugged) {
            0 -> "none"
            BatteryManager.BATTERY_PLUGGED_AC -> "ac"
            BatteryManager.BATTERY_PLUGGED_USB -> "usb"
            BatteryManager.BATTERY_PLUGGED_WIRELESS -> "wireless"
            8 -> "dock"
            else -> "unknown"
        }
    }

    /**
     * Battery current in mA, as the device reports it (the sign for discharging differs between vendors), or null if not supported.
     */
    private fun readBatteryCurrentMa(): Long? {
        val microAmps = try {
            batteryManager?.getIntProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_NOW) ?: return null
        } catch (e: Exception) {
            return null
        }
        if (microAmps == Int.MIN_VALUE || microAmps == 0) {
            return null
        }
        return microAmps / 1000L
    }

    /**
     * Adds this sampler's summary keys to [section]. Per-frame values are divided by the frames that ended inside the sampled window
     * ([windowFrames]), which starts at the baseline taken when capture starts and ends at the last sample.
     */
    fun addSummaryEntries(section: SummarySection) {
        val emulatedFrames = windowFrames
        val sampledSeconds = sampledWallNs / 1e9
        val hasSamples = cpuSamples > 0 && sampledSeconds > 0
        section.entry("system_samples", cpuSamples, "Samples behind the CPU, GPU and battery stats, after the baseline taken when frame capture starts")
        section.entry(
            "system_window_frames", emulatedFrames,
            "Emulated frames (all, fast-forward included) that ended between the baseline and the last sample; the per-frame values are divided " +
                "by this",
        )
        if (isProcessCpuAvailable) {
            section.entry(
                "process_cpu_cores_avg", decimal(if (hasSamples) ticksToNs(processTicksTotal) / 1e9 / sampledSeconds else null, 3),
                "Whole-process CPU time (utime + stime from /proc/self/stat) per second of wall time, so 1.0 = one core busy. Clock-tick " +
                    "resolution (${1000 / clockTicksPerSecond} ms)",
            )
            section.entry("process_cpu_cores_max", decimal(if (hasSamples) processCoresMax else null, 3), "Busiest one-second sample")
            section.entry(
                "process_cpu_ms_per_frame", decimal(if (emulatedFrames > 0 && hasSamples) ticksToNs(processTicksTotal) / 1e6 / emulatedFrames else null, 2),
                "Whole-process CPU ms per emulated frame (all frames, fast-forward included)",
            )
        }
        if (isThreadCpuAvailable) {
            val topThreads = threadsByName.values.sortedByDescending { it.ticks }.take(SUMMARY_TOP_THREADS)
            section.entry(
                "thread_cpu_top", topThreads.size,
                "thread_cpu_N: the busiest threads, grouped by thread name (several threads can share one), with their CPU ms per second of " +
                    "wall time and per emulated frame, and how many thread IDs had that name",
            )
            topThreads.forEachIndexed { index, thread ->
                val totalMs = ticksToNs(thread.ticks) / 1e6
                val perSecond = if (hasSamples) totalMs / sampledSeconds else null
                val perFrame = if (emulatedFrames > 0) totalMs / emulatedFrames else null
                section.entry(
                    "thread_cpu_${index + 1}",
                    "${thread.name.toCsvField()} cpu_ms_per_s=${decimal(perSecond, 1)} cpu_ms_per_frame=${decimal(perFrame, 3)} tids=${thread.tids.size}",
                )
            }
        }
        cpuPolicies.forEach { policy ->
            section.entry(
                "cpu_${policy.name}_mhz_avg", decimal(if (policy.samples > 0) policy.sumMhz / policy.samples else null, 0),
                "scaling_cur_freq once a second for CPUs ${policy.relatedCpus}; overstates bursty threads (see PERF-NOTES.md, Which clock to trust)",
            )
            section.entry("cpu_${policy.name}_mhz_max", policy.maxMhz.takeIf { policy.samples > 0 })
        }
        gpuSourceDescription?.let { section.entry("gpu_source", it, "Where the GPU stats come from") }
        gpuBusySource?.let {
            section.entry("gpu_busy_pct_avg", decimal(if (gpuBusySamples > 0) gpuBusySum / gpuBusySamples else null, 1), "Mean of the per-second GPU busy readings")
            section.entry("gpu_busy_pct_max", decimal(if (gpuBusySamples > 0) gpuBusyMax else null, 1))
        }
        gpuClockPath?.let {
            section.entry("gpu_clock_mhz_avg", decimal(if (gpuClockSamples > 0) gpuClockSumMhz / gpuClockSamples else null, 0), "Mean of the per-second GPU clock readings")
        }
        gpuClockStatsFrequenciesMhz?.let {
            val hasBusy = gpuClockStatsBusyUsTotal > 0
            section.entry(
                "gpu_clock_stats_busy_pct", decimal(if (hasBusy && sampledWallNs > 0) gpuClockStatsBusyUsTotal * 100.0 / (sampledWallNs / 1000.0) else null, 1),
                "GPU busy time from gpu_clock_stats (busy us per power level) over the sampled wall time",
            )
            section.entry(
                "gpu_busy_weighted_mhz", decimal(if (hasBusy) gpuClockStatsCyclesTotal / gpuClockStatsBusyUsTotal else null, 0),
                "Clock averaged over the GPU's busy time, sum(busy_us x MHz) / sum(busy_us)",
            )
            section.entry(
                "gpu_busy_ms_per_frame", decimal(if (hasBusy && emulatedFrames > 0) gpuClockStatsBusyUsTotal / 1000.0 / emulatedFrames else null, 3),
                "GPU busy ms per emulated frame, from gpu_clock_stats",
            )
            section.entry(
                "gpu_mcycles_per_frame", decimal(if (hasBusy && emulatedFrames > 0) gpuClockStatsCyclesTotal / 1e6 / emulatedFrames else null, 2),
                "Busy GPU cycles per emulated frame (clock-independent GPU work), from gpu_clock_stats",
            )
        }
        if (isBatteryCurrentAvailable) {
            section.entry(
                "battery_current_avg_ma", decimal(if (batteryCurrentSamples > 0) batteryCurrentSumMa / batteryCurrentSamples else null, 0),
                "BatteryManager CURRENT_NOW as the device reports it (negative usually means discharging, but some vendors flip the sign)",
            )
        }
        if (batterySamples > 0) {
            section.entry("battery_temp_start_c", decimal(batteryTempStart, 1))
            section.entry("battery_temp_max_c", decimal(batteryTempMax, 1))
            section.entry("battery_temp_end_c", decimal(batteryTempLast, 1))
            section.entry("battery_plugged_start", batteryPluggedStart, "none, ac, usb, wireless or dock")
            section.entry("battery_plugged_end", batteryPluggedLast)
            section.entry("battery_plugged_pct", decimal(batteryPluggedSamples * 100.0 / batterySamples, 0), "Share of the samples taken while plugged in")
            section.entry("battery_plugged_changes", batteryPluggedChanges)
        }
        if (isThermalHeadroomAvailable) {
            section.entry(
                "thermal_headroom_max", decimal(thermalHeadroomMax, 3),
                "PowerManager.getThermalHeadroom(0): 1.0 is where the device starts severe throttling. Blank in the CSV when the device can't tell",
            )
            section.entry("thermal_headroom_last", decimal(thermalHeadroomLast, 3))
            section.entry("thermal_headroom_samples", thermalHeadroomSamples)
        }
    }

    private fun decimal(value: Double?, decimals: Int): String {
        return if (value == null || value.isNaN()) "n/a" else String.format(Locale.US, "%.${decimals}f", value)
    }

    private fun ticksToNs(ticks: Long): Long {
        return ticks * 1_000_000_000L / clockTicksPerSecond
    }

    private fun readProcessTicks(): Long {
        val length = readFile("/proc/self/stat")
        return if (length > 0) parseStatCpuTicks(readBuffer, length) else -1L
    }

    private fun probeCpuPolicies() {
        val policyDirs = File(CPUFREQ_DIR).listFiles { file -> file.name.startsWith("policy") }
            ?.sortedBy { it.name.removePrefix("policy").toIntOrNull() ?: Int.MAX_VALUE }
            .orEmpty()
        policyDirs.forEach { dir ->
            val curFreqPath = "${dir.absolutePath}/scaling_cur_freq"
            val length = readFile(curFreqPath)
            if (length > 0 && parseLong(readBuffer, 0, length) > 0) {
                val relatedCpus = readText("${dir.absolutePath}/related_cpus")?.trim()?.replace(' ', '-') ?: "?"
                cpuPolicies.add(CpuPolicy(dir.name, curFreqPath, relatedCpus))
            }
        }
        if (cpuPolicies.isEmpty()) unavailableSources.add("$CPUFREQ_DIR/policy*/scaling_cur_freq")
    }

    private fun probeGpu() {
        // Busy: kgsl's percentage or busy/total pair, else Mali's utilization
        gpuBusySource = when {
            readFirstNumber("$KGSL_DIR/gpu_busy_percentage") != null -> GpuBusySource.KGSL_PERCENTAGE
            readGpuBusyPct(GpuBusySource.KGSL_GPUBUSY) != null -> GpuBusySource.KGSL_GPUBUSY
            readFirstNumber("$MALI_DIR/utilization") != null -> GpuBusySource.MALI_UTILIZATION
            else -> null
        }

        // Clock: kgsl's gpuclk, else a GPU devfreq node, else Mali's cur_freq
        val devfreqClock = File("/sys/class/devfreq").listFiles()
            ?.filter { dir -> dir.name.contains("gpu", ignoreCase = true) || dir.name.contains("mali", ignoreCase = true) || dir.name.contains("kgsl", ignoreCase = true) }
            ?.map { "${it.absolutePath}/cur_freq" }
            .orEmpty()
        gpuClockPath = (listOf("$KGSL_DIR/gpuclk", "$KGSL_DIR/devfreq/cur_freq") + devfreqClock + listOf("$MALI_DIR/cur_freq", "$MALI_DIR/clock"))
            .firstOrNull { readGpuClockMhz(it) != null }

        // Per power level busy time, with the level clocks it's weighted by
        val frequencies = readText("$KGSL_DIR/gpu_available_frequencies")
            ?.trim()?.split(Regex("\\s+"))?.mapNotNull { it.toLongOrNull()?.takeIf { hz -> hz > 0 } }
            ?.map { hz -> hz / 1_000_000 }
        val clockStatsLength = readFile("$KGSL_DIR/gpu_clock_stats")
        val clockStatsCount = if (clockStatsLength > 0) parseLongs(readBuffer, clockStatsLength, gpuNumbers) else 0
        if (frequencies != null && frequencies.isNotEmpty() && frequencies.size == clockStatsCount) {
            gpuClockStatsFrequenciesMhz = frequencies.toLongArray()
        }

        if (gpuBusySource == null && gpuClockPath == null && gpuClockStatsFrequenciesMhz == null) {
            unavailableSources.add("GPU busy and clock ($KGSL_DIR, /sys/class/devfreq, $MALI_DIR)")
        }
    }

    private fun readGpuBusyPct(source: GpuBusySource): Double? {
        return when (source) {
            GpuBusySource.KGSL_PERCENTAGE -> readFirstNumber("$KGSL_DIR/gpu_busy_percentage")?.toDouble()
            GpuBusySource.KGSL_GPUBUSY -> {
                // "busy total" over the driver's last window
                val length = readFile("$KGSL_DIR/gpubusy")
                if (length <= 0 || parseLongs(readBuffer, length, gpuNumbers) < 2) return null
                val total = gpuNumbers[1]
                if (total > 0) gpuNumbers[0] * 100.0 / total else 0.0
            }
            GpuBusySource.MALI_UTILIZATION -> readFirstNumber("$MALI_DIR/utilization")?.toDouble()
        }
    }

    /**
     * Reads a GPU clock node, normalizing Hz, kHz or MHz values to MHz.
     */
    private fun readGpuClockMhz(path: String): Long? {
        val value = readFirstNumber(path) ?: return null
        return when {
            value >= 10_000_000L -> value / 1_000_000
            value >= 10_000L -> value / 1000
            value > 0 -> value
            else -> null
        }
    }

    private fun readFirstNumber(path: String): Long? {
        val length = readFile(path)
        if (length <= 0) return null
        var start = 0
        while (start < length && isSeparator(readBuffer[start])) start++
        return parseLong(readBuffer, start, length).takeIf { it >= 0 }
    }

    private fun readText(path: String): String? {
        val length = readFile(path)
        return if (length > 0) String(readBuffer, 0, length, Charsets.US_ASCII) else null
    }

    /**
     * Reads [path] into [readBuffer], returning the byte count, or -1 if it can't be read (missing, or denied by SELinux).
     */
    private fun readFile(path: String): Int {
        return try {
            FileInputStream(path).use { stream ->
                var total = 0
                while (total < readBuffer.size) {
                    val read = stream.read(readBuffer, total, readBuffer.size - total)
                    if (read <= 0) break
                    total += read
                }
                total
            }
        } catch (e: Exception) {
            -1
        }
    }

    private fun appendDecimal(builder: StringBuilder, value: Double, decimals: Int) {
        // Rounded to the given decimals without String.format, which is slow and allocates
        var scale = 1L
        repeat(decimals) { scale *= 10 }
        val scaled = Math.round(Math.abs(value) * scale)
        if (value < 0 && scaled > 0) builder.append('-')
        builder.append(scaled / scale)
        if (decimals > 0) {
            builder.append('.')
            val fraction = (scaled % scale).toString()
            repeat(decimals - fraction.length) { builder.append('0') }
            builder.append(fraction)
        }
    }

    private enum class GpuBusySource(val description: String) {
        KGSL_PERCENTAGE("busy $KGSL_DIR/gpu_busy_percentage"),
        KGSL_GPUBUSY("busy $KGSL_DIR/gpubusy"),
        MALI_UTILIZATION("busy $MALI_DIR/utilization"),
    }

    private class CpuPolicy(val name: String, val curFreqPath: String, val relatedCpus: String) {
        var samples = 0L
        var sumMhz = 0.0
        var maxMhz = 0L
    }

    private class ThreadCpu(val tid: Int, val statPath: String) {
        var name = ""
        var startTime = -1L
        private val nameBytes = ByteArray(64)
        private var nameLength = -1
        var lastTicks = -1L
        var deltaTicks = 0L
        var seenGeneration = 0

        /**
         * Picks up the name from a stat line, only allocating a new String when it changed (threads are often renamed after they start).
         */
        fun updateName(buffer: ByteArray, length: Int) {
            val range = findStatName(buffer, length)
            if (range < 0) return
            val start = (range ushr 32).toInt()
            val end = (range and 0xFFFFFFFFL).toInt()
            val newLength = minOf(end - start, nameBytes.size)
            var isSame = newLength == nameLength
            if (isSame) {
                for (i in 0 until newLength) {
                    if (nameBytes[i] != buffer[start + i]) {
                        isSame = false
                        break
                    }
                }
            }
            if (!isSame) {
                System.arraycopy(buffer, start, nameBytes, 0, newLength)
                nameLength = newLength
                name = String(nameBytes, 0, newLength, Charsets.UTF_8)
            }
        }
    }

    private class ThreadTotal(val name: String) {
        var ticks = 0L
        val tids = HashSet<Int>()
    }
}
