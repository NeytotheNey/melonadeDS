package me.magnum.melonds.domain.model.layout

import me.magnum.melonds.domain.model.Rect
import kotlin.math.abs
import kotlin.math.roundToInt

// Tolerance in px below which a rect counts as already wide, so the editor's rounding doesn't get a rect widened again
private const val WIDE_RECT_TOLERANCE = 2

/**
 * Where a top screen drawn at [aspectRatio] goes for layout rect [rect]: centered if already wide, else grown sideways up to the surface and
 * [bottomRect], else shortened.
 */
fun getWideTopScreenRect(rect: Rect, bottomRect: Rect?, aspectRatio: Float, surfaceWidth: Float): Rect {
    val centerX = rect.x + rect.width / 2f
    val wantedWidth = rect.height * aspectRatio
    if (rect.width >= wantedWidth) {
        return buildCenteredRect(centerX, rect.y.toFloat(), wantedWidth, rect.height.toFloat())
    }
    if (rect.width >= wantedWidth - WIDE_RECT_TOLERANCE) {
        return rect
    }

    // A bottom screen that shares rows stops the growth at its near edge, on whichever side its center is
    var minX = 0f
    var maxX = surfaceWidth
    if (bottomRect != null && bottomRect.y < rect.bottom && rect.y < bottomRect.bottom) {
        if (bottomRect.x + bottomRect.width / 2f >= centerX) {
            maxX = minOf(maxX, maxOf(bottomRect.x, rect.right).toFloat())
        } else {
            minX = maxOf(minX, minOf(bottomRect.right, rect.x).toFloat())
        }
    }

    // Never narrower than the layout's own rect; the height is rounded first so the result lands within WIDE_RECT_TOLERANCE of the ratio
    val drawWidth = minOf(wantedWidth, 2f * minOf(centerX - minX, maxX - centerX)).coerceAtLeast(rect.width.toFloat())
    val drawHeight = (drawWidth / aspectRatio).roundToInt().toFloat()
    return buildCenteredRect(centerX, rect.y + (rect.height - drawHeight) / 2f, drawHeight * aspectRatio, drawHeight)
}

/**
 * Biggest [aspectRatio] rect centered in [rect], or [rect] itself if within tolerance.
 */
fun getLetterboxedRect(rect: Rect, aspectRatio: Float): Rect {
    if (abs(rect.width - rect.height * aspectRatio) <= WIDE_RECT_TOLERANCE) {
        return rect
    }
    val fittedWidth = minOf(rect.width.toFloat(), rect.height * aspectRatio)
    val fittedHeight = fittedWidth / aspectRatio
    return buildCenteredRect(rect.x + rect.width / 2f, rect.y + (rect.height - fittedHeight) / 2f, fittedWidth, fittedHeight)
}

/**
 * [layout] with each edited screen layout's top screen widened for [aspect] on [variant]'s surfaces.
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

fun buildCenteredRect(centerX: Float, top: Float, rectWidth: Float, rectHeight: Float): Rect {
    // Edges are rounded, not sizes, so the rect stays centered
    val left = (centerX - rectWidth / 2f).roundToInt()
    val right = (centerX + rectWidth / 2f).roundToInt()
    val y = top.roundToInt()
    return Rect(left, y, right - left, (top + rectHeight).roundToInt() - y)
}
