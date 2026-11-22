package me.magnum.melonds.domain.model.render

/**
 * What a script's `melon.screen.depth_of_field()` asks for: a blur over the DS top screen's 3D that grows with how far each pixel's depth is from
 * the focus, up to [radius] DS pixels at [strength] (0 to 1, a fade). The focus is the depth at ([focusX], [focusY]), DS pixels of the top
 * screen's 256 x 192 view (outside it on WideMelon's wings and margins), or [focusDepth] (0 to 1) when it's not negative. Depths within [range]
 * of the focus stay sharp, and the blur reaches its full radius [falloff] beyond that; both are in the renderer's depth, 0 to 1 (see
 * VideoFilterPipeline). 2D drawn over the 3D stays sharp, and what shows through transparent 3D blurs as the farthest depth.
 *
 * Immutable, so the render thread can read it without a lock.
 */
data class DepthOfField(
    val strength: Float,
    val radius: Float,
    val focusX: Float = 128f,
    val focusY: Float = 96f,
    val focusDepth: Float = -1f,
    val range: Float = 0.0002f,
    val falloff: Float = 0.0015f,
) {
    val isOff get() = strength <= 0f || radius <= 0f

    /**
     * The blur's radius at full strength in DS pixels
     */
    val appliedRadius get() = strength * radius
}
