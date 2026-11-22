package me.magnum.melonds.domain.model.render

/**
 * What a script's `melon.screen.bloom()`, `melon.screen.vignette()` and `melon.screen.haze()` ask for, over the DS top screen. Bloom adds a glow
 * of the parts brighter than [threshold] (a luma from 0 to 1, with a soft knee), blurred [radius] DS pixels wide, at [strength] (0 to 2); with
 * [onlyThreeD] only 3D pixels glow, when the frame has its depth. The vignette darkens the screen towards its corners, from [vignetteSize] (a
 * fraction of the half-diagonal from the center) to the corner, where it's [vignette] (0 to 1). The haze mixes 3D pixels towards [hazeColor]
 * (0xRRGGBB) at up to [haze] (0 to 1), easing in from [hazeStart] to [hazeStart] + [hazeFalloff] in the renderer's raw depth (0 to 1), and
 * only when the frame has its depth.
 *
 * Immutable, so the render thread can read it without a lock.
 */
data class Bloom(
    val strength: Float,
    val threshold: Float,
    val radius: Float,
    val vignette: Float,
    val vignetteSize: Float,
    val onlyThreeD: Boolean = true,
    val haze: Float = 0f,
    val hazeColor: Int = 0xDCE6F0,
    val hazeStart: Float = 0.9968f,
    val hazeFalloff: Float = 0.0008f,
) {
    /**
     * Whether there's a glow to draw; without it only the vignette and the haze run (see VideoFilterPipeline)
     */
    val hasBloom get() = strength > 0f && radius > 0f

    val hasVignette get() = vignette > 0f

    val hasHaze get() = haze > 0f

    val isOff get() = !hasBloom && !hasVignette && !hasHaze
}
