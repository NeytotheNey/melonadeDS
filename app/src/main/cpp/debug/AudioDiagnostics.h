#ifndef AUDIODIAGNOSTICS_H
#define AUDIODIAGNOSTICS_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <oboe/Oboe.h>

/**
 * Opt-in audio output diagnostics, on while the debug log or a performance capture is. While they're off, the output callback only pays for the
 * [isEnabled] check. While they're on, it reads the monotonic clock and updates a few relaxed atomics: no locks, no allocation, no logging.
 *
 * The buffer between the emulator and the callback is the core's SPU output ring (SPU::OutputBuffer). The emulator thread writes it while it runs
 * a frame, the callback reads it (both under the SPU's own lock). The fill levels logged here are sampled on the emulator thread. The callback
 * reads the fill too, for its drift control, and only hands over what that control made of it (the skew and the filtered fill).
 */
namespace AudioDiagnostics
{

// SPU::InitOutput at the 48 kHz output rate (EmulatorArgsBuilder) gives a 2048 frame ring. It holds one frame less than that when full, and the
// oldest samples are dropped from then on
constexpr int SPU_OUTPUT_SAMPLE_RATE = 48000;
constexpr int SPU_OUTPUT_BUFFER_FRAMES = 2048;

// Layout of getTotals(). Must match MelonEmulator.AUDIO_TOTAL_*
enum Total
{
    TOTAL_CALLBACKS = 0,
    TOTAL_CALLBACK_FRAMES,
    TOTAL_UNDERRUN_CALLBACKS,
    TOTAL_SILENCE_FRAMES,
    // Since the last performance capture started
    TOTAL_MAX_CALLBACK_GAP_NS,
    TOTAL_MAX_CALLBACK_FRAMES,
    // -1 if no stream so far could report them
    TOTAL_XRUNS,
    TOTAL_STREAM_OPENS,
    TOTAL_STREAM_ERRORS,
    // The part of TOTAL_UNDERRUN_CALLBACKS and TOTAL_SILENCE_FRAMES that happened while fast-forwarding
    TOTAL_FAST_FORWARD_UNDERRUN_CALLBACKS,
    TOTAL_FAST_FORWARD_SILENCE_FRAMES,
    // The output stream's AAudio buffer size now and when the stream opened, in frames. The last stream's if none is open, -1 if there never was
    // one
    TOTAL_BUFFER_SIZE,
    TOTAL_BUFFER_SIZE_AT_OPEN,
    // How many times a stream's buffer grew after an xrun
    TOTAL_BUFFER_GROWS,
    // Frames of silence the callback played while priming (waiting for the ring to fill after a start or after it ran dry). Not underruns
    TOTAL_PRIME_WAIT_FRAMES,
    // How many times the ring ran dry, so the callback primed again
    TOTAL_REPRIMES,
    // How many times the callback trimmed the ring when priming ended, and the frames it dropped
    TOTAL_PRIME_TRIMS,
    TOTAL_PRIME_TRIM_FRAMES,
    // TOTAL_PRIME_WAIT_FRAMES, TOTAL_REPRIMES, TOTAL_PRIME_TRIMS and TOTAL_PRIME_TRIM_FRAMES since the emulation session's audio was set up
    // (onAudioSessionStart())
    TOTAL_SESSION_PRIME_WAIT_FRAMES,
    TOTAL_SESSION_REPRIMES,
    TOTAL_SESSION_PRIME_TRIMS,
    TOTAL_SESSION_PRIME_TRIM_FRAMES,
    TOTAL_COUNT,
};

extern std::atomic_bool enabled;

inline bool isEnabled()
{
    return enabled.load(std::memory_order_relaxed);
}

void setDebugLogEnabled(bool isEnabled);
/**
 * Also resets the maxima reported by getTotals() when [isEnabled] is true.
 */
void setPerfCaptureEnabled(bool isEnabled);

struct CallbackCounters
{
    std::atomic<int64_t> callbacks { 0 };
    std::atomic<int64_t> frames { 0 };
    std::atomic<int64_t> underruns { 0 };
    std::atomic<int64_t> silenceFrames { 0 };
    std::atomic<int64_t> fastForwardUnderruns { 0 };
    std::atomic<int64_t> fastForwardSilenceFrames { 0 };
    // The priming counters below are counted even while the diagnostics are off: they only change while priming, which is rare
    std::atomic<int64_t> primeWaitFrames { 0 };
    std::atomic<int64_t> reprimes { 0 };
    std::atomic<int64_t> primeTrims { 0 };
    std::atomic<int64_t> primeTrimFrames { 0 };
    // Taken by the debug log every second
    std::atomic<int64_t> windowMaxGapNs { 0 };
    std::atomic<int64_t> windowMaxFrames { 0 };
    // Reset when a performance capture starts
    std::atomic<int64_t> sessionMaxGapNs { 0 };
    std::atomic<int64_t> sessionMaxFrames { 0 };
};

extern CallbackCounters counters;

/**
 * The output callback's drift control, as of its last callback (only updated while [isEnabled]).
 */
struct DriftState
{
    // How far the skew is from its base, in parts per million
    std::atomic<int32_t> skewPpm { 0 };
    // The low-pass filtered ring fill the control steers by, in frames. -1 while it isn't steering (muted or unlimited fast-forward)
    std::atomic<int32_t> filteredFill { -1 };
};

extern DriftState driftState;
// Bumped whenever the time since the last callback stops meaning anything (stream started or paused, diagnostics turned on)
extern std::atomic<uint32_t> callbackClockGeneration;

/**
 * Owned by one output callback object and only touched by its callback.
 */
struct CallbackClock
{
    int64_t lastCallbackNs = 0;
    uint32_t generation = 0;
};

inline void storeMax(std::atomic<int64_t>& target, int64_t value)
{
    int64_t current = target.load(std::memory_order_relaxed);
    while (value > current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {}
}

/**
 * Output callback, on entry. Only call while [isEnabled].
 */
inline void onCallback(CallbackClock& clock, int32_t numFrames)
{
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t nowNs = (int64_t) now.tv_sec * 1000000000LL + now.tv_nsec;

    counters.callbacks.fetch_add(1, std::memory_order_relaxed);
    counters.frames.fetch_add(numFrames, std::memory_order_relaxed);
    storeMax(counters.windowMaxFrames, numFrames);
    storeMax(counters.sessionMaxFrames, numFrames);

    uint32_t generation = callbackClockGeneration.load(std::memory_order_relaxed);
    if (generation == clock.generation && clock.lastCallbackNs != 0)
    {
        int64_t gapNs = nowNs - clock.lastCallbackNs;
        storeMax(counters.windowMaxGapNs, gapNs);
        storeMax(counters.sessionMaxGapNs, gapNs);
    }
    clock.generation = generation;
    clock.lastCallbackNs = nowNs;
}

/**
 * Output callback: the emulator had fewer than [numFrames] frames ready, so [missingFrames] were filled with silence or a repeated sample. Only
 * call while [isEnabled].
 */
inline void onUnderrun(int32_t missingFrames, bool isFastForward)
{
    counters.underruns.fetch_add(1, std::memory_order_relaxed);
    counters.silenceFrames.fetch_add(missingFrames, std::memory_order_relaxed);
    if (isFastForward)
    {
        counters.fastForwardUnderruns.fetch_add(1, std::memory_order_relaxed);
        counters.fastForwardSilenceFrames.fetch_add(missingFrames, std::memory_order_relaxed);
    }
}

/**
 * Output callback, after it set the skew. [adjustment] is the skew's relative distance from its base. Only call while [isEnabled].
 */
inline void onDriftUpdate(double adjustment, int filteredFill)
{
    driftState.skewPpm.store((int32_t) (adjustment * 1000000.0), std::memory_order_relaxed);
    driftState.filteredFill.store(filteredFill, std::memory_order_relaxed);
}

/**
 * Output callback: it grew the stream's buffer from [oldSize] to [newSize] frames after the xrun count reached [xruns]. Lock-free. The change is
 * logged later by the emulator thread (debug log) and the perf logger (stream events).
 */
void onBufferSizeChanged(int32_t oldSize, int32_t newSize, int32_t xruns);

/**
 * Output callback: it played [numFrames] of silence while priming. Called whether or not the diagnostics are on.
 */
inline void onPrimeWait(int32_t numFrames)
{
    counters.primeWaitFrames.fetch_add(numFrames, std::memory_order_relaxed);
}

/**
 * Output callback: the ring ran dry, so it primes again. Called whether or not the diagnostics are on.
 */
inline void onReprime()
{
    counters.reprimes.fetch_add(1, std::memory_order_relaxed);
}

/**
 * Output callback: priming ended with the ring over the target, so it dropped [droppedFrames]. Called whether or not the diagnostics are on.
 */
inline void onPrimeTrim(int droppedFrames)
{
    counters.primeTrims.fetch_add(1, std::memory_order_relaxed);
    counters.primeTrimFrames.fetch_add(droppedFrames, std::memory_order_relaxed);
}

/**
 * A new emulation session set its audio up (MelonDSAndroid::setupAudio()). The TOTAL_SESSION_* counts start from here.
 */
void onAudioSessionStart();

// Stream lifecycle. Called with audioOutputMutex held (MelonDSAudio.cpp), except onStreamError, which runs on Oboe's error thread

/**
 * [bufferSizeLimit] is the largest size the buffer may grow to, [isBufferTuned] whether it grows on xruns (low latency streams).
 */
void onStreamOpened(oboe::AudioStream& stream, int requestedLatency, const char* reason, int32_t bufferSizeLimit, bool isBufferTuned);
void onStreamOpenFailed(oboe::Result result, int requestedLatency, const char* reason);
void onStreamClosing(oboe::AudioStream& stream);
// Before requestStart(). Callbacks still running from before a pause then can't bridge the pause
void onStreamStarting();
// After requestStart() succeeded
void onStreamStarted();
void onStreamPaused();
void onStreamError(oboe::Result result);
/**
 * Called with audioOutputMutex held, by MelonDSAndroid::pollAudioOutputXRuns().
 */
void onXRunCount(oboe::AudioStream& stream);

/**
 * Emulator thread, once per frame while [isEnabled]. [fillAfterFrame] is the SPU ring's fill level (in frames) right after the frame ran, the
 * high point. [fillBeforeNextFrame] is the one after the frame's sleep, the low point.
 */
struct FrameSample
{
    // -1 when the frame wasn't sampled (the diagnostics were off when it ran)
    int64_t fill = -1;
    // Frames the callback filled with silence or repeats since the previous frame
    int64_t silenceFrames = 0;
    // The ring was full after the frame, so emulator samples were dropped
    bool isBufferFull = false;
    // The output skew's distance from its base (AudioDiagnostics::DriftState::skewPpm)
    int64_t skewPpm = 0;
    // The speed paced fast-forward really reached (MelonDSAndroid::getAudioAchievedSpeed()), in thousandths. -1 if unknown or not fast-forwarding
    int64_t achievedSpeedMilli = -1;
};

FrameSample onFrame(int fillAfterFrame, int fillBeforeNextFrame);

/**
 * Emulator thread, from the debug log's heartbeat. Logs this second's audio line: callback count, frames and timing, underruns and silence
 * frames, xruns, the ring's fill (min and avg of the low points, max of the high points) with the frames that ended with it full, the drift
 * control's skew and filtered fill, fast-forward's achieved speed, and the stream's buffer size. Also logs the buffer size changes since the last line.
 */
void logSecond(uint32_t frame);

/**
 * Emulator thread (or while it's parked). Starts the per-second stats from scratch and logs the current stream's facts.
 */
void resetForDebugLog();
// resetForDebugLog() runs on recording start and on game start, so the stream's facts are only logged if they weren't since the stream opened

/**
 * Fills [totals] (TOTAL_COUNT entries). Cumulative since the process started, apart from the maxima.
 */
void getTotals(int64_t* totals);

/**
 * The current stream's facts as "key=value" pairs separated by spaces, or an empty string if no stream is open.
 */
std::size_t getStreamInfo(char* buffer, std::size_t size);

constexpr std::size_t MAX_STREAM_INFO_LENGTH = 320;
constexpr std::size_t MAX_PENDING_EVENTS = 32;

struct StreamEvent
{
    // CLOCK_MONOTONIC
    int64_t timestampNs;
    char name[32];
    char detail[MAX_STREAM_INFO_LENGTH];
};

/**
 * Takes up to [maxEvents] of the stream events (opens, failures, errors) recorded while a performance capture was on, oldest first. Returns how
 * many were copied into [events]. Events beyond MAX_PENDING_EVENTS are dropped.
 */
std::size_t takeEvents(StreamEvent* events, std::size_t maxEvents);

}

#endif //AUDIODIAGNOSTICS_H
