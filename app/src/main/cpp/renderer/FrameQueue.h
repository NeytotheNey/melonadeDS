#ifndef FRAMEQUEUE_H
#define FRAMEQUEUE_H

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <queue>
#include <GLES3/gl3.h>
#ifdef __ANDROID__
#include "OpenGLContext.h"
#endif
#include "GpuFence.h"
#include "types.h"

using namespace melonDS;

// 9 frames should allow the emulator to run up 8x speed. This includes 8 frames ready to present, plus one frame currently being rendered to. A
// frame's textures are only created once it's first rendered to, and normal speed only needs about 3 of them (see FrameQueue::takeFreeFrame)
constexpr std::size_t FRAME_QUEUE_SIZE = 9;

// Rows [start, end) of a texture, empty when start >= end
struct TextureRows {
    int start{};
    int end{};

    bool isEmpty() const { return start >= end; }
};

struct Frame {
    GLuint frameTexture{};
    u32 width{};
    u32 height{};
    // Width of the top screen in DS pixels: 256, or wider when WideMelon widens the 3D view (GL renderers only), with the 256 wide bottom screen
    // centered in the frame between black bars
    u32 topScreenWidth = 256;
    // The WideMelon camera's margin (stage 2): rows of 3D above and below the top screen's 192, in DS pixels. The top screen's half of the
    // texture is then 192 + 2 * topScreenMargin rows tall, with its DS line y at row y + topScreenMargin, followed by the 2 row gap and the
    // bottom screen's 192 rows; 0 is the old 386 row layout. Software is always 0
    u32 topScreenMargin = 0;
    // Lua drawings for this frame (scripting/LuaDraw.h) in the frame texture's layout at 2x and premultiplied, created once a frame has drawings and
    // kept when the frame is reused
    GLuint overlayTexture{};
    // Rows of the overlay texture with drawings, for the top and the bottom screen (no rows means no drawings on that screen this frame)
    TextureRows overlayRows[2]{};
    // GPU::Comp1x (melonDS::OutputLayout): bit 0 (top screen) and bit 1 (bottom screen) are screens the compositor drew at 1x into the first
    // topScreenWidth x (their half's height) texels of their own rows (from row 0, or (192 + 2 * topScreenMargin + 2) * scaleAt1x), which are shown nearest-upscaled from there, while the
    // rest of their rows hold older frames. Set only when the compositor draws into this frame's texture, so a frame pushed without a draw keeps
    // the layout of what the texture still holds; emulator thread only until the frame is pushed. halfScaleAt1x is the scale those screens were
    // drawn at (OutputLayout::HalfScale): 1, or with filtered 2D the filter map's, so the image is topScreenWidth x 192 at that scale and is
    // upscaled by scaleAt1x / halfScaleAt1x
    int halvesAt1x = 0;
    int scaleAt1x = 1;
    int halfScaleAt1x = 1;
    // The WideMelon camera's per-frame data (melonDS::OutputLayout), set with halvesAt1x: the top screen's lines with 3D wings, held ones included
    int topWingLines = 0;
    GpuFence renderFence{};
    GpuFence presentFence{};
    // Increases every time a frame is pushed for presentation, so the presenter can tell new frames from repeated ones and detect skipped frames
    u64 sequence{};
    // Vsync (CLOCK_MONOTONIC, ns) the frame is meant to be presented on when frame pacing is locked to the display, or 0 to present it as soon as
    // possible; only accessed while holding the queue lock
    int64_t presentDueTimeNs{};
    // The queue's count of frames handed out for rendering as of when this one last became free, which tells how long its textures have gone
    // unused; only accessed while holding the queue lock
    u64 freeSinceRenderCount{};
};

class FrameQueue
{
public:
    FrameQueue();
    /**
     * Returns the frame to render to next. Emulator thread only, with its GL context current, since this is also where the textures of frames
     * that have gone unused for a long time are deleted.
     */
    Frame* getRenderFrame();
    /**
     * Returns the newest frame due on the vsync at [vsyncTimeNs] (within [dueToleranceNs]) or the previous one again, waiting until [deadline] only
     * if nothing is queued and [upcomingFrameDueNs] (the frame being emulated, 0 if unpaced) is due; older frames are released and ones not due yet
     * are kept.
     */
    Frame* getPresentFrame(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline, int64_t vsyncTimeNs, int64_t dueToleranceNs, int64_t upcomingFrameDueNs);
    void validateRenderFrame(Frame* frame, int requiredWidth, int requiredHeight);
    /**
     * Uploads this frame's Lua drawings into its overlay texture, only the rows that have or had drawings ([pixels] is the whole [width] x [height]
     * overlay, null if nothing was ever drawn); emulator thread, before the frame's render fence
     */
    void updateOverlay(Frame* frame, const u32* pixels, int width, int height, const TextureRows rows[2]);
    void pushRenderedFrame(Frame* frame, int64_t presentDueTimeNs);
    void discardRenderedFrame(Frame* frame);
    /**
     * Wakes a presenter waiting in [getPresentFrame] and stops it waiting again, before the queue is destroyed, so it doesn't keep the destroying
     * thread blocked.
     */
    void abortPresentWait();
    void clear();

private:
    static bool isDue(int64_t presentDueTimeNs, int64_t vsyncTimeNs, int64_t dueToleranceNs);
    int findDueFrameIndex(int64_t vsyncTimeNs, int64_t dueToleranceNs);
    void addFreeFrame(Frame* frame);
    Frame* takeFreeFrame();
    bool isPresentFinished(const Frame& frame) const;
    void releaseIdleFrame();
    void releaseFrameResources(Frame& frame);
#ifdef MELONADE_DEBUG_TOOLS
    void logTextureCount(const char* reason, u32 width, u32 height);
#endif

    std::mutex frameLock;
    std::condition_variable presentFrameReadyCondition;
    std::array<Frame, FRAME_QUEUE_SIZE> frames{};
    // Ordered from the frame released longest ago (front) to the one released last (back)
    std::deque<Frame*> freeFrames{};
    // Ordered from newest (front) to oldest (back)
    std::deque<Frame*> presentQueue{};
    Frame* previousFrame = nullptr;
    u64 lastFrameSequence = 0;
    // Frames handed out for rendering so far
    u64 renderFrameCount = 0;
    bool isPresentWaitAborted = false;
    // Display the frame textures and fences were created on, kept so clear() can destroy the fences on any thread
    GpuFenceDisplay display = GPU_FENCE_NO_DISPLAY;
};

#endif //FRAMEQUEUE_H
