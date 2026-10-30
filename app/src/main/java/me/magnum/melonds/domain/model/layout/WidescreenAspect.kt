package me.magnum.melonds.domain.model.layout

import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.rom.config.MAX_WIDE_VIEW_WIDTH
import me.magnum.melonds.domain.model.rom.config.MIN_WIDE_VIEW_WIDTH
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * The aspect ratios widescreen offers, in the order of R.array.widescreen_aspect_options. [viewWidth] is the width of the top screen's 3D
 * view that gives it: SCREEN_HEIGHT times the ratio, rounded to an even number and kept to what the core takes (256 to 768). That's 308, 342,
 * 426 and 448. [key] is how a wide variant stores it (UILayout.wideAspect), so the order can change
 */
enum class WidescreenAspect(val key: String, widthRatio: Int, heightRatio: Int) {
    RATIO_16_10("16:10", 16, 10),
    RATIO_16_9("16:9", 16, 9),
    RATIO_20_9("20:9", 20, 9),
    RATIO_21_9("21:9", 21, 9);

    val viewWidth = ((SCREEN_HEIGHT * widthRatio / (heightRatio * 2f)).roundToInt() * 2).coerceIn(MIN_WIDE_VIEW_WIDTH, MAX_WIDE_VIEW_WIDTH)

    // The shape the top screen is drawn at, which comes from the rounded width, so it's a little off the nominal ratio for some of them
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
