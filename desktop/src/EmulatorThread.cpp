#include "EmulatorThread.h"

#include <windows.h>

#include <chrono>
#include <cmath>

#include "MelonInstance.h"

using namespace melonDS;

namespace Frontend
{

namespace
{

// the DS's refresh rate: 33513982 Hz / (6 * 355 * 263), where the app's timer limiter uses EMULATOR_TARGET_FPS (60)
constexpr double kDsFps = 33513982.0 / (6.0 * 355 * 263);

double NowMs()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// A high-resolution waitable timer (Windows 10 1803 and up) up to the last half millisecond, then a spin. Plain sleeps follow the system
// timer's resolution, which Windows keeps at 15.6 ms for a process whose window is hidden or in the background, whatever
// timeBeginPeriod asks, and that oversleeping made the frames come in bunches, which the audio callback heard as a ring running dry and
// then overflowing. Without that timer (older Windows): a plain sleep up to the last 1.5 ms
void SleepUntilMs(double untilMs)
{
    static thread_local HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    for (;;)
    {
        double left = untilMs - NowMs();
        if (left <= 0)
            return;
        if (timer && left > 0.6)
        {
            LARGE_INTEGER due;
            // relative, in 100 ns units
            due.QuadPart = -(LONGLONG)((left - 0.5) * 10000.0);
            if (SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0))
            {
                WaitForSingleObject(timer, INFINITE);
                continue;
            }
        }
        if (!timer && left > 2.0)
            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(left - 1.5));
        else
            std::this_thread::yield();
    }
}
}

EmulatorThread::~EmulatorThread()
{
    Stop();
}

void EmulatorThread::Start(SDL_Window* window, SDL_GLContext context, std::unique_ptr<Instance> instance, Callbacks callbacks)
{
    // a thread that ran before is done (Stop() joined it): its state goes back to a fresh one
    if (Handle.joinable())
        Handle.join();
    {
        std::lock_guard guard(Lock);
        Commands.clear();
        FinishRequested = false;
        StopRequested = false;
    }
    Paused = false;
    FastForward = false;
    Finished = false;
    Fps = 0;
    HoldFrame = kNoFrame;
    Held = kNoFrame;
    Emu = std::move(instance);
    Hooks = std::move(callbacks);
    Handle = std::thread([this, window, context] { Run(window, context); });
}

void EmulatorThread::Pause()
{
    std::lock_guard guard(Lock);
    Paused = true;
}

void EmulatorThread::Resume()
{
    {
        std::lock_guard guard(Lock);
        Paused = false;
        Held = kNoFrame;
    }
    Wake.notify_all();
}

void EmulatorThread::HoldAt(u32 frame)
{
    HoldFrame = frame;
    Wake.notify_all();
}

void EmulatorThread::Post(std::function<void(Instance&)> command)
{
    {
        std::lock_guard guard(Lock);
        Commands.push_back(std::move(command));
    }
    Wake.notify_all();
}

void EmulatorThread::RequestFinish()
{
    {
        std::lock_guard guard(Lock);
        FinishRequested = true;
    }
    Wake.notify_all();
}

void EmulatorThread::Stop()
{
    {
        std::lock_guard guard(Lock);
        FinishRequested = true;
        StopRequested = true;
    }
    Wake.notify_all();
    if (Handle.joinable())
        Handle.join();
}

void EmulatorThread::RunCommands()
{
    std::vector<std::function<void(Instance&)>> commands;
    {
        std::lock_guard guard(Lock);
        commands.swap(Commands);
    }
    for (auto& command : commands)
        command(*Emu);
}

void EmulatorThread::LimitFrame(u32 lines, double speed, double& frameLimitError, double& lastTick)
{
    double currentTick = NowMs();
    double delay = currentTick - lastTick;
    double frameTimeStep = (double)lines / (kDsFps * 263.0) * 1000.0 / speed;
    if (frameTimeStep < 1)
        frameTimeStep = 1;

    frameLimitError += frameTimeStep - delay;
    if (frameLimitError < -frameTimeStep)
        frameLimitError = -frameTimeStep;
    if (frameLimitError > frameTimeStep)
        frameLimitError = frameTimeStep;

    if (frameLimitError > 0)
    {
        SleepUntilMs(currentTick + frameLimitError);
        double timeAfterSleep = NowMs();
        frameLimitError -= timeAfterSleep - currentTick;
        currentTick = timeAfterSleep;
    }
    lastTick = currentTick;
}

void EmulatorThread::Run(SDL_Window* window, SDL_GLContext context)
{
    SDL_GL_MakeCurrent(window, context);
    Emu->start();
    bool running = !Hooks.OnStart || Hooks.OnStart(*Emu);

    u32 frame = 0;
    double lastTick = NowMs();
    double lastMeasureFpsTick = lastTick;
    double frameLimitError = 0;
    int observedFrames = 0;

    while (running)
    {
        // Between frames: the posted commands, and the pause, during which commands still run
        for (;;)
        {
            RunCommands();
            std::unique_lock lock(Lock);
            if (FinishRequested)
            {
                running = false;
                break;
            }
            if (!Paused && Commands.empty())
                break;
            if (Paused && Commands.empty())
            {
                // As the app does when it parks: the scripts' storage and the last rewind capture's screenshot, before waiting
                lock.unlock();
                Emu->flushLuaStorage();
                Emu->finishRewindScreenshot(true);
                lock.lock();
                Wake.wait(lock, [this] { return FinishRequested || !Paused || !Commands.empty(); });
                if (!Paused)
                {
                    frameLimitError = 0;
                    lastTick = NowMs();
                }
            }
        }
        if (!running)
            break;

        // --ui-script's frame step: parked before this frame, until a Resume()
        if (frame == HoldFrame.load())
        {
            std::lock_guard guard(Lock);
            HoldFrame = kNoFrame;
            Paused = true;
            Held = frame;
            continue;
        }

        if (Hooks.BeforeFrame && !Hooks.BeforeFrame(*Emu, frame))
            break;

        auto frameStart = std::chrono::steady_clock::now();
        u32 lines = Emu->runFrame(0);
        if (GpuSync)
            glFinish();
        double workMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
        if (Hooks.AfterFrame)
            Hooks.AfterFrame(*Emu, frame, lines, workMs);
        frame++;

        // fast forward at its speed, or as fast as it can (the app's applyFastForwardPacing)
        float ffSpeed = FastForwardSpeed.load();
        if (LimitFps && (!FastForward || ffSpeed > 0))
            LimitFrame(lines, FastForward ? ffSpeed : 1.0, frameLimitError, lastTick);
        else
        {
            frameLimitError = 0;
            lastTick = NowMs();
        }

        if (++observedFrames >= 30)
        {
            Fps = (float)(observedFrames * 1000.0 / (lastTick - lastMeasureFpsTick));
            lastMeasureFpsTick = lastTick;
            observedFrames = 0;
        }
    }

    // The instance and its last frame stay for the UI thread (the final screenshots) until Stop(); commands posted meanwhile still run
    Finished = true;
    for (;;)
    {
        RunCommands();
        std::unique_lock lock(Lock);
        if (StopRequested)
            break;
        Wake.wait(lock, [this] { return StopRequested || !Commands.empty(); });
    }

    if (Hooks.OnStop)
        Hooks.OnStop(*Emu);
    Emu->stop();
    // With the context current, as the frame queue's textures and the renderer's GL objects go with it
    Emu.reset();
    SDL_GL_MakeCurrent(window, nullptr);
}

}
