package me.magnum.melonds.common.opengl

/**
 * A screen filter. The pre-passes run in order over the whole frame texture, each one rendering at the frame texture's size into its own
 * texture, so they run once per texel instead of once per screen pixel. Their output keeps the frame texture's layout and channel order. The
 * screen shader then draws the last pre-pass's output (or the frame texture itself, if there are no pre-passes) to the surface.
 */
class VideoFilter(
    val screenShader: ShaderProgramSource,
    val prePasses: List<ShaderProgramSource> = emptyList(),
)
