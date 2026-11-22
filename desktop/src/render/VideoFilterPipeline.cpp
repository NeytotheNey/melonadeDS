#include "render/VideoFilterPipeline.h"

#include <algorithm>
#include <cstdio>

namespace Frontend
{

namespace
{

const char* DEBAND_DEFINE = "DEBAND";
// the bloom's last pass without the glow, and with the haze (BloomCombinePrePassShader)
const char* NO_GLOW_DEFINE = "NO_GLOW";
const char* HAZE_DEFINE = "HAZE";
// the bloom's bright pass that only takes 3D pixels (BloomBrightPrePassShader)
const char* ONLY_3D_DEFINE = "ONLY_3D";
// the combine pass's variants, as bits of an index into CombinePasses; -1 is no combine pass
const int COMBINE_GLOW = 1;
const int COMBINE_HAZE = 2;
const int NO_COMBINE = -1;
const GLenum COLOR_ATTACHMENTS[] = {GL_COLOR_ATTACHMENT0};

const ShaderProgramSource* FindShader(const char* name)
{
    const ShaderLibrary* library = ShaderLibrary::Get();
    return library ? library->Find(name) : nullptr;
}

// the frame's depth texture [texture] on [unit], nearest since its values can't be mixed; leaves unit 0 active
void BindDepthTexture(GLenum unit, GLuint texture)
{
    glActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glActiveTexture(GL_TEXTURE0);
}

void UnbindTexture(GLenum unit)
{
    glActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
}

}

// one pass, [Scale] times its input on each axis, into a texture of its own
class VideoFilterPipeline::PrePass
{
public:
    PrePass(std::unique_ptr<Shader> shader, int scale, bool nativeResolutionOnly)
        : Scale(scale), NativeResolutionOnly(nativeResolutionOnly), Program(std::move(shader)) {}
    ~PrePass() { DeleteTarget(); }

    const int Scale;
    const bool NativeResolutionOnly;
    GLuint Texture = 0;

    // the target texture at [width] x [height], made again on a new size; whether its framebuffer can be drawn to
    bool PrepareTarget(int width, int height)
    {
        if (Texture && width == Width && height == Height)
            return IsFramebufferComplete;

        DeleteTarget();
        Width = width;
        Height = height;

        glGenTextures(1, &Texture);
        glBindTexture(GL_TEXTURE_2D, Texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenFramebuffers(1, &Framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Texture, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        IsFramebufferComplete = status == GL_FRAMEBUFFER_COMPLETE;
        if (!IsFramebufferComplete)
            fprintf(stderr, "pre-pass framebuffer incomplete (%dx%d): status 0x%x\n", width, height, status);
        return IsFramebufferComplete;
    }

    // the input through this pass's shader into the target; the caller binds the quad and sets the viewport and blending
    void Render(GLuint inputTexture, int inputWidth, int inputHeight, int topScreenWidth, int topScreenHeight, bool isBottomScreenWide,
        const std::optional<TiltShift>& tiltShift, int halvesAt1x = 0, int scaleAt1x = 1, int halfScaleAt1x = 1, const Bloom* bloom = nullptr,
        const DepthOfField* depthOfField = nullptr, float focusU = 0.f, float focusV = 0.f, float focusAgeStep = 0.f)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
        // The quad covers every texel, so the old contents never have to be loaded
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, COLOR_ATTACHMENTS);

        Program->Use();
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, Program->TextureFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, Program->TextureFilter);
        if (Program->AttribPos >= 0)
            glVertexAttribPointer(Program->AttribPos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        if (Program->AttribUv >= 0)
            glVertexAttribPointer(Program->AttribUv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glUniform1i(Program->UniformTex, 0);
        Program->SetTextureSize(inputWidth, inputHeight);
        Program->SetTopScreenWidth(topScreenWidth);
        Program->SetTopScreenHeight(topScreenHeight);
        Program->SetBottomScreenWide(isBottomScreenWide);
        if (tiltShift)
            Program->SetTiltShift(*tiltShift);
        if (bloom)
            Program->SetBloom(*bloom);
        if (depthOfField)
            Program->SetDepthOfField(*depthOfField, focusU, focusV, focusAgeStep);
        Program->SetHalvesAt1x(halvesAt1x, scaleAt1x, halfScaleAt1x);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // the target, made with PrepareTarget, filled with one color; glClearBufferfv leaves the renderer's clear color as it was
    void Clear(float red, float green, float blue, float alpha)
    {
        const GLfloat color[] = {red, green, blue, alpha};
        glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
        glClearBufferfv(GL_COLOR, 0, color);
    }

private:
    void DeleteTarget()
    {
        if (Framebuffer)
        {
            glDeleteFramebuffers(1, &Framebuffer);
            Framebuffer = 0;
        }
        if (Texture)
        {
            glDeleteTextures(1, &Texture);
            Texture = 0;
        }
        IsFramebufferComplete = false;
    }

    std::unique_ptr<Shader> Program;
    GLuint Framebuffer = 0;
    int Width = 0, Height = 0;
    bool IsFramebufferComplete = false;
};

struct VideoFilterPipeline::TiltShiftPasses
{
    std::unique_ptr<PrePass> Horizontal, Vertical;
};

// the focus pass's two 1 x 1 targets, each frame reading the last frame's focus from one and writing the other, then the blur's two directions
struct VideoFilterPipeline::DepthOfFieldPasses
{
    std::unique_ptr<PrePass> Focus[2];
    std::unique_ptr<PrePass> Horizontal, Vertical;
    // the focus target written next, and the focus passes run, as every other one ages a hold
    int NextFocus = 0;
    unsigned FocusFrameCount = 0;
    // whether the focus targets start over as no focus and no hold: when they're new, and after frames without the depth of field, whose last
    // focus is no longer the player's
    bool NeedsFocusClear = true;
};

// the glow's passes at DS-native resolution; a combine pass (CombinePasses) adds it at the input's
struct VideoFilterPipeline::BloomPasses
{
    std::unique_ptr<PrePass> Bright, Horizontal, Vertical;
};

VideoFilterPipeline::VideoFilterPipeline(const VideoFilter& filter, bool colorCorrection)
    : Filter(filter), ColorCorrection(colorCorrection)
{
    // A pre-pass that fails to build is left out, so the screen shader gets its input instead
    ScreenShaderProgram = CreateScreenShaderProgram(filter.ScreenShader, colorCorrection, IsScreenShaderFallback);
    if (filter.Deband)
    {
        if (std::unique_ptr<Shader> shader = CreateShaderProgramOrNull(FindShader("DebandPrePassShader")))
            DebandPass = std::make_unique<PrePass>(std::move(shader), 1, false);
    }
    for (const FilterPrePass& prePass : filter.PrePasses)
    {
        if (std::unique_ptr<Shader> shader = CreateShaderProgramOrNull(prePass.Source))
            PrePasses.push_back(std::make_unique<PrePass>(std::move(shader), prePass.Scale, prePass.NativeResolutionOnly));
    }
    IsNoFilter = filter.ScreenShader == FindShader("NoFilterShader") && !DebandPass && PrePasses.empty();
}

VideoFilterPipeline::~VideoFilterPipeline()
{
    if (CoordinateTextures[0])
        glDeleteTextures(2, CoordinateTextures);
    if (QuadVao)
    {
        glDeleteVertexArrays(1, &QuadVao);
        glDeleteBuffers(1, &QuadVbo);
    }
    // the shaders and passes delete their own
}

bool VideoFilterPipeline::CanFoldDeband() const
{
    return DebandPass && Filter.CanFoldDeband && !IsScreenShaderFallback && !(IsDebandScreenShaderBuilt && !DebandScreenShader);
}

const Shader* VideoFilterPipeline::GetDebandScreenShader()
{
    if (!IsDebandScreenShaderBuilt)
    {
        IsDebandScreenShaderBuilt = true;
        DebandScreenShader = CreateShaderProgramOrNull(Filter.ScreenShader, ColorCorrection, {DEBAND_DEFINE});
        if (!DebandScreenShader)
            fprintf(stderr, "the deband screen shader failed to build, so deband stays a pre-pass\n");
    }
    return DebandScreenShader.get();
}

void VideoFilterPipeline::BindHalvesAt1xCoordinates(int width, int height)
{
    if (!CoordinateTextures[0])
        glGenTextures(2, CoordinateTextures);
    bool isResized = width != CoordinatesWidth || height != CoordinatesHeight;
    GLint savedAlignment = 4;
    if (isResized)
    {
        size_t count = (size_t)std::max({width, height, 1});
        if (Coordinates.size() < count)
        {
            Coordinates.resize(count);
            for (size_t i = 0; i < count; i++)
                Coordinates[i] = (melonDS::u16)i;
        }
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &savedAlignment);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    }
    for (int axis = 0; axis < 2; axis++)
    {
        glActiveTexture(GL_TEXTURE1 + axis);
        glBindTexture(GL_TEXTURE_2D, CoordinateTextures[axis]);
        if (isResized)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, axis == 0 ? width : 1, axis == 0 ? 1 : height, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT,
                Coordinates.data());
        }
    }
    if (isResized)
    {
        glPixelStorei(GL_UNPACK_ALIGNMENT, savedAlignment);
        CoordinatesWidth = width;
        CoordinatesHeight = height;
    }
    glActiveTexture(GL_TEXTURE0);
}

const Shader* VideoFilterPipeline::GetHalvesAt1xShader()
{
    if (!IsHalvesAt1xShaderBuilt)
    {
        IsHalvesAt1xShaderBuilt = true;
        HalvesAt1xShader = CreateShaderProgramOrNull(FindShader("NoFilterHalvesAt1xShader"), ColorCorrection);
        if (!HalvesAt1xShader)
            fprintf(stderr, "the 1x screen shader failed to build\n");
    }
    return HalvesAt1xShader.get();
}

VideoFilterPipeline::PrePass* VideoFilterPipeline::GetExpandPass()
{
    if (!IsExpandPassBuilt)
    {
        IsExpandPassBuilt = true;
        if (std::unique_ptr<Shader> shader = CreateShaderProgramOrNull(FindShader("ExpandHalvesAt1xPrePassShader")))
            ExpandPass = std::make_unique<PrePass>(std::move(shader), 1, false);
        else
            fprintf(stderr, "the 1x expand shader failed to build\n");
    }
    return ExpandPass.get();
}

GLuint VideoFilterPipeline::ExpandHalvesAt1x(const FrameView& frame)
{
    int width = frame.Width;
    int height = frame.Height;
    PrePass* pass = GetExpandPass();
    if (!pass || width <= 0 || height <= 0)
        return frame.Texture;

    bool isExpanded = frame.Sequence != 0 && frame.Sequence == ExpandedFrameSequence && frame.Texture == ExpandedFrameTextureId
        && frame.HalvesAt1x == ExpandedHalvesAt1x && frame.HalfScale == ExpandedHalfScaleAt1x && width == ExpandedWidth && height == ExpandedHeight;
    if (isExpanded)
        return pass->Texture;

    SaveState();
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    if (!QuadVao)
        CreateQuad();
    glBindVertexArray(QuadVao);
    glBindBuffer(GL_ARRAY_BUFFER, QuadVbo);
    bool isReady = pass->PrepareTarget(width, height);
    if (isReady)
    {
        glViewport(0, 0, width, height);
        pass->Render(frame.Texture, width, height, frame.TopWidth, frame.TopScreenHeight(), false, std::nullopt, frame.HalvesAt1x, frame.Scale,
            frame.HalfScale);
    }
    RestoreState();

    if (!isReady)
    {
        ExpandedFrameSequence = 0;
        return frame.Texture;
    }
    ExpandedFrameTextureId = frame.Texture;
    ExpandedFrameSequence = frame.Sequence;
    ExpandedHalvesAt1x = frame.HalvesAt1x;
    ExpandedHalfScaleAt1x = frame.HalfScale;
    ExpandedWidth = width;
    ExpandedHeight = height;
    return pass->Texture;
}

const FilterOutput& VideoFilterPipeline::RenderPrePasses(const FrameView& frame, bool isBottomScreenWide, const std::optional<TiltShift>& tiltShift,
    const std::optional<Bloom>& bloom, const std::optional<DepthOfField>& depthOfField, bool foldDeband)
{
    Output.Set(frame.Texture, frame.Width, frame.Height);

    if (!frame.IsValid())
    {
        // Cleared on no frame between games, since a new game restarts the frame sequence and can reuse texture names
        LastFrameSequence = 0;
        ExpandedFrameSequence = 0;
        if (DepthOfFieldPassesBuilt)
            DepthOfFieldPassesBuilt->NeedsFocusClear = true;
        return Output;
    }

    // GPU::Comp1x: a frame with screens at 1x that comes here becomes the full frame first, which then goes through everything below exactly as
    // the full frame would
    GLuint frameTextureId = frame.HalvesAt1x != 0 ? ExpandHalvesAt1x(frame) : frame.Texture;
    Output.Set(frameTextureId, frame.Width, frame.Height);

    // Without a blur, or if its passes can't be built, only the color boost is left to apply, in one pass. Each is only tried once
    TiltShiftPasses* tiltShiftPasses = tiltShift && tiltShift->HasBlur() ? GetTiltShiftPasses() : nullptr;
    PrePass* tiltShiftColorPass = tiltShift && !tiltShiftPasses && tiltShift->HasColorBoost() ? GetTiltShiftColorPass() : nullptr;
    // The same for the bloom: without a glow, or if its passes can't be built, only the haze and the vignette are left, in a combine pass that
    // reads no glow. The haze needs the frame's depth, and so does a 3D-only glow, which takes every pixel without it
    bool hasDepth = frame.DepthTexture != 0;
    BloomPasses* bloomPasses = bloom && bloom->HasBloom() ? GetBloomPasses() : nullptr;
    int combineVariant = bloom ? PickCombineVariant(bloomPasses != nullptr, bloom->HasHaze() && hasDepth, bloom->HasVignette()) : NO_COMBINE;
    BloomPasses* glowPasses = combineVariant != NO_COMBINE && (combineVariant & COMBINE_GLOW) ? bloomPasses : nullptr;
    PrePass* onlyThreeDBrightPass = bloom && glowPasses && bloom->OnlyThreeD && hasDepth ? GetOnlyThreeDBrightPass() : nullptr;
    bool usesHaze = combineVariant != NO_COMBINE && (combineVariant & COMBINE_HAZE);
    DepthOfFieldPasses* depthOfFieldPasses = depthOfField && !depthOfField->IsOff() && hasDepth ? GetDepthOfFieldPasses() : nullptr;
    if (!depthOfFieldPasses && DepthOfFieldPassesBuilt)
        DepthOfFieldPassesBuilt->NeedsFocusClear = true;
    bool runsDeband = DebandPass && !foldDeband;
    bool hasLuaPasses = tiltShiftPasses || tiltShiftColorPass || combineVariant != NO_COMBINE || depthOfFieldPasses;
    if (!runsDeband && PrePasses.empty() && !hasLuaPasses)
        return Output;

    int width = frame.Width;
    int height = frame.Height;
    if (width <= 0 || height <= 0)
        return Output;

    std::optional<TiltShift> appliedTiltShift = tiltShiftPasses || tiltShiftColorPass ? tiltShift : std::nullopt;
    std::optional<Bloom> appliedBloom = combineVariant != NO_COMBINE ? bloom : std::nullopt;
    std::optional<DepthOfField> appliedDepthOfField = depthOfFieldPasses ? depthOfField : std::nullopt;
    GLuint depthTexture = depthOfFieldPasses || onlyThreeDBrightPass || usesHaze ? frame.DepthTexture : 0;
    bool isSameFrame = frame.Sequence != 0 && frame.Sequence == LastFrameSequence && frame.Texture == LastFrameTextureId && width == LastFrameWidth
        && height == LastFrameHeight && isBottomScreenWide == LastFrameBottomScreenWide && appliedTiltShift == LastFrameTiltShift
        && appliedBloom == LastFrameBloom && appliedDepthOfField == LastFrameDepthOfField && depthTexture == LastFrameDepthTexture
        && runsDeband == LastFrameRanDeband;
    if (isSameFrame)
    {
        Output.Set(LastOutputTextureId, LastOutputWidth, LastOutputHeight);
        return Output;
    }

    // The width can't tell, since widescreen makes the frame wider without upscaling it
    bool isUpscaled = height > frame.FrameHeight();
    InputTexture = frameTextureId;
    InputWidth = width;
    InputHeight = height;
    InputTopScreenHeight = frame.TopScreenHeight();
    IsStateSaved = false;
    bool isComplete = true;
    if (runsDeband)
        isComplete = RenderPass(*DebandPass, frame.TopWidth, isBottomScreenWide, std::nullopt);
    for (const std::unique_ptr<PrePass>& pass : PrePasses)
    {
        if (!isComplete)
            break;
        if (pass->NativeResolutionOnly && isUpscaled)
            continue;
        if (!RenderPass(*pass, frame.TopWidth, isBottomScreenWide, std::nullopt))
        {
            isComplete = false;
            break;
        }
    }

    if (isComplete && depthOfFieldPasses && appliedDepthOfField)
        isComplete = RenderDepthOfField(*depthOfFieldPasses, *appliedDepthOfField, frame, isBottomScreenWide);
    if (isComplete && appliedBloom)
        RenderBloom(glowPasses, onlyThreeDBrightPass, combineVariant, *appliedBloom, frame, isBottomScreenWide);
    if (isComplete && tiltShiftPasses)
    {
        isComplete = RenderPass(*tiltShiftPasses->Horizontal, frame.TopWidth, isBottomScreenWide, appliedTiltShift)
            && RenderPass(*tiltShiftPasses->Vertical, frame.TopWidth, isBottomScreenWide, appliedTiltShift);
    }
    else if (isComplete && tiltShiftColorPass)
        isComplete = RenderPass(*tiltShiftColorPass, frame.TopWidth, isBottomScreenWide, appliedTiltShift);

    if (IsStateSaved)
        RestoreState();

    if (!isComplete)
    {
        // Draw the frame without the pre-passes rather than nothing
        LastFrameSequence = 0;
        return Output;
    }

    LastFrameTextureId = frame.Texture;
    LastFrameSequence = frame.Sequence;
    LastFrameWidth = width;
    LastFrameHeight = height;
    LastFrameBottomScreenWide = isBottomScreenWide;
    LastFrameTiltShift = appliedTiltShift;
    LastFrameBloom = appliedBloom;
    LastFrameDepthOfField = appliedDepthOfField;
    LastFrameDepthTexture = depthTexture;
    LastFrameRanDeband = runsDeband;
    LastOutputTextureId = InputTexture;
    LastOutputWidth = InputWidth;
    LastOutputHeight = InputHeight;
    Output.Set(InputTexture, InputWidth, InputHeight);
    return Output;
}

bool VideoFilterPipeline::RenderPass(PrePass& pass, int topScreenWidth, bool isBottomScreenWide, const std::optional<TiltShift>& tiltShift)
{
    BeginPasses();
    int outputWidth = InputWidth * pass.Scale;
    int outputHeight = InputHeight * pass.Scale;
    if (!pass.PrepareTarget(outputWidth, outputHeight))
        return false;
    glViewport(0, 0, outputWidth, outputHeight);
    pass.Render(InputTexture, InputWidth, InputHeight, topScreenWidth, InputTopScreenHeight, isBottomScreenWide, tiltShift);
    InputTexture = pass.Texture;
    InputWidth = outputWidth;
    InputHeight = outputHeight;
    return true;
}

// the GL state the passes change saved and their quad bound, before a frame's first pass
void VideoFilterPipeline::BeginPasses()
{
    if (IsStateSaved)
        return;
    SaveState();
    IsStateSaved = true;
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    if (!QuadVao)
        CreateQuad();
    glBindVertexArray(QuadVao);
    glBindBuffer(GL_ARRAY_BUFFER, QuadVbo);
}

// [bloom] over the current input, whose size it keeps, through the combine pass [variant] (from PickCombineVariant): with COMBINE_GLOW the glow
// through [passes] first, its bright pass [onlyThreeDBright] when that isn't null, and with COMBINE_HAZE the haze from the frame's depth on
// unit 2. A glow target that can't be made leaves the glow out (the haze and vignette stay), and a combine target that can't be made leaves the
// bloom out, logged once, so the frame is still drawn
void VideoFilterPipeline::RenderBloom(BloomPasses* passes, PrePass* onlyThreeDBright, int variant, const Bloom& bloom, const FrameView& frame,
    bool isBottomScreenWide)
{
    BeginPasses();
    GLuint glowTexture = passes && (variant & COMBINE_GLOW) ? RenderGlow(*passes, onlyThreeDBright, bloom, frame, isBottomScreenWide) : 0;
    int combineVariant = glowTexture ? variant : variant & ~COMBINE_GLOW;
    // Without the glow, it's only worth a pass with the haze or the vignette
    PrePass* combine = combineVariant != 0 || bloom.HasVignette() ? GetCombinePass(combineVariant) : nullptr;
    if (!combine)
        return;
    if (!combine->PrepareTarget(InputWidth, InputHeight))
    {
        LogBloomTarget();
        return;
    }

    // The glow's target is linear from when it was made, and no pass reads it with anything else
    if (glowTexture)
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, glowTexture);
        glActiveTexture(GL_TEXTURE0);
    }
    bool hasHaze = combineVariant & COMBINE_HAZE;
    if (hasHaze)
        BindDepthTexture(GL_TEXTURE2, frame.DepthTexture);
    glViewport(0, 0, InputWidth, InputHeight);
    combine->Render(InputTexture, InputWidth, InputHeight, frame.TopWidth, InputTopScreenHeight, isBottomScreenWide, std::nullopt, 0, 1, 1, &bloom);
    if (glowTexture)
        UnbindTexture(GL_TEXTURE1);
    if (hasHaze)
        UnbindTexture(GL_TEXTURE2);
    InputTexture = combine->Texture;
}

// the current input's bright parts, blurred, at DS-native resolution (the frame's size in DS pixels): the texture of the glow's last pass, or 0
// if one of its targets can't be made. With [onlyThreeDBright] that's the bright pass, reading the frame's depth on unit 1
GLuint VideoFilterPipeline::RenderGlow(BloomPasses& passes, PrePass* onlyThreeDBright, const Bloom& bloom, const FrameView& frame,
    bool isBottomScreenWide)
{
    int width = frame.TopWidth;
    int height = frame.FrameHeight();
    PrePass& bright = onlyThreeDBright ? *onlyThreeDBright : *passes.Bright;
    bool isReady = width > 0 && bright.PrepareTarget(width, height) && passes.Horizontal->PrepareTarget(width, height)
        && passes.Vertical->PrepareTarget(width, height);
    if (!isReady)
    {
        LogBloomTarget();
        return 0;
    }

    glViewport(0, 0, width, height);
    if (onlyThreeDBright)
        BindDepthTexture(GL_TEXTURE1, frame.DepthTexture);
    bright.Render(InputTexture, InputWidth, InputHeight, width, InputTopScreenHeight, isBottomScreenWide, std::nullopt, 0, 1, 1, &bloom);
    if (onlyThreeDBright)
        UnbindTexture(GL_TEXTURE1);
    passes.Horizontal->Render(bright.Texture, width, height, width, InputTopScreenHeight, isBottomScreenWide, std::nullopt, 0, 1, 1, &bloom);
    passes.Vertical->Render(passes.Horizontal->Texture, width, height, width, InputTopScreenHeight, isBottomScreenWide, std::nullopt, 0, 1, 1, &bloom);
    return passes.Vertical->Texture;
}

// the current input blurred by [depthOfField] through [passes]: the focus pass picks this frame's focus from the last one's and the frame's depth
// texture on unit 1 (nearest, as its values can't be mixed), then the blur reads it on unit 2. Only for a new frame, as a frame drawn again
// mustn't age a hold; false if a target can't be drawn to
bool VideoFilterPipeline::RenderDepthOfField(DepthOfFieldPasses& passes, const DepthOfField& depthOfField, const FrameView& frame,
    bool isBottomScreenWide)
{
    BeginPasses();
    bool isReady = passes.Focus[0]->PrepareTarget(1, 1) && passes.Focus[1]->PrepareTarget(1, 1)
        && passes.Horizontal->PrepareTarget(InputWidth, InputHeight) && passes.Vertical->PrepareTarget(InputWidth, InputHeight);
    if (!isReady)
        return false;
    if (passes.NeedsFocusClear)
    {
        // No focus (RGB 0) and a hold that's run out (A 1), so the first frame takes whatever is at the point
        passes.Focus[0]->Clear(0.f, 0.f, 0.f, 1.f);
        passes.Focus[1]->Clear(0.f, 0.f, 0.f, 1.f);
        passes.NeedsFocusClear = false;
    }

    // the focus point in the depth texture, which is the top screen's half of the frame: the wings come before the 256 view's x 0, and the
    // margin before its y 0
    int topWidth = std::max(frame.TopWidth, 1);
    float focusU = (depthOfField.FocusX + (topWidth - 256) / 2.f) / topWidth;
    float focusV = (depthOfField.FocusY + frame.TopMargin) / frame.TopScreenHeight();
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, frame.DepthTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glActiveTexture(GL_TEXTURE0);

    PrePass& focus = *passes.Focus[passes.NextFocus];
    PrePass& lastFocus = *passes.Focus[1 - passes.NextFocus];
    float focusAgeStep = passes.FocusFrameCount % 2 == 0 ? 1.f : 0.f;
    passes.FocusFrameCount++;
    glViewport(0, 0, 1, 1);
    focus.Render(lastFocus.Texture, 1, 1, frame.TopWidth, InputTopScreenHeight, isBottomScreenWide, std::nullopt, 0, 1, 1, nullptr, &depthOfField,
        focusU, focusV, focusAgeStep);
    // Nearest, as the focus's bytes can't be mixed either (its target is made linear)
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, focus.Texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glActiveTexture(GL_TEXTURE0);

    glViewport(0, 0, InputWidth, InputHeight);
    passes.Horizontal->Render(InputTexture, InputWidth, InputHeight, frame.TopWidth, InputTopScreenHeight, isBottomScreenWide, std::nullopt, 0, 1,
        1, nullptr, &depthOfField, focusU, focusV);
    passes.Vertical->Render(passes.Horizontal->Texture, InputWidth, InputHeight, frame.TopWidth, InputTopScreenHeight, isBottomScreenWide,
        std::nullopt, 0, 1, 1, nullptr, &depthOfField, focusU, focusV);
    InputTexture = passes.Vertical->Texture;
    passes.NextFocus = 1 - passes.NextFocus;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    return true;
}

VideoFilterPipeline::DepthOfFieldPasses* VideoFilterPipeline::GetDepthOfFieldPasses()
{
    if (!IsDepthOfFieldBuilt)
    {
        IsDepthOfFieldBuilt = true;
        // a program for each focus target, as a PrePass owns its own
        std::unique_ptr<Shader> focus = CreateShaderProgramOrNull(FindShader("DepthOfFieldFocusShader"));
        std::unique_ptr<Shader> otherFocus = CreateShaderProgramOrNull(FindShader("DepthOfFieldFocusShader"));
        std::unique_ptr<Shader> horizontal = CreateShaderProgramOrNull(FindShader("DepthOfFieldHorizontalPrePassShader"));
        std::unique_ptr<Shader> vertical = CreateShaderProgramOrNull(FindShader("DepthOfFieldVerticalPrePassShader"));
        if (focus && otherFocus && horizontal && vertical)
        {
            DepthOfFieldPassesBuilt = std::make_unique<DepthOfFieldPasses>();
            DepthOfFieldPassesBuilt->Focus[0] = std::make_unique<PrePass>(std::move(focus), 1, false);
            DepthOfFieldPassesBuilt->Focus[1] = std::make_unique<PrePass>(std::move(otherFocus), 1, false);
            DepthOfFieldPassesBuilt->Horizontal = std::make_unique<PrePass>(std::move(horizontal), 1, false);
            DepthOfFieldPassesBuilt->Vertical = std::make_unique<PrePass>(std::move(vertical), 1, false);
        }
        else
        {
            // The frame is drawn sharp rather than not at all (what did build goes with the unique_ptrs)
            fprintf(stderr, "depth of field shaders failed to build, so the blur is left out\n");
        }
    }
    return DepthOfFieldPassesBuilt.get();
}

void VideoFilterPipeline::LogBloomTarget()
{
    if (!HasLoggedBloomTarget)
    {
        HasLoggedBloomTarget = true;
        fprintf(stderr, "a bloom target can't be made, so the bloom is left out\n");
    }
}

VideoFilterPipeline::BloomPasses* VideoFilterPipeline::GetBloomPasses()
{
    if (!IsBloomBuilt)
    {
        IsBloomBuilt = true;
        std::unique_ptr<Shader> bright = CreateShaderProgramOrNull(FindShader("BloomBrightPrePassShader"));
        std::unique_ptr<Shader> horizontal = CreateShaderProgramOrNull(FindShader("BloomBlurHorizontalPrePassShader"));
        std::unique_ptr<Shader> vertical = CreateShaderProgramOrNull(FindShader("BloomBlurVerticalPrePassShader"));
        if (bright && horizontal && vertical)
        {
            BloomPassesBuilt = std::make_unique<BloomPasses>();
            BloomPassesBuilt->Bright = std::make_unique<PrePass>(std::move(bright), 1, false);
            BloomPassesBuilt->Horizontal = std::make_unique<PrePass>(std::move(horizontal), 1, false);
            BloomPassesBuilt->Vertical = std::make_unique<PrePass>(std::move(vertical), 1, false);
        }
        else
        {
            // The frame is drawn without the glow rather than not at all (what did build goes with the unique_ptrs)
            fprintf(stderr, "bloom shaders failed to build, so the glow is left out\n");
        }
    }
    return BloomPassesBuilt.get();
}

VideoFilterPipeline::PrePass* VideoFilterPipeline::GetOnlyThreeDBrightPass()
{
    if (!IsOnlyThreeDBrightBuilt)
    {
        IsOnlyThreeDBrightBuilt = true;
        if (std::unique_ptr<Shader> shader = CreateShaderProgramOrNull(FindShader("BloomBrightPrePassShader"), false, {ONLY_3D_DEFINE}))
            OnlyThreeDBrightPass = std::make_unique<PrePass>(std::move(shader), 1, false);
        else
            // The glow takes every pixel, as it does on a frame without depth, rather than being left out
            fprintf(stderr, "3D-only bloom shader failed to build, so the glow takes 2D too\n");
    }
    return OnlyThreeDBrightPass.get();
}

// the combine pass variant for a frame with a glow whose passes built ([hasGlow]), the haze with the frame's depth ([hasHaze]) and the
// vignette, building it on first use, or NO_COMBINE for nothing to draw. A variant that doesn't build drops the glow first, then the haze, so
// what's left is still drawn
int VideoFilterPipeline::PickCombineVariant(bool hasGlow, bool hasHaze, bool hasVignette)
{
    int variant = (hasGlow ? COMBINE_GLOW : 0) | (hasHaze ? COMBINE_HAZE : 0);
    if ((variant & COMBINE_GLOW) && !GetCombinePass(variant))
        variant &= ~COMBINE_GLOW;
    if ((variant & COMBINE_HAZE) && !GetCombinePass(variant))
        variant &= ~COMBINE_HAZE;
    if (variant == 0 && (!hasVignette || !GetCombinePass(0)))
        return NO_COMBINE;
    return variant;
}

// the combine pass [variant] (COMBINE_GLOW and COMBINE_HAZE bits), built the first time it's asked for; null if it couldn't be built
VideoFilterPipeline::PrePass* VideoFilterPipeline::GetCombinePass(int variant)
{
    if (!IsCombineBuilt[variant])
    {
        IsCombineBuilt[variant] = true;
        std::vector<std::string> defines;
        if (!(variant & COMBINE_GLOW))
            defines.push_back(NO_GLOW_DEFINE);
        if (variant & COMBINE_HAZE)
            defines.push_back(HAZE_DEFINE);
        if (std::unique_ptr<Shader> shader = CreateShaderProgramOrNull(FindShader("BloomCombinePrePassShader"), false, defines))
            CombinePasses[variant] = std::make_unique<PrePass>(std::move(shader), 1, false);
        else
            // The frame is drawn without what this variant adds rather than not at all
            fprintf(stderr, "bloom combine shader (glow %d, haze %d) failed to build, so what it adds is left out\n", (variant & COMBINE_GLOW) != 0,
                (variant & COMBINE_HAZE) != 0);
    }
    return CombinePasses[variant].get();
}

VideoFilterPipeline::TiltShiftPasses* VideoFilterPipeline::GetTiltShiftPasses()
{
    if (!IsTiltShiftBuilt)
    {
        IsTiltShiftBuilt = true;
        std::unique_ptr<Shader> horizontal = CreateShaderProgramOrNull(FindShader("TiltShiftHorizontalPrePassShader"));
        std::unique_ptr<Shader> vertical = CreateShaderProgramOrNull(FindShader("TiltShiftVerticalPrePassShader"));
        if (horizontal && vertical)
        {
            TiltShiftPassesBuilt = std::make_unique<TiltShiftPasses>();
            TiltShiftPassesBuilt->Horizontal = std::make_unique<PrePass>(std::move(horizontal), 1, false);
            TiltShiftPassesBuilt->Vertical = std::make_unique<PrePass>(std::move(vertical), 1, false);
        }
        else
        {
            // The frame is drawn without the blur rather than not at all (what did build goes with the unique_ptrs)
            fprintf(stderr, "tilt-shift shaders failed to build, so the blur is left out\n");
        }
    }
    return TiltShiftPassesBuilt.get();
}

VideoFilterPipeline::PrePass* VideoFilterPipeline::GetTiltShiftColorPass()
{
    if (!IsTiltShiftColorBuilt)
    {
        IsTiltShiftColorBuilt = true;
        if (std::unique_ptr<Shader> shader = CreateShaderProgramOrNull(FindShader("TiltShiftColorPrePassShader")))
            TiltShiftColorPass = std::make_unique<PrePass>(std::move(shader), 1, false);
        else
            fprintf(stderr, "tilt-shift color shader failed to build, so the color boost is left out\n");
    }
    return TiltShiftColorPass.get();
}

void VideoFilterPipeline::CreateQuad()
{
    // Full-target quad whose UVs map target texel centers onto source texel (quadrant) centers
    const float vertexData[] = {
        // Position  UVs
        -1.f, -1.f,  0.f, 0.f,
        1.f, -1.f,   1.f, 0.f,
        -1.f, 1.f,   0.f, 1.f,
        1.f, 1.f,    1.f, 1.f,
    };
    glGenVertexArrays(1, &QuadVao);
    glGenBuffers(1, &QuadVbo);
    glBindVertexArray(QuadVao);
    glBindBuffer(GL_ARRAY_BUFFER, QuadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertexData), vertexData, GL_STATIC_DRAW);
}

// Only the state that the renderer doesn't set again before drawing: the target (the window), its viewport and blending
void VideoFilterPipeline::SaveState()
{
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &SavedDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &SavedReadFramebuffer);
    glGetIntegerv(GL_VIEWPORT, SavedViewport);
    SavedBlendEnabled = glIsEnabled(GL_BLEND);
}

void VideoFilterPipeline::RestoreState()
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)SavedDrawFramebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)SavedReadFramebuffer);
    glViewport(SavedViewport[0], SavedViewport[1], SavedViewport[2], SavedViewport[3]);
    if (SavedBlendEnabled)
        glEnable(GL_BLEND);
}

}
