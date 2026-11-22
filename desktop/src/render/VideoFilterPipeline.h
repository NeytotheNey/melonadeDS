// Port of the app's VideoFilterPipeline.kt: the GL objects of a VideoFilter (screen shader, pre-pass shaders and their targets), and the
// pre-passes run over a frame before its screens are drawn: deband (unless the screen shader takes it), the filter's own, then a Lua shading,
// then Lua sun shadows, then a Lua depth of field, then a Lua bloom, haze and vignette, then a Lua tilt-shift; and GPU::Comp1x's 1x shader and
// expand pass. UI thread only, with its GL context current
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "render/PresentFrame.h"
#include "render/Shader.h"
#include "render/ShaderLibrary.h"

namespace Frontend
{

// the texture the screen shader should draw and its size in texels, reused by every RenderPrePasses call
struct FilterOutput
{
    GLuint TextureId = 0;
    int Width = 0, Height = 0;

    void Set(GLuint textureId, int width, int height)
    {
        TextureId = textureId;
        Width = width;
        Height = height;
    }
};

class VideoFilterPipeline
{
public:
    VideoFilterPipeline(const VideoFilter& filter, bool colorCorrection);
    ~VideoFilterPipeline();
    VideoFilterPipeline(const VideoFilterPipeline&) = delete;
    VideoFilterPipeline& operator=(const VideoFilterPipeline&) = delete;

    // the filter's screen shader, the no-filter one if it didn't build (null only if that didn't either)
    const Shader* ScreenShader() const { return ScreenShaderProgram.get(); }

    // whether deband can run inside the screen shader instead of the pre-pass (the filter takes it, both built, the DEBAND variant didn't fail)
    bool CanFoldDeband() const;
    // the screen shader with DEBAND defined, built the first time it's asked for; null if it can't be built (the pre-pass debands then)
    const Shader* GetDebandScreenShader();

    // GPU::Comp1x: the 1x shader's coordinate textures for a frame of [width] x [height] on units 1 and 2 (made again on a new size); leaves unit
    // 0 active
    void BindHalvesAt1xCoordinates(int width, int height);
    // whether a frame with screens at 1x drawn with GetHalvesAt1xShader looks as the full frame does here: the no-filter shader alone, as long as
    // the 1x shader didn't fail to build. A Lua tilt-shift's or bloom's passes are the caller's to check
    bool CanDrawHalvesAt1x() const { return IsNoFilter && !(IsHalvesAt1xShaderBuilt && !HalvesAt1xShader); }
    // the 1x shader with this pipeline's color correction, built the first time; null if it can't be (RenderPrePasses expands the frame then)
    const Shader* GetHalvesAt1xShader();

    // runs the pre-passes, then [shade], [sun], [depthOfField], [bloom] and [tiltShift] if there are any (in that order, so the depth of field
    // blurs the shading and the shadows, the glow is of the focused picture and the tilt-shift's blur softens the glow too), and returns the
    // texture the screen shader should draw with its size (texSize); call it before drawing to the window and for no frame too, then set the
    // program, vertex array, array buffer and texture unit 0 again. The shading, the sun shadows, the depth of field, the haze and a 3D-only
    // glow need the frame's depth, so a frame without one is drawn unshaded, without shadows, sharp, without haze, and with a glow on every
    // pixel; the sun shadows also need the frame's depth camera. With [foldDeband] the deband pre-pass is left out, for a frame drawn with
    // GetDebandScreenShader
    const FilterOutput& RenderPrePasses(const FrameView& frame, bool isBottomScreenWide, const std::optional<TiltShift>& tiltShift,
        const std::optional<Bloom>& bloom, const std::optional<DepthOfField>& depthOfField, const std::optional<Shade>& shade,
        const std::optional<Sun>& sun, bool foldDeband = false);

private:
    class PrePass;
    struct TiltShiftPasses;
    struct BloomPasses;
    struct DepthOfFieldPasses;
    struct ShadePasses;
    struct SunPasses;
    struct SunCamera;

    bool RenderDepthOfField(DepthOfFieldPasses& passes, const DepthOfField& depthOfField, const FrameView& frame, bool isBottomScreenWide);
    DepthOfFieldPasses* GetDepthOfFieldPasses();
    void RenderShade(ShadePasses& passes, const Shade& shade, const FrameView& frame, bool isBottomScreenWide);
    ShadePasses* GetShadePasses();
    void RenderSun(SunPasses& passes, const Sun& sun, const FrameView& frame, bool isBottomScreenWide);
    SunPasses* GetSunPasses();

    bool RenderPass(PrePass& pass, int topScreenWidth, bool isBottomScreenWide, const std::optional<TiltShift>& tiltShift);
    void BeginPasses();
    void RenderBloom(BloomPasses* passes, PrePass* onlyThreeDBright, int variant, const Bloom& bloom, const FrameView& frame,
        bool isBottomScreenWide);
    GLuint RenderGlow(BloomPasses& passes, PrePass* onlyThreeDBright, const Bloom& bloom, const FrameView& frame, bool isBottomScreenWide);
    void LogBloomTarget();
    BloomPasses* GetBloomPasses();
    PrePass* GetOnlyThreeDBrightPass();
    int PickCombineVariant(bool hasGlow, bool hasHaze, bool hasVignette);
    PrePass* GetCombinePass(int variant);
    GLuint ExpandHalvesAt1x(const FrameView& frame);
    PrePass* GetExpandPass();
    TiltShiftPasses* GetTiltShiftPasses();
    PrePass* GetTiltShiftColorPass();
    void CreateQuad();
    void SaveState();
    void RestoreState();

    VideoFilter Filter;
    bool ColorCorrection;

    bool IsScreenShaderFallback = false;
    std::unique_ptr<Shader> ScreenShaderProgram;
    std::unique_ptr<PrePass> DebandPass;
    std::vector<std::unique_ptr<PrePass>> PrePasses;

    std::unique_ptr<Shader> DebandScreenShader;
    bool IsDebandScreenShaderBuilt = false;

    bool IsNoFilter = false;
    std::unique_ptr<Shader> HalvesAt1xShader;
    bool IsHalvesAt1xShaderBuilt = false;
    std::unique_ptr<PrePass> ExpandPass;
    bool IsExpandPassBuilt = false;
    // the frame the expand pass's target holds, as a frame is presented more than once (and again and again while paused)
    GLuint ExpandedFrameTextureId = 0;
    melonDS::u64 ExpandedFrameSequence = 0;
    int ExpandedHalvesAt1x = 0, ExpandedHalfScaleAt1x = 1, ExpandedWidth = 0, ExpandedHeight = 0;
    GLuint CoordinateTextures[2] {};
    int CoordinatesWidth = 0, CoordinatesHeight = 0;
    // the 1x shader's coordinates, made once at the largest size asked for so a new size allocates only when it's bigger
    std::vector<melonDS::u16> Coordinates;

    std::unique_ptr<TiltShiftPasses> TiltShiftPassesBuilt;
    bool IsTiltShiftBuilt = false;
    std::unique_ptr<PrePass> TiltShiftColorPass;
    bool IsTiltShiftColorBuilt = false;
    // the same for a script's glow, its 3D-only bright pass, and each variant of the combine pass that adds the glow, the haze and the vignette
    // (by COMBINE_GLOW and COMBINE_HAZE bits); a target that can't be made is only logged once
    static constexpr int COMBINE_VARIANTS = 4;
    std::unique_ptr<BloomPasses> BloomPassesBuilt;
    bool IsBloomBuilt = false;
    std::unique_ptr<PrePass> OnlyThreeDBrightPass;
    bool IsOnlyThreeDBrightBuilt = false;
    std::unique_ptr<PrePass> CombinePasses[COMBINE_VARIANTS];
    bool IsCombineBuilt[COMBINE_VARIANTS] {};
    bool HasLoggedBloomTarget = false;
    // the same for a script's depth of field
    std::unique_ptr<DepthOfFieldPasses> DepthOfFieldPassesBuilt;
    bool IsDepthOfFieldBuilt = false;
    // the same for a script's shading; a target that can't be made is only logged once
    std::unique_ptr<ShadePasses> ShadePassesBuilt;
    bool IsShadeBuilt = false;
    bool HasLoggedShadeTarget = false;
    // the same for a script's sun shadows, and the camera they're marched with, filled for each new frame
    std::unique_ptr<SunPasses> SunPassesBuilt;
    std::unique_ptr<SunCamera> SunCameraOfFrame;
    bool IsSunBuilt = false;
    bool HasLoggedSunTarget = false;

    FilterOutput Output;

    GLuint QuadVao = 0, QuadVbo = 0;

    // the frame the pre-pass textures hold now, and what they were run with
    GLuint LastFrameTextureId = 0;
    melonDS::u64 LastFrameSequence = 0;
    int LastFrameWidth = 0, LastFrameHeight = 0;
    bool LastFrameBottomScreenWide = false;
    std::optional<TiltShift> LastFrameTiltShift;
    std::optional<Bloom> LastFrameBloom;
    std::optional<DepthOfField> LastFrameDepthOfField;
    std::optional<Shade> LastFrameShade;
    std::optional<Sun> LastFrameSun;
    GLuint LastFrameDepthTexture = 0;
    bool LastFrameRanDeband = false;
    GLuint LastOutputTextureId = 0;
    int LastOutputWidth = 0, LastOutputHeight = 0;

    GLint SavedViewport[4] {};
    GLint SavedDrawFramebuffer = 0, SavedReadFramebuffer = 0;
    bool SavedBlendEnabled = false;

    // the next pass's input while RenderPrePasses runs, and whether the state above is saved yet
    GLuint InputTexture = 0;
    int InputWidth = 0, InputHeight = 0;
    int InputTopScreenHeight = 0;
    bool IsStateSaved = false;
};

}
