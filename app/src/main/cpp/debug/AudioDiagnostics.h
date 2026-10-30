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
 * a frame, the callback reads it (both under the SPU's own lock). Its fill level is sampled on the emulator thread, never in the callback.
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
    // Taken by the debug log every second
    std::atomic<int64_t> windowMaxGapNs { 0 };
    std::atomic<int64_t> windowMaxFrames { 0 };
    // Reset when a performance capture starts
    std::atomic<int64_t> sessionMaxGapNs { 0 };
    std::atomic<int64_t> sessionMaxFrames { 0 };
};

extern CallbackCounters counters;
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

// Stream lifecycle. Called with audioOutputMutex held (MelonDSAudio.cpp), except onStreamError, which runs on Oboe's error thread

void onStreamOpened(oboe::AudioStream& stream, int requestedLatency, const char* reason);
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
};

FrameSample onFrame(int fillAfterFrame, int fillBeforeNextFrame);

/**
 * Emulator thread, from the debug log's heartbeat. Logs this second's audio line: callback count, frames and timing, underruns and silence
 * frames, xruns, and the ring's fill (min and avg of the low points, max of the high points) with the frames that ended with it full.
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
