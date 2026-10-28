#include "FrameQueue.h"
#include <Platform.h>

FrameQueue::FrameQueue()
{
    for (auto& frame : frames)
    {
        freeQueue.push(&frame);
    }
}

Frame* FrameQueue::getRenderFrame()
{
    std::unique_lock lock(frameLock);

    Frame* frame;

    // If there are no free frames, use oldest present frame. While pacing is locked only a couple of frames wait for their vsync, so this only
    // happens without pacing (for example in fast-forward). The frame being presented (previousFrame) is never in the present queue
    if (freeQueue.empty())
    {
        frame = presentQueue.back();
        presentQueue.pop_back();
    }
    else
    {
        frame = freeQueue.front();
        freeQueue.pop();
    }

    return frame;
}

Frame* FrameQueue::getPresentFrame(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline, int64_t vsyncTimeNs, int64_t dueToleranceNs, int64_t upcomingFrameDueNs)
{
    std::unique_lock lock(frameLock);

    int frameIndex = findDueFrameIndex(vsyncTimeNs, dueToleranceNs);
    if (frameIndex < 0) {
        // Only wait if the frame that will be pushed next can be due on this vsync. If a queued frame isn't due yet, any frame pushed after it
        // is due even later. The same goes for the frame being emulated, if its due time is after this vsync. Waiting until the deadline in
        // those cases would make the render thread miss the next vsync
        bool canNextFrameBeDue = presentQueue.empty() && isDue(upcomingFrameDueNs, vsyncTimeNs, dueToleranceNs);

        bool hasNewFrame = false;
        if (deadline.has_value() && canNextFrameBeDue && !isPresentWaitAborted)
        {
            presentFrameReadyCondition.wait_until(lock, *deadline, [&] {
                frameIndex = findDueFrameIndex(vsyncTimeNs, dueToleranceNs);
                return frameIndex >= 0 || isPresentWaitAborted;
            });
            hasNewFrame = frameIndex >= 0;
        }

        if (!hasNewFrame)
            return previousFrame;
    }

    if (previousFrame)
    {
        freeQueue.push(previousFrame);
        previousFrame = nullptr;
    }

    // Release the frames older than the one being presented. Newer frames are not due yet, so they stay in the queue
    while (presentQueue.size() > (std::size_t) frameIndex + 1)
    {
        freeQueue.push(presentQueue.back());
        presentQueue.pop_back();
    }

    Frame* frame = presentQueue.back();
    presentQueue.pop_back();
    previousFrame = frame;
    return frame;
}

bool FrameQueue::isDue(int64_t presentDueTimeNs, int64_t vsyncTimeNs, int64_t dueToleranceNs)
{
    // Frames due this much later than the vsync are presented anyway, so that a bad due time can never keep a frame from being shown
    constexpr int64_t MAX_PRESENT_HOLD_NS = 100000000;

    // Frames without a due time (pushed while pacing isn't locked) are always due, which keeps the "newest frame wins" behaviour for them
    return presentDueTimeNs == 0 || vsyncTimeNs <= 0 || presentDueTimeNs <= vsyncTimeNs + dueToleranceNs || presentDueTimeNs > vsyncTimeNs + MAX_PRESENT_HOLD_NS;
}

int FrameQueue::findDueFrameIndex(int64_t vsyncTimeNs, int64_t dueToleranceNs)
{
    // The queue goes from newest to oldest, so the first due frame is the newest one
    for (std::size_t i = 0; i < presentQueue.size(); i++)
    {
        if (isDue(presentQueue[i]->presentDueTimeNs, vsyncTimeNs, dueToleranceNs))
            return (int) i;
    }

    return -1;
}

void FrameQueue::validateRenderFrame(Frame* frame, int requiredWidth, int requiredHeight)
{
    if (frame->width != requiredWidth || frame->height != requiredHeight)
    {
        // Update frame texture to have the required size
        if (!frame->frameTexture)
        {
            display = eglGetCurrentDisplay();
            glGenTextures(1, &frame->frameTexture);
            glBindTexture(GL_TEXTURE_2D, frame->frameTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, frame->frameTexture);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, requiredWidth, requiredHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        frame->width = requiredWidth;
        frame->height = requiredHeight;
    }
}

void FrameQueue::pushRenderedFrame(Frame* frame, int64_t presentDueTimeNs)
{
    std::unique_lock lock(frameLock);
    frame->sequence = ++lastFrameSequence;
    frame->presentDueTimeNs = presentDueTimeNs;
    presentQueue.push_front(frame);
    presentFrameReadyCondition.notify_one();
}

void FrameQueue::discardRenderedFrame(Frame* frame)
{
    std::unique_lock lock(frameLock);
    freeQueue.push(frame);
}

void FrameQueue::abortPresentWait()
{
    std::unique_lock lock(frameLock);
    isPresentWaitAborted = true;
    presentFrameReadyCondition.notify_all();
}

void FrameQueue::clear()
{
    std::unique_lock lock(frameLock);

    for (auto f : presentQueue)
    {
        freeQueue.push(f);
    }

    presentQueue.clear();
    previousFrame = nullptr;

    // Must be called with the emulator's GL context current, or the textures are not deleted
    for (auto& frame : frames) {
        glDeleteTextures(1, &frame.frameTexture);
        if (frame.renderFence)
            eglDestroySyncKHR(display, frame.renderFence);
        if (frame.presentFence)
            eglDestroySyncKHR(display, frame.presentFence);
        frame.frameTexture = 0;
        frame.width = 0;
        frame.height = 0;
        frame.renderFence = 0;
        frame.presentFence = 0;
    }
}
