// The vsyncs of the main window's monitor, as the app's Choreographer callback gives them (onDisplayVsync): a thread of its own waits for each
// vertical blank of the DXGI output the window is on (IDXGIOutput::WaitForVBlank), picked again when the window moves to another monitor, and
// hands its time to a DisplayVsyncTracker. The emulator thread starts its frames on that grid (VsyncFramePacer) and the UI thread presents once
// per vsync. Times are monotonicNanos(): MinGW's clock_gettime(CLOCK_MONOTONIC) reads QueryPerformanceCounter, as std::chrono::steady_clock
// does, so the frame queue's steady_clock deadlines are on the same clock. Detached, the second window's monitor isn't followed. Started and
// stopped on the UI thread; never in a hidden run
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#include <SDL.h>

#include "renderer/FramePacing.h"

namespace Frontend
{

class VsyncSource
{
public:
    ~VsyncSource();

    // starts the thread on the monitor [window] is on now; false when it can't (no window handle, or a thread that didn't stop is still
    // waiting)
    bool Start(SDL_Window* window);
    // stops the thread and waits for it, at most half a second: a wait that never returns (a display that's gone) is left to end on its own
    void Stop();
    bool IsRunning() const { return Handle.joinable(); }

    // the vsyncs so far, one tracker for the source's life (each thread run writes it in turn), so the emulator thread can keep the pointer
    const DisplayVsyncTracker* Tracker() const { return &Shared->Tracker; }
    // how many vsyncs the threads have seen
    uint64_t Count() const;
    // waits up to [timeoutMs] for a vsync after the [seen]th one, and returns the count then (still [seen] when none came)
    uint64_t WaitAfter(uint64_t seen, int timeoutMs) const;

private:
    // what the thread uses, held by it as well, so one left waiting by Stop never touches freed memory
    struct State
    {
        DisplayVsyncTracker Tracker;
        void* Window = nullptr; // the main window's HWND, set before each run
        std::atomic_bool StopRequested = false;
        mutable std::mutex Lock;
        mutable std::condition_variable Signal;
        uint64_t Count = 0;     // under Lock
        bool Exited = true;     // under Lock: no thread runs on it
    };
    static void Run(std::shared_ptr<State> state);

    std::shared_ptr<State> Shared = std::make_shared<State>();
    std::thread Handle;
};

}
