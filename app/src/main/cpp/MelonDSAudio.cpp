#include "MelonDSAudio.h"
#include "MicInputOboeCallback.h"
#include "debug/AudioDiagnostics.h"
#include "mic_blow.h"
#include "OboeCallback.h"
#include <oboe/Oboe.h>
#include <atomic>
#include <mutex>

#define MIC_BUFFER_SIZE 2048

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
std::mutex micBufferMutex;
int actualMicSource = 0;
bool isMicInputEnabled = true;
bool isMicOn = false;
int micBufferReadPos = 0;

namespace MelonDSAndroid
{
    // AUDIO OUTPUT

    void resetAudioOutputStream(oboe::AudioStream* erroredStream);

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
            audioStream->setBufferSizeInFrames(std::min(audioStream->getBufferCapacityInFrames(), 2048));
            AudioDiagnostics::onStreamOpened(*audioStream, audioLatency, reason);
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

        AudioDiagnostics::onStreamStarting();
        oboe::Result result = audioStream->requestStart();
        if (result == oboe::Result::ErrorDisconnected) {
            recreateAudioOutputStream(audioLatency, volume, "disconnected on start");
            if (!audioStream)
                return;
            AudioDiagnostics::onStreamStarting();
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
            AudioDiagnostics::onStreamStarting();
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

        muteFastForwardAudio = audioSettings.muteFastForwardAudio;

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

    void setAudioFastForwardActive(bool active)
    {
        isAudioFastForwardActive = active;
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
        muteFastForwardAudio = false;
        // Forget the settings so that startAudio() doesn't reopen a stream with no emulator to feed it
        currentAudioSettings = { };
        cleanupAudioOutputStream();
        cleanupMicInputStream();
    }

    void startAudio()
    {
        std::lock_guard<std::mutex> lock(audioOutputMutex);

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