#include "EmulatorThread.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "Configuration.h"
#include "MelonInstance.h"
#include "MonotonicClock.h"

using namespace melonDS;

namespace Frontend
{

namespace
{

// the app's getCurrentMillis(), on the clock the vsyncs and the pacer's times are on
double NowMs()
{
    return monotonicNanos() / 1000000.0;
}

// A high-resolution waitable timer (Windows 10 1803 and up) up to the last half millisecond, then a spin. Plain sleeps follow the system
// timer's resolution, which Windows keeps at 15.6 ms for a process whose window is hidden or in the background, whatever
// timeBeginPeriod asks, and that oversleeping made the frames come in bunches, which the audio callback heard as a ring running dry and
// then overflowing. Without that timer (older Windows): a plain sleep up to the last 1.5 ms
// one per thread, closed when the thread ends (an emulator thread per game)
struct WaitableTimer
{
    HANDLE Handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    ~WaitableTimer() { if (Handle) CloseHandle(Handle); }
};

void SleepUntilMs(double untilMs)
{
    static thread_local WaitableTimer waitable;
    HANDLE timer = waitable.Handle;
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
    if (FramePushed)
        CloseHandle((HANDLE)FramePushed);
    if (FrameWaitTimer)
        CloseHandle((HANDLE)FrameWaitTimer);
}

bool EmulatorThread::WaitForFrame(int64_t untilNs)
{
    int64_t leftNs = untilNs - monotonicNanos();
    if (leftNs <= 0 || !FramePushed)
        return false;
    if (!FrameWaitTimer)
        FrameWaitTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    // the event with a millisecond timeout when the timer can't be had (older Windows), a millisecond past the deadline at most
    DWORD timeoutMs = (DWORD)(leftNs / 1000000 + 1);
    HANDLE handles[2] = {(HANDLE)FramePushed, (HANDLE)FrameWaitTimer};
    LARGE_INTEGER due;
    // relative, in 100 ns units
    due.QuadPart = -std::max<LONGLONG>(leftNs / 100, 1);
    bool timed = FrameWaitTimer && SetWaitableTimerEx((HANDLE)FrameWaitTimer, &due, 0, nullptr, nullptr, nullptr, 0);
    DWORD result = WaitForMultipleObjects(timed ? 2 : 1, handles, FALSE, timed ? timeoutMs + 1 : timeoutMs);
    if (timed && result != WAIT_OBJECT_0 + 1)
        CancelWaitableTimer((HANDLE)FrameWaitTimer);
    return result == WAIT_OBJECT_0;
}

void EmulatorThread::Start(SDL_Window* window, SDL_GLContext context, std::unique_ptr<Instance> instance, Callbacks callbacks)
{
    // a thread that ran before is done (Stop() joined it): its state goes back to a fresh one
    if (Handle.joinable())
        Handle.join();
    // auto-reset, set after each frame (WaitForFrame)
    if (!FramePushed)
        FramePushed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
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
    }
    Wake.notify_all();
}

void EmulatorThread::HoldAt(u32 frame)
{
    {
        std::lock_guard guard(Lock);
        HoldFrame = frame;
        // a hold it's parked at now lets go, so it runs on to the new one
        Held = kNoFrame;
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

EmulatorThread::PacingStats EmulatorThread::GetPacingStats() const
{
    PacingStats stats;
    stats.Frames = StatFrames.load();
    stats.Paced = StatPaced.load();
    stats.Locked = StatLocked.load();
    stats.TimerWithPacing = StatTimerWithPacing.load();
    stats.LastDivisor = StatDivisor.load();
    return stats;
}

void EmulatorThread::ResetPacing(PacingState& pacing, int64_t dueNs)
{
    pacing.NextDueNs = dueNs;
    pacing.NextLocked = false;
    pacing.NextDivisor = 0;
    UpcomingDueNs.store(dueNs, std::memory_order_relaxed);
}

bool EmulatorThread::SchedulePacedFrame(PacingState& pacing, int64_t workNs)
{
    const DisplayVsyncTracker* tracker = Tracker.load(std::memory_order_acquire);
    if (!tracker)
        return false;
    int64_t lastVsyncNs = tracker->getLastVsyncNs();
    int64_t refreshPeriodNs = tracker->getRefreshPeriodNs();
    int64_t currentNs = monotonicNanos();
    int divisor = VsyncFramePacer::getRefreshDivisor(currentNs, lastVsyncNs, refreshPeriodNs);
    if (divisor <= 0)
        return false;

    // Without the buffer a locked frame is due on the first vsync after one frame period, and the buffer adds one more
    VsyncFramePacer::FrameSchedule schedule = pacing.Pacer.scheduleNextFrame(currentNs, lastVsyncNs, refreshPeriodNs, divisor, workNs,
        PacingBuffer.load(std::memory_order_relaxed));
    pacing.NextDueNs = schedule.dueNs;
    pacing.NextLocked = schedule.isLocked;
    pacing.NextDivisor = divisor;
    // Published before sleeping so that the UI thread knows whether this frame can still be due on the vsync it's presenting
    UpcomingDueNs.store(schedule.dueNs, std::memory_order_relaxed);
    if (schedule.startNs > currentNs)
        SleepUntilMs(schedule.startNs / 1000000.0);
    return true;
}

void EmulatorThread::PaceNextFrame(PacingState& pacing, u32 lines, int64_t workNs, double& frameLimitError, double& lastTick)
{
    // Lock the next frame's start to the display's vsync grid if possible, otherwise fall back to the timer
    bool isFpsLimited = LimitFps.load(std::memory_order_relaxed);
    bool isFastForward = FastForward.load(std::memory_order_relaxed);
    bool isNormalSpeed = isFpsLimited && !isFastForward;
    bool wantsPacing = isNormalSpeed && PacingVsync.load(std::memory_order_relaxed);
    bool isPaced = wantsPacing && SchedulePacedFrame(pacing, workNs);
    if (isNormalSpeed)
    {
        StatFrames.fetch_add(1, std::memory_order_relaxed);
        if (isPaced)
        {
            StatPaced.fetch_add(1, std::memory_order_relaxed);
            if (pacing.NextLocked)
                StatLocked.fetch_add(1, std::memory_order_relaxed);
            StatDivisor.store(pacing.NextDivisor, std::memory_order_relaxed);
        }
        else if (wantsPacing)
            StatTimerWithPacing.fetch_add(1, std::memory_order_relaxed);
    }
    if (isPaced)
    {
        // Start the timer limiter from scratch if pacing stops
        frameLimitError = 0;
        lastTick = NowMs();
        return;
    }

    // The first frame after pacing stops is kept behind a buffered frame that may still be queued, and later ones are presented right away
    ResetPacing(pacing, pacing.Pacer.unlockFromRunning());
    // fast forward at its speed, or as fast as it can (the app's applyFastForwardPacing)
    float ffSpeed = FastForwardSpeed.load();
    if (isFpsLimited && (!isFastForward || ffSpeed > 0))
        LimitFrame(lines, isFastForward ? ffSpeed : 1.0, frameLimitError, lastTick);
    else
    {
        frameLimitError = 0;
        lastTick = NowMs();
    }
}

void EmulatorThread::LimitFrame(u32 lines, double speed, double& frameLimitError, double& lastTick)
{
    double currentTick = NowMs();
    double delay = currentTick - lastTick;
    double frameTimeStep = (double)lines / (EMULATOR_TARGET_FPS * 263.0) * 1000.0 / speed;
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
    PacingState pacing;
    ResetPacing(pacing, 0);

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
            bool parked = Paused || Held != kNoFrame;
            if (!parked && Commands.empty())
                break;
            if (parked && Commands.empty())
            {
                // As the app does when it parks: the scripts' storage and the last rewind capture's screenshot, before waiting
                lock.unlock();
                Emu->flushLuaStorage();
                Emu->finishRewindScreenshot(true);
                lock.lock();
                auto woken = [this] { return FinishRequested || (!Paused && Held == kNoFrame) || !Commands.empty(); };
                if (!Hooks.OnIdle)
                    Wake.wait(lock, woken);
                // the idle hook between timed waits; a wake ends the wait as before, so the pacing after a resume is the same
                while (Hooks.OnIdle && !Wake.wait_for(lock, std::chrono::milliseconds(IDLE_INTERVAL_MS), woken))
                {
                    lock.unlock();
                    Hooks.OnIdle(*Emu);
                    lock.lock();
                }
                if (!Paused && Held == kNoFrame)
                {
                    frameLimitError = 0;
                    lastTick = NowMs();
                    // The first frame after a pause starts right away, off the vsync grid, and pacing re-syncs after it
                    pacing.Pacer.unlock();
                    ResetPacing(pacing, 0);
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
            Held = frame;
            continue;
        }

        if (Hooks.BeforeFrame && !Hooks.BeforeFrame(*Emu, frame))
            break;

        auto frameStart = std::chrono::steady_clock::now();
        u32 lines = Emu->runFrame(pacing.NextDueNs);
        // the frame's in the queue: a present waiting for it goes on
        if (FramePushed)
            SetEvent((HANDLE)FramePushed);
        if (GpuSync)
            glFinish();
        auto workTime = std::chrono::steady_clock::now() - frameStart;
        double workMs = std::chrono::duration<double, std::milli>(workTime).count();
        if (Hooks.AfterFrame)
            Hooks.AfterFrame(*Emu, frame, lines, workMs);
        frame++;

        PaceNextFrame(pacing, lines, std::chrono::duration_cast<std::chrono::nanoseconds>(workTime).count(), frameLimitError, lastTick);

        if (++observedFrames >= 30)
        {
            Fps = (float)(observedFrames * 1000.0 / (lastTick - lastMeasureFpsTick));
            lastMeasureFpsTick = lastTick;
            observedFrames = 0;
        }
    }

    // The instance and its last frame stay for the UI thread (the final screenshots) until Stop(); commands posted meanwhile still run
    UpcomingDueNs.store(0, std::memory_order_relaxed);
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
