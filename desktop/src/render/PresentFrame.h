// What the presentation reads of a frame (the app's PresentFrameWrapper.kt) and the Lua tilt-shift and bloom a script asks for (TiltShift.kt,
// Bloom.kt)
#pragma once

#include <algorithm>
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
// (0 to 2), and a vignette that darkens it from [VignetteSize] of the half-diagonal out to the corners, where it's [Vignette] (0 to 1)
struct Bloom
{
    float Strength = 0, Threshold = 0.7f, Radius = 3, Vignette = 0, VignetteSize = 0.6f;

    bool operator==(const Bloom&) const = default;

    bool HasBloom() const { return Strength > 0.f && Radius > 0.f; }
    bool HasVignette() const { return Vignette > 0.f; }
    bool IsOff() const { return !HasBloom() && !HasVignette(); }
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

}
