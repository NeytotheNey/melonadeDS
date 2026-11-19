package me.magnum.melonds.domain.model

data class RendererConfiguration(
    val renderer: VideoRenderer,
    val videoFiltering: VideoFiltering,
    // Deband in front of or inside the filter (any renderer); the "Smooth gradients" switch, which also turns on colorDithering
    val deband: Boolean,
    // Always false: the "DS screen colors" switch is gone from the settings (SharedPreferencesSettingsRepository.isDsScreenColorsEnabled)
    val dsScreenColors: Boolean,
    val screenShadow: Boolean,
    val threadedRendering: Boolean,
    // Draws 2D layers on their own thread (read natively)
    val threaded2dRendering: Boolean,
    // Redo display capture at internal resolution (OpenGL/compute, read natively)
    val hiresCapture: Boolean,
    // Smooth 2D edges in the compositor (OpenGL/compute at 2x and up, read natively)
    val filter2d: Boolean,
    private val internalResolutionScaling: Int,
    // Draw the games' edge marking, the outlines around 3D models (OpenGL/compute; software always does; read natively by name)
    val edgeMarking: Boolean = true,
    // Draw rotated and scaled 2D layers at the internal resolution, the GPU 2D shadow in mode 1 (OpenGL/compute at 2x to 6x, read natively)
    val sharperRotated2d: Boolean = false,
    // Compute 3D depth at full precision instead of the DS's steps (GPU::PreciseDepth, any renderer, read natively)
    val preciseDepth: Boolean = false,
    // The OpenGL floor seams fix, "Fix floor seams" (GPU::GLSeams 3; read natively, which ignores it for software and compute)
    val glSeams: Boolean = false,
) {

    // Dither polygon color gradients instead of banding them: compute only, with deband, which took over its switch. A field, as it's read
    // natively by name (MelonDSAndroidConfiguration::buildRenderSettings)
    val colorDithering: Boolean = deband && renderer == VideoRenderer.COMPUTE

    val resolutionScaling get() = when (renderer) {
        VideoRenderer.SOFTWARE -> 1
        VideoRenderer.OPENGL -> internalResolutionScaling
        VideoRenderer.COMPUTE -> internalResolutionScaling.coerceAtMost(MAX_COMPUTE_RESOLUTION_SCALING)
    }

    companion object {
        /**
         * The 12x and 16x settings run Compute at 8x. Its work buffers grow with the pixel count: on the PC build (White 2's title) it took
         * 0.6 GB of graphics memory at 8x, 1.1 GB at 12x and 1.9 GB at 16x, and the Fold already used about 0.86 GB at 8x, which phones take
         * from their shared RAM. Its GPU time should grow about as the pixels do (2.25x and 4x of 8x's), and 8x already costs twice OpenGL's. OpenGL goes to 16x.
         * The core also drops any scale that doesn't fit the GPU's limits on its own, for both renderers.
         */
        const val MAX_COMPUTE_RESOLUTION_SCALING = 8
    }
}