package me.magnum.melonds.ui.emulator.render

import me.magnum.melonds.domain.model.render.OVERLAY_SCREEN_BOTTOM
import me.magnum.melonds.domain.model.render.OVERLAY_SCREEN_TOP

fun interface FrameRenderCallback {
    /**
     * @param frameTextureWidth Width of the frame texture in texels. Bigger than the DS's resolution when the 3D renderer is upscaled. 0 if the
     * frame is not valid
     * @param frameTextureHeight Height of the frame texture in texels (both screens plus the gap between them). 0 if the frame is not valid
     * @param overlayTextureId Texture with the frame's Lua drawings (melon.draw), in the frame texture's layout at 2x. Each frame slot of the
     * queue has its own, so it's 0 if this slot never had drawings, in which case [overlayScreens] is 0 too
     * @param overlayScreens Which screens have Lua drawings in this frame, as [OVERLAY_SCREEN_TOP] and [OVERLAY_SCREEN_BOTTOM] bits
     * @param frameSequence Sequence number of the presented emulator frame. Increases by 1 for every frame the emulator produces, so the same value
     * means a repeated frame and a jump bigger than 1 means frames were skipped. 0 if the frame is not valid
     */
    fun renderFrame(isValidFrame: Boolean, frameTextureId: Int, frameTextureWidth: Int, frameTextureHeight: Int, overlayTextureId: Int, overlayScreens: Int, frameSequence: Long)
}