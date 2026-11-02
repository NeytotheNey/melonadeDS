package me.magnum.melonds.ui.emulator.render

import me.magnum.melonds.domain.model.render.OVERLAY_SCREEN_BOTTOM
import me.magnum.melonds.domain.model.render.OVERLAY_SCREEN_TOP

fun interface FrameRenderCallback {
    /**
     * Presents one emulator frame; the sizes and [frameSequence] are 0 if the frame is not valid.
     *
     * @param frameTextureWidth Width of the frame texture in texels, bigger than the DS's when the 3D renderer is upscaled.
     * @param frameTextureHeight Height of the frame texture in texels, both screens plus the gap between them.
     * @param topScreenWidth Width of the top screen in DS pixels, more than SCREEN_WIDTH when widescreen widens its 3D view.
     * @param overlayTextureId This queue slot's Lua drawing texture at 2x, or 0 if the slot never had drawings.
     * @param overlayScreens Which screens have Lua drawings in this frame, as [OVERLAY_SCREEN_TOP] and [OVERLAY_SCREEN_BOTTOM] bits.
     * @param frameSequence Sequence number of the presented emulator frame, increasing by 1 for every frame the emulator produces.
     */
    fun renderFrame(isValidFrame: Boolean, frameTextureId: Int, frameTextureWidth: Int, frameTextureHeight: Int, topScreenWidth: Int, overlayTextureId: Int, overlayScreens: Int, frameSequence: Long)
}