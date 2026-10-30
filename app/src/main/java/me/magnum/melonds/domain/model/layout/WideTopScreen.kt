package me.magnum.melonds.domain.model.layout

import me.magnum.melonds.domain.model.Rect
import kotlin.math.abs
import kotlin.math.roundToInt

// How far a top screen's rect can fall short of the wide width and still count as wide enough, so that the editor's rounding doesn't push a
// rect that was made for that width into being widened again
private const val WIDE_RECT_TOLERANCE = 2

/**
 * Where a top screen drawn at [aspectRatio] (wider than the DS's 4:3) goes, for a layout rect [rect] on a surface [surfaceWidth] wide. A rect
 * that's already that wide or wider (a 16:9 layout) gets it in its middle, at the rect's height, and one that's within WIDE_RECT_TOLERANCE of
 * it is used as it is. A narrower one grows sideways around its centre, up to the surface's edges and the bottom screen's rect [bottomRect]
 * when it shares rows. If that's not enough room, it gets as wide as it can and shorter, in the middle of [rect].
 *
 * DSRenderer draws a wide top screen with this, and the layout editor uses it to widen a wide layout's top screens when its ratio is set
 */
fun getWideTopScreenRect(rect: Rect, bottomRect: Rect?, aspectRatio: Float, surfaceWidth: Float): Rect {
    val centreX = rect.x + rect.width / 2f
    val wantedWidth = rect.height * aspectRatio
    if (rect.width >= wantedWidth) {
        return buildCentredRect(centreX, rect.y.toFloat(), wantedWidth, rect.height.toFloat())
    }
    if (rect.width >= wantedWidth - WIDE_RECT_TOLERANCE) {
        return rect
    }

    // A bottom screen that shares rows with the top screen stops it at its near edge, on whichever side its centre is. That edge never
    // counts as further in than the top screen's own one, so a bottom screen that overlaps the rect only stops the growth on that side
    var minX = 0f
    var maxX = surfaceWidth
    if (bottomRect != null && bottomRect.y < rect.bottom && rect.y < bottomRect.bottom) {
        if (bottomRect.x + bottomRect.width / 2f >= centreX) {
            maxX = minOf(maxX, maxOf(bottomRect.x, rect.right).toFloat())
        } else {
            minX = maxOf(minX, minOf(bottomRect.right, rect.x).toFloat())
        }
    }

    // Never narrower than the layout's own rect, even if that's partly off the surface. The height is rounded first and the width worked out
    // from it, so that the rect lands within WIDE_RECT_TOLERANCE of the ratio and a rect made with this isn't widened again in game
    val drawWidth = minOf(wantedWidth, 2f * minOf(centreX - minX, maxX - centreX)).coerceAtLeast(rect.width.toFloat())
    val drawHeight = (drawWidth / aspectRatio).roundToInt().toFloat()
    return buildCentredRect(centreX, rect.y + (rect.height - drawHeight) / 2f, drawHeight * aspectRatio, drawHeight)
}

/**
 * The biggest rect at [aspectRatio] that fits in [rect], in its middle. A rect that's within WIDE_RECT_TOLERANCE of that shape is used as
 * it is. DSRenderer draws a wide layout's top screen like this, so the frame keeps its own shape in the rect the player placed
 */
fun getLetterboxedRect(rect: Rect, aspectRatio: Float): Rect {
    if (abs(rect.width - rect.height * aspectRatio) <= WIDE_RECT_TOLERANCE) {
        return rect
    }
    val fittedWidth = minOf(rect.width.toFloat(), rect.height * aspectRatio)
    val fittedHeight = fittedWidth / aspectRatio
    return buildCentredRect(rect.x + rect.width / 2f, rect.y + (rect.height - fittedHeight) / 2f, fittedWidth, fittedHeight)
}

/**
 * [layout] with the top screen of each of its screen layouts placed with [getWideTopScreenRect] for [aspect]. [variant] gives the surface
 * sizes: the main screen layout is on the variant's UI and the secondary one on its secondary display. Screen layouts that were never edited
 * (no components) are left alone
 */
fun widenTopScreens(layout: UILayout, variant: UILayoutVariant, aspect: WidescreenAspect): UILayout {
    return layout.copy(
        mainScreenLayout = widenTopScreen(layout.mainScreenLayout, variant.uiSize.x, aspect),
        secondaryScreenLayout = variant.displays.secondaryScreenDisplay?.let {
            widenTopScreen(layout.secondaryScreenLayout, it.width, aspect)
        } ?: layout.secondaryScreenLayout,
    )
}

private fun widenTopScreen(screenLayout: ScreenLayout, surfaceWidth: Int, aspect: WidescreenAspect): ScreenLayout {
    val components = screenLayout.components ?: return screenLayout
    val bottomRect = components.firstOrNull { it.component == LayoutComponent.BOTTOM_SCREEN }?.rect
    return screenLayout.copy(
        components = components.map {
            if (it.component == LayoutComponent.TOP_SCREEN) {
                it.copy(rect = getWideTopScreenRect(it.rect, bottomRect, aspect.aspectRatio, surfaceWidth.toFloat()))
            } else {
                it
            }
        },
    )
}

fun buildCentredRect(centreX: Float, top: Float, rectWidth: Float, rectHeight: Float): Rect {
    // The edges are rounded rather than the sizes, so that the rect stays centred
    val left = (centreX - rectWidth / 2f).roundToInt()
    val right = (centreX + rectWidth / 2f).roundToInt()
    val y = top.roundToInt()
    return Rect(left, y, right - left, (top + rectHeight).roundToInt() - y)
}
