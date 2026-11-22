package me.magnum.melonds.domain.model.render

import me.magnum.melonds.domain.model.SCREEN_GAP
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.camera.WideWindow

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
    // The WideMelon camera's margin (stage 2): rows of 3D above and below the top screen's SCREEN_HEIGHT, in DS pixels. The top screen's half
    // of the texture is then topScreenHeight rows, with its DS line y at row y + topScreenMargin, then the SCREEN_GAP rows and the bottom
    // screen's SCREEN_HEIGHT; 0 is the old FRAME_TEXTURE_HEIGHT layout
    var topScreenMargin: Int = 0,
    // Lua drawing overlay texture (2x, premultiplied) and the OVERLAY_SCREEN_* bits saying which screens use it
    var overlayTextureId: Int = 0,
    var overlayScreens: Int = 0,
    // debug.melonade.comp1x: the OVERLAY_SCREEN_* bits of the screens the compositor drew at 1x, each in the first topScreenWidth x (its half's
    // height) texels of its own rows (from row 0, or (topScreenHeight + SCREEN_GAP) * scaleAt1x), and the frame's internal scale. halfScaleAt1x is the
    // scale those screens were drawn at: 1, or with filtered 2D the filter map's, so their image is that many times bigger
    var halvesAt1x: Int = 0,
    var scaleAt1x: Int = 1,
    var halfScaleAt1x: Int = 1,
    // The top screen half's depth for a depth of field (GL_RG8, textureWidth by the top half's rows, see FrameRenderCallback), or 0 for none
    var depthTextureId: Int = 0,
    // Emulator frame sequence number, 0 when the frame is invalid
    var frameSequence: Long = 0,
    // The WideMelon camera's window of the top-slot screen for this presentation, or null to draw it without one
    var wideWindow: WideWindow? = null,
) {
    // The top screen's half of the frame in DS pixels (SCREEN_HEIGHT plus the margin on both sides), and the whole frame's
    val topScreenHeight get() = SCREEN_HEIGHT + 2 * topScreenMargin
    val frameHeight get() = topScreenHeight + SCREEN_GAP + SCREEN_HEIGHT
}
