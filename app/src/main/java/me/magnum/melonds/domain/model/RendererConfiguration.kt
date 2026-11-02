package me.magnum.melonds.domain.model

data class RendererConfiguration(
    val renderer: VideoRenderer,
    val videoFiltering: VideoFiltering,
    val dsScreenColors: Boolean,
    val screenShadow: Boolean,
    val threadedRendering: Boolean,
    // Draws 2D layers on their own thread (read natively)
    val threaded2dRendering: Boolean,
    // Redo display capture at internal resolution (OpenGL/compute, read natively)
    val hiresCapture: Boolean,
    private val internalResolutionScaling: Int,
) {

    val resolutionScaling get() = when (renderer) {
        VideoRenderer.SOFTWARE -> 1
        VideoRenderer.OPENGL -> internalResolutionScaling
        VideoRenderer.COMPUTE -> internalResolutionScaling
    }
}