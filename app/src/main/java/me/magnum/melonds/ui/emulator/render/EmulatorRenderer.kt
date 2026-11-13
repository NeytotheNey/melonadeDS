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
     * How many times the native size (SCREEN_WIDTH x SCREEN_HEIGHT) this renderer shows a screen's middle columns at, rounded up, over the
     * screens it shows, for debug.melonade.outcap (GPU::OutputCap): the frame's scale is capped by the largest one over every surface. 0 for
     * none, as with a renderer that doesn't show the frame. Render thread only.
     */
    fun outputScaleNeed(): Int = 0

    /**
     * Frees the renderer's GL objects when it won't draw again; called on the render thread with the GL context current.
     */
    fun release()
}