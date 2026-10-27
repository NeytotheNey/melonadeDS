package me.magnum.melonds.common.opengl

/**
 * A screen filter. The pre-passes run in order over the whole frame texture, each one rendering into its own texture at its input's size
 * times its scale, so they run once per output texel instead of once per screen pixel. Their output keeps the frame texture's layout and
 * channel order. The screen shader then draws the last pre-pass's output (or the frame texture itself, if there are no pre-passes or none of
 * them ran) to the surface.
 */
class VideoFilter(
    val screenShader: ShaderProgramSource,
    val prePasses: List<FilterPrePass> = emptyList(),
)

/**
 * One pre-pass of a [VideoFilter]. Its output is [scale] times the size of its input on each axis. A [nativeResolutionOnly] pass is skipped
 * when the frame texture is bigger than the DS's own resolution (the 3D renderer is upscaled), and the next pass gets its input instead.
 */
class FilterPrePass(
    val source: ShaderProgramSource,
    val scale: Int = 1,
    val nativeResolutionOnly: Boolean = false,
)
