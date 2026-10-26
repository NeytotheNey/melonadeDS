package me.magnum.melonds.impl.layout

import android.content.Context
import android.os.Build
import android.view.WindowInsets
import android.view.WindowManager
import androidx.core.content.getSystemService
import me.magnum.melonds.domain.model.Rect
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.SCREEN_WIDTH
import me.magnum.melonds.domain.model.consoleAspectRatio
import me.magnum.melonds.domain.model.layout.Insets
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.domain.model.layout.LayoutDisplay
import me.magnum.melonds.domain.model.layout.PositionedLayoutComponent
import me.magnum.melonds.domain.model.layout.ScreenFold
import me.magnum.melonds.domain.model.layout.ScreenLayout
import me.magnum.melonds.domain.model.layout.UILayout
import me.magnum.melonds.domain.model.layout.UILayoutVariant
import me.magnum.melonds.domain.model.ui.Orientation
import me.magnum.melonds.impl.ScreenUnitsConverter
import kotlin.math.min
import kotlin.math.roundToInt

class DefaultLayoutProvider(
    private val context: Context,
    private val screenUnitsConverter: ScreenUnitsConverter,
) {

    fun buildDefaultLayout(variant: UILayoutVariant): UILayout {
        val width = variant.uiSize.x
        val height = variant.uiSize.y
        val orientation = variant.orientation
        val folds = variant.folds
        val mainDisplay = variant.displays.mainScreenDisplay
        val secondaryDisplay = variant.displays.secondaryScreenDisplay
        val mainDisplayInsets = variant.uiInsets

        val (mainScreenLayout, secondaryScreenLayout) = if (secondaryDisplay != null) {
            val secondaryDisplayInsets = getDisplaySafeInsets(secondaryDisplay)

            // Prioritise scenarios where a secondary display is present. In this scenario, one screen is shown in each display, and they should take the maximum available
            // space, independently of orientation or folds.

            // Check if it's a dual-screen device (both displays are built-in) or it's using an external display
            if (mainDisplay.type == LayoutDisplay.Type.BUILT_IN && secondaryDisplay.type == LayoutDisplay.Type.BUILT_IN) {
                // Assume that the default display is always the top one
                if (mainDisplay.isDefaultDisplay) {
                    val secondaryDisplayOrientation = if (secondaryDisplay.width > secondaryDisplay.height) Orientation.LANDSCAPE else Orientation.PORTRAIT
                    val secondaryLayout = when (secondaryDisplayOrientation) {
                        Orientation.PORTRAIT -> buildDefaultPortraitLayout(secondaryDisplay.width, secondaryDisplay.height, secondaryDisplayInsets, LayoutComponent.BOTTOM_SCREEN)
                        Orientation.LANDSCAPE -> buildDefaultLandscapeLayout(secondaryDisplay.width, secondaryDisplay.height, secondaryDisplayInsets, LayoutComponent.BOTTOM_SCREEN)
                    }
                    buildSingleScreenLayout(width, height, LayoutComponent.TOP_SCREEN) to secondaryLayout
                } else {
                    val mainLayout = when (orientation) {
                        Orientation.PORTRAIT -> buildDefaultPortraitLayout(width, height, mainDisplayInsets, LayoutComponent.BOTTOM_SCREEN)
                        Orientation.LANDSCAPE -> buildDefaultLandscapeLayout(width, height, mainDisplayInsets, LayoutComponent.BOTTOM_SCREEN)
                    }
                    mainLayout to buildSingleScreenLayout(secondaryDisplay.width, secondaryDisplay.height, LayoutComponent.TOP_SCREEN)
                }
            } else {
                // An external display is being used. Display the bottom screen on the main display, together with all soft input components
                val mainLayout = when (orientation) {
                    Orientation.PORTRAIT -> buildDefaultPortraitLayout(width, height, mainDisplayInsets, LayoutComponent.BOTTOM_SCREEN)
                    Orientation.LANDSCAPE -> buildDefaultLandscapeLayout(width, height, mainDisplayInsets, LayoutComponent.BOTTOM_SCREEN)
                }
                mainLayout to buildSingleScreenLayout(secondaryDisplay.width, secondaryDisplay.height, LayoutComponent.TOP_SCREEN)
            }
        } else {
            val mainScreenLayout = when (orientation) {
                Orientation.PORTRAIT -> {
                    if (folds.any { it.orientation == Orientation.LANDSCAPE }) {
                        // Flip-phone layout
                        buildDefaultFoldingPortraitLayout(width, height, folds, mainDisplayInsets)
                    } else {
                        // Simple portrait layout. Ignore vertical fold since there's no good way to support it
                        buildDefaultPortraitLayout(width, height, mainDisplayInsets)
                    }
                }
                Orientation.LANDSCAPE -> {
                    if (folds.any { it.orientation == Orientation.PORTRAIT }) {
                        // Book layout
                        buildDefaultFoldingLandscapeLayout(width, height, folds, mainDisplayInsets)
                    } else if (folds.any { it.orientation == Orientation.LANDSCAPE }) {
                        // Flip-phone layout
                        buildDefaultFoldingPortraitLayout(width, height, folds, mainDisplayInsets)
                    } else {
                        // No fold
                        buildDefaultLandscapeLayout(width, height, mainDisplayInsets)
                    }
                }
            }
            mainScreenLayout to ScreenLayout()
        }

        return UILayout(mainScreenLayout, secondaryScreenLayout)
    }

    private fun buildDefaultPortraitLayout(width: Int, height: Int, insets: Insets, singleScreenComponent: LayoutComponent? = null): ScreenLayout {
        require(singleScreenComponent?.isScreen() != false) { "When specifying a single screen component, it must be a screen component" }

        val safeLeft = insets.left
        val safeTop = insets.top
        val safeRight = insets.right
        val safeBottom = insets.bottom
        val safeWidth = width - safeLeft - safeRight
        val safeHeight = height - safeTop - safeBottom

        val largeButtonsSize = screenUnitsConverter.dpToPixels(140f).toInt()
        val lrButtonsSize = screenUnitsConverter.dpToPixels(50f).toInt()
        val smallButtonsSize = screenUnitsConverter.dpToPixels(40f).toInt()
        val spacing4dp = screenUnitsConverter.dpToPixels(4f).toInt()

        var screenWidth = safeWidth
        var screenHeight = (safeWidth / consoleAspectRatio).toInt()

        val screenComponents = if (singleScreenComponent == null) {
            var screenMargin = 0
            if (screenHeight * 2 > safeHeight) {
                screenWidth = (safeHeight / 2 * consoleAspectRatio).toInt()
                screenHeight = safeHeight / 2
                screenMargin = (safeWidth - screenWidth) / 2
            }

            val topScreenView = Rect(safeLeft + screenMargin, safeTop, screenWidth, screenHeight)
            val bottomScreenView = Rect(safeLeft + screenMargin, safeTop + screenHeight, screenWidth, screenHeight)

            arrayOf(
                PositionedLayoutComponent(topScreenView, LayoutComponent.TOP_SCREEN),
                PositionedLayoutComponent(bottomScreenView, LayoutComponent.BOTTOM_SCREEN),
            )
        } else {
            // Align screen to the top of the safe area
            val screenArea = Rect(safeLeft, safeTop, screenWidth, screenHeight)
            arrayOf(PositionedLayoutComponent(screenArea, singleScreenComponent))
        }

        val screensBottom = if (singleScreenComponent == null) safeTop + screenHeight * 2 else safeTop + screenHeight
        // Check if there's space to put all the buttons below the screens. If not, place L, R, and utility buttons aligned with the top of the bottom screen
        val utilityButtonsTop = if (screensBottom + lrButtonsSize + largeButtonsSize > height) {
            safeTop + screenHeight
        } else {
            screensBottom
        }

        val dpadView = Rect(safeLeft, height - safeBottom - largeButtonsSize, largeButtonsSize, largeButtonsSize)
        val buttonsView = Rect(width - safeRight - largeButtonsSize, height - safeBottom - largeButtonsSize, largeButtonsSize, largeButtonsSize)

        val components = listOf(
            *screenComponents,
            PositionedLayoutComponent(dpadView, LayoutComponent.DPAD),
            PositionedLayoutComponent(buttonsView, LayoutComponent.BUTTONS),
            PositionedLayoutComponent(Rect(safeLeft, utilityButtonsTop, lrButtonsSize, lrButtonsSize), LayoutComponent.BUTTON_L),
            PositionedLayoutComponent(Rect(width - safeRight - lrButtonsSize, utilityButtonsTop, lrButtonsSize, lrButtonsSize), LayoutComponent.BUTTON_R),
            PositionedLayoutComponent(Rect(width / 2 - smallButtonsSize - spacing4dp / 2, height - safeBottom - smallButtonsSize, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_SELECT),
            PositionedLayoutComponent(Rect(width / 2 + spacing4dp / 2, height - safeBottom - smallButtonsSize, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_START),
        )
        val safeArea = Rect(safeLeft, safeTop, safeWidth, safeHeight)

        return ScreenLayout(components + buildUtilityButtons(safeArea, utilityButtonsTop, lrButtonsSize, smallButtonsSize, spacing4dp, components))
    }

    private fun buildDefaultLandscapeLayout(width: Int, height: Int, insets: Insets, singleScreenComponent: LayoutComponent? = null): ScreenLayout {
        require(singleScreenComponent?.isScreen() != false) { "When specifying a single screen component, it must be a screen component" }

        val safeLeft = insets.left
        val safeTop = insets.top
        val safeRight = insets.right
        val safeBottom = insets.bottom
        val safeWidth = width - safeLeft - safeRight
        val safeHeight = height - safeTop - safeBottom

        val largeButtonsSize = screenUnitsConverter.dpToPixels(140f).toInt()
        val lrButtonsSize = screenUnitsConverter.dpToPixels(50f).toInt()
        val smallButtonsSize = screenUnitsConverter.dpToPixels(40f).toInt()
        val spacing4dp = screenUnitsConverter.dpToPixels(4f).toInt()

        val screenComponents = if (singleScreenComponent == null) {
            var topScreenWidth = (safeWidth * 0.66f).roundToInt()
            var topScreenHeight = (topScreenWidth / consoleAspectRatio).toInt()
            if (topScreenHeight > safeHeight) {
                topScreenWidth = (safeHeight * consoleAspectRatio).toInt()
                topScreenHeight = safeHeight
            }

            val topScreenView = Rect(safeLeft, safeTop, topScreenWidth, topScreenHeight)
            val bottomScreenWidth = safeWidth - topScreenWidth
            val bottomScreenHeight = (bottomScreenWidth / consoleAspectRatio).toInt()
            val bottomScreenView = Rect(safeLeft + topScreenWidth, safeTop, bottomScreenWidth, bottomScreenHeight)

            arrayOf(
                PositionedLayoutComponent(topScreenView, LayoutComponent.TOP_SCREEN),
                PositionedLayoutComponent(bottomScreenView, LayoutComponent.BOTTOM_SCREEN),
            )
        } else {
            val screenArea = centerScreenIn(safeWidth, safeHeight)
            val offsetScreen = Rect(screenArea.x + safeLeft, screenArea.y + safeTop, screenArea.width, screenArea.height)
            arrayOf(PositionedLayoutComponent(offsetScreen, singleScreenComponent))
        }

        val dpadView = Rect(safeLeft, height - safeBottom - largeButtonsSize, largeButtonsSize, largeButtonsSize)
        val buttonsView = Rect(width - safeRight - largeButtonsSize, height - safeBottom - largeButtonsSize, largeButtonsSize, largeButtonsSize)

        return ScreenLayout(
            listOf(
                *screenComponents,
                PositionedLayoutComponent(dpadView, LayoutComponent.DPAD),
                PositionedLayoutComponent(buttonsView, LayoutComponent.BUTTONS),
                PositionedLayoutComponent(Rect(safeLeft, safeTop, lrButtonsSize, lrButtonsSize), LayoutComponent.BUTTON_L),
                PositionedLayoutComponent(Rect(width - safeRight - lrButtonsSize, safeTop, lrButtonsSize, lrButtonsSize), LayoutComponent.BUTTON_R),
                PositionedLayoutComponent(Rect((width - spacing4dp) / 2 - smallButtonsSize, height - safeBottom - smallButtonsSize, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_SELECT),
                PositionedLayoutComponent(Rect((width + spacing4dp) / 2, height - safeBottom - smallButtonsSize, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_START),
                // The menu button goes on the left end of the row, over the top screen side, so that it stays clear of the bottom screen on wide displays
                PositionedLayoutComponent(Rect(width / 2 - (smallButtonsSize * 3.0 + spacing4dp * 2.5).toInt(), safeTop, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_PAUSE),
                PositionedLayoutComponent(Rect(width / 2 - (smallButtonsSize * 2.0 + spacing4dp * 1.5).toInt(), safeTop, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_HINGE),
                PositionedLayoutComponent(Rect(width / 2 - smallButtonsSize - (spacing4dp / 2.0).toInt(), safeTop, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_TOGGLE_SOFT_INPUT),
                PositionedLayoutComponent(Rect(width / 2 + (spacing4dp / 2.0).toInt(), safeTop, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_MICROPHONE_TOGGLE),
                PositionedLayoutComponent(Rect(width / 2 + smallButtonsSize + (spacing4dp * 1.5).toInt(), safeTop, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_FAST_FORWARD_TOGGLE),
            )
        )
    }

    /**
     * Creates a portrait layout that supports a horizontal fold. Screens are attached to either sides of the fold and as large as they can be to fit the screen.
     */
    private fun buildDefaultFoldingPortraitLayout(width: Int, height: Int, folds: List<ScreenFold>, insets: Insets): ScreenLayout {
        // Only one fold is supported for now
        val mainFold = folds.first()

        val safeLeft = insets.left
        val safeTop = insets.top
        val safeRight = insets.right
        val safeBottom = insets.bottom
        val safeWidth = width - safeLeft - safeRight

        val largeButtonsSize = screenUnitsConverter.dpToPixels(140f).toInt()
        val lrButtonsSize = screenUnitsConverter.dpToPixels(50f).toInt()
        val smallButtonsSize = screenUnitsConverter.dpToPixels(40f).toInt()
        val spacing4dp = screenUnitsConverter.dpToPixels(4f).toInt()

        var screenWidth = safeWidth
        var screenHeight = (safeWidth / consoleAspectRatio).toInt()
        val topHalfHeight = mainFold.foldBounds.y - safeTop
        val bottomHalfHeight = height - safeBottom - mainFold.foldBounds.bottom
        var screenMargin = 0
        if (screenHeight > topHalfHeight || screenHeight > bottomHalfHeight) {
            val limitingHeight = min(topHalfHeight, bottomHalfHeight)
            screenWidth = (limitingHeight * consoleAspectRatio).toInt()
            screenHeight = limitingHeight
            screenMargin = (safeWidth - screenWidth) / 2
        }

        val topScreenView = Rect(safeLeft + screenMargin, mainFold.foldBounds.y - screenHeight, screenWidth, screenHeight)
        val bottomScreenView = Rect(safeLeft + screenMargin, mainFold.foldBounds.bottom, screenWidth, screenHeight)
        val dpadView = Rect(safeLeft, height - safeBottom - largeButtonsSize, largeButtonsSize, largeButtonsSize)
        val buttonsView = Rect(width - safeRight - largeButtonsSize, height - safeBottom - largeButtonsSize, largeButtonsSize, largeButtonsSize)

        val components = listOf(
            PositionedLayoutComponent(topScreenView, LayoutComponent.TOP_SCREEN),
            PositionedLayoutComponent(bottomScreenView, LayoutComponent.BOTTOM_SCREEN),
            PositionedLayoutComponent(dpadView, LayoutComponent.DPAD),
            PositionedLayoutComponent(buttonsView, LayoutComponent.BUTTONS),
            PositionedLayoutComponent(Rect(safeLeft, mainFold.foldBounds.bottom, lrButtonsSize, lrButtonsSize), LayoutComponent.BUTTON_L),
            PositionedLayoutComponent(Rect(width - safeRight - lrButtonsSize, mainFold.foldBounds.bottom, lrButtonsSize, lrButtonsSize), LayoutComponent.BUTTON_R),
            PositionedLayoutComponent(Rect(width / 2 - smallButtonsSize - spacing4dp / 2, height - safeBottom - smallButtonsSize, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_SELECT),
            PositionedLayoutComponent(Rect(width / 2 + spacing4dp / 2, height - safeBottom - smallButtonsSize, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_START),
        )
        val safeArea = Rect(safeLeft, safeTop, safeWidth, height - safeTop - safeBottom)

        return ScreenLayout(components + buildUtilityButtons(safeArea, mainFold.foldBounds.bottom, lrButtonsSize, smallButtonsSize, spacing4dp, components))
    }

    /**
     * Creates a landscape layout that supports a vertical fold. Screens are attached to either sides of the fold and as large as they can be to fit the screen.
     */
    private fun buildDefaultFoldingLandscapeLayout(width: Int, height: Int, folds: List<ScreenFold>, insets: Insets): ScreenLayout {
        // Only one fold is supported for now
        val mainFold = folds.first()

        val safeLeft = insets.left
        val safeTop = insets.top
        val safeRight = insets.right
        val safeBottom = insets.bottom
        val safeHeight = height - safeTop - safeBottom

        val largeButtonsSize = screenUnitsConverter.dpToPixels(140f).toInt()
        val lrButtonsSize = screenUnitsConverter.dpToPixels(50f).toInt()
        val smallButtonsSize = screenUnitsConverter.dpToPixels(40f).toInt()
        val spacing8dp = screenUnitsConverter.dpToPixels(8f).toInt()

        // Position to the left of the fold, attached to the fold. As big as the screen allows
        var topScreenWidth = mainFold.foldBounds.x - safeLeft
        var topScreenHeight = (topScreenWidth / consoleAspectRatio).toInt()
        if (topScreenHeight > safeHeight) {
            topScreenHeight = safeHeight
            topScreenWidth = (safeHeight * consoleAspectRatio).toInt()
        }

        // Position to the right of the fold, attached to the fold. As big as the screen allows
        var bottomScreenWidth = width - safeRight - mainFold.foldBounds.right
        var bottomScreenHeight = (bottomScreenWidth / consoleAspectRatio).toInt()
        if (bottomScreenHeight > safeHeight) {
            bottomScreenHeight = safeHeight
            bottomScreenWidth = (safeHeight * consoleAspectRatio).toInt()
        }

        // Check if the screens are small enough to be positioned under the L/R buttons
        val screenYPos = if (topScreenHeight < (safeHeight - lrButtonsSize - spacing8dp) && bottomScreenHeight < (safeHeight - lrButtonsSize - spacing8dp)) {
            safeTop + lrButtonsSize + spacing8dp
        } else {
            safeTop
        }

        val topScreenView = Rect(mainFold.foldBounds.x - topScreenWidth, screenYPos, topScreenWidth, topScreenHeight)
        val bottomScreenView = Rect(mainFold.foldBounds.right, screenYPos, bottomScreenWidth, bottomScreenHeight)
        val dpadView = Rect(safeLeft, height - safeBottom - largeButtonsSize, largeButtonsSize, largeButtonsSize)
        val buttonsView = Rect(width - safeRight - largeButtonsSize, height - safeBottom - largeButtonsSize, largeButtonsSize, largeButtonsSize)

        return ScreenLayout(
            listOf(
                PositionedLayoutComponent(topScreenView, LayoutComponent.TOP_SCREEN),
                PositionedLayoutComponent(bottomScreenView, LayoutComponent.BOTTOM_SCREEN),
                PositionedLayoutComponent(dpadView, LayoutComponent.DPAD),
                PositionedLayoutComponent(buttonsView, LayoutComponent.BUTTONS),
                PositionedLayoutComponent(Rect(safeLeft, safeTop, lrButtonsSize, lrButtonsSize), LayoutComponent.BUTTON_L),
                PositionedLayoutComponent(Rect(width - safeRight - lrButtonsSize, safeTop, lrButtonsSize, lrButtonsSize), LayoutComponent.BUTTON_R),
                PositionedLayoutComponent(Rect(mainFold.foldBounds.x - smallButtonsSize - spacing8dp, height - safeBottom - smallButtonsSize, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_SELECT),
                PositionedLayoutComponent(Rect(mainFold.foldBounds.right + spacing8dp, height - safeBottom - smallButtonsSize, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_START),
                PositionedLayoutComponent(Rect(mainFold.foldBounds.x - smallButtonsSize * 2 - spacing8dp * 2, safeTop, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_HINGE),
                PositionedLayoutComponent(Rect(mainFold.foldBounds.x - smallButtonsSize - spacing8dp, safeTop, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_TOGGLE_SOFT_INPUT),
                PositionedLayoutComponent(Rect(mainFold.foldBounds.right + spacing8dp, safeTop, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_MICROPHONE_TOGGLE),
                PositionedLayoutComponent(Rect(mainFold.foldBounds.right + smallButtonsSize + spacing8dp * 2, safeTop, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_FAST_FORWARD_TOGGLE),
                PositionedLayoutComponent(Rect(mainFold.foldBounds.right + smallButtonsSize * 2 + spacing8dp * 3, safeTop, smallButtonsSize, smallButtonsSize), LayoutComponent.BUTTON_PAUSE),
            )
        )
    }

    /**
     * Builds the row of small utility buttons (lid, soft input, microphone, fast forward and menu) at [rowTop], horizontally centered in [safeArea]. The row shares its line
     * with the L and R buttons, which sit on the edges of the safe area. If the safe area is too narrow to fit all the buttons between L and R, the menu button is taken out
     * of the row and placed in the closest free spot directly above or below the center of the row.
     */
    private fun buildUtilityButtons(
        safeArea: Rect,
        rowTop: Int,
        lrButtonsSize: Int,
        buttonSize: Int,
        spacing: Int,
        otherComponents: List<PositionedLayoutComponent>,
    ): List<PositionedLayoutComponent> {
        val rowComponents = listOf(
            LayoutComponent.BUTTON_HINGE,
            LayoutComponent.BUTTON_TOGGLE_SOFT_INPUT,
            LayoutComponent.BUTTON_MICROPHONE_TOGGLE,
            LayoutComponent.BUTTON_FAST_FORWARD_TOGGLE,
            LayoutComponent.BUTTON_PAUSE,
        )
        val centerX = safeArea.x + safeArea.width / 2
        val fullRowWidth = buttonSize * rowComponents.size + spacing * (rowComponents.size - 1)
        if (fullRowWidth + (lrButtonsSize + spacing) * 2 <= safeArea.width) {
            return buildCenteredButtonRow(rowComponents, centerX, rowTop, buttonSize, spacing)
        }

        val row = buildCenteredButtonRow(rowComponents - LayoutComponent.BUTTON_PAUSE, centerX, rowTop, buttonSize, spacing)
        val menuButtonRect = findFreeSpotAboveOrBelowRow(safeArea, centerX - buttonSize / 2, rowTop, buttonSize, spacing, otherComponents + row) ?: run {
            // There's no free space around the row. Place the button over the top-right corner of the top screen instead, since that screen doesn't take any input
            val screenRect = (otherComponents.firstOrNull { it.component == LayoutComponent.TOP_SCREEN } ?: otherComponents.first { it.isScreen() }).rect
            Rect(screenRect.right - spacing - buttonSize, screenRect.y + spacing, buttonSize, buttonSize)
        }

        return row + PositionedLayoutComponent(menuButtonRect, LayoutComponent.BUTTON_PAUSE)
    }

    private fun buildCenteredButtonRow(components: List<LayoutComponent>, centerX: Int, top: Int, buttonSize: Int, spacing: Int): List<PositionedLayoutComponent> {
        val rowWidth = buttonSize * components.size + spacing * (components.size - 1)
        val rowLeft = centerX - rowWidth / 2

        return components.mapIndexed { index, component ->
            PositionedLayoutComponent(Rect(rowLeft + index * (buttonSize + spacing), top, buttonSize, buttonSize), component)
        }
    }

    /**
     * Looks for a spot for a button at [x], directly above or below the row that starts at [rowTop], that doesn't overlap any of the [placedComponents]. The button is placed
     * [spacing] away from the row, or centered in the free space if there isn't enough room for that.
     */
    private fun findFreeSpotAboveOrBelowRow(safeArea: Rect, x: Int, rowTop: Int, buttonSize: Int, spacing: Int, placedComponents: List<PositionedLayoutComponent>): Rect? {
        val rowBottom = rowTop + buttonSize
        val columnRects = placedComponents.map { it.rect }.filter { it.x < x + buttonSize && x < it.right }

        val freeSpaceTop = columnRects.map { it.bottom }.filter { it <= rowTop }.fold(safeArea.y) { top, bottom -> maxOf(top, bottom) }
        val aboveRect = Rect(x, rowTop - buttonSize - min(spacing, (rowTop - freeSpaceTop - buttonSize) / 2), buttonSize, buttonSize)

        val freeSpaceBottom = columnRects.map { it.y }.filter { it >= rowBottom }.fold(safeArea.bottom) { bottom, top -> min(bottom, top) }
        val belowRect = Rect(x, rowBottom + min(spacing, (freeSpaceBottom - rowBottom - buttonSize) / 2), buttonSize, buttonSize)

        return listOf(aboveRect, belowRect).firstOrNull { candidate ->
            safeArea.contains(candidate) && placedComponents.none { it.rect.overlaps(candidate) }
        }
    }

    private fun Rect.overlaps(other: Rect): Boolean {
        return x < other.right && other.x < right && y < other.bottom && other.y < bottom
    }

    private fun buildSingleScreenLayout(width: Int, height: Int, screenComponent: LayoutComponent): ScreenLayout {
        val screenView = centerScreenIn(width, height)
        val positionedScreenComponent = PositionedLayoutComponent(screenView, screenComponent)

        return ScreenLayout(listOf(positionedScreenComponent))
    }

    private fun centerScreenIn(width: Int, height: Int): Rect {
        val areaAspectRatio = width.toFloat() / height
        return if (areaAspectRatio > consoleAspectRatio) {
            // Center horizontally
            val scale = height.toFloat() / SCREEN_HEIGHT
            val scaledWidth = (SCREEN_WIDTH * scale).toInt()
            Rect((width - scaledWidth) / 2, 0, scaledWidth, height)
        } else {
            // Center vertically
            val scale = width.toFloat() / SCREEN_WIDTH
            val scaledHeight = (SCREEN_HEIGHT * scale).toInt()
            Rect(0, (height - scaledHeight) / 2, width, scaledHeight)
        }
    }

    private fun getDisplaySafeInsets(display: LayoutDisplay): Insets {
        if (!display.isDefaultDisplay) {
            return Insets.Zero
        }

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            return Insets.Zero
        }

        val windowManager = context.getSystemService<WindowManager>() ?: return Insets.Zero

        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val cutoutInsets = windowManager.currentWindowMetrics.windowInsets.getInsets(WindowInsets.Type.displayCutout())
            Insets(cutoutInsets.left, cutoutInsets.top, cutoutInsets.right, cutoutInsets.bottom)
        } else {
            @Suppress("DEPRECATION")
            val displayCutout = windowManager.defaultDisplay.cutout ?: return Insets.Zero
            Insets(displayCutout.safeInsetLeft, displayCutout.safeInsetTop, displayCutout.safeInsetRight, displayCutout.safeInsetBottom)
        }
    }
}