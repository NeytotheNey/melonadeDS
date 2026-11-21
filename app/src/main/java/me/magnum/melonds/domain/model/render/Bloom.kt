package me.magnum.melonds.domain.model.render

/**
 * What a script's `melon.screen.bloom()` and `melon.screen.vignette()` ask for, over the DS top screen. Bloom adds a glow of the parts brighter
 * than [threshold] (a luma from 0 to 1, with a soft knee), blurred [radius] DS pixels wide, at [strength] (0 to 2). The vignette darkens the
 * screen towards its corners, from [vignetteSize] (a fraction of the half-diagonal from the center) to the corner, where it's [vignette] (0 to 1).
 *
 * Immutable, so the render thread can read it without a lock.
 */
data class Bloom(
    val strength: Float,
    val threshold: Float,
    val radius: Float,
    val vignette: Float,
    val vignetteSize: Float,
) {
    /**
     * Whether there's a glow to draw; without it only the vignette runs (see VideoFilterPipeline)
     */
    val hasBloom get() = strength > 0f && radius > 0f

    val hasVignette get() = vignette > 0f

    val isOff get() = !hasBloom && !hasVignette
}
