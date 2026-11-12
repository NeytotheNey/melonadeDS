package me.magnum.melonds.domain.model.render

// Bits of PresentFrameWrapper.overlayScreens and halvesAt1x; must match presentFrame() in MelonDSAndroidJNI.cpp
const val OVERLAY_SCREEN_TOP = 1
const val OVERLAY_SCREEN_BOTTOM = 2

data class PresentFrameWrapper(
    var isValidFrame: Boolean = false,
    var textureId: Int = 0,
    // Real size of the frame texture, which grows with the internal resolution scaling of the OpenGL and Compute renderers
    var textureWidth: Int = 0,
    var textureHeight: Int = 0,
    // Top screen width in DS pixels; wider than SCREEN_WIDTH when widescreen is on
    var topScreenWidth: Int = 0,
    // Lua drawing overlay texture (2x, premultiplied) and the OVERLAY_SCREEN_* bits saying which screens use it
    var overlayTextureId: Int = 0,
    var overlayScreens: Int = 0,
    // debug.melonade.comp1x: the OVERLAY_SCREEN_* bits of the screens the compositor drew at 1x, each in the first topScreenWidth x SCREEN_HEIGHT
    // texels of its own rows (from row screen * (SCREEN_HEIGHT + SCREEN_GAP) * scaleAt1x), and the frame's internal scale
    var halvesAt1x: Int = 0,
    var scaleAt1x: Int = 1,
    // Emulator frame sequence number, 0 when the frame is invalid
    var frameSequence: Long = 0,
)
