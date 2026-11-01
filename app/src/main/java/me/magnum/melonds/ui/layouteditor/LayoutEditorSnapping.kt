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
 * The lines an item can snap to along one axis of the editor (x or y). The grid starts from the middle of the editor, so that centred items are
 * on it, and goes out from there every [cell].
 *
 * @property limit The editor's size on this axis. Both editor edges always count as lines, even when they aren't on the grid
 * @property cell The grid spacing in pixels
 * @property gaps Areas that always count and that an item can sit flush against on either side, like a fold. The start of a gap is a line for
 * an item's end edge, and its end a line for an item's start edge
 * @property otherScreen Where the other screen is on this axis, so that a screen can be lined up with it. Null when not snapping a screen or
 * when there's no other screen
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
 * The snapping maths behind the layout editor's snap grid. Everything works on one axis at a time, in editor pixels.
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
     * Moves a control so that its centre sits on the nearest grid point that keeps it inside the editor. When no grid point fits, the control
     * stays where it was put, inside the editor.
     */
    fun snapCenter(start: Int, length: Int, axis: SnapAxis): Int {
        val cell = axis.cell
        // Counted from the centred position rather than from the middle line minus half the length, so that a control on the middle line is
        // exactly where the centre buttons put it, even with odd sizes
        val centeredStart = centeredStart(length, axis.limit)
        var snapped = centeredStart + Math.round((start - centeredStart) / cell.toFloat()) * cell
        // Step whole cells back inside the editor, so the centre stays on the grid
        if (snapped < 0) snapped += (-snapped + cell - 1) / cell * cell
        if (snapped + length > axis.limit) snapped -= (snapped + length - axis.limit + cell - 1) / cell * cell
        if (snapped < 0 || snapped + length > axis.limit) {
            snapped = start
        }
        return clamp(snapped, length, axis.limit)
    }

    /**
     * Moves a screen so that whichever of its edges is closer to a line sits on it. The lines are the grid, the editor edges and the gap edges.
     * The other screen's edges count when they are within the alignment threshold, and so does its centre for the moved screen's centre.
     */
    fun snapEdges(start: Int, length: Int, axis: SnapAxis): Int {
        val maxStart = axis.limit - length
        if (maxStart < 0) {
            return clamp(start, length, axis.limit)
        }

        // Every candidate is a new start position. Ones that would put the screen outside the editor are skipped, so the result is always on a line
        val nearest = Nearest(start, 0, maxStart)
        val end = start + length
        // The start edge goes first, so it wins ties
        offerGridLines(nearest, start, axis) { it }
        offerGridLines(nearest, end, axis) { it - length }
        nearest.offer(0)
        nearest.offer(maxStart)
        // Where the centre buttons put the screen, so a drag can land back on it
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
     * Snaps an edge that's being dragged to resize an item, while its opposite edge stays at [fixedEdge]. [growsTowardsEnd] says which side of
     * the fixed edge the dragged one is on (right or bottom when true). Besides the lines that [snapEdges] uses, the edge also snaps to where the
     * item would be as long as the other screen, so sizes can be matched.
     */
    fun snapResizedEdge(position: Int, fixedEdge: Int, minLength: Int, growsTowardsEnd: Boolean, axis: SnapAxis): SnappedEdge {
        // The dragged edge stays on its own side of the fixed one, at least the minimum size away. Lines closer than that would only be undone
        // by the minimum size
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
     * Offers the grid lines on both sides of [position]. [toCandidate] turns a line into the value being snapped.
     */
    private inline fun offerGridLines(nearest: Nearest, position: Int, axis: SnapAxis, toCandidate: (Int) -> Int) {
        val lineBefore = axis.origin + Math.floorDiv(position - axis.origin, axis.cell) * axis.cell
        nearest.offer(toCandidate(lineBefore))
        nearest.offer(toCandidate(lineBefore + axis.cell))
    }

    /**
     * The start position that centres an item in the editor. Same as the centre buttons.
     */
    private fun centeredStart(length: Int, limit: Int): Int {
        return (limit - length) / 2
    }

    private fun clamp(start: Int, length: Int, limit: Int): Int {
        return start.coerceAtMost(limit - length).coerceAtLeast(0)
    }

    /**
     * Keeps the candidate closest to [from] among the ones in [min]..[max]. If none is offered, the value stays at [from].
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
