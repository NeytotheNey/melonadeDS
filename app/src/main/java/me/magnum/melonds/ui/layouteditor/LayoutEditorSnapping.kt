package me.magnum.melonds.ui.layouteditor

import kotlin.math.abs

/**
 * How close, as a fraction of the other screen's size, a screen has to be to line up with it or match its size.
 */
private const val SCREEN_ALIGNMENT_THRESHOLD = 0.05f

/**
 * A segment along one axis of the editor, like an item's left edge and width.
 */
data class SnapSpan(val start: Int, val length: Int) {
    val end get() = start + length
    val center get() = start + length / 2
}

/**
 * The lines an item can snap to along one axis of the editor (x or y), on a grid going out every [cell] from the middle.
 *
 * @property limit The editor's size on this axis, whose edges always count as lines
 * @property cell The grid spacing in pixels
 * @property gaps Areas like a fold that always count and that an item can sit flush against on either side
 * @property otherScreen Where the other screen is on this axis, or null when not snapping a screen or there's no other screen
 */
class SnapAxis(val limit: Int, val cell: Int, val gaps: List<SnapSpan> = emptyList(), val otherScreen: SnapSpan? = null) {
    /** The grid line in the middle of the editor, which all the other grid lines are counted from */
    val origin get() = limit / 2

    /** How close an edge has to be to the other screen's lines to line up with them */
    val alignmentThreshold get() = otherScreen?.let { (it.length * SCREEN_ALIGNMENT_THRESHOLD).toInt() } ?: 0
}

/**
 * A snapped edge position, and how far it is from where the edge would have been without snapping.
 */
data class SnappedEdge(val position: Int, val distance: Int)

/**
 * The snapping math behind the layout editor's snap grid, working on one axis at a time in editor pixels.
 */
object LayoutEditorSnapping {

    /**
     * Returns where the grid lines are along an axis of the given size: the middle line, the ones every [cell] out from it, and both editor
     * edges.
     */
    fun gridLines(limit: Int, cell: Int): List<Int> {
        val origin = limit / 2
        val firstLine = origin - origin / cell * cell
        return (listOf(0) + (firstLine..limit step cell) + limit).distinct()
    }

    /**
     * Moves a control so that its center sits on the nearest grid point that keeps it inside the editor, or leaves it be if none fits.
     */
    fun snapCenter(start: Int, length: Int, axis: SnapAxis): Int {
        val cell = axis.cell
        // Counted from the centered position rather than from the middle line minus half the length, so that a control on the middle line is
        // exactly where the center buttons put it, even with odd sizes
        val centeredStart = centeredStart(length, axis.limit)
        var snapped = centeredStart + Math.round((start - centeredStart) / cell.toFloat()) * cell
        // Step whole cells back inside the editor, so the center stays on the grid
        if (snapped < 0) snapped += (-snapped + cell - 1) / cell * cell
        if (snapped + length > axis.limit) snapped -= (snapped + length - axis.limit + cell - 1) / cell * cell
        if (snapped < 0 || snapped + length > axis.limit) {
            snapped = start
        }
        return clamp(snapped, length, axis.limit)
    }

    /**
     * Moves a screen so that whichever of its edges is closer to a line (grid, editor edge, gap edge or the other screen's) sits on it.
     */
    fun snapEdges(start: Int, length: Int, axis: SnapAxis): Int {
        val maxStart = axis.limit - length
        if (maxStart < 0) {
            return clamp(start, length, axis.limit)
        }

        // Every candidate is a new start position, skipping ones outside the editor so the result is always on a line
        val nearest = Nearest(start, 0, maxStart)
        val end = start + length
        // The start edge goes first, so it wins ties
        offerGridLines(nearest, start, axis) { it }
        offerGridLines(nearest, end, axis) { it - length }
        nearest.offer(0)
        nearest.offer(maxStart)
        // Where the center buttons put the screen, so a drag can land back on it
        nearest.offer(centeredStart(length, axis.limit))
        // A screen can sit flush on either side of a fold, but never over it
        axis.gaps.forEach {
            nearest.offer(it.end)
            nearest.offer(it.start - length)
        }
        axis.otherScreen?.let { other ->
            val threshold = axis.alignmentThreshold
            listOf(other.start, other.end).forEach {
                nearest.offer(it, threshold)
                nearest.offer(it - length, threshold)
            }
            nearest.offer(other.center - length / 2, threshold)
        }
        return nearest.value
    }

    /**
     * Snaps an edge dragged to resize an item, right of or below [fixedEdge] when [growsTowardsEnd], also matching the other screen's size.
     */
    fun snapResizedEdge(position: Int, fixedEdge: Int, minLength: Int, growsTowardsEnd: Boolean, axis: SnapAxis): SnappedEdge {
        // The dragged edge stays on its own side of the fixed one, at least the minimum size away
        val min = if (growsTowardsEnd) fixedEdge + minLength else 0
        val max = if (growsTowardsEnd) axis.limit else fixedEdge - minLength
        if (min > max) {
            return SnappedEdge(position, Int.MAX_VALUE)
        }

        // A finger dragged past the fixed edge snaps from the minimum size, not from the other side
        val from = position.coerceIn(min, max)
        val nearest = Nearest(from, min, max)
        offerGridLines(nearest, from, axis) { it }
        nearest.offer(0)
        nearest.offer(axis.limit)
        // The end edge sits flush before a fold, the start edge flush after it
        axis.gaps.forEach { nearest.offer(if (growsTowardsEnd) it.start else it.end) }
        axis.otherScreen?.let { other ->
            val threshold = axis.alignmentThreshold
            nearest.offer(other.start, threshold)
            nearest.offer(other.end, threshold)
            nearest.offer(if (growsTowardsEnd) fixedEdge + other.length else fixedEdge - other.length, threshold)
        }
        return SnappedEdge(nearest.value, abs(nearest.value - position))
    }

    /**
     * Offers the grid lines on both sides of [position], turned into the value being snapped by [toCandidate].
     */
    private inline fun offerGridLines(nearest: Nearest, position: Int, axis: SnapAxis, toCandidate: (Int) -> Int) {
        val lineBefore = axis.origin + Math.floorDiv(position - axis.origin, axis.cell) * axis.cell
        nearest.offer(toCandidate(lineBefore))
        nearest.offer(toCandidate(lineBefore + axis.cell))
    }

    /**
     * The start position that centers an item in the editor.
     */
    fun centeredStart(length: Int, limit: Int): Int {
        return (limit - length) / 2
    }

    private fun clamp(start: Int, length: Int, limit: Int): Int {
        return start.coerceAtMost(limit - length).coerceAtLeast(0)
    }

    /**
     * Keeps the candidate closest to [from] among the ones in [min]..[max], or [from] if none is offered.
     */
    private class Nearest(private val from: Int, private val min: Int, private val max: Int) {
        var value = from
            private set
        var distance = Int.MAX_VALUE
            private set

        fun offer(candidate: Int, maxDistance: Int = Int.MAX_VALUE) {
            if (candidate < min || candidate > max) return
            val candidateDistance = abs(candidate - from)
            if (candidateDistance <= maxDistance && candidateDistance < distance) {
                value = candidate
                distance = candidateDistance
            }
        }
    }
}
