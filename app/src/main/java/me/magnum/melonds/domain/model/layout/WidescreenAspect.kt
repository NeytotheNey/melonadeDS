package me.magnum.melonds.domain.model.layout

import me.magnum.melonds.domain.model.MAX_WIDE_VIEW_WIDTH
import me.magnum.melonds.domain.model.MIN_WIDE_VIEW_WIDTH
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * Widescreen ratios in R.array.widescreen_aspect_options order; [viewWidth] is the even 3D-view width that gives each, and [key] is what a
 * wide variant stores.
 */
enum class WidescreenAspect(val key: String, widthRatio: Int, heightRatio: Int) {
    RATIO_16_10("16:10", 16, 10),
    RATIO_16_9("16:9", 16, 9),
    RATIO_20_9("20:9", 20, 9),
    RATIO_21_9("21:9", 21, 9);

    val viewWidth = ((SCREEN_HEIGHT * widthRatio / (heightRatio * 2f)).roundToInt() * 2).coerceIn(MIN_WIDE_VIEW_WIDTH, MAX_WIDE_VIEW_WIDTH)

    // Drawn shape, derived from the rounded width
    val aspectRatio get() = viewWidth / SCREEN_HEIGHT.toFloat()

    companion object {
        val DEFAULT = RATIO_16_9

        /**
         * The choice whose width is closest to [viewWidth], for a valid width that was stored before (RomModsConfig.validWideViewWidth)
         */
        fun fromViewWidth(viewWidth: Int): WidescreenAspect {
            return entries.minBy { abs(it.viewWidth - viewWidth) }
        }

        /**
         * The choice stored as [key], or null if there's none (a normal layout, or a key this version doesn't know)
         */
        fun fromKey(key: String?): WidescreenAspect? {
            return entries.firstOrNull { it.key == key }
        }
    }
}
