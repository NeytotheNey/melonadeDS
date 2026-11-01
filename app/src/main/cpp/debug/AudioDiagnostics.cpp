#include "AudioDiagnostics.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <mutex>
#include "MelonDSAudio.h"
#include "MelonLog.h"
#include "MonotonicClock.h"

namespace AudioDiagnostics
{

std::atomic_bool enabled = false;
CallbackCounters counters;
DriftState driftState;
std::atomic<uint32_t> callbackClockGeneration = 0;

// Guards everything below that isn't atomic, apart from the emulator thread's stats. Never taken in the output callback. When it's taken
// together with audioOutputMutex, audioOutputMutex is always taken first
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

static double framesToMillis(int64_t frames)
{
    return frames * 1000.0 / SPU_OUTPUT_SAMPLE_RATE;
}

static const char* getLatencyName(int audioLatency)
{
    switch (audioLatency)
    {
        case 0: return "LOW";
        case 1: return "MEDIUM";
        case 2: return "HIGH";
        default: return "unknown";
    }
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
    // Full: nobody has taken them for a while. The newest one is dropped
    if (writeCount - bufferSizeChangeReadCount.load(std::memory_order_acquire) >= MAX_BUFFER_SIZE_CHANGES)
        return;

    BufferSizeChange& change = bufferSizeChanges[writeCount % MAX_BUFFER_SIZE_CHANGES];
    change.timestampNs = monotonicNanos();
    change.oldSize = oldSize;
    change.newSize = newSize;
    change.xruns = xruns;
    bufferSizeChangeWriteCount.store(writeCount + 1, std::memory_order_release);
}

// Must be called with stateMutex held. Logs the buffer size changes the output callback made since the last call, and keeps them for the perf
// logger
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
            LOG_INFO(LOG_TAG_AUDIO, "audio buffer grew after an xrun %.0f ms ago: %d -> %d frames (%.1f -> %.1f ms), xruns=%d",
                    (nowNs - change.timestampNs) / 1000000.0, change.oldSize, change.newSize, framesToMillis(change.oldSize),
                    framesToMillis(change.newSize), change.xruns);
        recordEvent("audio_buffer_size", detail, change.timestampNs);
    }
    bufferSizeChangeReadCount.store(readCount, std::memory_order_release);
}

// Must be called with stateMutex held. -1 if no stream so far could report them
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

void onStreamOpened(oboe::AudioStream& stream, int requestedLatency, const char* reason, int32_t sizeLimit, bool isBufferTuned)
{
    bool isAAudio = stream.getAudioApi() == oboe::AudioApi::AAudio;
    // Only AAudio streams can tell. The extension casts the underlying stream to an AAudio one
    const char* mmap = isAAudio ? (oboe::OboeExtensions::isMMapUsed(&stream) ? "1" : "0") : "n/a";
    bool isXRunCountSupported = stream.isXRunCountSupported();
    int32_t sampleRate = stream.getSampleRate();
    int32_t streamBufferSize = stream.getBufferSizeInFrames();

    std::lock_guard<std::mutex> lock(stateMutex);
    // Changes the previous stream made are logged before this one's facts
    takeBufferSizeChanges();
    // buffer_size is the size the stream started with. A tuned buffer grows from there up to buffer_max
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
        LOG_INFO(LOG_TAG_AUDIO, "audio stream opened (%s): %s", reason, streamInfo);
        lastLoggedStreamKey = streamOpens;
    }
    recordEvent("audio_stream_open", reason);
    recordEvent("audio_stream", streamInfo);
}

void onStreamOpenFailed(oboe::Result result, int requestedLatency, const char* reason)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    char detail[96];
    snprintf(detail, sizeof(detail), "%s: %s requested_latency=%s", reason, oboe::convertToText(result), getLatencyName(requestedLatency));
    if (isDebugLogOn)
        LOG_WARN(LOG_TAG_AUDIO, "audio stream open failed (%s)", detail);
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
    // isStreamRunning is left alone: the error can be for a stream that was already replaced. Closing the current one clears it
    std::lock_guard<std::mutex> lock(stateMutex);
    streamErrors++;
    if (isDebugLogOn)
        LOG_WARN(LOG_TAG_AUDIO, "audio stream error: %s (stream closed by Oboe)", oboe::convertToText(result));
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
    float achievedSpeed = MelonDSAndroid::getAudioAchievedSpeed();
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

static void formatXRuns(char* buffer, std::size_t size, int64_t xruns)
{
    if (xruns < 0)
        snprintf(buffer, size, "n/a");
    else
        snprintf(buffer, size, "%lld", (long long) xruns);
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
        // The stream was being opened or closed. The count is read again next second
        snprintf(xrunText, sizeof(xrunText), "busy");
    else
        formatXRuns(xrunText, sizeof(xrunText), second.xruns >= 0 ? totalXRuns - second.xruns : 0);
    char totalXRunText[24];
    formatXRuns(totalXRunText, sizeof(totalXRunText), totalXRuns);

    int64_t secondCallbacks = callbacks - second.callbacks;
    int64_t secondFrames = frames - second.frames;
    bool hasFill = second.fillSamples > 0;
    int fillMin = hasFill ? second.fillMin : 0;
    int fillAvg = hasFill ? (int) (second.fillSum / second.fillSamples) : 0;
    int fillMax = hasFill ? second.fillMax : 0;
    const char* streamState = !isOpen ? "closed" : (isStreamRunning.load(std::memory_order_relaxed) ? "running" : "stopped");
    int32_t filteredFill = driftState.filteredFill.load(std::memory_order_relaxed);
    char filteredFillText[16];
    if (filteredFill < 0)
        snprintf(filteredFillText, sizeof(filteredFillText), "n/a");
    else
        snprintf(filteredFillText, sizeof(filteredFillText), "%d", filteredFill);
    int32_t currentBufferSize = isOpen ? bufferSize.load(std::memory_order_relaxed) : -1;
    // The smoothed speed paced fast-forward reaches. Only logged: the audio still plays at the requested speed
    float achievedSpeed = MelonDSAndroid::getAudioAchievedSpeed();
    char achievedSpeedText[16];
    if (achievedSpeed > 0)
        snprintf(achievedSpeedText, sizeof(achievedSpeedText), "%.3f", achievedSpeed);
    else
        snprintf(achievedSpeedText, sizeof(achievedSpeedText), "n/a");

    LOG_INFO(LOG_TAG_AUDIO,
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
        LOG_INFO(LOG_TAG_AUDIO, "audio stream (current): %s", info);
    else
        LOG_INFO(LOG_TAG_AUDIO, "audio stream (current): none open");
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
