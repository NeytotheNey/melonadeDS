package me.magnum.melonds.impl.layout

import me.magnum.melonds.domain.model.Rect
import me.magnum.melonds.domain.model.layout.Insets
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.domain.model.layout.PositionedLayoutComponent
import me.magnum.melonds.domain.model.layout.ScreenLayout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The default layouts' utility button rows. The layouts are built at a density of 1, so sizes are in dp: L/R are 50, the small buttons 40 and the spacing 4
 */
class UtilityButtonsTest {

    private val builder = DefaultLayoutBuilder { it }

    private val utilityButtons = setOf(
        LayoutComponent.BUTTON_PAUSE,
        LayoutComponent.BUTTON_SWAP_SCREENS,
        LayoutComponent.BUTTON_HINGE,
        LayoutComponent.BUTTON_TOGGLE_SOFT_INPUT,
        LayoutComponent.BUTTON_MICROPHONE_TOGGLE,
        LayoutComponent.BUTTON_FAST_FORWARD_TOGGLE,
    )

    @Test
    fun wideEnoughPortraitPhoneFitsTheWholeRow() {
        val layout = builder.buildDefaultPortraitLayout(411, 900, Insets.Zero)

        val rowButtons = getRowButtons(layout)
        assertEquals(utilityButtons, rowButtons.map { it.component }.toSet())
        assertEquals(LayoutComponent.BUTTON_HINGE, rowButtons[rowButtons.indexOfFirst { it.component == LayoutComponent.BUTTON_SWAP_SCREENS } + 1].component)
        assertNoOverlaps(layout)
    }

    @Test
    fun narrowPortraitPhoneMovesTheMenuButtonUnderTheRow() {
        val layout = builder.buildDefaultPortraitLayout(360, 780, Insets.Zero)

        assertEquals(utilityButtons - LayoutComponent.BUTTON_PAUSE, getRowButtons(layout).map { it.component }.toSet())
        // Centered, a spacing under the row (which is under the screens). Above it is the bottom screen
        assertEquals(Rect(160, rowTop(layout) + 44, 40, 40), layout.rectOf(LayoutComponent.BUTTON_PAUSE))
        assertNoOverlaps(layout)
    }

    @Test
    fun coverScreenMovesTheMenuAndSwapButtonsUnderTheRow() {
        val layout = builder.buildDefaultPortraitLayout(317, 700, Insets.Zero)

        assertEquals(utilityButtons - LayoutComponent.BUTTON_PAUSE - LayoutComponent.BUTTON_SWAP_SCREENS, getRowButtons(layout).map { it.component }.toSet())
        // Together, in their row order, centered under the row. The D-pad's top is less than two spacings under them, so they're halfway
        val swapRect = layout.rectOf(LayoutComponent.BUTTON_SWAP_SCREENS)
        assertEquals(Rect(116, swapRect.y, 40, 40), swapRect)
        assertEquals(Rect(160, swapRect.y, 40, 40), layout.rectOf(LayoutComponent.BUTTON_PAUSE))
        assertEquals(rowTop(layout) + 40 + 3, swapRect.y)
        assertEquals(layout.rectOf(LayoutComponent.DPAD).y - 3, swapRect.bottom)
        assertNoOverlaps(layout)
    }

    @Test
    fun landscapePhoneFitsTheWholeRowWithTheMenuOnTheLeft() {
        val layout = builder.buildDefaultLandscapeLayout(640, 360, Insets.Zero)

        val rowButtons = getRowButtons(layout)
        assertEquals(utilityButtons, rowButtons.map { it.component }.toSet())
        assertEquals(LayoutComponent.BUTTON_PAUSE, rowButtons.first().component)
        assertEquals(LayoutComponent.BUTTON_HINGE, rowButtons[rowButtons.indexOfFirst { it.component == LayoutComponent.BUTTON_SWAP_SCREENS } + 1].component)
        // Soft input and microphone either side of the middle
        assertEquals(318, layout.rectOf(LayoutComponent.BUTTON_TOGGLE_SOFT_INPUT).right)
        assertEquals(322, layout.rectOf(LayoutComponent.BUTTON_MICROPHONE_TOGGLE).x)
        assertNoOverlaps(layout)
    }

    @Test
    fun narrowLandscapeWindowMovesTheMenuButtonToASecondRow() {
        val layout = builder.buildDefaultLandscapeLayout(400, 360, Insets.Zero)

        assertEquals(utilityButtons - LayoutComponent.BUTTON_PAUSE, getRowButtons(layout).map { it.component }.toSet())
        // Right under the start of what's left of the row (swap screens)
        assertEquals(Rect(70, 44, 40, 40), layout.rectOf(LayoutComponent.BUTTON_PAUSE))
        assertNoOverlaps(layout)
    }

    @Test
    fun shortLandscapeWindowKeepsTheMovedButtonOffTheDpad() {
        val layout = builder.buildDefaultLandscapeLayout(400, 220, Insets.Zero)

        assertEquals(utilityButtons - LayoutComponent.BUTTON_PAUSE, getRowButtons(layout).map { it.component }.toSet())
        assertEquals(44, layout.rectOf(LayoutComponent.BUTTON_PAUSE).y)
        assertNoOverlaps(layout)
        assertClearOfBottomScreen(layout, LayoutComponent.BUTTON_PAUSE)
    }

    @Test
    fun shortNarrowLandscapeWindowKeepsTheMovedButtonsOffTheControls() {
        val layout = builder.buildDefaultLandscapeLayout(340, 200, Insets.Zero)

        assertEquals(utilityButtons - LayoutComponent.BUTTON_PAUSE - LayoutComponent.BUTTON_SWAP_SCREENS, getRowButtons(layout).map { it.component }.toSet())
        assertNoOverlaps(layout)
        assertClearOfBottomScreen(layout, LayoutComponent.BUTTON_PAUSE)
        assertClearOfBottomScreen(layout, LayoutComponent.BUTTON_SWAP_SCREENS)
    }

    /**
     * The utility buttons on L's line, left to right
     */
    private fun getRowButtons(layout: ScreenLayout): List<PositionedLayoutComponent> {
        val rowTop = rowTop(layout)
        return layout.components.orEmpty().filter { it.component in utilityButtons && it.rect.y == rowTop }.sortedBy { it.rect.x }
    }

    private fun rowTop(layout: ScreenLayout): Int {
        return layout.rectOf(LayoutComponent.BUTTON_L).y
    }

    private fun ScreenLayout.rectOf(component: LayoutComponent): Rect {
        return components.orEmpty().first { it.component == component }.rect
    }

    /**
     * No utility button overlaps another component, except the screens, which buttons may go over. Overlaps between the other components are
     * none of the rows' business
     */
    private fun assertNoOverlaps(layout: ScreenLayout) {
        val components = layout.components.orEmpty()
        assertEquals(utilityButtons, components.filter { it.component in utilityButtons }.map { it.component }.toSet())
        components.filter { it.component in utilityButtons }.forEach { button ->
            components.filter { it !== button && !it.isScreen() }.forEach {
                assertFalse("${button.component} at ${button.rect} overlaps ${it.component} at ${it.rect}", button.rect.overlaps(it.rect))
            }
        }
    }

    private fun assertClearOfBottomScreen(layout: ScreenLayout, component: LayoutComponent) {
        val rect = layout.rectOf(component)
        assertTrue("$component at $rect is over the bottom screen", !rect.overlaps(layout.rectOf(LayoutComponent.BOTTOM_SCREEN)))
    }
}
