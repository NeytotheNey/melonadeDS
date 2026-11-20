#ifndef GPUFENCE_H
#define GPUFENCE_H

/**
 * A GPU fence between the emulator's and the presenter's GL contexts (Frame::renderFence and presentFence): an EGL fence sync on Android, a GL
 * sync object on the desktop, whose two contexts are in one share group (SDL_GL_SHARE_WITH_CURRENT_CONTEXT), so its fences work across them.
 * [display] is the EGL display the fence belongs to on Android and unused on the desktop.
 */
#ifdef __ANDROID__

#include <EGL/egl.h>
#include <EGL/eglext.h>

typedef EGLSyncKHR GpuFence;
typedef EGLDisplay GpuFenceDisplay;
#define GPU_FENCE_NO_DISPLAY EGL_NO_DISPLAY

inline GpuFenceDisplay getCurrentGpuFenceDisplay() { return eglGetCurrentDisplay(); }
inline GpuFence createGpuFence(GpuFenceDisplay display) { return eglCreateSyncKHR(display, EGL_SYNC_FENCE_KHR, nullptr); }
inline void destroyGpuFence(GpuFenceDisplay display, GpuFence fence) { eglDestroySyncKHR(display, fence); }
// Makes the current context's GPU wait for the fence, without blocking the calling thread
inline void waitGpuFence(GpuFenceDisplay display, GpuFence fence) { eglWaitSyncKHR(display, fence, 0); }
// Only asks, without waiting
inline bool isGpuFenceSignaled(GpuFenceDisplay display, GpuFence fence) { return eglClientWaitSyncKHR(display, fence, 0, 0) == EGL_CONDITION_SATISFIED_KHR; }

#else

#include <GLES3/gl3.h>

typedef GLsync GpuFence;
typedef void* GpuFenceDisplay;
#define GPU_FENCE_NO_DISPLAY nullptr

inline GpuFenceDisplay getCurrentGpuFenceDisplay() { return nullptr; }
inline GpuFence createGpuFence(GpuFenceDisplay) { return glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); }
inline void destroyGpuFence(GpuFenceDisplay, GpuFence fence) { glDeleteSync(fence); }
inline void waitGpuFence(GpuFenceDisplay, GpuFence fence) { glWaitSync(fence, 0, GL_TIMEOUT_IGNORED); }
inline bool isGpuFenceSignaled(GpuFenceDisplay, GpuFence fence)
{
    GLenum result = glClientWaitSync(fence, 0, 0);
    return result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED;
}

#endif

#endif //GPUFENCE_H
