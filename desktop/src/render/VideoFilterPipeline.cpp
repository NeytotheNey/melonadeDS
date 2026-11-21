#include "render/VideoFilterPipeline.h"

#include <algorithm>
#include <cstdio>

namespace Frontend
{

namespace
{

const char* DEBAND_DEFINE = "DEBAND";
const GLenum COLOR_ATTACHMENTS[] = {GL_COLOR_ATTACHMENT0};

const ShaderProgramSource* FindShader(const char* name)
{
    const ShaderLibrary* library = ShaderLibrary::Get();
    return library ? library->Find(name) : nullptr;
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
        const std::optional<TiltShift>& tiltShift, int halvesAt1x = 0, int scaleAt1x = 1, int halfScaleAt1x = 1)
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
        Program->SetHalvesAt1x(halvesAt1x, scaleAt1x, halfScaleAt1x);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
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
    bool foldDeband)
{
    Output.Set(frame.Texture, frame.Width, frame.Height);

    if (!frame.IsValid())
    {
        // Cleared on no frame between games, since a new game restarts the frame sequence and can reuse texture names
        LastFrameSequence = 0;
        ExpandedFrameSequence = 0;
        return Output;
    }

    // GPU::Comp1x: a frame with screens at 1x that comes here becomes the full frame first, which then goes through everything below exactly as
    // the full frame would
    GLuint frameTextureId = frame.HalvesAt1x != 0 ? ExpandHalvesAt1x(frame) : frame.Texture;
    Output.Set(frameTextureId, frame.Width, frame.Height);

    // Without a blur, or if its passes can't be built, only the color boost is left to apply, in one pass. Each is only tried once
    TiltShiftPasses* tiltShiftPasses = tiltShift && tiltShift->HasBlur() ? GetTiltShiftPasses() : nullptr;
    PrePass* tiltShiftColorPass = tiltShift && !tiltShiftPasses && tiltShift->HasColorBoost() ? GetTiltShiftColorPass() : nullptr;
    bool runsDeband = DebandPass && !foldDeband;
    if (!runsDeband && PrePasses.empty() && !tiltShiftPasses && !tiltShiftColorPass)
        return Output;

    int width = frame.Width;
    int height = frame.Height;
    if (width <= 0 || height <= 0)
        return Output;

    std::optional<TiltShift> appliedTiltShift = tiltShiftPasses || tiltShiftColorPass ? tiltShift : std::nullopt;
    bool isSameFrame = frame.Sequence != 0 && frame.Sequence == LastFrameSequence && frame.Texture == LastFrameTextureId && width == LastFrameWidth
        && height == LastFrameHeight && isBottomScreenWide == LastFrameBottomScreenWide && appliedTiltShift == LastFrameTiltShift
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
    LastFrameRanDeband = runsDeband;
    LastOutputTextureId = InputTexture;
    LastOutputWidth = InputWidth;
    LastOutputHeight = InputHeight;
    Output.Set(InputTexture, InputWidth, InputHeight);
    return Output;
}

bool VideoFilterPipeline::RenderPass(PrePass& pass, int topScreenWidth, bool isBottomScreenWide, const std::optional<TiltShift>& tiltShift)
{
    if (!IsStateSaved)
    {
        SaveState();
        IsStateSaved = true;
        glDisable(GL_BLEND);
        glActiveTexture(GL_TEXTURE0);
        if (!QuadVao)
            CreateQuad();
        glBindVertexArray(QuadVao);
        glBindBuffer(GL_ARRAY_BUFFER, QuadVbo);
    }

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
