#ifndef MELONDS_OBOECALLBACK_H
#define MELONDS_OBOECALLBACK_H

#include <oboe/Oboe.h>
#include <fstream>
#include "MelonInstance.h"
#include "debug/AudioDiagnostics.h"

/**
 * Steers the SPU output ring towards a target fill by nudging the output skew (the blip_buf resampling ratio) a little, like RetroArch's dynamic
 * rate control: skew = base * (1 + adjustment). A higher skew makes fewer output frames per emulated second, so a ring that's too full gets a
 * positive adjustment. Only used from the output callback.
 */
class AudioDriftController {
public:
    // 20 ms at the 48 kHz output rate. Plain mode streams (large bursts) may use another (OboeCallback::setBurstFrames())
    static constexpr double TARGET_FILL_FRAMES = 960.0;
    // Time constant of the fill's low-pass filter. The fill saw-tooths by ~300 frames every video frame (the emulator makes a frame's audio in
    // ~10 ms, then idles), and the filter keeps that from wobbling the pitch
    static constexpr double FILL_FILTER_SECONDS = 0.5;
    // Adjustment at a fill error the size of the target (an empty ring, or one twice the target). With the filter above this makes a critically
    // damped loop that settles within 5% of the target in about 3 s
    static constexpr double PROPORTIONAL_GAIN = 0.01;
    // The integral term catches up with the proportional one after this long at a constant error. It's what makes the fill settle on the target
    // when the base rate is a bit off
    static constexpr double INTEGRAL_SECONDS = 20.0;
    // The adjustment stays within ±0.5% (about 9 cents)
    static constexpr double MAX_ADJUSTMENT = 0.005;
    // Below this part of the target, the raw fill counts as starving: the filtered fill (which lags) is pulled down to the raw one, the skew
    // can't go above its base and the integral holds. The fill's normal low point sits only ~200-300 frames under the filtered fill, well
    // above half the target
    static constexpr double STARVING_FILL_FRACTION = 0.5;

    void reset();
    void setTargetFill(double frames) { targetFill = frames; }
    /**
     * [fill] is the ring's fill after this callback read [numFrames] from it. Returns the new adjustment.
     */
    double update(int fill, int32_t numFrames, int32_t sampleRate);
    double getFilteredFill() const { return filteredFill; }

private:
    double targetFill = TARGET_FILL_FRAMES;
    bool hasFill = false;
    double filteredFill = 0;
    double integral = 0;
    // The filter coefficient for callbacks of filterFrames frames at filterSampleRate
    int32_t filterFrames = 0;
    int32_t filterSampleRate = 0;
    double filterCoefficient = 0;
};

/**
 * Grows the AAudio buffer of a low latency stream by a burst for every new xrun, up to a limit. It never shrinks it. Only used from the output
 * callback, apart from [enable], which is called before the stream starts.
 */
class AudioBufferTuner {
public:
    void enable(int32_t burstFrames, int32_t maxBufferFrames, int32_t xruns, int32_t sampleRate);
    inline bool isActive() const { return active; }
    void onCallback(oboe::AudioStream& stream, int32_t numFrames);

private:
    bool active = false;
    int32_t burstFrames = 0;
    int32_t maxBufferFrames = 0;
    int32_t lastXRuns = 0;
    // The xrun count is polled about every 100 ms
    int32_t pollIntervalFrames = 0;
    int32_t framesUntilPoll = 0;
};

class OboeCallback : public oboe::AudioStreamCallback {
private:
    int _volume;
    void (*onErrorCallback)(oboe::AudioStream* stream);
    std::ostream* _recordingStream;
    float audioSampleFrac;
    AudioDiagnostics::CallbackClock diagnosticsClock;
    AudioDriftController driftController;
    AudioBufferTuner bufferTuner;
    // What the drift control state belongs to. It restarts when any of them changes
    const MelonDSAndroid::MelonInstance* driftInstance = nullptr;
    uint32_t driftRateGeneration = 0;
    bool isDriftSteering = false;
    // Priming: while unprimed, the callback plays silence without reading the ring until the fill reaches the prime target (the reprime one
    // after the ring ran dry). It starts unprimed, and goes unprimed again when the stream (re)starts, the instance changes, or the ring runs dry
    bool isPrimed = false;
    bool isRepriming = false;
    const MelonDSAndroid::MelonInstance* primeInstance = nullptr;
    uint32_t primeStartGeneration = 0;
    // A (re)started stream that reached the prime target keeps priming until the emulator thread trimmed the ring at the start of a frame
    // (MelonDSAndroid::requestAudioOutputTrimBeforeFrame())
    bool isWaitingForTrimBeforeFrame = false;
    uint32_t trimBeforeFrameRequest = 0;

    // Set from the stream's burst before it starts (setBurstFrames()). Priming mode (small bursts) primes, reprimes and trims at the fixed
    // thresholds below. Plain mode (large bursts) does none of it, like before priming existed, and only steers, to a target that leaves room
    // for a burst
    bool isPrimingMode = true;
    // A callback larger than this never reprimes when it comes up short. It's just padded
    int32_t maxRepriminingCallbackFrames = MIN_REPRIMING_CALLBACK_LIMIT_FRAMES;

public:
    // Priming mode thresholds: prime at 960, reprime at 480, trim to 960 above 1200 when repriming ends (and for fast-forward and mute changes)
    static constexpr int PRIME_FRAMES = 960;
    static constexpr int REPRIME_FRAMES = 480;
    static constexpr int TRIM_THRESHOLD_FRAMES = 1200;
    static constexpr int TRIM_KEEP_FRAMES = 960;
    // A (re)started stream's ring is trimmed to this at the start of a frame, the low point. The low point sits ~130 frames under the filtered fill
    // at normal speed and ~70 at 2x fast-forward, so this starts the drift control close to its target
    static constexpr int TRIM_BEFORE_FRAME_KEEP_FRAMES = 840;
    // Streams with bursts up to this many frames use priming mode
    static constexpr int32_t MAX_PRIMING_BURST_FRAMES = 256;
    // Callbacks up to max(4 bursts, this) may reprime
    static constexpr int32_t MIN_REPRIMING_CALLBACK_LIMIT_FRAMES = 480;
    // The SPU ring holds one frame less than its 2048
    static constexpr int RING_USABLE_FRAMES = AudioDiagnostics::SPU_OUTPUT_BUFFER_FRAMES - 1;

    static bool isPrimingModeBurst(int32_t burstFrames) { return burstFrames > 0 && burstFrames <= MAX_PRIMING_BURST_FRAMES; }

    std::weak_ptr<MelonDSAndroid::MelonInstance> activeInstance;

    OboeCallback(int volume, void (*onErrorCallback)(oboe::AudioStream* stream)) : OboeCallback(volume, onErrorCallback, nullptr) { };
    OboeCallback(int volume, void (*onErrorCallback)(oboe::AudioStream* stream), std::ostream* recordingStream);
    /**
     * Turns on the buffer growth for a low latency stream. Must be called before the stream starts.
     */
    void enableBufferTuning(int32_t burstFrames, int32_t maxBufferFrames, int32_t xruns, int32_t sampleRate);
    /**
     * Picks priming or plain mode from the stream's burst size. Must be called before the stream starts.
     */
    void setBurstFrames(int32_t burstFrames);
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream, void *audioData, int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result result) override;

private:
    int getNumSamplesOut(int len);
    // [canSteer] is false while the callback is priming: the skew then stays at its base and the drift control restarts once it's primed
    void updateOutputSkew(MelonDSAndroid::MelonInstance& instance, int32_t numFrames, int32_t sampleRate, bool canSteer, bool isDiagnosticsEnabled);
};


#endif //MELONDS_OBOECALLBACK_H
