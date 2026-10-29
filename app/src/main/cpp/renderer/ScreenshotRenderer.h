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
    // Draws the Lua overlay over the screenshot (see renderOverlay()). 0 if it couldn't be built
    GLuint overlayRenderShader = 0;
    GLuint overlayTextureUniformLocation;
    bool screenshotRequested;
    bool stopped;

    void setupFrameBuffer();
    // False if the screenshot shader couldn't be built
    bool setupShaders();
    void setupVertexBuffers();
    void setupOverlayShader();
    void renderOverlay(GLuint overlayTexture);
    void blendOverlay(const LuaOverlay& overlay);
    void notifyScreenshotReady();

    std::mutex screenshotMutex;
    std::condition_variable screenshotCondition;

public:
    ScreenshotRenderer(u32* screenshotBuffer);
    void init();
    /**
     * Renders [renderFrame] into the screenshot buffer. If [overlay] isn't null, the frame's Lua drawings (which [overlay] holds, and the frame's
     * overlay texture too) are drawn over it, scaled down to the DS's resolution
     */
    void renderScreenshot(GPU* gpu, Renderer renderer, Frame* renderFrame, const LuaOverlay* overlay);
    u32* getScreenshot();

    bool takeScreenshot();
    bool isScreenshotPending();

    void cleanup();
};

}

#endif //SCREENSHOTRENDERER_H
