package me.magnum.melonds.ui.layouteditor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LayoutEditorSnappingTest {

    // With a 48 px grid on a 1000 px editor, the lines are 500 ± 48 * n: ..., 404, 452, 500, 548, 596, ...
    private val axis = SnapAxis(limit = 1000, cell = 48)

    // A grid this coarse only has its middle line (500), so the other screen's lines can be tested on their own
    private fun coarseAxis(gaps: List<SnapSpan> = emptyList(), otherScreen: SnapSpan? = null) =
        SnapAxis(limit = 1000, cell = 1000, gaps = gaps, otherScreen = otherScreen)

    // Grid lines

    @Test
    fun gridLinesStartFromTheMiddleAndIncludeBothEdges() {
        // Origin 50, so the lines are 18, 50 and 82, plus the editor edges
        assertEquals(listOf(0, 18, 50, 82, 100), LayoutEditorSnapping.gridLines(100, 32))
    }

    @Test
    fun gridLinesDontRepeatEdgesThatAreOnTheGrid() {
        assertEquals(listOf(0, 32, 64), LayoutEditorSnapping.gridLines(64, 32))
    }

    @Test
    fun gridLinesRunThroughTheMiddleOfTheCoverScreen() {
        // The 1248 px cover screen, where a grid counted from 0 left centred controls off the grid
        val lines = LayoutEditorSnapping.gridLines(1248, 32)
        assertTrue(624 in lines)
        assertEquals(listOf(0, 16, 48), lines.take(3))
        assertEquals(listOf(1232, 1248), lines.takeLast(2))
    }

    // Controls

    @Test
    fun controlCentreSnapsToNearestGridPoint() {
        // Centred, the 100 px control starts at 450 with its centre on 500. Moved to 480, its centre (530) is closer to 548 than to 500
        assertEquals(498, LayoutEditorSnapping.snapCenter(480, 100, axis))
        assertEquals(450, LayoutEditorSnapping.snapCenter(465, 100, axis))
    }

    @Test
    fun centredControlStaysWhereTheCentreButtonsPutIt() {
        // Odd sizes on the cover screen, where half the length gets rounded
        val coverAxis = SnapAxis(limit = 1248, cell = 42)
        val centredStart = (1248 - 75) / 2
        assertEquals(centredStart, LayoutEditorSnapping.snapCenter(centredStart, 75, coverAxis))
    }

    @Test
    fun controlNearAnEdgeStepsBackInsideOnTheGrid() {
        // The nearest grid point would put the 160 px control 12 px past the edge, so it moves one cell inwards, still on the grid (centres at
        // 116 and 884, 8 cells either side of the middle)
        assertEquals(36, LayoutEditorSnapping.snapCenter(0, 160, axis))
        assertEquals(804, LayoutEditorSnapping.snapCenter(840, 160, axis))
    }

    // Screens

    @Test
    fun screenSnapsWhicheverEdgeIsCloserToALine() {
        // Start edge 396 is 8 from 404; end edge 646 is 2 from 644
        assertEquals(394, LayoutEditorSnapping.snapEdges(396, 250, axis))
        // Start edge 402 is 2 from 404; end edge 652 is 8 from 644
        assertEquals(404, LayoutEditorSnapping.snapEdges(402, 250, axis))
    }

    @Test
    fun startEdgeWinsATie() {
        // Start edge 423 and end edge 673 are both 19 from a line (404 and 692)
        assertEquals(404, LayoutEditorSnapping.snapEdges(423, 250, axis))
    }

    @Test
    fun screenSnapsToTheEditorEdges() {
        assertEquals(0, LayoutEditorSnapping.snapEdges(8, 300, coarseAxis()))
        assertEquals(700, LayoutEditorSnapping.snapEdges(690, 300, coarseAxis()))
    }

    @Test
    fun screenCanLandBackOnTheCentredPosition() {
        // Neither edge of the centred 251 px screen (374..625) is on a line, but the centred position still counts
        assertEquals(374, LayoutEditorSnapping.snapEdges(378, 251, axis))
    }

    @Test
    fun screenSitsFlushOnEitherSideOfAFold() {
        val fold = SnapSpan(490, 20)
        val foldAxis = coarseAxis(gaps = listOf(fold))
        // Just after the fold, the start edge goes to its far side
        assertEquals(510, LayoutEditorSnapping.snapEdges(515, 200, foldAxis))
        // Just before it, the end edge goes to its near side
        assertEquals(290, LayoutEditorSnapping.snapEdges(285, 200, foldAxis))
    }

    @Test
    fun screenLinesUpWithTheOtherScreen() {
        // The other screen is at 100..460, so the threshold is 5% of 360 = 18 and its centre is 280
        val otherAxis = coarseAxis(otherScreen = SnapSpan(100, 360))
        // Start edges line up
        assertEquals(100, LayoutEditorSnapping.snapEdges(110, 300, otherAxis))
        // End edges line up
        assertEquals(160, LayoutEditorSnapping.snapEdges(165, 300, otherAxis))
        // Centres line up
        assertEquals(130, LayoutEditorSnapping.snapEdges(135, 300, otherAxis))
        // Flush side by side: this screen's start on the other's end
        assertEquals(460, LayoutEditorSnapping.snapEdges(470, 300, otherAxis))
    }

    @Test
    fun otherScreenOnlyCountsWithinTheThreshold() {
        val otherAxis = coarseAxis(otherScreen = SnapSpan(100, 360))
        // 25 px from the other screen's start is past the 18 px threshold, so the screen goes to the editor edge instead
        assertEquals(0, LayoutEditorSnapping.snapEdges(75, 300, otherAxis))
    }

    // Corner-handle resizes

    @Test
    fun resizedEdgeSnapsToNearestLine() {
        assertEquals(SnappedEdge(404, 4), LayoutEditorSnapping.snapResizedEdge(400, 100, 50, true, axis))
        // Dragging the start edge, with the end edge fixed
        assertEquals(SnappedEdge(452, 2), LayoutEditorSnapping.snapResizedEdge(450, 800, 50, false, axis))
    }

    @Test
    fun resizedEdgeNeverSnapsBelowTheMinimumSize() {
        // Fixed at 100 with a 50 px minimum, the edge can't go below 150. Line 116 is closer to the finger, but 164 is the nearest allowed one
        assertEquals(SnappedEdge(164, 44), LayoutEditorSnapping.snapResizedEdge(120, 100, 50, true, axis))
        // A finger dragged past the fixed edge snaps from the minimum size
        assertEquals(164, LayoutEditorSnapping.snapResizedEdge(50, 100, 50, true, axis).position)
        // Same when dragging the start edge: fixed at 600, the edge can't go past 550, so the finger at 580 snaps from there to 548
        assertEquals(SnappedEdge(548, 32), LayoutEditorSnapping.snapResizedEdge(580, 600, 50, false, axis))
    }

    @Test
    fun resizedEdgeIsLeftAloneWhenTheMinimumSizeDoesntFit() {
        assertEquals(SnappedEdge(990, Int.MAX_VALUE), LayoutEditorSnapping.snapResizedEdge(990, 980, 50, true, axis))
    }

    @Test
    fun resizedEdgeStopsFlushAgainstAFold() {
        val foldAxis = coarseAxis(gaps = listOf(SnapSpan(490, 20)))
        // The end edge stops before the fold, the start edge after it
        assertEquals(490, LayoutEditorSnapping.snapResizedEdge(485, 100, 50, true, foldAxis).position)
        assertEquals(510, LayoutEditorSnapping.snapResizedEdge(515, 800, 50, false, foldAxis).position)
    }

    @Test
    fun resizedEdgeMatchesTheOtherScreen() {
        // The other screen is at 600..900, so the threshold is 15 px
        val otherAxis = coarseAxis(otherScreen = SnapSpan(600, 300))
        // Same size as the other screen: 100 + 300
        assertEquals(400, LayoutEditorSnapping.snapResizedEdge(410, 100, 50, true, otherAxis).position)
        // Lined up with the other screen's start
        assertEquals(600, LayoutEditorSnapping.snapResizedEdge(590, 100, 50, true, otherAxis).position)
    }
}
