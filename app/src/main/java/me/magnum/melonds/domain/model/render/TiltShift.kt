package me.magnum.melonds.domain.model.render

/**
 * What a script's `melon.screen.tilt_shift()` asks for: a sharp band across the DS top screen, [center] and [width] being fractions of the screen's
 * height, with a blur that grows towards the screen's edges up to [radius] DS pixels times the strength of that side, [top] or [bottom]. [saturation]
 * and [contrast] are color multipliers for the whole screen, 1 meaning no change.
 *
 * Immutable, so the render thread can read it without a lock.
 */
data class TiltShift(
    val top: Float,
    val bottom: Float,
    val center: Float,
    val width: Float,
    val radius: Float,
    val saturation: Float = 1f,
    val contrast: Float = 1f,
) {
    /**
     * Where the sharp band starts and ends, as fractions of the screen's height from its top
     */
    val bandTop = (center - width / 2f).coerceAtLeast(0f)
    val bandBottom = (center + width / 2f).coerceAtMost(1f)

    /**
     * The blur's radius at the screen's top and bottom edges in DS pixels, 0 when the band leaves no room for it
     */
    val topRadius = if (bandTop > 0f) radius * top else 0f
    val bottomRadius = if (bandBottom < 1f) radius * bottom else 0f

    /**
     * The color multipliers to apply, which fade with the blur: they follow the stronger band, so clearing only one band leaves them as they are
     */
    val appliedSaturation = 1f + (saturation - 1f) * maxOf(top, bottom)
    val appliedContrast = 1f + (contrast - 1f) * maxOf(top, bottom)

    val isOff get() = topRadius <= 0f && bottomRadius <= 0f && appliedSaturation == 1f && appliedContrast == 1f
}
