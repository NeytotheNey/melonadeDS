package me.magnum.melonds.ui.layouteditor

import android.content.Context
import android.content.SharedPreferences
import me.magnum.melonds.impl.dpToPixels
import kotlin.math.max
import kotlin.math.roundToInt

private const val MIN_GRID_CELL_PX = 4

/**
 * The snap grid setting as one editor uses it. Between [attach] and [detach] it follows every change, including ones made from the editor on the
 * other display, and calls [onChanged] after each.
 */
internal class SnapGrid(private val context: Context, private val onChanged: () -> Unit) {
    private val settings = LayoutEditorGridSettings(context)
    private var listener: SharedPreferences.OnSharedPreferenceChangeListener? = null

    var isEnabled = false
        private set

    /** The grid spacing in pixels */
    var cellSize = 1
        private set

    init {
        load()
    }

    fun attach() {
        listener = settings.addListener { load() }
        load()
    }

    fun detach() {
        listener?.let { settings.removeListener(it) }
        listener = null
    }

    private fun load() {
        isEnabled = settings.isSnapEnabled
        cellSize = max(MIN_GRID_CELL_PX, context.dpToPixels(settings.gridSize.dp.toFloat()).roundToInt())
        onChanged()
    }
}
