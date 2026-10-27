package me.magnum.melonds.ui.emulator.render

fun interface FrameRenderCallback {
    /**
     * @param frameTextureWidth Width of the frame texture in texels. Bigger than the DS's resolution when the 3D renderer is upscaled. 0 if the
     * frame is not valid
     * @param frameTextureHeight Height of the frame texture in texels (both screens plus the gap between them). 0 if the frame is not valid
     * @param frameSequence Sequence number of the presented emulator frame. Increases by 1 for every frame the emulator produces, so the same value
     * means a repeated frame and a jump bigger than 1 means frames were skipped. 0 if the frame is not valid
     */
    fun renderFrame(isValidFrame: Boolean, frameTextureId: Int, frameTextureWidth: Int, frameTextureHeight: Int, frameSequence: Long)
}