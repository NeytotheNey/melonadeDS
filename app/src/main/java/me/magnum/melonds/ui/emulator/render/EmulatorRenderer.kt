package me.magnum.melonds.ui.emulator.render

import me.magnum.melonds.domain.model.render.PresentFrameWrapper
import me.magnum.melonds.ui.emulator.model.RuntimeRendererConfiguration

interface EmulatorRenderer {
    fun updateRendererConfiguration(newRendererConfiguration: RuntimeRendererConfiguration?)
    fun setLeftRotationEnabled(enabled: Boolean)
    fun onSurfaceCreated()
    fun onSurfaceChanged(width: Int, height: Int)
    fun drawFrame(presentFrameWrapper: PresentFrameWrapper)

    /**
     * Frees the renderer's GL objects. Called on the render thread, with the GL context current, when the renderer won't draw again. The next
     * [onSurfaceCreated] would create them again.
     */
    fun release()
}