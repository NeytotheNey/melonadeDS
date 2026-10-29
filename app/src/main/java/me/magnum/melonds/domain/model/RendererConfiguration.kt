package me.magnum.melonds.domain.model

data class RendererConfiguration(
    val renderer: VideoRenderer,
    val videoFiltering: VideoFiltering,
    val dsScreenColors: Boolean,
    val screenShadow: Boolean,
    val threadedRendering: Boolean,
    // Draws the 2D layers on their own thread. Read by the native code (MelonDSAndroidConfiguration.cpp)
    val threaded2dRendering: Boolean,
    private val internalResolutionScaling: Int,
) {

    val resolutionScaling get() = when (renderer) {
        VideoRenderer.SOFTWARE -> 1
        VideoRenderer.OPENGL -> internalResolutionScaling
        VideoRenderer.COMPUTE -> internalResolutionScaling
    }
}