#include "MelonDSAudio.h"
#include "MicInputOboeCallback.h"
#include "debug/AudioDiagnostics.h"
#include "mic_blow.h"
#include "OboeCallback.h"
#include "renderer/FramePacing.h"
#include <oboe/Oboe.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>
#include <mutex>

#define MIC_BUFFER_SIZE 2048

// The largest output buffer used (it's also capped by the stream's capacity)
static constexpr int32_t MAX_OUTPUT_BUFFER_FRAMES = 2048;
// Where a low latency output buffer starts, in bursts. The worst gap between two callbacks measured on the Fold was 4.7 ms, a bit over two
// 2 ms bursts
static constexpr int32_t INITIAL_LOW_LATENCY_BUFFER_BURSTS = 3;

std::weak_ptr<MelonDSAndroid::MelonInstance> activeInstance;

// Guards audioStream, outputCallback and isAudioRunning. They are touched from the JNI thread and
// from the thread Oboe spawns to report stream errors (resetAudioOutputStream()). Never taken
// in the real-time data callback.
std::mutex audioOutputMutex;
std::shared_ptr<oboe::AudioStream> audioStream;
std::shared_ptr<OboeCallback> outputCallback;
// Whether the app wants audio playing. A stream reset after a device disconnect must not start
// playback while the emulator is paused.
bool isAudioRunning = false;

std::shared_ptr<oboe::AudioStream> micInputStream;
std::shared_ptr<MicInputOboeCallback> micInputCallback;

MelonDSAndroid::AudioSettings currentAudioSettings;
// Read lock-free by the output data callback to decide whether to output silence while fast-forwarding
std::atomic_bool isAudioFastForwardActive = false;
std::atomic_bool muteFastForwardAudio = false;
std::atomic<float> audioFastForwardSpeed = 0;
std::atomic<uint32_t> audioRateGeneration = 0;
std::atomic<uint32_t> audioStartGeneration = 0;
// Whether the current output stream trims the ring (priming mode, small bursts: OboeCallback::isPrimingModeBurst()). Set when it opens
std::atomic_bool areAudioOutputTrimsEnabled = true;
// Set when fast-forward ends, for the emulator thread to trim once more after the frame it was running. That trim only fires this far over
// the usual threshold
static constexpr int32_t POST_FRAME_TRIM_EXTRA_FRAMES = 400;
std::atomic_bool isAudioTrimAfterFramePending = false;
// The trim a (re)started stream's callback asks for when it finishes priming (OboeCallback): the callback bumps the request, the emulator thread
// trims at the start of its next frame and stores the request it answered
std::atomic<uint32_t> audioTrimBeforeFrameRequest = 0;
std::atomic<uint32_t> audioTrimBeforeFrameDone = 0;
// The speed paced fast-forward really reaches, smoothed, in thousandths (low 32 bits), with the audioRateGeneration it was measured under (high
// 32 bits). Only the emulator thread writes it. A speed of 0 means unknown
std::atomic<uint64_t> audioAchievedSpeed = 0;
// Time constant of the achieved speed's smoothing. Short enough to follow a slow scene within a fraction of a second, long enough not to wobble
// with the frame-to-frame pacing
static constexpr double ACHIEVED_SPEED_SECONDS = 0.35;
// The speed is only published once it covers this much wall time
static constexpr double ACHIEVED_SPEED_MIN_SECONDS = 0.1;
// Emulator thread only. Emulated frames and wall time, each decayed with ACHIEVED_SPEED_SECONDS, so their ratio is the frame rate over about
// that long. Measured over wall time rather than per frame, since the frames come in bursts (work, then sleep)
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
std::mutex micBufferMutex;
int actualMicSource = 0;
bool isMicInputEnabled = true;
bool isMicOn = false;
int micBufferReadPos = 0;

namespace MelonDSAndroid
{
    // AUDIO OUTPUT

    void resetAudioOutputStream(oboe::AudioStream* erroredStream);

    // Trims a ring left too full by muted or unlimited fast-forward back to about the target, since the drift control would take seconds to
    // drain it at its limit. For fast-forward and mute changes on a stream that keeps playing. A stream that (re)starts is trimmed by its
    // callback instead, when it finishes priming, since the emulator keeps filling the ring while requestStart() runs. Callers trim before
    // changing the state, so a primed callback can't play the stale ring first, then bump audioRateGeneration. Must be called with
    // audioOutputMutex held. The SPU's audio lock is taken inside it, which is fine: nothing takes audioOutputMutex while holding that one
    static int trimOutput(MelonInstance& instance)
    {
        if (!areAudioOutputTrimsEnabled.load(std::memory_order_relaxed))
            return 0;
        return instance.trimAudioOutput(OboeCallback::TRIM_THRESHOLD_FRAMES, OboeCallback::TRIM_KEEP_FRAMES);
    }

    static void trimActiveOutput()
    {
        if (std::shared_ptr<MelonInstance> instance = activeInstance.lock())
            trimOutput(*instance);
    }

    // Before every requestStart() of the output stream. Must be called with audioOutputMutex held. The callback primes again unless the
    // stream is already playing (startAudio() can be called while it is)
    static void onOutputStreamStarting()
    {
        AudioDiagnostics::onStreamStarting();
        if (audioStream && audioStream->getState() != oboe::StreamState::Started && audioStream->getState() != oboe::StreamState::Starting)
            audioStartGeneration++;
    }

    // setupAudioOutputStream(), cleanupAudioOutputStream() and recreateAudioOutputStream() must be called with audioOutputMutex held. [reason]
    // is for the audio diagnostics
    void setupAudioOutputStream(int audioLatency, int volume, const char* reason)
    {
        oboe::PerformanceMode performanceMode;
        switch (audioLatency) {
            case 0:
                performanceMode = oboe::PerformanceMode::LowLatency;
                break;
            case 1:
                performanceMode = oboe::PerformanceMode::None;
                break;
            case 2:
                performanceMode = oboe::PerformanceMode::PowerSaving;
                break;
            default:
                performanceMode = oboe::PerformanceMode::None;
        }

        outputCallback = std::make_shared<OboeCallback>(volume, resetAudioOutputStream);

        outputCallback->activeInstance = activeInstance;

        oboe::AudioStreamBuilder streamBuilder;
        streamBuilder.setChannelCount(2);
        streamBuilder.setSampleRate(48000);
        streamBuilder.setFormat(oboe::AudioFormat::I16);
        streamBuilder.setFormatConversionAllowed(true);
        streamBuilder.setDirection(oboe::Direction::Output);
        streamBuilder.setPerformanceMode(performanceMode);
        streamBuilder.setSharingMode(oboe::SharingMode::Shared);
        streamBuilder.setUsage(oboe::Usage::Game);
        streamBuilder.setDataCallback(outputCallback);
        streamBuilder.setErrorCallback(outputCallback);

        oboe::Result result = streamBuilder.openStream(audioStream);
        if (result != oboe::Result::OK) {
            Log(Error, "Failed to init audio stream");
            outputCallback = nullptr;
            AudioDiagnostics::onStreamOpenFailed(result, audioLatency, reason);
        } else {
            audioStream->setPerformanceHintEnabled(true);

            // A low latency stream starts with a small buffer that grows by a burst on every xrun, up to the size every stream used to get.
            // The other modes, or a stream that can't count its xruns, get that size from the start
            int32_t bufferSizeLimit = std::min(audioStream->getBufferCapacityInFrames(), MAX_OUTPUT_BUFFER_FRAMES);
            int32_t burstFrames = audioStream->getFramesPerBurst();
            // Small bursts prime, reprime and trim. Large ones (no low latency path, Bluetooth, say) only steer, as before priming existed
            outputCallback->setBurstFrames(burstFrames);
            areAudioOutputTrimsEnabled = OboeCallback::isPrimingModeBurst(burstFrames);
            bool isBufferTuned = audioStream->getPerformanceMode() == oboe::PerformanceMode::LowLatency && burstFrames > 0
                    && audioStream->isXRunCountSupported();
            audioStream->setBufferSizeInFrames(isBufferTuned ? std::min(burstFrames * INITIAL_LOW_LATENCY_BUFFER_BURSTS, bufferSizeLimit) : bufferSizeLimit);
            if (isBufferTuned) {
                oboe::ResultWithValue<int32_t> xruns = audioStream->getXRunCount();
                outputCallback->enableBufferTuning(burstFrames, bufferSizeLimit, xruns ? xruns.value() : 0, audioStream->getSampleRate());
            }
            AudioDiagnostics::onStreamOpened(*audioStream, audioLatency, reason, bufferSizeLimit, isBufferTuned);
        }
    }

    void cleanupAudioOutputStream()
    {
        if (audioStream) {
            AudioDiagnostics::onStreamClosing(*audioStream);
            oboe::StreamState state = audioStream->getState();
            if (state < oboe::StreamState::Closing) {
                audioStream->requestStop();
                audioStream->close();
            } else if (state == oboe::StreamState::Disconnected) {
                // Disconnected sorts after Closed, but the stream still has to be closed to free its
                // handle. If Oboe's error thread closes it at the same time, the second close() is a no-op
                audioStream->close();
            }

            audioStream = nullptr;
            outputCallback = nullptr;
        }
    }

    void recreateAudioOutputStream(int audioLatency, int volume, const char* reason)
    {
        cleanupAudioOutputStream();
        setupAudioOutputStream(audioLatency, volume, reason);
    }

    // Starts the output stream. Must be called with audioOutputMutex held. A stream that got
    // disconnected without an error callback (e.g. while paused) can't be started again, so it's
    // recreated with the given settings instead
    void startAudioOutputStream(int audioLatency, int volume)
    {
        if (audioStream && audioStream->getState() == oboe::StreamState::Disconnected) {
            recreateAudioOutputStream(audioLatency, volume, "disconnected while stopped");
        }

        if (!audioStream)
            return;

        onOutputStreamStarting();
        oboe::Result result = audioStream->requestStart();
        if (result == oboe::Result::ErrorDisconnected) {
            recreateAudioOutputStream(audioLatency, volume, "disconnected on start");
            if (!audioStream)
                return;
            onOutputStreamStarting();
            result = audioStream->requestStart();
        }

        if (result == oboe::Result::OK)
            AudioDiagnostics::onStreamStarted();
    }

    void resetAudioOutputStream(oboe::AudioStream* erroredStream)
    {
        // Runs on Oboe's error thread, after Oboe has already closed the errored stream, so
        // cleanupAudioOutputStream() won't try to close it again.
        std::lock_guard<std::mutex> lock(audioOutputMutex);

        // The stream was already replaced or cleaned up in the meantime. Nothing to restore
        if (erroredStream != audioStream.get())
            return;

        recreateAudioOutputStream(currentAudioSettings.audioLatency, currentAudioSettings.volume, "restart after error");
        if (audioStream && isAudioRunning) {
            onOutputStreamStarting();
            if (audioStream->requestStart() == oboe::Result::OK)
                AudioDiagnostics::onStreamStarted();
        }
    }

    bool pollAudioOutputXRuns()
    {
        std::unique_lock<std::mutex> lock(audioOutputMutex, std::try_to_lock);
        if (!lock.owns_lock())
            return false;

        if (audioStream)
            AudioDiagnostics::onXRunCount(*audioStream);
        return true;
    }

    // MICROPHONE

    void setupMicInputStream()
    {
        micInputCallback = std::make_shared<MicInputOboeCallback>(MIC_BUFFER_SIZE, micBufferMutex);
        oboe::AudioStreamBuilder micStreamBuilder;
        micStreamBuilder.setChannelCount(1);
        micStreamBuilder.setFramesPerCallback(1024);
        micStreamBuilder.setSampleRate(48000);
        micStreamBuilder.setFormat(oboe::AudioFormat::I16);
        micStreamBuilder.setFormatConversionAllowed(true);
        micStreamBuilder.setDirection(oboe::Direction::Input);
        micStreamBuilder.setInputPreset(oboe::InputPreset::VoiceRecognition);
        micStreamBuilder.setPerformanceMode(oboe::PerformanceMode::None);
        micStreamBuilder.setSharingMode(oboe::SharingMode::Exclusive);
        micStreamBuilder.setUsage(oboe::Usage::Game);
        micStreamBuilder.setDataCallback(micInputCallback);

        oboe::Result micResult = micStreamBuilder.openStream(micInputStream);
        if (micResult != oboe::Result::OK)
        {
            actualMicSource = 1;
            Log(Error, "Failed to init mic audio stream");
            micInputCallback = nullptr;
        }
    }

    void cleanupMicInputStream()
    {
        if (micInputStream)
        {
            micInputStream->requestStop();
            micInputStream->close();

            micInputStream = nullptr;

            std::lock_guard<std::mutex> lock(micBufferMutex);
            micInputCallback = nullptr;
        }
    }

    void startMicStreamIfAllowed()
    {
        if (actualMicSource == 2 && micInputStream && isMicInputEnabled && isMicOn)
            micInputStream->requestStart();
    }

    void userEnableMic()
    {
        isMicInputEnabled = true;
        startMicStreamIfAllowed();
    }

    void userDisableMic()
    {
        isMicInputEnabled = false;
        if (micInputStream)
            micInputStream->requestStop();
    }

    void enableMic()
    {
        isMicOn = true;
        startMicStreamIfAllowed();
    }

    void disableMic()
    {
        isMicOn = false;
        if (micInputStream)
            micInputStream->requestStop();
    }

    int readMic(s16* data, int maxlength)
    {
        int micSource = actualMicSource;
        if (!isMicInputEnabled)
        {
            micSource = 0;
        }

        if (micSource == 0)
        {
            memset(data, 0, maxlength * sizeof(s16));
            return maxlength;
        }

        int micBufferLength;
        s16* micBuffer;

        if (micSource == 2)
        {
            micBufferMutex.lock();
            if (!micInputCallback)
            {
                micBufferMutex.unlock();
                memset(data, 0, maxlength * sizeof(s16));
                return maxlength;
            }
            micBufferLength = MIC_BUFFER_SIZE / sizeof(s16);
            micBuffer = micInputCallback->buffer;
        }
        else
        {
            micBufferLength = sizeof(mic_blow) / sizeof(s16);
            micBuffer = (s16*) &mic_blow[0];
        }

        int readlength = 0;
        while (readlength < maxlength)
        {
            int thislen = maxlength - readlength;
            if ((micBufferReadPos + thislen) > micBufferLength)
                thislen = micBufferLength - micBufferReadPos;

            if (micSource == 2)
            {
                if (thislen > micInputCallback->bufferCount)
                    thislen = micInputCallback->bufferCount;

                micInputCallback->bufferCount -= thislen;
            }

            if (!thislen)
                break;

            memcpy(data, &micBuffer[micBufferReadPos], thislen * sizeof(s16));
            data += thislen;
            micBufferReadPos += thislen;
            if (micBufferReadPos >= micBufferLength)
                micBufferReadPos -= micBufferLength;

            readlength += thislen;
        }

        if (micSource == 2)
            micBufferMutex.unlock();

        return readlength;
    }

    // GENERAL

    void setupAudio(AudioSettings audioSettings)
    {
        std::lock_guard<std::mutex> lock(audioOutputMutex);

        // A previous session's streams may still be around (and running). Replacing them without
        // closing them first destroys a stream while its callback is still using it
        cleanupAudioOutputStream();
        cleanupMicInputStream();

        isAudioRunning = false;
        isMicOn = false;
        isAudioFastForwardActive = false;
        audioFastForwardSpeed = 0;
        audioRateGeneration++;
        AudioDiagnostics::onAudioSessionStart();
        muteFastForwardAudio = audioSettings.muteFastForwardAudio;
        actualMicSource = audioSettings.micSource;
        currentAudioSettings = audioSettings;

        if (audioSettings.soundEnabled)
            setupAudioOutputStream(audioSettings.audioLatency, audioSettings.volume, "setup");

        if (audioSettings.micSource == 2)
            setupMicInputStream();
    }

    void updateAudioSettings(AudioSettings audioSettings)
    {
        std::lock_guard<std::mutex> lock(audioOutputMutex);

        // Unmuted mid fast-forward (from the pause menu): steering starts with the ring muted fast-forward filled. Trimmed before unmuting
        if (isAudioFastForwardActive && muteFastForwardAudio && !audioSettings.muteFastForwardAudio) {
            trimActiveOutput();
            muteFastForwardAudio = false;
            audioRateGeneration++;
        } else {
            muteFastForwardAudio = audioSettings.muteFastForwardAudio;
        }

        if (audioSettings.soundEnabled && audioSettings.volume > 0) {
            if (!audioStream) {
                setupAudioOutputStream(audioSettings.audioLatency, audioSettings.volume, "settings change");
            } else if (currentAudioSettings.audioLatency != audioSettings.audioLatency || currentAudioSettings.volume != audioSettings.volume) {
                // Recreate audio stream with new settings
                recreateAudioOutputStream(audioSettings.audioLatency, audioSettings.volume, "settings change");
            }

            // A newly created stream is stopped. Start it if audio is supposed to be playing. Don't
            // call requestStart() on a stream that's already playing, since Oboe resets the
            // performance hint session on every start request
            if (audioStream && isAudioRunning && audioStream->getState() != oboe::StreamState::Started)
                startAudioOutputStream(audioSettings.audioLatency, audioSettings.volume);
        } else if (audioStream) {
            cleanupAudioOutputStream();
        }

        int oldMicSource = actualMicSource;
        actualMicSource = audioSettings.micSource;

        if (oldMicSource == 2 && audioSettings.micSource != 2) {
            // No longer using device mic. Destroy stream
            cleanupMicInputStream();
        } else if (oldMicSource != 2 && audioSettings.micSource == 2) {
            // Now using device mic. Setup stream
            setupMicInputStream();
        }

        currentAudioSettings = audioSettings;
    }

    void setAudioFastForwardActive(bool active, float speedMultiplier)
    {
        // Runs on the JNI thread, never in the output callback
        std::lock_guard<std::mutex> lock(audioOutputMutex);

        // Unlimited fast-forward is stored as 0, whatever the caller passed
        float speed = active && speedMultiplier > 0 ? speedMultiplier : 0;
        bool wasActive = isAudioFastForwardActive.load(std::memory_order_relaxed);
        // It's called again with the same state (when the activity is recreated, or for every configuration update), and that must not throw
        // away the drift control's state
        if (active == wasActive && speed == audioFastForwardSpeed.load(std::memory_order_relaxed))
            return;

        // Fast-forward ending (muted or unlimited above all) leaves the ring full, and starting it with sound on begins steering at a new base.
        // Trimmed before the state changes, then the drift control restarts
        trimActiveOutput();
        audioFastForwardSpeed = speed;
        isAudioFastForwardActive = active;
        audioRateGeneration++;
        // The emulator may be in the middle of a fast-forward frame, whose audio lands after the trim above. The caller restores the frame
        // pacing before calling this, so the emulator thread trims once more right after that frame
        if (wasActive && !active)
            // Release: the emulator thread that sees the flag also sees the fast-forward state stored above
            isAudioTrimAfterFramePending.store(true, std::memory_order_release);
    }


    void runPendingAudioOutputTrim(MelonInstance& instance)
    {
        if (!isAudioTrimAfterFramePending.load(std::memory_order_relaxed)) [[likely]]
            return;
        // Acquire pairs with the release in setAudioFastForwardActive()
        if (!isAudioTrimAfterFramePending.exchange(false, std::memory_order_acquire))
            return;

        // Fast-forward was turned back on meanwhile: its own trim already ran
        if (isAudioFastForwardActive.load(std::memory_order_relaxed))
            return;

        // A normal frame's audio can briefly push the ring to ~1500, so this trim only fires on more than that: a ring still overfull from
        // fast-forward (~1700 and up). The drift control restarts from the trimmed fill
        if (!areAudioOutputTrimsEnabled.load(std::memory_order_relaxed))
            return;
        if (instance.trimAudioOutput(OboeCallback::TRIM_THRESHOLD_FRAMES + POST_FRAME_TRIM_EXTRA_FRAMES, OboeCallback::TRIM_KEEP_FRAMES) > 0)
            audioRateGeneration++;
    }

    uint32_t requestAudioOutputTrimBeforeFrame()
    {
        return audioTrimBeforeFrameRequest.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    bool isAudioOutputTrimBeforeFrameDone(uint32_t request)
    {
        // Acquire pairs with the release in runAudioOutputTrimBeforeFrame(), so the callback sees the trimmed ring
        return audioTrimBeforeFrameDone.load(std::memory_order_acquire) == request;
    }

    void runAudioOutputTrimBeforeFrame(MelonInstance& instance)
    {
        // Only this thread writes the done value
        uint32_t request = audioTrimBeforeFrameRequest.load(std::memory_order_relaxed);
        if (request == audioTrimBeforeFrameDone.load(std::memory_order_relaxed)) [[likely]]
            return;

        // The frame's start is the ring's low point, and the callback has been playing silence while it primed, so the AAudio buffer is already
        // full. The fill set here is where the following low points stay, whatever the phase the stream started at. A request left over from a
        // stream that was replaced by a plain mode one (large bursts) is only answered: those never trim
        if (areAudioOutputTrimsEnabled.load(std::memory_order_relaxed))
        {
            int droppedFrames = instance.trimAudioOutput(OboeCallback::TRIM_BEFORE_FRAME_KEEP_FRAMES, OboeCallback::TRIM_BEFORE_FRAME_KEEP_FRAMES);
            if (droppedFrames > 0)
                AudioDiagnostics::onPrimeTrim(droppedFrames);
        }
        audioTrimBeforeFrameDone.store(request, std::memory_order_release);
    }

    static void publishAchievedSpeed(uint32_t rateGeneration, double speed)
    {
        uint64_t speedMilli = speed > 0 ? (uint64_t) std::min(speed * 1000.0 + 0.5, (double) UINT32_MAX) : 0;
        audioAchievedSpeed.store(((uint64_t) rateGeneration << 32) | speedMilli, std::memory_order_relaxed);
    }

    void onEmulatorFrameEnd(double intervalMs, bool isPacedFastForward, float requestedSpeed)
    {
        // A new fast-forward state (or speed, or unmute) bumps the generation, and so does a new session. The value published under the old one
        // is ignored from then on, even if it lands after the bump
        uint32_t rateGeneration = audioRateGeneration.load(std::memory_order_relaxed);
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
        // The speed starts over after the pause. Published now, so the old one isn't logged while the stream starts again
        achievedSpeedMeter.isActive = false;
        publishAchievedSpeed(audioRateGeneration.load(std::memory_order_relaxed), 0);
    }

    float getAudioAchievedSpeed()
    {
        uint64_t value = audioAchievedSpeed.load(std::memory_order_relaxed);
        if ((uint32_t) (value >> 32) != audioRateGeneration.load(std::memory_order_relaxed))
            return 0;
        return (float) (uint32_t) value / 1000.0f;
    }

    // Called from the real-time output callback. Must stay lock-free
    bool shouldMuteAudioOutput()
    {
        return isAudioFastForwardActive.load(std::memory_order_relaxed) && muteFastForwardAudio.load(std::memory_order_relaxed);
    }

    bool isAudioFastForwarding()
    {
        return isAudioFastForwardActive.load(std::memory_order_relaxed);
    }

    float getAudioFastForwardSpeed()
    {
        return audioFastForwardSpeed.load(std::memory_order_relaxed);
    }

    uint32_t getAudioRateGeneration()
    {
        // Acquire pairs with the (sequentially consistent) increments, which all come after the state they announce was stored
        return audioRateGeneration.load(std::memory_order_acquire);
    }

    uint32_t getAudioStartGeneration()
    {
        return audioStartGeneration.load(std::memory_order_relaxed);
    }

    void setAudioActiveInstance(std::shared_ptr<MelonInstance> instance)
    {
        std::lock_guard<std::mutex> lock(audioOutputMutex);

        activeInstance = instance;
        if (outputCallback)
            outputCallback->activeInstance = activeInstance;
    }

    void cleanupAudio()
    {
        std::lock_guard<std::mutex> lock(audioOutputMutex);

        isAudioRunning = false;
        isAudioFastForwardActive = false;
        audioFastForwardSpeed = 0;
        audioRateGeneration++;
        muteFastForwardAudio = false;
        // Forget the settings so that startAudio() doesn't reopen a stream with no emulator to feed it
        currentAudioSettings = { };
        cleanupAudioOutputStream();
        cleanupMicInputStream();
        // Drop the reference to the instance that's about to be destroyed, so that nothing here (trimActiveOutput() from a late fast-forward
        // toggle, say) can end up holding its last reference and destroy it on the wrong thread. Every new session sets
        // it again with setAudioActiveInstance(), after setupAudio(). The callback's copy went with the callback
        activeInstance.reset();
    }

    void startAudio()
    {
        std::lock_guard<std::mutex> lock(audioOutputMutex);

        // A resumed stream primes again, and its callback trims the ring when priming ends
        isAudioRunning = true;

        // A device disconnect closes the stream, and reopening it can fail while the app is in the
        // background. Try again now, otherwise audio stays off for the rest of the session
        if (!audioStream && currentAudioSettings.soundEnabled && currentAudioSettings.volume > 0)
            setupAudioOutputStream(currentAudioSettings.audioLatency, currentAudioSettings.volume, "reopen on start");

        startAudioOutputStream(currentAudioSettings.audioLatency, currentAudioSettings.volume);

        startMicStreamIfAllowed();
    }

    void pauseAudio()
    {
        std::lock_guard<std::mutex> lock(audioOutputMutex);

        isAudioRunning = false;

        if (audioStream) {
            audioStream->requestPause();
            AudioDiagnostics::onStreamPaused();
        }

        if (micInputStream)
            micInputStream->requestStop();
    }
}