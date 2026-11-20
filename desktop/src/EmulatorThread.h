// The desktop's emulator thread, a port of emulate() in the app's MelonDSAndroidJNI.cpp without JNI: it makes its own GL context current
// (shared with the UI thread's through SDL, so the frames' textures and fences are seen by both), runs a MelonInstance frame after frame
// (which pushes each frame into its FrameQueue for the UI thread), parks while paused, and paces itself to the DS's refresh rate unless
// it's fast forwarding or unthrottled. It owns the instance, which is destroyed on this thread with the context current, as the app does.
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <SDL.h>

#include "types.h"

namespace MelonDSAndroid { class MelonInstance; }

namespace Frontend
{

class EmulatorThread
{
public:
    using Instance = MelonDSAndroid::MelonInstance;

    // All called on the emulator thread, with its GL context current
    struct Callbacks
    {
        // after MelonInstance::start(), before the first frame; false stops the thread there
        std::function<bool(Instance&)> OnStart;
        // before the frame numbered [frame] (0 is the first one after the start); false stops before it
        std::function<bool(Instance&, melonDS::u32 frame)> BeforeFrame;
        // after it, with its scanlines and its work time (MelonInstance::runFrame, plus a glFinish with GpuSync)
        std::function<void(Instance&, melonDS::u32 frame, melonDS::u32 lines, double workMs)> AfterFrame;
        // as the thread ends, before the instance's stop() and destruction
        std::function<void(Instance&)> OnStop;
    };

    ~EmulatorThread();

    // Starts the thread on [window] with [context] (which mustn't be current anywhere), running [instance]
    void Start(SDL_Window* window, SDL_GLContext context, std::unique_ptr<Instance> instance, Callbacks callbacks);

    void Pause();
    void Resume();
    bool IsPaused() const { return Paused.load(); }
    // Off: as fast as it can, for benchmarks (--unthrottled). Fast forward (Tab) is the same while it's held
    void SetLimitFps(bool enabled) { LimitFps = enabled; }
    void SetFastForward(bool enabled) { FastForward = enabled; }
    bool IsFastForward() const { return FastForward.load(); }
    // glFinish after each frame, so the work time includes the GPU's (--gpu-sync)
    void SetGpuSync(bool enabled) { GpuSync = enabled; }

    // Runs [command] on the emulator thread between frames, also while it's paused; any thread
    void Post(std::function<void(Instance&)> command);
    // Asks the thread to leave its frame loop after the current frame; the instance and its last frame stay until Stop()
    void RequestFinish();
    // Whether the thread left its frame loop (a callback said so, RequestFinish() or Stop())
    bool IsFinished() const { return Finished.load(); }
    // Ends the thread and waits for it; the instance is destroyed on it first
    void Stop();
    // Frames per second over the last 30 frames, as the app measures it
    float GetFps() const { return Fps.load(); }

private:
    void Run(SDL_Window* window, SDL_GLContext context);
    void RunCommands();
    // The app's timer limiter (runTimerLimiter): sleeps off the rest of the time [lines] should take at the DS's rate, carrying the error
    void LimitFrame(melonDS::u32 lines, double& frameLimitError, double& lastTick);

    std::unique_ptr<Instance> Emu;
    Callbacks Hooks;
    std::thread Handle;

    std::mutex Lock;
    std::condition_variable Wake;
    std::vector<std::function<void(Instance&)>> Commands;
    bool FinishRequested = false;
    bool StopRequested = false;

    std::atomic_bool Paused = false;
    std::atomic_bool LimitFps = true;
    std::atomic_bool FastForward = false;
    std::atomic_bool GpuSync = false;
    std::atomic_bool Finished = false;
    std::atomic<float> Fps = 0;
};

}
