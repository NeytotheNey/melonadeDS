#include "AudioDiagnostics.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include "MelonDSAudio.h"
#include "LogQueue.h"
#include "MelonLog.h"
#include "MonotonicClock.h"

namespace AudioDiagnostics
{

std::atomic_bool enabled = false;
CallbackCounters counters;
DriftState driftState;
std::atomic<uint32_t> callbackClockGeneration = 0;

// Guards everything below that isn't atomic, apart from the emulator thread's stats; never taken in the output callback, and always taken after
// audioOutputMutex when both are held
static std::mutex stateMutex;
static bool isDebugLogOn = false;
static bool isPerfCaptureOn = false;
// Set when the diagnostics turn on, so that the emulator thread restarts its per-frame baseline
static std::atomic_bool frameBaselineResetPending = false;
static std::atomic_bool isStreamRunning = false;

static bool isStreamOpen = false;
static char streamInfo[MAX_STREAM_INFO_LENGTH] = {};
static bool areStreamXRunsSupported = false;
static int64_t streamXRuns = 0;
static int64_t closedStreamXRuns = 0;
static bool wereXRunsEverSupported = false;
static int64_t streamOpens = 0;
static int64_t streamErrors = 0;
// Which stream state the debug log last showed: streamOpens while that stream is open, -1 - streamOpens once it's closed
static int64_t lastLoggedStreamKey = INT64_MIN;

// The current (or last) stream's buffer size, written by the output callback when it grows the buffer and under stateMutex when a stream opens
static std::atomic<int32_t> bufferSize = -1;
static int32_t bufferSizeAtOpen = -1;
static int32_t bufferSizeLimit = -1;
static std::atomic<int64_t> bufferGrows = 0;
// The priming counters when the emulation session's audio was set up
static std::atomic<int64_t> sessionPrimeWaitBaseline = 0;
static std::atomic<int64_t> sessionReprimeBaseline = 0;
static std::atomic<int64_t> sessionPrimeTrimBaseline = 0;
static std::atomic<int64_t> sessionPrimeTrimFrameBaseline = 0;

// Buffer size changes, from the output callback (the only writer) to whoever holds stateMutex (the only reader)
struct BufferSizeChange
{
    int64_t timestampNs;
    int32_t oldSize;
    int32_t newSize;
    int32_t xruns;
};
static constexpr uint32_t MAX_BUFFER_SIZE_CHANGES = 16;
static BufferSizeChange bufferSizeChanges[MAX_BUFFER_SIZE_CHANGES];
static std::atomic<uint32_t> bufferSizeChangeWriteCount = 0;
static std::atomic<uint32_t> bufferSizeChangeReadCount = 0;

static StreamEvent pendingEvents[MAX_PENDING_EVENTS];
static std::size_t pendingEventStart = 0;
static std::size_t pendingEventCount = 0;

// Emulator thread only
struct SecondStats
{
    int64_t callbacks = 0;
    int64_t frames = 0;
    int64_t underruns = 0;
    int64_t silenceFrames = 0;
    int64_t primeWaitFrames = 0;
    int64_t reprimes = 0;
    int64_t primeTrims = 0;
    int64_t primeTrimFrames = 0;
    int64_t xruns = -1;
    int fillMin = INT_MAX;
    int fillMax = 0;
    int64_t fillSum = 0;
    int fillSamples = 0;
    int fullFrames = 0;
};
static SecondStats second;
static int64_t frameSilenceBaseline = 0;

// The speed paced fast-forward really reaches, smoothed, in thousandths (low 32 bits, 0 means unknown), with the audio rate generation it was
// measured under (high 32 bits); only the emulator thread writes it
static std::atomic<uint64_t> achievedSpeed = 0;
// Time constant of the achieved speed's smoothing, short enough to follow a slow scene and long enough not to wobble with the frame-to-frame pacing
static constexpr double ACHIEVED_SPEED_SECONDS = 0.35;
// The speed is only published once it covers this much wall time
static constexpr double ACHIEVED_SPEED_MIN_SECONDS = 0.1;
// Emulator thread only: emulated frames and wall time, each decayed with ACHIEVED_SPEED_SECONDS, so their ratio is the frame rate over about that
// long (by wall time, since the frames come in bursts)
struct AchievedSpeedMeter
{
    bool isActive = false;
    uint32_t rateGeneration = 0;
    float requestedSpeed = 0;
    double frames = 0;
    double seconds = 0;
    double observedSeconds = 0;
};
static AchievedSpeedMeter achievedSpeedMeter;

static double framesToMillis(int64_t frames)
{
    return frames * 1000.0 / SPU_OUTPUT_SAMPLE_RATE;
}

static const char* getLatencyName(MelonDSAndroid::AudioLatency audioLatency)
{
    switch (audioLatency)
    {
        case MelonDSAndroid::AudioLatency::Low: return "LOW";
        case MelonDSAndroid::AudioLatency::Medium: return "MEDIUM";
        case MelonDSAndroid::AudioLatency::High: return "HIGH";
    }
    return "unknown";
}

// Must be called with stateMutex held
static void updateEnabled()
{
    bool isOn = isDebugLogOn || isPerfCaptureOn;
    if (isOn && !enabled.load(std::memory_order_relaxed))
    {
        frameBaselineResetPending.store(true, std::memory_order_relaxed);
        callbackClockGeneration.fetch_add(1, std::memory_order_relaxed);
    }
    enabled.store(isOn, std::memory_order_relaxed);
}

// Must be called with stateMutex held
static void recordEvent(const char* name, const char* detail, int64_t timestampNs = 0)
{
    if (!isPerfCaptureOn)
        return;

    if (pendingEventCount == MAX_PENDING_EVENTS)
    {
        pendingEventStart = (pendingEventStart + 1) % MAX_PENDING_EVENTS;
        pendingEventCount--;
    }

    StreamEvent& event = pendingEvents[(pendingEventStart + pendingEventCount) % MAX_PENDING_EVENTS];
    event.timestampNs = timestampNs != 0 ? timestampNs : monotonicNanos();
    snprintf(event.name, sizeof(event.name), "%s", name);
    snprintf(event.detail, sizeof(event.detail), "%s", detail);
    pendingEventCount++;
}

void onAudioSessionStart()
{
    sessionPrimeWaitBaseline.store(counters.primeWaitFrames.load(std::memory_order_relaxed), std::memory_order_relaxed);
    sessionReprimeBaseline.store(counters.reprimes.load(std::memory_order_relaxed), std::memory_order_relaxed);
    sessionPrimeTrimBaseline.store(counters.primeTrims.load(std::memory_order_relaxed), std::memory_order_relaxed);
    sessionPrimeTrimFrameBaseline.store(counters.primeTrimFrames.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

void onBufferSizeChanged(int32_t oldSize, int32_t newSize, int32_t xruns)
{
    bufferSize.store(newSize, std::memory_order_relaxed);
    bufferGrows.fetch_add(1, std::memory_order_relaxed);
    if (!isEnabled())
        return;

    uint32_t writeCount = bufferSizeChangeWriteCount.load(std::memory_order_relaxed);
    // Full, since nobody has taken them for a while: the newest one is dropped
    if (writeCount - bufferSizeChangeReadCount.load(std::memory_order_acquire) >= MAX_BUFFER_SIZE_CHANGES)
        return;

    BufferSizeChange& change = bufferSizeChanges[writeCount % MAX_BUFFER_SIZE_CHANGES];
    change.timestampNs = monotonicNanos();
    change.oldSize = oldSize;
    change.newSize = newSize;
    change.xruns = xruns;
    bufferSizeChangeWriteCount.store(writeCount + 1, std::memory_order_release);
}

// Logs the buffer size changes the output callback made since the last call and keeps them for the perf logger; must be called with stateMutex held
static void takeBufferSizeChanges()
{
    uint32_t readCount = bufferSizeChangeReadCount.load(std::memory_order_relaxed);
    uint32_t writeCount = bufferSizeChangeWriteCount.load(std::memory_order_acquire);
    if (readCount == writeCount)
        return;

    int64_t nowNs = monotonicNanos();
    for (; readCount != writeCount; readCount++)
    {
        const BufferSizeChange& change = bufferSizeChanges[readCount % MAX_BUFFER_SIZE_CHANGES];
        char detail[96];
        snprintf(detail, sizeof(detail), "old=%d new=%d new_ms=%.1f xruns=%d", change.oldSize, change.newSize, framesToMillis(change.newSize),
                change.xruns);
        if (isDebugLogOn)
            LOG_INFO_QUEUED(LOG_TAG_AUDIO, "audio buffer grew after an xrun %.0f ms ago: %d -> %d frames (%.1f -> %.1f ms), xruns=%d",
                    (nowNs - change.timestampNs) / 1000000.0, change.oldSize, change.newSize, framesToMillis(change.oldSize),
                    framesToMillis(change.newSize), change.xruns);
        recordEvent("audio_buffer_size", detail, change.timestampNs);
    }
    bufferSizeChangeReadCount.store(readCount, std::memory_order_release);
}

// -1 if no stream so far could report them; must be called with stateMutex held
static int64_t getTotalXRuns()
{
    return wereXRunsEverSupported ? closedStreamXRuns + streamXRuns : -1;
}

void setDebugLogEnabled(bool isEnabled)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    isDebugLogOn = isEnabled;
    lastLoggedStreamKey = INT64_MIN;
    updateEnabled();
}

void setPerfCaptureEnabled(bool isEnabled)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    isPerfCaptureOn = isEnabled;
    if (isEnabled)
    {
        counters.sessionMaxGapNs.store(0, std::memory_order_relaxed);
        counters.sessionMaxFrames.store(0, std::memory_order_relaxed);
        pendingEventStart = 0;
        pendingEventCount = 0;
        // The stream is usually opened before the capture starts
        if (isStreamOpen)
            recordEvent("audio_stream", streamInfo);
    }
    updateEnabled();
}

void onStreamOpened(oboe::AudioStream& stream, MelonDSAndroid::AudioLatency requestedLatency, const char* reason, int32_t sizeLimit, bool isBufferTuned)
{
    bool isAAudio = stream.getAudioApi() == oboe::AudioApi::AAudio;
    // Only AAudio streams can tell (the extension casts the underlying stream to an AAudio one)
    const char* mmap = isAAudio ? (oboe::OboeExtensions::isMMapUsed(&stream) ? "1" : "0") : "n/a";
    bool isXRunCountSupported = stream.isXRunCountSupported();
    int32_t sampleRate = stream.getSampleRate();
    int32_t streamBufferSize = stream.getBufferSizeInFrames();

    std::lock_guard<std::mutex> lock(stateMutex);
    // Changes the previous stream made are logged before this one's facts
    takeBufferSizeChanges();
    // buffer_size is the size the stream started with, which a tuned buffer grows from up to buffer_max
    snprintf(streamInfo, sizeof(streamInfo),
            "api=%s sharing=%s performance=%s mmap=%s rate=%d channels=%d format=%s burst=%d capacity=%d buffer_size=%d buffer_ms=%.1f "
            "buffer_max=%d buffer_tuning=%s device=%d requested_latency=%s xruns=%s",
            oboe::convertToText(stream.getAudioApi()), oboe::convertToText(stream.getSharingMode()), oboe::convertToText(stream.getPerformanceMode()),
            mmap, sampleRate, stream.getChannelCount(), oboe::convertToText(stream.getFormat()), stream.getFramesPerBurst(),
            stream.getBufferCapacityInFrames(), streamBufferSize, sampleRate > 0 ? streamBufferSize * 1000.0 / sampleRate : 0.0, sizeLimit,
            isBufferTuned ? "on" : "off", stream.getDeviceId(), getLatencyName(requestedLatency), isXRunCountSupported ? "supported" : "unsupported");

    bufferSize.store(streamBufferSize, std::memory_order_relaxed);
    bufferSizeAtOpen = streamBufferSize;
    bufferSizeLimit = sizeLimit;

    isStreamOpen = true;
    areStreamXRunsSupported = isXRunCountSupported;
    wereXRunsEverSupported |= isXRunCountSupported;
    streamXRuns = 0;
    streamOpens++;

    if (isDebugLogOn)
    {
        LOG_INFO_QUEUED(LOG_TAG_AUDIO, "audio stream opened (%s): %s", reason, streamInfo);
        lastLoggedStreamKey = streamOpens;
    }
    recordEvent("audio_stream_open", reason);
    recordEvent("audio_stream", streamInfo);
}

void onStreamOpenFailed(oboe::Result result, MelonDSAndroid::AudioLatency requestedLatency, const char* reason)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    char detail[96];
    snprintf(detail, sizeof(detail), "%s: %s requested_latency=%s", reason, oboe::convertToText(result), getLatencyName(requestedLatency));
    if (isDebugLogOn)
        LOG_WARN_QUEUED(LOG_TAG_AUDIO, "audio stream open failed (%s)", detail);
    recordEvent("audio_stream_open_failed", detail);
}

void onStreamClosing(oboe::AudioStream& stream)
{
    isStreamRunning.store(false, std::memory_order_relaxed);

    // Fails once Oboe has closed the stream itself (after an error), in which case the last polled count is kept
    oboe::ResultWithValue<int32_t> xruns = stream.isXRunCountSupported() ? stream.getXRunCount() : oboe::ResultWithValue<int32_t>(oboe::Result::ErrorUnimplemented);

    std::lock_guard<std::mutex> lock(stateMutex);
    if (!isStreamOpen)
        return;

    if (xruns)
        streamXRuns = xruns.value();
    closedStreamXRuns += streamXRuns;
    streamXRuns = 0;
    isStreamOpen = false;
}

void onStreamStarting()
{
    callbackClockGeneration.fetch_add(1, std::memory_order_relaxed);
}

void onStreamStarted()
{
    isStreamRunning.store(true, std::memory_order_relaxed);
}

void onStreamPaused()
{
    isStreamRunning.store(false, std::memory_order_relaxed);
    callbackClockGeneration.fetch_add(1, std::memory_order_relaxed);
}

void onStreamError(oboe::Result result)
{
    // isStreamRunning is left alone, since the error can be for a stream that was already replaced
    std::lock_guard<std::mutex> lock(stateMutex);
    streamErrors++;
    if (isDebugLogOn)
        LOG_WARN_QUEUED(LOG_TAG_AUDIO, "audio stream error: %s (stream closed by Oboe)", oboe::convertToText(result));
    recordEvent("audio_stream_error", oboe::convertToText(result));
}

void onXRunCount(oboe::AudioStream& stream)
{
    if (!stream.isXRunCountSupported())
        return;

    oboe::ResultWithValue<int32_t> xruns = stream.getXRunCount();
    if (!xruns)
        return;

    std::lock_guard<std::mutex> lock(stateMutex);
    if (isStreamOpen)
        streamXRuns = xruns.value();
}

FrameSample onFrame(int fillAfterFrame, int fillBeforeNextFrame)
{
    int64_t silenceFrames = counters.silenceFrames.load(std::memory_order_relaxed);
    if (frameBaselineResetPending.exchange(false, std::memory_order_relaxed)) [[unlikely]]
        frameSilenceBaseline = silenceFrames;

    FrameSample sample;
    sample.fill = fillBeforeNextFrame;
    sample.silenceFrames = silenceFrames - frameSilenceBaseline;
    // Without a running stream nothing drains the ring, so it being full means nothing
    sample.isBufferFull = isStreamRunning.load(std::memory_order_relaxed) && fillAfterFrame >= SPU_OUTPUT_BUFFER_FRAMES - 1;
    sample.skewPpm = driftState.skewPpm.load(std::memory_order_relaxed);
    float achievedSpeed = getAchievedSpeed();
    sample.achievedSpeedMilli = achievedSpeed > 0 ? (int64_t) (achievedSpeed * 1000.0f + 0.5f) : -1;
    frameSilenceBaseline = silenceFrames;

    if (fillBeforeNextFrame < second.fillMin)
        second.fillMin = fillBeforeNextFrame;
    if (fillAfterFrame > second.fillMax)
        second.fillMax = fillAfterFrame;
    second.fillSum += fillBeforeNextFrame;
    second.fillSamples++;
    if (sample.isBufferFull)
        second.fullFrames++;

    return sample;
}

static void publishAchievedSpeed(uint32_t rateGeneration, double speed)
{
    uint64_t speedMilli = speed > 0 ? (uint64_t) std::min(speed * 1000.0 + 0.5, (double) UINT32_MAX) : 0;
    achievedSpeed.store(((uint64_t) rateGeneration << 32) | speedMilli, std::memory_order_relaxed);
}

void onEmulatorFrameEnd(double intervalMs, bool isPacedFastForward, float requestedSpeed)
{
    // A new fast-forward state (or speed, or unmute) or a new session bumps the generation, and a value published under the old one is ignored from
    // then on, even if it lands after the bump
    uint32_t rateGeneration = MelonDSAndroid::getAudioRateGeneration();
    if (!isPacedFastForward)
    {
        if (achievedSpeedMeter.isActive)
        {
            achievedSpeedMeter.isActive = false;
            publishAchievedSpeed(rateGeneration, 0);
        }
        return;
    }

    if (!achievedSpeedMeter.isActive || rateGeneration != achievedSpeedMeter.rateGeneration || requestedSpeed != achievedSpeedMeter.requestedSpeed)
    {
        // The frame that saw the change ran at least partly under the old state, so it isn't counted
        achievedSpeedMeter = AchievedSpeedMeter();
        achievedSpeedMeter.isActive = true;
        achievedSpeedMeter.rateGeneration = rateGeneration;
        achievedSpeedMeter.requestedSpeed = requestedSpeed;
        publishAchievedSpeed(rateGeneration, 0);
        return;
    }

    double seconds = intervalMs / 1000.0;
    if (seconds <= 0)
        return;
    double decay = std::exp(-seconds / ACHIEVED_SPEED_SECONDS);
    achievedSpeedMeter.frames = achievedSpeedMeter.frames * decay + 1.0;
    achievedSpeedMeter.seconds = achievedSpeedMeter.seconds * decay + seconds;
    achievedSpeedMeter.observedSeconds += seconds;
    if (achievedSpeedMeter.observedSeconds >= ACHIEVED_SPEED_MIN_SECONDS)
        publishAchievedSpeed(rateGeneration, achievedSpeedMeter.frames / achievedSpeedMeter.seconds / EMULATOR_TARGET_FPS);
}

void onEmulatorPaused()
{
    // The speed starts over after the pause, published now so the old one isn't logged while the stream starts again
    achievedSpeedMeter.isActive = false;
    publishAchievedSpeed(MelonDSAndroid::getAudioRateGeneration(), 0);
}

float getAchievedSpeed()
{
    uint64_t value = achievedSpeed.load(std::memory_order_relaxed);
    if ((uint32_t) (value >> 32) != MelonDSAndroid::getAudioRateGeneration())
        return 0;
    return (float) (uint32_t) value / 1000.0f;
}

// Starts a new second from the current counters
static void startSecond(int64_t xruns)
{
    second = SecondStats();
    second.callbacks = counters.callbacks.load(std::memory_order_relaxed);
    second.frames = counters.frames.load(std::memory_order_relaxed);
    second.underruns = counters.underruns.load(std::memory_order_relaxed);
    second.silenceFrames = counters.silenceFrames.load(std::memory_order_relaxed);
    second.primeWaitFrames = counters.primeWaitFrames.load(std::memory_order_relaxed);
    second.reprimes = counters.reprimes.load(std::memory_order_relaxed);
    second.primeTrims = counters.primeTrims.load(std::memory_order_relaxed);
    second.primeTrimFrames = counters.primeTrimFrames.load(std::memory_order_relaxed);
    second.xruns = xruns;
    counters.windowMaxGapNs.store(0, std::memory_order_relaxed);
    counters.windowMaxFrames.store(0, std::memory_order_relaxed);
}

// Writes "n/a" if the value isn't [known], otherwise the value formatted with [format]
static void formatOptional(char* buffer, std::size_t size, bool known, const char* format, ...)
{
    if (!known)
    {
        snprintf(buffer, size, "n/a");
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, size, format, args);
    va_end(args);
}

void logSecond(uint32_t frame)
{
    // The heartbeat right after a reset covers no frames
    if (second.fillSamples == 0)
        return;

    bool isXRunCountCurrent = MelonDSAndroid::pollAudioOutputXRuns();

    int64_t callbacks = counters.callbacks.load(std::memory_order_relaxed);
    int64_t frames = counters.frames.load(std::memory_order_relaxed);
    int64_t underruns = counters.underruns.load(std::memory_order_relaxed);
    int64_t silenceFrames = counters.silenceFrames.load(std::memory_order_relaxed);
    int64_t primeWaitFrames = counters.primeWaitFrames.load(std::memory_order_relaxed);
    int64_t reprimes = counters.reprimes.load(std::memory_order_relaxed);
    int64_t primeTrims = counters.primeTrims.load(std::memory_order_relaxed);
    int64_t primeTrimFrames = counters.primeTrimFrames.load(std::memory_order_relaxed);
    int64_t maxGapNs = counters.windowMaxGapNs.exchange(0, std::memory_order_relaxed);
    int64_t maxFrames = counters.windowMaxFrames.exchange(0, std::memory_order_relaxed);

    bool isOpen;
    bool areXRunsSupported;
    int64_t totalXRuns;
    int32_t currentBufferSizeLimit;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        takeBufferSizeChanges();
        currentBufferSizeLimit = bufferSizeLimit;
        isOpen = isStreamOpen;
        areXRunsSupported = isStreamOpen && areStreamXRunsSupported;
        totalXRuns = getTotalXRuns();
    }

    char xrunText[24];
    if (!areXRunsSupported)
        snprintf(xrunText, sizeof(xrunText), "n/a");
    else if (!isXRunCountCurrent)
        // The stream was being opened or closed, so the count is read again next second
        snprintf(xrunText, sizeof(xrunText), "busy");
    else
        snprintf(xrunText, sizeof(xrunText), "%lld", (long long) (second.xruns >= 0 ? totalXRuns - second.xruns : 0));
    char totalXRunText[24];
    formatOptional(totalXRunText, sizeof(totalXRunText), totalXRuns >= 0, "%lld", (long long) totalXRuns);

    int64_t secondCallbacks = callbacks - second.callbacks;
    int64_t secondFrames = frames - second.frames;
    bool hasFill = second.fillSamples > 0;
    int fillMin = hasFill ? second.fillMin : 0;
    int fillAvg = hasFill ? (int) (second.fillSum / second.fillSamples) : 0;
    int fillMax = hasFill ? second.fillMax : 0;
    const char* streamState = !isOpen ? "closed" : (isStreamRunning.load(std::memory_order_relaxed) ? "running" : "stopped");
    int32_t filteredFill = driftState.filteredFill.load(std::memory_order_relaxed);
    char filteredFillText[16];
    formatOptional(filteredFillText, sizeof(filteredFillText), filteredFill >= 0, "%d", filteredFill);
    int32_t currentBufferSize = isOpen ? bufferSize.load(std::memory_order_relaxed) : -1;
    float achievedSpeed = getAchievedSpeed();
    char achievedSpeedText[16];
    formatOptional(achievedSpeedText, sizeof(achievedSpeedText), achievedSpeed > 0, "%.3f", achievedSpeed);

    LOG_INFO_QUEUED(LOG_TAG_AUDIO,
            "audio frame=%u stream=%s callbacks=%lld frames=%lld cb_frames_avg=%.1f cb_frames_max=%lld cb_gap_max_ms=%.2f underruns=%lld silence_frames=%lld "
            "prime_wait_frames=%lld reprimes=%lld prime_trims=%lld prime_trim_frames=%lld xruns=%s fill_min=%d fill_avg=%d fill_max=%d fill_ms=%.1f/%.1f/%.1f buffer_full_frames=%d skew_ppm=%d "
            "fill_filtered=%s ff_speed=%s buffer_size=%d buffer_ms=%.1f buffer_max=%d | total underruns=%lld silence_frames=%lld prime_wait_frames=%lld reprimes=%lld "
            "prime_trims=%lld prime_trim_frames=%lld xruns=%s",
            frame, streamState, (long long) secondCallbacks, (long long) secondFrames, secondCallbacks > 0 ? (double) secondFrames / secondCallbacks : 0.0,
            (long long) maxFrames, maxGapNs / 1000000.0, (long long) (underruns - second.underruns), (long long) (silenceFrames - second.silenceFrames),
            (long long) (primeWaitFrames - second.primeWaitFrames), (long long) (reprimes - second.reprimes),
            (long long) (primeTrims - second.primeTrims), (long long) (primeTrimFrames - second.primeTrimFrames), xrunText, fillMin, fillAvg, fillMax, framesToMillis(fillMin), framesToMillis(fillAvg), framesToMillis(fillMax), second.fullFrames,
            driftState.skewPpm.load(std::memory_order_relaxed), filteredFillText, achievedSpeedText, currentBufferSize, framesToMillis(std::max(currentBufferSize, 0)),
            currentBufferSizeLimit, (long long) underruns, (long long) silenceFrames, (long long) primeWaitFrames, (long long) reprimes,
            (long long) primeTrims, (long long) primeTrimFrames, totalXRunText);

    // A count that couldn't be read is compared against the last one that could
    startSecond(isXRunCountCurrent || second.xruns < 0 ? totalXRuns : second.xruns);
}

void resetForDebugLog()
{
    MelonDSAndroid::pollAudioOutputXRuns();

    char info[MAX_STREAM_INFO_LENGTH];
    bool isOpen;
    bool isLogged;
    int64_t totalXRuns;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        isOpen = isStreamOpen;
        memcpy(info, streamInfo, sizeof(info));
        totalXRuns = getTotalXRuns();
        int64_t streamKey = isStreamOpen ? streamOpens : -1 - streamOpens;
        isLogged = streamKey == lastLoggedStreamKey;
        lastLoggedStreamKey = streamKey;
    }

    startSecond(totalXRuns);
    if (isLogged)
        return;
    if (isOpen)
        LOG_INFO_QUEUED(LOG_TAG_AUDIO, "audio stream (current): %s", info);
    else
        LOG_INFO_QUEUED(LOG_TAG_AUDIO, "audio stream (current): none open");
}

void getTotals(int64_t* totals)
{
    MelonDSAndroid::pollAudioOutputXRuns();

    totals[TOTAL_CALLBACKS] = counters.callbacks.load(std::memory_order_relaxed);
    totals[TOTAL_CALLBACK_FRAMES] = counters.frames.load(std::memory_order_relaxed);
    totals[TOTAL_UNDERRUN_CALLBACKS] = counters.underruns.load(std::memory_order_relaxed);
    totals[TOTAL_SILENCE_FRAMES] = counters.silenceFrames.load(std::memory_order_relaxed);
    totals[TOTAL_FAST_FORWARD_UNDERRUN_CALLBACKS] = counters.fastForwardUnderruns.load(std::memory_order_relaxed);
    totals[TOTAL_FAST_FORWARD_SILENCE_FRAMES] = counters.fastForwardSilenceFrames.load(std::memory_order_relaxed);
    totals[TOTAL_MAX_CALLBACK_GAP_NS] = counters.sessionMaxGapNs.load(std::memory_order_relaxed);
    totals[TOTAL_MAX_CALLBACK_FRAMES] = counters.sessionMaxFrames.load(std::memory_order_relaxed);

    totals[TOTAL_BUFFER_GROWS] = bufferGrows.load(std::memory_order_relaxed);
    totals[TOTAL_PRIME_WAIT_FRAMES] = counters.primeWaitFrames.load(std::memory_order_relaxed);
    totals[TOTAL_REPRIMES] = counters.reprimes.load(std::memory_order_relaxed);
    totals[TOTAL_PRIME_TRIMS] = counters.primeTrims.load(std::memory_order_relaxed);
    totals[TOTAL_PRIME_TRIM_FRAMES] = counters.primeTrimFrames.load(std::memory_order_relaxed);
    totals[TOTAL_SESSION_PRIME_WAIT_FRAMES] = totals[TOTAL_PRIME_WAIT_FRAMES] - sessionPrimeWaitBaseline.load(std::memory_order_relaxed);
    totals[TOTAL_SESSION_REPRIMES] = totals[TOTAL_REPRIMES] - sessionReprimeBaseline.load(std::memory_order_relaxed);
    totals[TOTAL_SESSION_PRIME_TRIMS] = totals[TOTAL_PRIME_TRIMS] - sessionPrimeTrimBaseline.load(std::memory_order_relaxed);
    totals[TOTAL_SESSION_PRIME_TRIM_FRAMES] = totals[TOTAL_PRIME_TRIM_FRAMES] - sessionPrimeTrimFrameBaseline.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(stateMutex);
    takeBufferSizeChanges();
    totals[TOTAL_XRUNS] = getTotalXRuns();
    totals[TOTAL_STREAM_OPENS] = streamOpens;
    totals[TOTAL_STREAM_ERRORS] = streamErrors;
    totals[TOTAL_BUFFER_SIZE] = bufferSize.load(std::memory_order_relaxed);
    totals[TOTAL_BUFFER_SIZE_AT_OPEN] = bufferSizeAtOpen;
}

std::size_t getStreamInfo(char* buffer, std::size_t size)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    int length = snprintf(buffer, size, "%s", isStreamOpen ? streamInfo : "");
    return length > 0 ? (std::size_t) length : 0;
}

std::size_t takeEvents(StreamEvent* events, std::size_t maxEvents)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    takeBufferSizeChanges();
    std::size_t count = pendingEventCount < maxEvents ? pendingEventCount : maxEvents;
    for (std::size_t i = 0; i < count; i++)
        events[i] = pendingEvents[(pendingEventStart + i) % MAX_PENDING_EVENTS];

    pendingEventStart = (pendingEventStart + count) % MAX_PENDING_EVENTS;
    pendingEventCount -= count;
    return count;
}

}
