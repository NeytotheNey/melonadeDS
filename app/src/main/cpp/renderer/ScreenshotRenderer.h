#ifndef SCREENSHOTRENDERER_H
#define SCREENSHOTRENDERER_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <GLES3/gl3.h>
#include "GPU.h"
#include "FrameQueue.h"
#include "types.h"
#include "renderer/Renderer.h"
#include "scripting/LuaDraw.h"

namespace MelonDSAndroid
{

class ScreenshotRenderer {
private:
    static const int SCREENSHOT_WIDTH = 256;
    static const int SCREENSHOT_HEIGHT = 192 * 2;

    u32* screenshotBuffer;
    GLuint frameBuffer;
    GLuint bufferTexture;
    GLuint vao;
    GLuint vbo;
    GLuint screenshotRenderVertexShader;
    GLuint screenshotRenderFragmentShader;
    GLuint screenshotRenderShader;
    GLuint textureUniformLocation;
    GLuint posAttribLocation;
    GLuint texCoordAttribLocation;
    // Draws the Lua overlay over the screenshot (see renderOverlay()), or 0 if it couldn't be built
    GLuint overlayRenderShader = 0;
    GLuint overlayTextureUniformLocation;
    // Frame::topScreenWidth that the vertex buffer's UVs crop for
    int vertexBufferTopScreenWidth = 256;
    // Where startScreenshotReadback() reads the image to, and the fence that signals once it's there (null while none is pending)
    GLuint pixelPackBuffer = 0;
    GLsync readbackFence = nullptr;
    bool screenshotRequested;
    bool stopped;

    void setupFrameBuffer();
    // False if the screenshot shader couldn't be built
    bool setupShaders();
    void setupVertexBuffers();
    // Fills the bound vertex buffer with the quads for a frame whose top screen is [topScreenWidth] wide (in DS pixels)
    void fillVertexBuffer(int topScreenWidth);
    void setupOverlayShader();
    // Renders the screenshot, reading the GL renderers' image into screenshotBuffer, or queueing it into pixelPackBuffer if [intoPixelBuffer]
    void drawScreenshot(GPU* gpu, Renderer renderer, Frame* renderFrame, const LuaOverlay* overlay, bool intoPixelBuffer);
    void renderOverlay(GLuint overlayTexture);
    void blendOverlay(const LuaOverlay& overlay);
    void notifyScreenshotReady();

    std::mutex screenshotMutex;
    std::condition_variable screenshotCondition;

public:
    ScreenshotRenderer(u32* screenshotBuffer);
    void init();
    /**
     * Renders [renderFrame] into the screenshot buffer, with the frame's Lua drawings from [overlay] (if not null) drawn over it, scaled down to the
     * DS's resolution
     */
    void renderScreenshot(GPU* gpu, Renderer renderer, Frame* renderFrame, const LuaOverlay* overlay);
    /**
     * Like renderScreenshot() but without waiting for the GPU or telling a pending takeScreenshot(), with the GL renderers' image collected later by
     * finishScreenshotReadback() (the software renderer's is done right away)
     */
    void startScreenshotReadback(GPU* gpu, Renderer renderer, Frame* renderFrame, const LuaOverlay* overlay);
    /**
     * Copies the image of the pending startScreenshotReadback() into [destination] (a screenshot's size, or null to drop it), and returns false only
     * if, without [wait], the GPU isn't done with it yet
     */
    bool finishScreenshotReadback(u8* destination, bool wait);
    bool isScreenshotReadbackPending() { return readbackFence != nullptr; }
    u32* getScreenshot();

    bool takeScreenshot();
    bool isScreenshotPending();

    void cleanup();
};

}

#endif //SCREENSHOTRENDERER_H
