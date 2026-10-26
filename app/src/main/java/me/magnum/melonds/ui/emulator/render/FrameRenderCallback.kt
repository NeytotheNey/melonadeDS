package me.magnum.melonds.ui.emulator.render

fun interface FrameRenderCallback {
    /**
     * @param frameSequence Sequence number of the presented emulator frame. Increases by 1 for every frame the emulator produces, so the same value
     * means a repeated frame and a jump bigger than 1 means frames were skipped. 0 if the frame is not valid
     */
    fun renderFrame(isValidFrame: Boolean, frameTextureId: Int, frameSequence: Long)
}