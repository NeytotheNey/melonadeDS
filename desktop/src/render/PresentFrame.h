// What the presentation reads of a frame (the app's PresentFrameWrapper.kt) and the Lua tilt-shift, bloom, depth of field, shading and sun
// shadows a script asks for (TiltShift.kt, Bloom.kt, DepthOfField.kt, Shade.kt, Sun.kt)
#pragma once

#include <algorithm>
#include <array>
#include <optional>

#include "PlatformOGL.h"
#include "types.h"

struct Frame;

namespace Frontend
{

// a Frame (renderer/FrameQueue.h) as the presenter draws it; Texture 0 is no frame (the app's isValidFrame false)
struct FrameView
{
    GLuint Texture = 0;
    // the texture's size in texels
    int Width = 0, Height = 0;
    // the top screen's width in DS pixels (wider than 256 with WideMelon), and its margin rows above and below (WideMelon camera stage 2)
    int TopWidth = 256, TopMargin = 0;
    // GPU::Comp1x: the halves the compositor drew at 1x (bit 0 the top one), the frame's scale and the scale they were drawn at
    int HalvesAt1x = 0, Scale = 1, HalfScale = 1;
    // the frame queue's sequence, so a frame presented again isn't filtered again (0: none)
    melonDS::u64 Sequence = 0;
    // the top screen half's depth (Frame::depthTexture, GL_RGBA8), 0 when the frame has none
    GLuint DepthTexture = 0;
    // the camera that depth was drawn with (melonDS::DepthCamera in the core's GPU3D.h), only meaningful while DepthCameraValid:
    // DepthProjection is the projection column-major as GLSL's mat4 takes it (mat4 * vec4(view, 1) is the clip position), DepthViewport
    // its viewport's left, top, width and height in DS pixels from the native top screen's top-left corner, rows going down. A pixel at
    // (x, y) there (its center at +0.5) with a Z-buffered 24-bit depth d is at NDC x = (x - left) * 2 / width - 1,
    // y = 1 - (y - top) * 2 / height, z = (d - 0x7FFE00) / 2^23, and in view space at inverse(projection) * (ndc, 1), divided by its w
    std::array<float, 16> DepthProjection {};
    std::array<float, 4> DepthViewport {};
    bool DepthCameraValid = false;

    bool IsValid() const { return Texture != 0; }
    // the top screen's half of the frame in DS pixels (192 plus the margin on both sides), and the whole frame's
    int TopScreenHeight() const { return 192 + 2 * TopMargin; }
    int FrameHeight() const { return TopScreenHeight() + 2 + 192; }

    static FrameView Of(const Frame& frame);
};

// TiltShift.kt: a blur over the DS top screen that's sharp in a band ([Center] and [Width], fractions of the screen's height), [Radius] DS
// pixels at the screen's edges times [Top] and [Bottom] (each 0 to 1, a fade), and a saturation and contrast boost that follows [Color], or
// without one the stronger band
struct TiltShift
{
    float Top = 0, Bottom = 0, Center = 0.5f, Width = 0, Radius = 0;
    float Saturation = 1, Contrast = 1;
    std::optional<float> Color;

    bool operator==(const TiltShift&) const = default;

    // where the sharp band starts and ends, as fractions of the screen's height from its top
    float BandTop() const { return std::max(Center - Width / 2.f, 0.f); }
    float BandBottom() const { return std::min(Center + Width / 2.f, 1.f); }
    // the blur's radius at the screen's top and bottom edges in DS pixels, 0 when the band leaves no room for it
    float TopRadius() const { return BandTop() > 0.f ? Radius * Top : 0.f; }
    float BottomRadius() const { return BandBottom() < 1.f ? Radius * Bottom : 0.f; }
    bool HasBlur() const { return TopRadius() > 0.f || BottomRadius() > 0.f; }
    float AppliedSaturation() const { return 1.f + (Saturation - 1.f) * Color.value_or(std::max(Top, Bottom)); }
    float AppliedContrast() const { return 1.f + (Contrast - 1.f) * Color.value_or(std::max(Top, Bottom)); }
    bool HasColorBoost() const { return AppliedSaturation() != 1.f || AppliedContrast() != 1.f; }
};

// Bloom.kt: a glow over the DS top screen of what's brighter than [Threshold] (a luma, 0 to 1), blurred [Radius] DS pixels wide, at [Strength]
// (0 to 2), only of 3D pixels with [OnlyThreeD] when the frame has its depth; a vignette that darkens it from [VignetteSize] of the
// half-diagonal out to the corners, where it's [Vignette] (0 to 1); and a haze that mixes 3D pixels towards [HazeColor] (0xRRGGBB) at up to
// [Haze] (0 to 1), easing in from [HazeStart] to [HazeStart] + [HazeFalloff] in the renderer's raw depth (0 to 1), only when the frame has
// its depth
struct Bloom
{
    float Strength = 0, Threshold = 0.7f, Radius = 3;
    bool OnlyThreeD = true;
    float Vignette = 0, VignetteSize = 0.6f;
    float Haze = 0;
    melonDS::u32 HazeColor = 0xDCE6F0;
    float HazeStart = 0.9968f, HazeFalloff = 0.0008f;

    bool operator==(const Bloom&) const = default;

    bool HasBloom() const { return Strength > 0.f && Radius > 0.f; }
    bool HasVignette() const { return Vignette > 0.f; }
    bool HasHaze() const { return Haze > 0.f; }
    bool IsOff() const { return !HasBloom() && !HasVignette() && !HasHaze(); }
};

// DepthOfField.kt: a blur over the DS top screen's 3D that grows with each pixel's depth away from the focus, up to [Radius] DS pixels at
// [Strength] (0 to 1). The focus is the depth at ([FocusX], [FocusY]) of the top screen's 256 x 192 view, or [FocusDepth] (0 to 1) when it's not
// negative; depths within [Range] of it stay sharp and the blur is full [Falloff] past that, both in the renderer's depth (0 to 1)
struct DepthOfField
{
    float Strength = 0, Radius = 4, FocusX = 128, FocusY = 96, FocusDepth = -1, Range = 0.0002f, Falloff = 0.0015f;

    bool operator==(const DepthOfField&) const = default;

    bool IsOff() const { return Strength <= 0.f || Radius <= 0.f; }
    float AppliedRadius() const { return Strength * Radius; }
};

// Shade.kt: soft contact shadows on the DS top screen's 3D, from unsharp masking its depth. Where a pixel's depth is farther than its
// neighborhood's, blurred [Radius] DS pixels wide, it's tinted towards [Color] (0xRRGGBB), fully once it's [Range] farther in the renderer's
// raw depth (0 to 1), at up to [Strength] (0 to 1). Only 3D pixels are shaded, and only when the frame has its depth
struct Shade
{
    float Strength = 0, Radius = 8, Range = 0.0008f;
    melonDS::u32 Color = 0x33384F;

    bool operator==(const Shade&) const = default;

    bool IsOff() const { return Strength <= 0.f || Radius <= 0.f; }
};

// Sun.kt: sun shadows on the DS top screen's 3D, marched in screen space from its depth and the camera it was drawn with. Each 3D pixel's
// view-space position steps [Length] units towards [Direction] in [Steps] steps, and it's in shadow where the depth at a step is nearer than
// the step by more than [Bias] units; the shadow is softened [Softness] DS pixels wide and tinted towards [Color] (0xRRGGBB) by [Strength]
// (0 to 1). [Direction] and [Up] are in the camera's view space, normalized where they're used. [Up] isn't used by the march; it's kept for
// the characters' shadows, which the core will draw. Only 3D pixels are shadowed, and only when the frame has its depth and a usable camera
struct Sun
{
    float Strength = 0;
    std::array<float, 3> Direction {0.50f, 0.83f, 0.26f};
    std::array<float, 3> Up {0.0006f, 0.607f, 0.794f};
    float Length = 96;
    int Steps = 64;
    float Bias = 1, Softness = 1.5f;
    melonDS::u32 Color = 0x33384F;

    bool operator==(const Sun&) const = default;

    bool IsOff() const { return Strength <= 0.f || Steps <= 0 || Length <= 0.f; }
};

}
