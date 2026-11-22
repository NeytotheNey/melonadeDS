package me.magnum.melonds.domain.model.render

/**
 * What a script's `melon.screen.shade()` asks for: soft contact shadows on the DS top screen's 3D, from unsharp masking its depth. Where a
 * pixel's depth is farther than its neighborhood's, blurred [radius] DS pixels wide, it's tinted towards [color] (0xRRGGBB), fully once
 * it's [range] farther in the renderer's raw depth (0 to 1, see VideoFilterPipeline), at up to [strength] (0 to 1). Only 3D pixels are
 * shaded, and only when the frame has its depth.
 *
 * Immutable, so the render thread can read it without a lock.
 */
data class Shade(
    val strength: Float,
    val radius: Float = 8f,
    val range: Float = 0.0008f,
    val color: Int = 0x33384F,
) {
    val isOff get() = strength <= 0f || radius <= 0f
}
