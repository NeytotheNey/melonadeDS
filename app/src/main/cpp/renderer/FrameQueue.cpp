#include "FrameQueue.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <Platform.h>
#ifdef MELONADE_DEBUG_TOOLS
#include "debug/LogQueue.h"
#endif

// A frame that stayed free while this many others were rendered to (30 seconds at normal speed) has its textures deleted, so the ones that
// fast-forward or a slow stretch needed aren't kept for the rest of the session
constexpr u64 IDLE_FRAME_RELEASE_COUNT = 1800;

FrameQueue::FrameQueue()
{
    for (auto& frame : frames)
    {
        freeFrames.push_back(&frame);
    }
}

// Must be called with the queue lock held
void FrameQueue::addFreeFrame(Frame* frame)
{
    frame->freeSinceRenderCount = renderFrameCount;
    freeFrames.push_back(frame);
}

Frame* FrameQueue::getRenderFrame()
{
    std::unique_lock lock(frameLock);

    Frame* frame = takeFreeFrame();

    // With no free frame, reuse the oldest present frame, which only happens without pacing (in fast-forward, say); the frame being presented
    // (previousFrame) is never in the present queue
    if (!frame)
    {
        frame = presentQueue.back();
        presentQueue.pop_back();
    }

    renderFrameCount++;
    releaseIdleFrame();

    return frame;
}

// Frame textures are big (6 MiB each at 4x), so a free frame that already has one goes first, and of those the first in the array, so that the
// same few frames are used over and over and the others stay without a texture or lose it again (releaseIdleFrame): about 3 at normal speed (one
// being rendered to, one on screen, one in between), all of them in fast-forward. A frame whose last present the GPU hasn't finished is skipped,
// since the emulator thread waits for that before it renders to the frame (its presentFence) and a new texture costs less than the wait. Only when
// every free frame has a texture and none is finished does the one released longest ago go out, which is the order they all went out in before.
// Must be called with the queue lock held
Frame* FrameQueue::takeFreeFrame()
{
    auto chosen = freeFrames.end();
    auto unused = freeFrames.end();
    for (Frame& candidate : frames)
    {
        auto position = std::find(freeFrames.begin(), freeFrames.end(), &candidate);
        if (position == freeFrames.end())
            continue;

        if (!candidate.frameTexture)
        {
            if (unused == freeFrames.end())
                unused = position;
        }
        else if (isPresentFinished(candidate))
        {
            chosen = position;
            break;
        }
    }

    if (chosen == freeFrames.end())
        chosen = unused != freeFrames.end() ? unused : freeFrames.begin();
    if (chosen == freeFrames.end())
        return nullptr;

    Frame* frame = *chosen;
    freeFrames.erase(chosen);
    return frame;
}

// Only for free frames: the presenter replaces the fence of the frame it's presenting without the queue lock
bool FrameQueue::isPresentFinished(const Frame& frame) const
{
    // Only asks, without waiting. A fence that the presenter's context hasn't flushed yet (it's created after the swap) isn't signaled either
    return !frame.presentFence || isGpuFenceSignaled(display, frame.presentFence);
}

// At most one frame per call, as deleting a texture isn't free, and only one the GPU is done presenting. Must be called with the queue lock held,
// on the emulator thread
void FrameQueue::releaseIdleFrame()
{
    for (Frame* frame : freeFrames)
    {
        if (frame->frameTexture && renderFrameCount - frame->freeSinceRenderCount >= IDLE_FRAME_RELEASE_COUNT && isPresentFinished(*frame))
        {
#ifdef MELONADE_DEBUG_TOOLS
            u32 width = frame->width;
            u32 height = frame->height;
#endif
            releaseFrameResources(*frame);
#ifdef MELONADE_DEBUG_TOOLS
            logTextureCount("release", width, height);
#endif
            return;
        }
    }
}

Frame* FrameQueue::getPresentFrame(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline, int64_t vsyncTimeNs, int64_t dueToleranceNs, int64_t upcomingFrameDueNs)
{
    std::unique_lock lock(frameLock);

    int frameIndex = findDueFrameIndex(vsyncTimeNs, dueToleranceNs);
    if (frameIndex < 0) {
        // Only wait if the next pushed frame can still be due on this vsync, or the render thread would miss the next one
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
        addFreeFrame(previousFrame);
        previousFrame = nullptr;
    }

    // Release the frames older than the one being presented (newer ones aren't due yet, so they stay in the queue)
    while (presentQueue.size() > (std::size_t) frameIndex + 1)
    {
        addFreeFrame(presentQueue.back());
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

    // Frames without a due time (pushed while pacing isn't locked) are always due, which keeps the "newest frame wins" behavior for them
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

typedef void (GL_APIENTRY* ColorMaskiFn)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean);

// glColorMaski on GLES 3.2, the EXT or OES one on 3.0/3.1 with indexed draw buffers, null otherwise. Read once, with a context current: every
// context the app makes is a client version 3 one on the same display, so they all give the same answer
static ColorMaskiFn getColorMaski()
{
    static const ColorMaskiFn colorMaski = []() -> ColorMaskiFn
    {
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (major > 3 || (major == 3 && minor >= 2))
            return glColorMaski;
        // one name at a time with glGetStringi, compared whole (as GPU3D_OpenGL.cpp does for framebuffer fetch)
        GLint numExtensions = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
        for (GLint e = 0; e < numExtensions; e++)
        {
            const char* extension = (const char*) glGetStringi(GL_EXTENSIONS, (GLuint) e);
            const char* entryPoint = nullptr;
            if (extension && strcmp(extension, "GL_EXT_draw_buffers_indexed") == 0)
                entryPoint = "glColorMaskiEXT";
            else if (extension && strcmp(extension, "GL_OES_draw_buffers_indexed") == 0)
                entryPoint = "glColorMaskiOES";
            if (entryPoint)
            {
#ifdef __ANDROID__
                if (auto function = (ColorMaskiFn) eglGetProcAddress(entryPoint))
                    return function;
#else
                // The desktop's context is always GLES 3.2, which returned above
                (void) entryPoint;
#endif
            }
        }
        return nullptr;
    }();
    return colorMaski;
}

// Clears all of [texture] to 0 (transparent black) with a draw-framebuffer clear, on the emulator thread with its GL context current. That's
// the context the frame is then rendered in, after the present fence wait in MelonInstance::runFrame, so the clear comes after the last present's
// reads and before the frame's own draws and its render fence, which the presenter waits on before it reads the texture. The framebuffer is
// made and deleted here (framebuffers aren't shared between contexts and this only runs on a new size), and the draw framebuffer, scissor
// test and color mask are put back. False when the framebuffer isn't complete, so the caller uploads zeros instead
static bool clearTexture(GLuint texture)
{
    GLint previousFramebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
    GLuint framebuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    bool complete = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete)
    {
        GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        GLboolean colorMask[4] {};
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        if (scissor)
            glDisable(GL_SCISSOR_TEST);
        // draw buffer 0's mask only (GL_COLOR_WRITEMASK reads it): glColorMask would set every buffer's and put buffer 0's back on all of
        // them, overwriting the masks the core's passes left on the others. Without indexed masks (below GLES 3.2, neither extension) the app
        // only offers the software renderer, so no other buffer has a mask to keep
        ColorMaskiFn colorMaski = getColorMaski();
        if (colorMaski)
            colorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        else
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        const GLfloat zero[4] {};
        glClearBufferfv(GL_COLOR, 0, zero);
        if (colorMaski)
            colorMaski(0, colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        else
            glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        if (scissor)
            glEnable(GL_SCISSOR_TEST);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint) previousFramebuffer);
    glDeleteFramebuffers(1, &framebuffer);
    return complete;
}

void FrameQueue::validateRenderFrame(Frame* frame, int requiredWidth, int requiredHeight)
{
    if (frame->width != requiredWidth || frame->height != requiredHeight)
    {
        // Update frame texture to have the required size
        if (!frame->frameTexture)
        {
            display = getCurrentGpuFenceDisplay();
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
        // New storage starts out cleared, as it isn't guaranteed to be and frames get theirs at any point of a session. The padding lines between
        // the two screens are never drawn but filtering reads them at the screens' edges, and a frame can be pushed without the compositor having
        // drawn it (one that resumes from sleep past line 192, say), which would show whatever the driver handed back. Only on a new size, which
        // the output cap (GPU::OutputCap) makes on every fold and unfold, so it's cleared on the GPU (clearTexture) instead of uploading a
        // zeroed image of the whole frame (100 MB at 16x); the upload is only the fallback
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, requiredWidth, requiredHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        if (!clearTexture(frame->frameTexture))
        {
            glBindTexture(GL_TEXTURE_2D, frame->frameTexture);
            std::vector<u8> clearPixels((size_t) requiredWidth * requiredHeight * 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, requiredWidth, requiredHeight, GL_RGBA, GL_UNSIGNED_BYTE, clearPixels.data());
        }
        glBindTexture(GL_TEXTURE_2D, 0);

#ifdef MELONADE_DEBUG_TOOLS
        bool isResize = frame->width != 0;
#endif
        frame->width = requiredWidth;
        frame->height = requiredHeight;
        // The new storage is the whole (cleared) image
        frame->halvesAt1x = 0;
        frame->topWingLines = 0;
#ifdef MELONADE_DEBUG_TOOLS
        logTextureCount(isResize ? "resize" : "alloc", frame->width, frame->height);
#endif
    }
}

void FrameQueue::validateDepthTexture(Frame* frame, int rows)
{
    if (rows <= 0)
    {
        if (frame->depthTexture)
        {
            glDeleteTextures(1, &frame->depthTexture);
            frame->depthTexture = 0;
            frame->depthWidth = 0;
            frame->depthHeight = 0;
            frame->hasDepth = false;
        }
        return;
    }
    if (frame->depthTexture && frame->depthWidth == frame->width && frame->depthHeight == (u32) rows)
        return;

    if (!frame->depthTexture)
    {
        glGenTextures(1, &frame->depthTexture);
        glBindTexture(GL_TEXTURE_2D, frame->depthTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, frame->depthTexture);
    }
    // Not cleared: hasDepth stays false until the compositor has drawn all of it
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, (GLsizei) frame->width, rows, 0, GL_RG, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    frame->depthWidth = frame->width;
    frame->depthHeight = (u32) rows;
    frame->hasDepth = false;
}

#ifdef MELONADE_DEBUG_TOOLS
// How many frames have a texture, which is what the queue costs in graphics memory; only called when that or a texture's size changes
void FrameQueue::logTextureCount(const char* reason, u32 width, u32 height)
{
    int textures = 0;
    for (const Frame& frame : frames)
    {
        if (frame.frameTexture)
            textures++;
    }

    LOG_INFO_QUEUED(LOG_TAG_DEBUG, "FRAMEQ textures=%d/%d %ux%u reason=%s", textures, (int) FRAME_QUEUE_SIZE, width, height, reason);
}
#endif

void FrameQueue::updateOverlay(Frame* frame, const u32* pixels, int width, int height, const TextureRows rows[2])
{
    if (!frame->overlayTexture)
    {
        // A frame without the texture has no old drawings to clear either
        if (rows[0].isEmpty() && rows[1].isEmpty())
            return;

        // The whole overlay, which is empty outside the drawings, so the rest of the texture is cleared too
        glGenTextures(1, &frame->overlayTexture);
        glBindTexture(GL_TEXTURE_2D, frame->overlayTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glBindTexture(GL_TEXTURE_2D, 0);
        frame->overlayRows[0] = rows[0];
        frame->overlayRows[1] = rows[1];
        return;
    }

    bool isBound = false;
    for (int screen = 0; screen < 2; screen++)
    {
        // Old drawings outside the new ones are overwritten with the overlay's empty rows
        const TextureRows& oldRows = frame->overlayRows[screen];
        TextureRows uploadRows = rows[screen];
        if (uploadRows.isEmpty())
            uploadRows = oldRows;
        else if (!oldRows.isEmpty())
            uploadRows = { std::min(oldRows.start, uploadRows.start), std::max(oldRows.end, uploadRows.end) };

        if (!uploadRows.isEmpty())
        {
            if (!isBound)
            {
                glBindTexture(GL_TEXTURE_2D, frame->overlayTexture);
                isBound = true;
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, uploadRows.start, width, uploadRows.end - uploadRows.start, GL_RGBA, GL_UNSIGNED_BYTE, pixels + (size_t) uploadRows.start * width);
        }
        frame->overlayRows[screen] = rows[screen];
    }

    if (isBound)
        glBindTexture(GL_TEXTURE_2D, 0);
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
    addFreeFrame(frame);
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
        addFreeFrame(f);
    }

    presentQueue.clear();
    previousFrame = nullptr;

    // Must be called with the emulator's GL context current, or the textures are not deleted
#ifdef MELONADE_DEBUG_TOOLS
    u32 releasedWidth = 0;
    u32 releasedHeight = 0;
#endif
    for (auto& frame : frames) {
#ifdef MELONADE_DEBUG_TOOLS
        if (frame.frameTexture)
        {
            releasedWidth = frame.width;
            releasedHeight = frame.height;
        }
#endif
        releaseFrameResources(frame);
    }
#ifdef MELONADE_DEBUG_TOOLS
    // One line for all of them
    if (releasedWidth)
        logTextureCount("release", releasedWidth, releasedHeight);
#endif
}

// Leaves the frame as one that was never rendered to, so it gets new textures if it's used again
void FrameQueue::releaseFrameResources(Frame& frame)
{
    glDeleteTextures(1, &frame.frameTexture);
    if (frame.overlayTexture)
        glDeleteTextures(1, &frame.overlayTexture);
    if (frame.depthTexture)
        glDeleteTextures(1, &frame.depthTexture);
    frame.depthTexture = 0;
    frame.depthWidth = 0;
    frame.depthHeight = 0;
    frame.hasDepth = false;
    if (frame.renderFence)
        destroyGpuFence(display, frame.renderFence);
    if (frame.presentFence)
        destroyGpuFence(display, frame.presentFence);
    frame.frameTexture = 0;
    frame.overlayTexture = 0;
    frame.overlayRows[0] = {};
    frame.overlayRows[1] = {};
    frame.width = 0;
    frame.height = 0;
    frame.renderFence = 0;
    frame.presentFence = 0;
}
