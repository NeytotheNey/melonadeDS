package me.magnum.melonds.domain.model.render

/**
 * What a script's `melon.screen.sun()` asks for: sun shadows on the DS top screen's 3D, marched in screen space from its depth and the
 * camera it was drawn with. Each 3D pixel's view-space position steps [length] units towards [direction] in [steps] steps, and it's in shadow
 * where the depth at a step is nearer than the step by more than [bias] units; the shadow is softened [softness] DS pixels wide and tinted
 * towards [color] (0xRRGGBB) by [strength] (0 to 1). 3D nearer the camera than [near] units casts nothing (0 for no limit), for what a game
 * draws right in front of the camera, like Pokémon White's speech bubble tails. [direction] and [up] are in the camera's view space,
 * normalized where they're used. [up] isn't used by the march; it's kept for the characters' shadows, which the core will draw. Only 3D
 * pixels are shadowed, and only when the frame has its depth and a usable camera.
 *
 * Immutable, so the render thread can read it without a lock.
 */
data class Sun(
    val strength: Float,
    val directionX: Float = 0.50f,
    val directionY: Float = 0.83f,
    val directionZ: Float = 0.26f,
    val upX: Float = 0.0006f,
    val upY: Float = 0.607f,
    val upZ: Float = 0.794f,
    val length: Float = 96f,
    val steps: Int = 64,
    val bias: Float = 1f,
    val softness: Float = 1.5f,
    val color: Int = 0x33384F,
    val near: Float = 0f,
) {
    val isOff get() = strength <= 0f || steps <= 0 || length <= 0f
}
