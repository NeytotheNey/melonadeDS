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
     * Whether the frames that come next can have screens the compositor drew at 1x (PresentFrameWrapper.halvesAt1x, debug.melonade.comp1x)
     * without changing what's shown. A renderer must still draw such a frame properly when this is false, as the emulator only learns it a few
     * frames later. Render thread only.
     */
    fun canDrawHalvesAt1x(): Boolean = false

    /**
     * Frees the renderer's GL objects when it won't draw again; called on the render thread with the GL context current.
     */
    fun release()
}