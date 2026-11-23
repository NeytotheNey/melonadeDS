package me.magnum.melonds.domain.model.render

import me.magnum.melonds.domain.model.SCREEN_GAP
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.camera.WideWindow

// Bits of PresentFrameWrapper.overlayScreens and halvesAt1x; must match presentFrame() in MelonDSAndroidJNI.cpp
const val OVERLAY_SCREEN_TOP = 1
const val OVERLAY_SCREEN_BOTTOM = 2

// PresentFrameWrapper.depthCamera's layout; must match presentFrame() in MelonDSAndroidJNI.cpp. The camera the top screen's depth was drawn
// with (the core's melonDS::DepthCamera in GPU3D.h), for effects that rebuild positions from the depth:
// - [DEPTH_CAMERA_PROJECTION], 16 floats: the projection, column-major as GLSL's mat4 takes it, so mat4 * vec4(view, 1.0) is the clip position.
// - [DEPTH_CAMERA_VIEWPORT], 4 floats: the viewport's left, top, width and height in DS pixels, measured from the native top screen's top-left
//   corner with rows going down (the WideMelon wings and margin rows are outside 0..SCREEN_WIDTH and 0..SCREEN_HEIGHT).
// A pixel at (x, y) in those DS pixels (its center at +0.5) with a Z-buffered 24-bit depth d is at NDC x = (x - left) * 2 / width - 1,
// y = 1 - (y - top) * 2 / height, z = (d - 0x7FFE00) / 2^23, and in view space at inverse(projection) * vec4(ndc, 1.0), divided by its w
const val DEPTH_CAMERA_PROJECTION = 0
const val DEPTH_CAMERA_VIEWPORT = 16
const val DEPTH_CAMERA_SIZE = 20

// PresentFrameWrapper.depthFocus when the frame has none; must match NO_DEPTH_FOCUS in FrameQueue.h
const val NO_DEPTH_FOCUS = -1f

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
    // The sun shadows' guard band (melon.screen.sun's guard) in DS pixels, included in both of the above: that many columns on the left and
    // right and rows above and below of the top screen's half are drawn and filtered, so the sun's march finds casters just off screen, but
    // not shown. The shown top screen is shownTopScreenWidth wide with shownTopScreenMargin margin rows, with the native screen in its middle
    // at nativeLeft and nativeTop (and the bottom screen's columns under it); FrameGuard.NONE for none
    var topScreenGuard: FrameGuard = FrameGuard.NONE,
    // Lua drawing overlay texture (2x, premultiplied) and the OVERLAY_SCREEN_* bits saying which screens use it
    var overlayTextureId: Int = 0,
    var overlayScreens: Int = 0,
    // debug.melonade.comp1x: the OVERLAY_SCREEN_* bits of the screens the compositor drew at 1x, each in the first topScreenWidth x (its half's
    // height) texels of its own rows (from row 0, or (topScreenHeight + SCREEN_GAP) * scaleAt1x), and the frame's internal scale. halfScaleAt1x is the
    // scale those screens were drawn at: 1, or with filtered 2D the filter map's, so their image is that many times bigger
    var halvesAt1x: Int = 0,
    var scaleAt1x: Int = 1,
    var halfScaleAt1x: Int = 1,
    // The top screen half's depth for a depth of field (GL_RGBA8, textureWidth by the top half's rows, see FrameRenderCallback), or 0 for none
    var depthTextureId: Int = 0,
    // The camera that depth was drawn with (the DEPTH_CAMERA_* layout), written by presentFrame() before each frame with depth is presented.
    // Only to be read while isDepthCameraValid (off for a W-buffered or orthographic scene, one where the camera covers under half of it, or
    // no depth), but for the depth of field's view distances, which only take the projection's depth terms and read them whenever there's depth
    val depthCamera: FloatArray = FloatArray(DEPTH_CAMERA_SIZE),
    var isDepthCameraValid: Boolean = false,
    // The depth of field's focus on the focus textures (melon.gpu.focus_textures): the player's depth as depthTextureId holds it (0 to 1, as a
    // script's focus_depth), eased from scene to scene and held a while after she's gone, or NO_DEPTH_FOCUS
    var depthFocus: Float = NO_DEPTH_FOCUS,
    // Emulator frame sequence number, 0 when the frame is invalid
    var frameSequence: Long = 0,
    // The WideMelon camera's window of the top-slot screen for this presentation, or null to draw it without one
    var wideWindow: WideWindow? = null,
) {
    // The top screen's half of the frame in DS pixels (SCREEN_HEIGHT plus the margin on both sides), and the whole frame's
    val topScreenHeight get() = SCREEN_HEIGHT + 2 * topScreenMargin
    val frameHeight get() = topScreenHeight + SCREEN_GAP + SCREEN_HEIGHT
    // The top screen's width and margin rows as shown: the guard band left out
    val shownTopScreenWidth get() = topScreenGuard.shownWidth(topScreenWidth)
    val shownTopScreenMargin get() = topScreenGuard.shownMargin(topScreenMargin)
    // The frame's columns left of the native screens and the top screen half's rows above DS line 0, in DS pixels
    val nativeLeft get() = topScreenGuard.nativeLeft(topScreenWidth)
    val nativeTop get() = topScreenGuard.nativeTop(topScreenMargin)
}
