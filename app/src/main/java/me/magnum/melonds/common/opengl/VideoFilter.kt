package me.magnum.melonds.common.opengl

/**
 * A screen filter: pre-passes run in order over the whole frame texture, keeping its layout and channel order, and the screen shader draws
 * the last one's output (or the frame texture) to the surface.
 */
class VideoFilter(
    val screenShader: ShaderProgramSource,
    val prePasses: List<FilterPrePass> = emptyList(),
)

/**
 * One pre-pass of a [VideoFilter], [scale] times its input on each axis; a [nativeResolutionOnly] pass is skipped when the 3D is upscaled.
 */
class FilterPrePass(
    val source: ShaderProgramSource,
    val scale: Int = 1,
    val nativeResolutionOnly: Boolean = false,
)
