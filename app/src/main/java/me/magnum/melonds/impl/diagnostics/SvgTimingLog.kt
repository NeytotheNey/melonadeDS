package me.magnum.melonds.impl.diagnostics

import android.util.Log
import me.magnum.melonds.BuildConfig
import java.util.Locale

/**
 * One logcat line for each time an SVG background is loaded or rasterized, for benchmarking them on a phone. Always on in debug builds; in release
 * builds only while the debug log is on. Only runs on loads, never per frame.
 */
internal object SvgTimingLog {
    const val SOURCE_RENDERER = "renderer"
    const val SOURCE_THUMBNAIL = "thumbnail"
    const val SOURCE_EDITOR = "editor"

    /**
     * @param lastWidth Width it was last rasterized at from the same parse, or 0 if this is the first time
     * @param lastHeight Height it was last rasterized at from the same parse, or 0 if this is the first time
     * @param parseNanos How long parsing took, or null if an earlier parse was used
     * @param uploadNanos How long the GL upload took, or null if there was none
     */
    fun log(
        source: String,
        backgroundName: String?,
        lastWidth: Int,
        lastHeight: Int,
        width: Int,
        height: Int,
        isSvgNanos: Long,
        parseNanos: Long?,
        rasterNanos: Long,
        uploadNanos: Long? = null,
    ) {
        if (!BuildConfig.DEBUG && !DebugLog.isEnabled) {
            return
        }

        val loadKind = loadKind(lastWidth, lastHeight, width, height)
        Log.i(LogTags.SVG, formatLine(source, backgroundName, loadKind, width, height, isSvgNanos, parseNanos, rasterNanos, uploadNanos))
    }

    /**
     * "first" for the first rasterization of a parse, "resize" for another one at a new size, and "reload" for one at the same size, which happens
     * when the background mode or the GL context changes.
     */
    fun loadKind(lastWidth: Int, lastHeight: Int, width: Int, height: Int): String {
        return when {
            lastWidth <= 0 || lastHeight <= 0 -> "first"
            lastWidth != width || lastHeight != height -> "resize"
            else -> "reload"
        }
    }

    fun formatLine(
        source: String,
        backgroundName: String?,
        loadKind: String,
        width: Int,
        height: Int,
        isSvgNanos: Long,
        parseNanos: Long?,
        rasterNanos: Long,
        uploadNanos: Long?,
    ): String {
        val totalNanos = isSvgNanos + (parseNanos ?: 0L) + rasterNanos + (uploadNanos ?: 0L)
        return buildString {
            append(source)
            append(" bg=\"").append(backgroundName).append('"')
            append(" load=").append(loadKind)
            append(" target=").append(width).append('x').append(height)
            append(" isSvg=").append(formatMillis(isSvgNanos))
            append(" parse=").append(parseNanos?.let { formatMillis(it) } ?: "cached")
            append(" raster=").append(formatMillis(rasterNanos))
            if (uploadNanos != null) {
                append(" upload=").append(formatMillis(uploadNanos))
            }
            append(" total=").append(formatMillis(totalNanos))
        }
    }

    private fun formatMillis(nanos: Long): String {
        return String.format(Locale.US, "%.1fms", nanos / 1_000_000.0)
    }
}
