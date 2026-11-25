#include "VsyncSource.h"

#include <windows.h>
#include <dxgi.h>
#include <SDL_syswm.h>

#include <chrono>
#include <cstdio>

#include "MonotonicClock.h"

namespace Frontend
{

namespace
{

// how often the thread checks which monitor the window is on, in vsyncs
constexpr int MONITOR_CHECK_VSYNCS = 16;
// a wait that returns sooner than this after the last one didn't wait for a vertical blank (a display that's off or gone)
constexpr int64_t MIN_VSYNC_INTERVAL_NS = 1000000;
// Stop()'s wait for the thread
constexpr auto STOP_TIMEOUT = std::chrono::milliseconds(500);

template <typename T> void Release(T*& object)
{
    if (object)
        object->Release();
    object = nullptr;
}

// the output that shows [monitor], or null; [factory] is made again when the displays changed since it was
IDXGIOutput* FindOutput(IDXGIFactory1*& factory, HMONITOR monitor)
{
    if (factory && !factory->IsCurrent())
        Release(factory);
    if (!factory && FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
    {
        factory = nullptr;
        return nullptr;
    }
    IDXGIAdapter1* adapter = nullptr;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; a++)
    {
        IDXGIOutput* output = nullptr;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; o++)
        {
            DXGI_OUTPUT_DESC desc;
            if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor)
            {
                Release(adapter);
                return output;
            }
            Release(output);
        }
        Release(adapter);
    }
    return nullptr;
}

}

VsyncSource::~VsyncSource()
{
    Stop();
}

bool VsyncSource::Start(SDL_Window* window)
{
    if (Handle.joinable())
        return true;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!window || !SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS)
    {
        fprintf(stderr, "vsync: no window handle, frames aren't paced to the display\n");
        return false;
    }
    {
        std::lock_guard guard(Shared->Lock);
        // a thread Stop() left waiting still writes the tracker, which has one writer at a time
        if (!Shared->Exited)
        {
            fprintf(stderr, "vsync: the last vsync thread hasn't ended, frames aren't paced to the display\n");
            return false;
        }
        Shared->Exited = false;
    }
    Shared->Window = info.info.win.window;
    Shared->StopRequested = false;
    Handle = std::thread(Run, Shared);
    return true;
}

void VsyncSource::Stop()
{
    if (!Handle.joinable())
        return;
    Shared->StopRequested = true;
    bool exited;
    {
        std::unique_lock lock(Shared->Lock);
        exited = Shared->Signal.wait_for(lock, STOP_TIMEOUT, [this] { return Shared->Exited; });
    }
    if (exited)
        Handle.join();
    else
    {
        fprintf(stderr, "vsync: the vsync thread is still waiting for the display; it's left to end on its own\n");
        Handle.detach();
    }
}

uint64_t VsyncSource::Count() const
{
    std::lock_guard guard(Shared->Lock);
    return Shared->Count;
}

uint64_t VsyncSource::WaitAfter(uint64_t seen, int timeoutMs) const
{
    std::unique_lock lock(Shared->Lock);
    Shared->Signal.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return Shared->Count != seen; });
    return Shared->Count;
}

void VsyncSource::Run(std::shared_ptr<State> state)
{
    // woken as close to the vertical blank as it can be, since its time is the grid every paced frame starts on
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    HWND window = (HWND)state->Window;
    IDXGIFactory1* factory = nullptr;
    IDXGIOutput* output = nullptr;
    HMONITOR monitor = nullptr;
    int sinceCheck = 0;
    int64_t lastReturnNs = 0;
    while (!state->StopRequested.load())
    {
        if (!output || ++sinceCheck >= MONITOR_CHECK_VSYNCS)
        {
            sinceCheck = 0;
            HMONITOR now = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            if (!output || now != monitor)
            {
                Release(output);
                output = FindOutput(factory, now);
                monitor = now;
                if (output)
                {
                    DXGI_OUTPUT_DESC desc;
                    if (SUCCEEDED(output->GetDesc(&desc)))
                        printf("vsync: waiting on %ls\n", desc.DeviceName);
                }
            }
        }
        // no output for the window's monitor (the displays changing): tried again in a moment, with no vsyncs meanwhile
        if (!output)
        {
            Sleep(16);
            continue;
        }
        HRESULT result = output->WaitForVBlank();
        int64_t nowNs = monotonicNanos();
        bool waited = nowNs - lastReturnNs >= MIN_VSYNC_INTERVAL_NS;
        lastReturnNs = nowNs;
        if (FAILED(result) || !waited)
        {
            // the output went away or doesn't wait: found again (with a new factory if the displays changed) after a pause
            Release(output);
            Sleep(16);
            continue;
        }
        state->Tracker.onVsync(nowNs);
        {
            std::lock_guard guard(state->Lock);
            state->Count++;
        }
        state->Signal.notify_all();
    }
    Release(output);
    Release(factory);
    {
        std::lock_guard guard(state->Lock);
        state->Exited = true;
    }
    state->Signal.notify_all();
}

}
