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
     * @param topScreenMargin The WideMelon camera's rows above and below the top screen's SCREEN_HEIGHT, in DS pixels (see
     * PresentFrameWrapper.topScreenMargin).
     * @param overlayTextureId This queue slot's Lua drawing texture at 2x, or 0 if the slot never had drawings.
     * @param overlayScreens Which screens have Lua drawings in this frame, as [OVERLAY_SCREEN_TOP] and [OVERLAY_SCREEN_BOTTOM] bits.
     * @param halvesAt1x Which screens the compositor drew at 1x (debug.melonade.comp1x), as the same bits: such a screen's image is in the first
     * topScreenWidth x SCREEN_HEIGHT texels of its own rows, to be shown nearest-upscaled by [scaleAt1x].
     * @param scaleAt1x The frame's internal scale, for [halvesAt1x].
     * @param halfScaleAt1x The scale the screens in [halvesAt1x] were drawn at: 1, or with filtered 2D the filter map's, so their image is that many
     * times bigger and is upscaled by [scaleAt1x] / [halfScaleAt1x].
     * @param topWingLines The DS top screen's lines with 3D wings (held ones included), for the WideMelon camera.
     * @param depthTextureId The top screen half's depth, for a depth of field: GL_RGBA8, the frame's width by its top half's rows, 24-bit values
     * in RGB with the high byte in R and flags in A (see ShaderProgramSource's DEPTH_DECODE_FUNCTION); 0 when the frame has none.
     * @param isDepthCameraValid Whether the array given to MelonEmulator.presentFrame now holds the camera that depth was drawn with (see
     * PresentFrameWrapper's DEPTH_CAMERA_* layout).
     * @param frameSequence Sequence number of the presented emulator frame, increasing by 1 for every frame the emulator produces.
     * @return Whether every surface can draw a frame with screens at 1x, which the emulator only produces while this is true.
     */
    fun renderFrame(
        isValidFrame: Boolean,
        frameTextureId: Int,
        frameTextureWidth: Int,
        frameTextureHeight: Int,
        topScreenWidth: Int,
        topScreenMargin: Int,
        overlayTextureId: Int,
        overlayScreens: Int,
        halvesAt1x: Int,
        scaleAt1x: Int,
        halfScaleAt1x: Int,
        topWingLines: Int,
        depthTextureId: Int,
        isDepthCameraValid: Boolean,
        frameSequence: Long,
    ): Boolean
}
