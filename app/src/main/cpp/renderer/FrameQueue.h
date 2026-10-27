#ifndef FRAMEQUEUE_H
#define FRAMEQUEUE_H

#include <array>
#include <chrono>
#include <cstdint>
#include <queue>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include "OpenGLContext.h"
#include "types.h"

using namespace melonDS;

// 9 frames should allow the emulator to run up 8x speed. This includes 8 frames ready to present, plus one frame currently being rendered to.
constexpr std::size_t FRAME_QUEUE_SIZE = 9;

struct Frame {
    GLuint frameTexture{};
    u32 width{};
    u32 height{};
    EGLSyncKHR renderFence{};
    EGLSyncKHR presentFence{};
    // Increases every time a frame is pushed for presentation. Allows the presenter to tell new frames from repeated ones and detect skipped frames
    u64 sequence{};
    // Vsync (CLOCK_MONOTONIC, ns) on which the frame is meant to be presented when frame pacing is locked to the display, or 0 to present it as
    // soon as possible. Only accessed while holding the queue lock
    int64_t presentDueTimeNs{};
};

class FrameQueue
{
public:
    FrameQueue();
    Frame* getRenderFrame();
    /**
     * Returns the frame to present on the vsync at [vsyncTimeNs]: the newest frame that is due by then (with [dueToleranceNs] of tolerance), or
     * the previous frame again if there is none. If [deadline] is set, waits until then for a due frame to be pushed, but only if the next frame
     * to be pushed can be due on this vsync: nothing is queued and [upcomingFrameDueNs] (the due time of the frame being emulated, 0 if it isn't
     * paced) is due. Frames older than the returned one are released, while frames that are not due yet are kept for later vsyncs.
     */
    Frame* getPresentFrame(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline, int64_t vsyncTimeNs, int64_t dueToleranceNs, int64_t upcomingFrameDueNs);
    void validateRenderFrame(Frame* frame, int requiredWidth, int requiredHeight);
    void pushRenderedFrame(Frame* frame, int64_t presentDueTimeNs);
    void discardRenderedFrame(Frame* frame);
    void clear();

private:
    static bool isDue(int64_t presentDueTimeNs, int64_t vsyncTimeNs, int64_t dueToleranceNs);
    int findDueFrameIndex(int64_t vsyncTimeNs, int64_t dueToleranceNs);

    std::mutex frameLock;
    std::condition_variable presentFrameReadyCondition;
    std::array<Frame, FRAME_QUEUE_SIZE> frames{};
    std::queue<Frame*> freeQueue{};
    // Ordered from newest (front) to oldest (back)
    std::deque<Frame*> presentQueue{};
    Frame* previousFrame = nullptr;
    u64 lastFrameSequence = 0;
};

#endif //FRAMEQUEUE_H
