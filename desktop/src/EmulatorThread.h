// The desktop's emulator thread, a port of emulate() in the app's MelonDSAndroidJNI.cpp without JNI: it makes its own GL context current
// (shared with the UI thread's through SDL, so the frames' textures and fences are seen by both), runs a MelonInstance frame after frame
// (which pushes each frame into its FrameQueue for the UI thread), parks while paused, and paces itself as the app does: at normal speed with
// frame pacing on, each frame starts on the display's vsync grid and carries the vsync it's due on (VsyncFramePacer), otherwise the timer
// limiter runs it at 60 fps (fast forward at its speed, unthrottled not at all). It owns the instance, which is destroyed on this thread with
// the context current, as the app does.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <SDL.h>

#include "renderer/FramePacing.h"
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

    // Starts the thread on [window] with [context] (which mustn't be current anywhere), running [instance]. After Stop() it can be started
    // again with another instance (the menus' Quit to start screen, then another game)
    void Start(SDL_Window* window, SDL_GLContext context, std::unique_ptr<Instance> instance, Callbacks callbacks);
    bool IsRunning() const { return Handle.joinable(); }

    void Pause();
    void Resume();
    bool IsPaused() const { return Paused.load(); }
    // Off: as fast as it can, for benchmarks (--unthrottled). Fast forward (Tab) is the same while it's held
    void SetLimitFps(bool enabled) { LimitFps = enabled; }
    void SetFastForward(bool enabled) { FastForward = enabled; }
    // fast forward's pace, times 60 fps (fast_forward_speed_multiplier); 0 or less: as fast as it can
    void SetFastForwardSpeed(float speed) { FastForwardSpeed = speed; }
    bool IsFastForward() const { return FastForward.load(); }
    // glFinish after each frame, so the work time includes the GPU's (--gpu-sync)
    void SetGpuSync(bool enabled) { GpuSync = enabled; }

    // Frame pacing (frame_pacing_vsync, frame_pacing_buffer): at normal speed, frames start on the vsync grid of [tracker] (the main window's
    // monitor; null for none) whenever a lock is possible, due one frame later, or two with the buffer; any thread
    void SetFramePacing(const DisplayVsyncTracker* tracker, bool vsync, bool buffer)
    {
        Tracker = tracker;
        PacingVsync = vsync;
        PacingBuffer = buffer;
    }
    // the vsync the frame being emulated now is due on, 0 when it isn't paced (the app's upcomingFrameDueNs), for the UI thread's present
    int64_t UpcomingFrameDueNs() const { return UpcomingDueNs.load(std::memory_order_relaxed); }
    // UI thread: waits until the thread pushes a frame or monotonicNanos() reaches [untilNs]; false once it's there. The frame queue's
    // own wait is a timed condition variable, which winpthreads wakes on the system timer's 15.6 ms ticks whatever timeBeginPeriod asks,
    // far too late for a vsync; this waits on an event and a high-resolution timer
    bool WaitForFrame(int64_t untilNs);
    // the frames at normal speed since the program started (all games), how many of them were paced and locked to the vsync grid, how many
    // the timer ran with pacing on (the display can't lock: a refresh rate that isn't near a multiple of 60, no recent vsyncs), and the last
    // paced frame's refreshes per frame
    struct PacingStats
    {
        uint64_t Frames = 0, Paced = 0, Locked = 0, TimerWithPacing = 0;
        int LastDivisor = 0;
    };
    PacingStats GetPacingStats() const;

    // Parks before the frame numbered [frame] (as BeforeFrame numbers them) when the thread gets there, for --ui-script's frame step;
    // HeldAt() is that frame once it's parked there. The hold is apart from the pause: a Pause() and Resume() (a menu opened and closed
    // over it) leave it held; only another HoldAt() moves it on, from wherever it is
    void HoldAt(melonDS::u32 frame);
    melonDS::u32 HeldAt() const { return Held.load(); }
    // paused, or parked at a hold
    bool IsParked() const { return Paused.load() || Held.load() != kNoFrame; }
    static constexpr melonDS::u32 kNoFrame = 0xFFFFFFFF;

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
    // the pacing carried from one frame to the next (the app's FramePacingState)
    struct PacingState
    {
        VsyncFramePacer Pacer;
        // the next frame's due vsync (0 when it isn't paced), refreshes per frame, and whether its start is locked to the grid
        int64_t NextDueNs = 0;
        int NextDivisor = 0;
        bool NextLocked = false;
    };

    void Run(SDL_Window* window, SDL_GLContext context);
    void RunCommands();
    // the next frame isn't paced: it starts right away and is due at [dueNs] (0: as soon as it's ready)
    void ResetPacing(PacingState& pacing, int64_t dueNs);
    // after a frame that took [workNs] for its [lines]: the next one locked to the vsync grid (sleeping until its start) when it can be,
    // otherwise the timer; [lastTick] moves past any sleep
    void PaceNextFrame(PacingState& pacing, melonDS::u32 lines, int64_t workNs, double& frameLimitError, double& lastTick);
    // the app's schedulePacedFrame: false when frames can't be locked to the display, so the caller uses the timer
    bool SchedulePacedFrame(PacingState& pacing, int64_t workNs);
    // The app's timer limiter (runTimerLimiter): sleeps off the rest of the time [lines] should take at 60 fps times [speed], carrying the error
    void LimitFrame(melonDS::u32 lines, double speed, double& frameLimitError, double& lastTick);

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
    std::atomic<float> FastForwardSpeed = 0;
    std::atomic_bool GpuSync = false;
    std::atomic_bool Finished = false;
    std::atomic<float> Fps = 0;
    std::atomic<melonDS::u32> HoldFrame = kNoFrame;
    std::atomic<melonDS::u32> Held = kNoFrame;

    // set by the UI thread, read by this one
    std::atomic<const DisplayVsyncTracker*> Tracker = nullptr;
    std::atomic_bool PacingVsync = false;
    std::atomic_bool PacingBuffer = false;
    // written by this thread: the due vsync of the frame it runs, read by the UI thread's present; the stats, read at the end of the run
    std::atomic<int64_t> UpcomingDueNs = 0;
    std::atomic<uint64_t> StatFrames = 0, StatPaced = 0, StatLocked = 0, StatTimerWithPacing = 0;
    std::atomic<int> StatDivisor = 0;
    // WaitForFrame's: an auto-reset event this thread sets after each frame, and the waiting thread's high-resolution timer (HANDLEs)
    void* FramePushed = nullptr;
    void* FrameWaitTimer = nullptr;
};

}
