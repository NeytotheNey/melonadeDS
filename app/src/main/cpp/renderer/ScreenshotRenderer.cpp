#include "ScreenshotRenderer.h"
#include "MelonLog.h"
#include "GPU.h"
#include <algorithm>

namespace MelonDSAndroid
{

ScreenshotRenderer::ScreenshotRenderer(u32* screenshotBuffer)
{
    this->screenshotBuffer = screenshotBuffer;
    this->screenshotRequested = false;
    this->stopped = false;
}

void ScreenshotRenderer::init()
{
    setupFrameBuffer();
    // The overlay shader binds its position to the screenshot shader's, so it needs that one built
    if (setupShaders())
        setupOverlayShader();
    setupVertexBuffers();
}

void ScreenshotRenderer::renderScreenshot(GPU* gpu, Renderer renderer, Frame* renderFrame, const LuaOverlay* overlay)
{
    drawScreenshot(gpu, renderer, renderFrame, overlay, false);
    notifyScreenshotReady();
}

void ScreenshotRenderer::startScreenshotReadback(GPU* gpu, Renderer renderer, Frame* renderFrame, const LuaOverlay* overlay)
{
    drawScreenshot(gpu, renderer, renderFrame, overlay, true);
}

bool ScreenshotRenderer::finishScreenshotReadback(u8* destination, bool wait)
{
    if (!readbackFence)
        return true;

    if (!wait)
    {
        GLint status = GL_UNSIGNALED;
        glGetSynciv(readbackFence, GL_SYNC_STATUS, 1, nullptr, &status);
        if (status != GL_SIGNALED)
            return false;
    }

    glDeleteSync(readbackFence);
    readbackFence = nullptr;
    if (!destination)
        return true;

    // Once the fence has signaled the map doesn't wait, otherwise it waits for the copy
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pixelPackBuffer);
    const void* pixels = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, SCREENSHOT_WIDTH * SCREENSHOT_HEIGHT * 4, GL_MAP_READ_BIT);
    if (pixels)
    {
        memcpy(destination, pixels, SCREENSHOT_WIDTH * SCREENSHOT_HEIGHT * 4);
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    else
    {
        // Blank rather than the reused state's old screenshot
        LOG_ERROR(LOG_TAG_GL, "Failed to map the screenshot pixel buffer");
        memset(destination, 0, SCREENSHOT_WIDTH * SCREENSHOT_HEIGHT * 4);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    return true;
}

void ScreenshotRenderer::drawScreenshot(GPU* gpu, Renderer renderer, Frame* renderFrame, const LuaOverlay* overlay, bool intoPixelBuffer)
{
    if (renderer == Renderer::Software)
    {
        int frontBuffer = gpu->FrontBuffer;
        memcpy(screenshotBuffer, gpu->Framebuffer[frontBuffer][0].get(), 256 * 192 * 4);
        memcpy(&screenshotBuffer[256 * 192], gpu->Framebuffer[frontBuffer][1].get(), 256 * 192 * 4);

        if (overlay)
            blendOverlay(*overlay);
    }
    else
    {
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_BLEND);
        glViewport(0, 0, SCREENSHOT_WIDTH, SCREENSHOT_HEIGHT);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer);
        glUseProgram(screenshotRenderShader);

        // Render the frame texture into the FBO
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, renderFrame->frameTexture);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        if (renderFrame->topScreenWidth != vertexBufferTopScreenWidth || renderFrame->topScreenMargin != vertexBufferTopScreenMargin
            || renderFrame->nativeLeft() != vertexBufferNativeLeft || renderFrame->nativeTop() != vertexBufferNativeTop)
            fillVertexBuffer(renderFrame->topScreenWidth, renderFrame->topScreenMargin, renderFrame->nativeLeft(), renderFrame->nativeTop());
        glEnableVertexAttribArray(posAttribLocation);
        glEnableVertexAttribArray(texCoordAttribLocation);
        glVertexAttribPointer(posAttribLocation, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glVertexAttribPointer(texCoordAttribLocation, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glUniform1i(textureUniformLocation, 0);

        glDrawArrays(GL_TRIANGLES, 0, 12);

        if (renderFrame->halvesAt1x != 0)
            copyHalvesAt1x(renderFrame);

        if (overlay && renderFrame->overlayTexture)
            renderOverlay(renderFrame->overlayTexture);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, frameBuffer);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        if (intoPixelBuffer)
        {
            // Only queued: the GPU copies the image once the frame is drawn and finishScreenshotReadback() collects it, since reading straight into
            // screenshotBuffer would wait for the whole frame's GPU work (3-17 ms on the Fold with the compute renderer at 8x)
            if (readbackFence)
                glDeleteSync(readbackFence);

            glBindBuffer(GL_PIXEL_PACK_BUFFER, pixelPackBuffer);
            glReadPixels(0, 0, SCREENSHOT_WIDTH, SCREENSHOT_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            readbackFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            glFlush();
        }
        else
        {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glReadPixels(0, 0, SCREENSHOT_WIDTH, SCREENSHOT_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, screenshotBuffer);
        }

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }
}

void ScreenshotRenderer::copyHalvesAt1x(Frame* renderFrame)
{
    // The screenshot is at 1x, so such a screen's image is copied over what the quads drew from the old frames in the rest of its rows: its middle
    // 256 columns (Frame::nativeLeft), from the first row of its rows. The frame's rows go down from the top screen's first one, as the screenshot's do. A screen drawn
    // with filtered 2D at the filter map's scale (halfScaleAt1x) is scaled down to 1x on the way, nearest
    if (!frameReadBuffer)
        glGenFramebuffers(1, &frameReadBuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, frameReadBuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderFrame->frameTexture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    int halfScale = std::max(renderFrame->halfScaleAt1x, 1);
    int left = renderFrame->nativeLeft() * halfScale;
    // With the WideMelon camera's margin the top screen's half is taller, and its native rows start Frame::nativeTop rows into its image
    int margin = (int) renderFrame->topScreenMargin;
    int nativeTop = renderFrame->nativeTop();
    for (int screen = 0; screen < 2; screen++)
    {
        if (!(renderFrame->halvesAt1x & (1 << screen)))
            continue;

        int sourceRow = screen == 0 ? nativeTop * halfScale : (192 + 2 * margin + 2) * renderFrame->scaleAt1x;
        glBlitFramebuffer(left, sourceRow, left + 256 * halfScale, sourceRow + 192 * halfScale, 0, screen * 192, 256, screen * 192 + 192, GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
    }

    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

void ScreenshotRenderer::renderOverlay(GLuint overlayTexture)
{
    if (!overlayRenderShader)
        return;

    // Premultiplied over the frame, keeping the frame's alpha; the blend state is put back afterwards except GL_BLEND itself, which
    // renderScreenshot() leaves off anyway
    GLint blendSourceRgb, blendDestinationRgb, blendSourceAlpha, blendDestinationAlpha, blendEquationRgb, blendEquationAlpha;
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSourceRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDestinationRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSourceAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDestinationAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);

    // The shader uses texelFetch(), so the texture's filter is left alone while the render thread may be presenting this frame, and it reuses the
    // frame's quads (which cover the whole screenshot) for its position attribute
    glUseProgram(overlayRenderShader);
    glBindTexture(GL_TEXTURE_2D, overlayTexture);
    glUniform1i(overlayTextureUniformLocation, 0);
    glDrawArrays(GL_TRIANGLES, 0, 12);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBlendEquationSeparate(blendEquationRgb, blendEquationAlpha);
    glBlendFuncSeparate(blendSourceRgb, blendDestinationRgb, blendSourceAlpha, blendDestinationAlpha);
    glDisable(GL_BLEND);
}

void ScreenshotRenderer::blendOverlay(const LuaOverlay& overlay)
{
    const u32* overlayPixels = overlay.getPixels();
    if (!overlayPixels)
        return;

    for (int screen = LuaOverlay::Top; screen <= LuaOverlay::Bottom; screen++)
    {
        int rowStart = overlay.getRowStart((LuaOverlay::Screen) screen);
        int rowEnd = overlay.getRowEnd((LuaOverlay::Screen) screen);
        if (rowStart >= rowEnd)
            continue;

        // Each screenshot pixel is the average of the 2x2 overlay pixels it covers, like renderOverlay() does
        int screenRow = screen == LuaOverlay::Bottom ? LuaOverlay::kBottomScreenRow : 0;
        int firstRow = (rowStart - screenRow) / LuaOverlay::kScale;
        int lastRow = (rowEnd - screenRow + LuaOverlay::kScale - 1) / LuaOverlay::kScale;
        for (int y = firstRow; y < lastRow; y++)
        {
            const u32* overlayRow = &overlayPixels[(size_t) (screenRow + y * LuaOverlay::kScale) * LuaOverlay::kWidth];
            const u32* nextOverlayRow = overlayRow + LuaOverlay::kWidth;
            u32* screenshotRow = &screenshotBuffer[(screen * 192 + y) * 256];
            for (int x = 0; x < 256; x++)
            {
                u32 a = overlayRow[x * 2], b = overlayRow[x * 2 + 1], c = nextOverlayRow[x * 2], d = nextOverlayRow[x * 2 + 1];
                if ((a | b | c | d) == 0)
                    continue;

                // Both are 0xAARRGGBB with the overlay premultiplied, and the screenshot keeps its alpha
                u32 sourceAlpha = ((a >> 24) + (b >> 24) + (c >> 24) + (d >> 24) + 2) / 4;
                u32 destination = screenshotRow[x];
                u32 result = destination & 0xFF000000;
                for (int shift = 0; shift < 24; shift += 8)
                {
                    u32 source = (((a >> shift) & 0xFF) + ((b >> shift) & 0xFF) + ((c >> shift) & 0xFF) + ((d >> shift) & 0xFF) + 2) / 4;
                    u32 channel = source + (((destination >> shift) & 0xFF) * (255 - sourceAlpha) + 127) / 255;
                    result |= std::min<u32>(channel, 255) << shift;
                }
                screenshotRow[x] = result;
            }
        }
    }
}

u32* ScreenshotRenderer::getScreenshot()
{
    return screenshotBuffer;
}

bool ScreenshotRenderer::takeScreenshot()
{
    std::unique_lock lock(screenshotMutex);

    screenshotRequested = true;
    screenshotCondition.wait(lock);

    return !stopped;
}

bool ScreenshotRenderer::isScreenshotPending()
{
    std::lock_guard lock(screenshotMutex);
    return screenshotRequested;
}

void ScreenshotRenderer::setupFrameBuffer()
{
    glGenFramebuffers(1, &frameBuffer);
    glGenTextures(1, &bufferTexture);

    glBindTexture(GL_TEXTURE_2D, bufferTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCREENSHOT_WIDTH, SCREENSHOT_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bufferTexture, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Read into once here, since the driver's first readback into it takes 5-10 ms, which would otherwise land on the first rewind capture
    glGenBuffers(1, &pixelPackBuffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pixelPackBuffer);
    glBufferData(GL_PIXEL_PACK_BUFFER, SCREENSHOT_WIDTH * SCREENSHOT_HEIGHT * 4, nullptr, GL_STREAM_READ);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, frameBuffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, SCREENSHOT_WIDTH, SCREENSHOT_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

bool ScreenshotRenderer::setupShaders()
{
    const char *vertexShaderSource =
            "#version 100\n"
            "attribute vec2 aTexCoord;\n"
            "attribute vec2 aPosition;\n"
            "varying vec2 vTexCoord;\n"
            "void main() {\n"
            "    gl_Position = vec4(aPosition, 0.0, 1.0);\n"
            "    vTexCoord = aTexCoord;\n"
            "}\n";
    const char *fragmentShaderSource =
            "#version 100\n"
            "precision mediump float;\n"
            "precision mediump sampler2D;\n"
            "varying vec2 vTexCoord;\n"
            "uniform sampler2D sTexture;\n"
            "void main() {\n"
            "    gl_FragColor = texture2D(sTexture, vTexCoord);\n"
            "}\n";

    GLint shaderResult;
    screenshotRenderVertexShader = glCreateShader(GL_VERTEX_SHADER);
    GLint len = strlen(vertexShaderSource);
    glShaderSource(screenshotRenderVertexShader, 1, &vertexShaderSource, &len);
    glCompileShader(screenshotRenderVertexShader);
    glGetShaderiv(screenshotRenderVertexShader, GL_COMPILE_STATUS, &shaderResult);
    if (shaderResult != GL_TRUE)
    {
        glGetShaderiv(screenshotRenderVertexShader, GL_INFO_LOG_LENGTH, &shaderResult);
        if (shaderResult < 1) shaderResult = 1024;
        char* log = new char[shaderResult + 1];
        glGetShaderInfoLog(screenshotRenderVertexShader, shaderResult + 1, NULL, log);
        LOG_ERROR(LOG_TAG_GL, "Failed to compile vertex shader: %s", log);
        delete[] log;

        glDeleteShader(screenshotRenderVertexShader);

        return false;
    }

    screenshotRenderFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    len = strlen(fragmentShaderSource);
    glShaderSource(screenshotRenderFragmentShader, 1, &fragmentShaderSource, &len);
    glCompileShader(screenshotRenderFragmentShader);
    glGetShaderiv(screenshotRenderFragmentShader, GL_COMPILE_STATUS, &shaderResult);
    if (shaderResult != GL_TRUE)
    {
        glGetShaderiv(screenshotRenderFragmentShader, GL_INFO_LOG_LENGTH, &shaderResult);
        if (shaderResult < 1) shaderResult = 1024;
        char* log = new char[shaderResult + 1];
        glGetShaderInfoLog(screenshotRenderFragmentShader, shaderResult + 1, NULL, log);
        LOG_ERROR(LOG_TAG_GL, "Failed to compile fragment shader: %s", log);
        delete[] log;

        glDeleteShader(screenshotRenderVertexShader);
        glDeleteShader(screenshotRenderFragmentShader);

        return false;
    }

    screenshotRenderShader = glCreateProgram();
    glAttachShader(screenshotRenderShader, screenshotRenderVertexShader);
    glAttachShader(screenshotRenderShader, screenshotRenderFragmentShader);
    glLinkProgram(screenshotRenderShader);
    glGetProgramiv(screenshotRenderShader, GL_LINK_STATUS, &shaderResult);
    if (shaderResult != GL_TRUE)
    {
        glGetProgramiv(screenshotRenderShader, GL_INFO_LOG_LENGTH, &shaderResult);
        if (shaderResult < 1) shaderResult = 1024;
        char* log = new char[shaderResult + 1];
        glGetProgramInfoLog(screenshotRenderShader, shaderResult + 1, NULL, log);
        LOG_ERROR(LOG_TAG_GL, "Failed to link shader program: %s", log);
        delete[] log;

        glDeleteShader(screenshotRenderVertexShader);
        glDeleteShader(screenshotRenderFragmentShader);
        glDeleteProgram(screenshotRenderShader);

        return false;
    }

    posAttribLocation = glGetAttribLocation(screenshotRenderShader, "aPosition");
    texCoordAttribLocation = glGetAttribLocation(screenshotRenderShader, "aTexCoord");
    textureUniformLocation = glGetUniformLocation(screenshotRenderShader, "sTexture");
    return true;
}

static GLuint compileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint result;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &result);
    if (result != GL_TRUE)
    {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LOG_ERROR(LOG_TAG_GL, "Failed to compile screenshot overlay shader: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

void ScreenshotRenderer::setupOverlayShader()
{
    // GLSL ES 3.00 for texelFetch(); gl_FragCoord rows go up from the top screen's first row (the image is read back upside down), the bottom screen
    // starts 4 rows further down the overlay than twice its row (the gap between the screens), and the overlay keeps the frame texture's byte order,
    // so unlike the app's overlay shader there's no swizzle
    const char* vertexShaderSource =
            "#version 300 es\n"
            "in vec2 aPosition;\n"
            "void main() {\n"
            "    gl_Position = vec4(aPosition, 0.0, 1.0);\n"
            "}\n";
    const char* fragmentShaderSource =
            "#version 300 es\n"
            "precision highp float;\n"
            "precision highp int;\n"
            "uniform sampler2D sOverlay;\n"
            "out vec4 fragColor;\n"
            "void main() {\n"
            "    ivec2 pixel = ivec2(gl_FragCoord.xy);\n"
            "    ivec2 texel = pixel * 2;\n"
            "    if (pixel.y >= 192) texel.y += 4;\n"
            "    vec4 sum = texelFetch(sOverlay, texel, 0) + texelFetch(sOverlay, texel + ivec2(1, 0), 0)\n"
            "        + texelFetch(sOverlay, texel + ivec2(0, 1), 0) + texelFetch(sOverlay, texel + ivec2(1, 1), 0);\n"
            "    fragColor = sum * 0.25;\n"
            "}\n";

    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    if (vertexShader && fragmentShader)
    {
        GLuint program = glCreateProgram();
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        // The screenshot shader's position attribute, so the vertex array needs no changes
        glBindAttribLocation(program, posAttribLocation, "aPosition");
        glLinkProgram(program);

        GLint result;
        glGetProgramiv(program, GL_LINK_STATUS, &result);
        if (result == GL_TRUE)
        {
            overlayRenderShader = program;
            overlayTextureUniformLocation = glGetUniformLocation(program, "sOverlay");
        }
        else
        {
            char log[1024];
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            LOG_ERROR(LOG_TAG_GL, "Failed to link screenshot overlay shader: %s", log);
            glDeleteProgram(program);
        }
    }

    // The program keeps what it needs
    if (vertexShader)
        glDeleteShader(vertexShader);
    if (fragmentShader)
        glDeleteShader(fragmentShader);
}

void ScreenshotRenderer::setupVertexBuffers()
{
    glGenBuffers(1, &vbo);
    glGenVertexArrays(1, &vao);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    fillVertexBuffer(256, 0, 0, 0);
}

void ScreenshotRenderer::fillVertexBuffer(int topScreenWidth, int topScreenMargin, int nativeLeft, int nativeTop)
{
    float margin = 1.0f / 192.0f;
    // Both screens take the native 256 columns of the frame: all of a 256 wide top screen, or nativeLeft columns into a wider one, which is
    // also where the bottom screen is. The right edge as the left one's distance from it, so a centred screen gets 1 - left as before
    float left = nativeLeft / (float) topScreenWidth;
    float right = 1.0f - (topScreenWidth - nativeLeft - 256) / (float) topScreenWidth;
    // The rows of each screen: the old 386 row layout's without the WideMelon camera's margin (kept exactly), otherwise the top screen's native
    // 192 rows nativeTop rows into its taller half (the margin rows are left out), and the bottom screen's after the gap
    float topScreenTopV = 0.0f;
    float topScreenBottomV = 0.5f - margin;
    float bottomScreenTopV = 0.5f + margin;
    if (topScreenMargin > 0)
    {
        float frameRows = (float) (192 + 2 * topScreenMargin + 2 + 192);
        topScreenTopV = nativeTop / frameRows;
        topScreenBottomV = (nativeTop + 192) / frameRows;
        bottomScreenTopV = (192 + 2 * topScreenMargin + 2) / frameRows;
    }
    // Image is vertically flipped
    const float vertices[] = {
        //Position        // UV
        -1.0f,  -1.0f,    left, topScreenTopV,
        -1.0f,  0.0f,     left, topScreenBottomV,
        1.0f,  0.0f,      right, topScreenBottomV,

        -1.0f,  -1.0f,    left, topScreenTopV,
        1.0f,  0.0f,      right, topScreenBottomV,
        1.0f, -1.0f,      right, topScreenTopV,

        -1.0f, 0.0f,      left, bottomScreenTopV,
        -1.0f, 1.0f,      left, 1.0f,
        1.0f, 1.0f,       right, 1.0f,

        -1.0f, 0.0f,      left, bottomScreenTopV,
        1.0f, 1.0f,       right, 1.0f,
        1.0f, 0.0f,       right, bottomScreenTopV,
    };

    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    vertexBufferTopScreenWidth = topScreenWidth;
    vertexBufferTopScreenMargin = topScreenMargin;
    vertexBufferNativeLeft = nativeLeft;
    vertexBufferNativeTop = nativeTop;
}

void ScreenshotRenderer::notifyScreenshotReady()
{
    std::lock_guard lock(screenshotMutex);
    screenshotRequested = false;
    screenshotCondition.notify_all();
}

void ScreenshotRenderer::cleanup()
{
    {
        std::lock_guard lock(screenshotMutex);
        stopped = true;
        // Prevent and screenshot listeners from being stuck
        screenshotCondition.notify_all();
    }
    glDeleteShader(screenshotRenderVertexShader);
    glDeleteShader(screenshotRenderFragmentShader);
    glDeleteProgram(screenshotRenderShader);
    if (overlayRenderShader)
    {
        glDeleteProgram(overlayRenderShader);
        overlayRenderShader = 0;
    }
    // A readback still pending is dropped
    if (readbackFence)
    {
        glDeleteSync(readbackFence);
        readbackFence = nullptr;
    }
    glDeleteBuffers(1, &pixelPackBuffer);
    glDeleteTextures(1, &bufferTexture);
    glDeleteFramebuffers(1, &frameBuffer);
    if (frameReadBuffer)
    {
        glDeleteFramebuffers(1, &frameReadBuffer);
        frameReadBuffer = 0;
    }
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
}

}