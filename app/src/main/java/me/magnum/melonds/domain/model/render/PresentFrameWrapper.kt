package me.magnum.melonds.domain.model.render

// Bits of PresentFrameWrapper.overlayScreens. Must match presentFrame() in MelonDSAndroidJNI.cpp
const val OVERLAY_SCREEN_TOP = 1
const val OVERLAY_SCREEN_BOTTOM = 2

data class PresentFrameWrapper(
    var isValidFrame: Boolean = false,
    var textureId: Int = 0,
    // Real size of the frame texture, which grows with the internal resolution scaling of the OpenGL and Compute renderers
    var textureWidth: Int = 0,
    var textureHeight: Int = 0,
    // Lua drawings (melon.draw): a premultiplied texture in the frame texture's layout at 2x, and which screens have any in this frame
    // (OVERLAY_SCREEN_* bits). The texture is only meant to be drawn for those screens
    var overlayTextureId: Int = 0,
    var overlayScreens: Int = 0,
    // Sequence number of the emulator frame. The same value means the same frame is being presented again. 0 if the frame is not valid
    var frameSequence: Long = 0,
)