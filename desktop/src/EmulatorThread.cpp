#include "EmulatorThread.h"

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

// a coarse sleep (1 ms timer resolution, timeBeginPeriod in main) up to the last 1.5 ms, then a spin, since Windows oversleeps
void SleepUntilMs(double untilMs)
{
    for (;;)
    {
        double left = untilMs - NowMs();
        if (left <= 0)
            return;
        if (left > 2.0)
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
    }
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

void EmulatorThread::LimitFrame(u32 lines, double& frameLimitError, double& lastTick)
{
    double currentTick = NowMs();
    double delay = currentTick - lastTick;
    double frameTimeStep = (double)lines / (kDsFps * 263.0) * 1000.0;
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

        if (LimitFps && !FastForward)
            LimitFrame(lines, frameLimitError, lastTick);
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
