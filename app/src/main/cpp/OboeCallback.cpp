#include "OboeCallback.h"
#include "MelonDSAudio.h"
#include "types.h"
#include "Platform.h"
#include "SPU.h"
#include <algorithm>
#include <cmath>

using namespace melonDS;

#define INTERNAL_FRAME_RATE 59.8260982880808f

void AudioDriftController::reset()
{
    hasFill = false;
    filteredFill = 0;
    integral = 0;
}

double AudioDriftController::update(int fill, int32_t numFrames, int32_t sampleRate)
{
    if (numFrames <= 0 || sampleRate <= 0)
        return std::clamp(integral, -MAX_ADJUSTMENT, MAX_ADJUSTMENT);

    // The filter works on elapsed time rather than on the callback count, so it doesn't depend on the burst size
    if (numFrames != filterFrames || sampleRate != filterSampleRate)
    {
        filterFrames = numFrames;
        filterSampleRate = sampleRate;
        filterCoefficient = 1.0 - std::exp(-(double) numFrames / (FILL_FILTER_SECONDS * sampleRate));
    }

    if (hasFill)
    {
        filteredFill += filterCoefficient * (fill - filteredFill);
    }
    else
    {
        filteredFill = fill;
        hasFill = true;
    }

    // The filter lags, so a ring that's running dry could still look too full. Recover from where the fill really is
    bool isStarving = fill < targetFill * STARVING_FILL_FRACTION;
    if (isStarving)
        filteredFill = std::min(filteredFill, (double) fill);

    double error = (filteredFill - targetFill) / targetFill;
    double proportional = PROPORTIONAL_GAIN * error;
    double integralStep = PROPORTIONAL_GAIN / INTEGRAL_SECONDS * error * numFrames / sampleRate;

    // Anti-windup: while the adjustment is at a limit, the integral only moves back towards the middle. It holds while starving, which is a
    // stall rather than a rate error
    double unclamped = proportional + integral + integralStep;
    if (!isStarving && (unclamped < MAX_ADJUSTMENT || integralStep < 0) && (unclamped > -MAX_ADJUSTMENT || integralStep > 0))
        integral = std::clamp(integral + integralStep, -MAX_ADJUSTMENT, MAX_ADJUSTMENT);

    double adjustment = std::clamp(proportional + integral, -MAX_ADJUSTMENT, MAX_ADJUSTMENT);
    // A starving ring must never be drained faster
    if (isStarving)
        adjustment = std::min(adjustment, 0.0);
    return adjustment;
}

void AudioBufferTuner::enable(int32_t burstFrames, int32_t maxBufferFrames, int32_t xruns, int32_t sampleRate)
{
    this->burstFrames = burstFrames;
    this->maxBufferFrames = maxBufferFrames;
    lastXRuns = xruns;
    pollIntervalFrames = std::max(sampleRate / 10, 1);
    framesUntilPoll = pollIntervalFrames;
    active = burstFrames > 0;
}

void AudioBufferTuner::onCallback(oboe::AudioStream& stream, int32_t numFrames)
{
    framesUntilPoll -= numFrames;
    if (framesUntilPoll > 0)
        return;
    framesUntilPoll = pollIntervalFrames;

    // getXRunCount() and get/setBufferSizeInFrames() take Oboe's stream lock, as in Oboe's own LatencyTuner. They only run every ~100 ms or on an xrun
    oboe::ResultWithValue<int32_t> xruns = stream.getXRunCount();
    if (!xruns)
    {
        active = false;
        return;
    }

    int32_t newXRuns = xruns.value() - lastXRuns;
    if (newXRuns <= 0)
        return;
    lastXRuns = xruns.value();

    int32_t oldSize = stream.getBufferSizeInFrames();
    int32_t requestedSize = std::min(oldSize + burstFrames * newXRuns, maxBufferFrames);
    // AAudio allows this from the data callback. It only changes how much of the allocated capacity is used
    oboe::ResultWithValue<int32_t> newSize = stream.setBufferSizeInFrames(requestedSize);
    if (!newSize)
    {
        active = false;
        return;
    }

    if (newSize.value() != oldSize)
        AudioDiagnostics::onBufferSizeChanged(oldSize, newSize.value(), lastXRuns);
    // At the limit (AAudio may clip the size below it)
    if (newSize.value() >= maxBufferFrames || newSize.value() <= oldSize)
        active = false;
}

OboeCallback::OboeCallback(int volume, void (*onErrorCallback)(oboe::AudioStream* stream), std::ostream* recordingStream) : _volume(volume), onErrorCallback(onErrorCallback), _recordingStream(recordingStream) {
    audioSampleFrac = 0;
}

void OboeCallback::setBurstFrames(int32_t burstFrames)
{
    isPrimingMode = isPrimingModeBurst(burstFrames);
    maxRepriminingCallbackFrames = std::max(burstFrames * 4, MIN_REPRIMING_CALLBACK_LIMIT_FRAMES);
    if (isPrimingMode)
    {
        driftController.setTargetFill(AudioDriftController::TARGET_FILL_FRAMES);
    }
    else
    {
        // Room for a couple of bursts, but never more than half the ring with a burst taken off
        double target = std::min(std::max(AudioDriftController::TARGET_FILL_FRAMES, 2.0 * burstFrames), (RING_USABLE_FRAMES - burstFrames) / 2.0);
        driftController.setTargetFill(std::max(target, 48.0));
    }
}

void OboeCallback::enableBufferTuning(int32_t burstFrames, int32_t maxBufferFrames, int32_t xruns, int32_t sampleRate)
{
    bufferTuner.enable(burstFrames, maxBufferFrames, xruns, sampleRate);
}

void OboeCallback::updateOutputSkew(MelonDSAndroid::MelonInstance& instance, int32_t numFrames, int32_t sampleRate, bool canSteer, bool isDiagnosticsEnabled)
{
    // The emulator runs 60 DS frames a second, slightly more than the DS's own rate
    double baseSkew = std::clamp(60.0 / INTERNAL_FRAME_RATE, 0.995, 1.005);
    // Read before the fast-forward state, with acquire: the state is stored before the generation is bumped, so a new generation comes with
    // the new state
    uint32_t rateGeneration = MelonDSAndroid::getAudioRateGeneration();
    bool isSteering = canSteer;
    if (MelonDSAndroid::isAudioFastForwarding())
    {
        // Paced fast-forward plays its audio sped up (and pitched up). Muted fast-forward discards it, and unlimited fast-forward has no
        // known rate to follow, so both keep the plain base without steering
        float speed = MelonDSAndroid::getAudioFastForwardSpeed();
        // A scene too heavy for the requested speed makes less audio per second, so the ring runs dry. Following the speed really reached
        // (MelonDSAndroid::getAudioAchievedSpeed()) bent the pitch audibly, so that's left to time-stretching
        if (speed > 0 && !MelonDSAndroid::shouldMuteAudioOutput())
            baseSkew *= speed;
        else
            isSteering = false;
    }

    if (&instance != driftInstance || rateGeneration != driftRateGeneration || isSteering != isDriftSteering)
    {
        driftController.reset();
        driftInstance = &instance;
        driftRateGeneration = rateGeneration;
        isDriftSteering = isSteering;
    }

    double adjustment = 0;
    if (isSteering)
        adjustment = driftController.update(instance.getAudioOutputBufferFill(), numFrames, sampleRate);

    // Only stored here. The emulator thread applies it between two blip frames
    instance.setAudioOutputSkew(baseSkew * (1.0 + adjustment));

    if (isDiagnosticsEnabled) [[unlikely]]
        AudioDiagnostics::onDriftUpdate(adjustment, isSteering ? (int) driftController.getFilteredFill() : -1);
}

oboe::DataCallbackResult
OboeCallback::onAudioReady(oboe::AudioStream *stream, void *audioData, int32_t numFrames) {
    bool isDiagnosticsEnabled = AudioDiagnostics::isEnabled();
    if (isDiagnosticsEnabled) [[unlikely]]
        AudioDiagnostics::onCallback(diagnosticsClock, numFrames);

    if (bufferTuner.isActive())
        bufferTuner.onCallback(*stream, numFrames);

    auto currentInstance = activeInstance.lock();

    if (!currentInstance)
    {
        driftInstance = nullptr;
        primeInstance = nullptr;
        memset(audioData, 0, numFrames * sizeof(u16) * 2);
        return oboe::DataCallbackResult::Continue;
    }

    uint32_t startGeneration = MelonDSAndroid::getAudioStartGeneration();
    if (currentInstance.get() != primeInstance || startGeneration != primeStartGeneration)
    {
        primeInstance = currentInstance.get();
        primeStartGeneration = startGeneration;
        isPrimed = false;
        isRepriming = false;
        isWaitingForTrimBeforeFrame = false;
    }

    // Muted fast-forward keeps reading (and discarding) the ring below, primed or not
    bool isMuted = MelonDSAndroid::shouldMuteAudioOutput();
    if (!isPrimed && !isPrimingMode)
    {
        // Plain mode (large bursts): play whatever there is, as before priming existed
        isPrimed = true;
        driftController.reset();
    }
    else if (!isPrimed && !isMuted)
    {
        // Wait for the ring to hold enough instead of playing every scrap the emulator makes as it comes: one clean gap rather than a
        // stuttery run of tiny ones
        bool isReady;
        if (isWaitingForTrimBeforeFrame)
            isReady = MelonDSAndroid::isAudioOutputTrimBeforeFrameDone(trimBeforeFrameRequest);
        else
            isReady = currentInstance->getAudioOutputBufferFill() >= (isRepriming ? REPRIME_FRAMES : PRIME_FRAMES);

        // The emulator keeps running while AAudio starts a stream (13-130 ms), so the ring is often full by now. Trimming it here lands at
        // whatever phase of the emulator's frame this is: right after a frame's audio, the next low points sat ~500 frames under the target,
        // and the skew at its limit for seconds. So a (re)started stream keeps priming until the emulator thread trimmed the ring at the start
        // of its next frame (at most a frame away). The silence played meanwhile also fills the AAudio buffer, which a start empties
        if (isReady && !isRepriming && !isWaitingForTrimBeforeFrame)
        {
            trimBeforeFrameRequest = MelonDSAndroid::requestAudioOutputTrimBeforeFrame();
            isWaitingForTrimBeforeFrame = true;
            isReady = false;
        }

        if (!isReady)
        {
            updateOutputSkew(*currentInstance, numFrames, stream->getSampleRate(), false, isDiagnosticsEnabled);
            // Counted even with the diagnostics off (it's rare), so the summary can show the startup prime from before a capture began
            AudioDiagnostics::onPrimeWait(numFrames);
            memset(audioData, 0, numFrames * sizeof(s16) * 2);
            return oboe::DataCallbackResult::Continue;
        }
        isPrimed = true;
        isWaitingForTrimBeforeFrame = false;

        // A reprime ends with whatever the emulator caught up with, which is rarely over the threshold. Trimmed here if it is
        if (isRepriming)
        {
            int droppedFrames = currentInstance->trimAudioOutput(TRIM_THRESHOLD_FRAMES, TRIM_KEEP_FRAMES);
            if (droppedFrames > 0)
                AudioDiagnostics::onPrimeTrim(droppedFrames);
        }
        // The drift control starts from the primed fill
        driftController.reset();
    }

    int len = numFrames;

    int len_in = getNumSamplesOut(len);
    if (len_in > numFrames) len_in = numFrames;

    int num_in = currentInstance->readAudioOutput((s16*) audioData, len_in);
    if (num_in < numFrames) [[unlikely]]
    {
        if (isDiagnosticsEnabled)
            AudioDiagnostics::onUnderrun(numFrames - std::max(num_in, 0), MelonDSAndroid::isAudioFastForwarding());
        // The ring ran dry. Wait until it holds half the target again before playing on. An oversized callback (larger than a few bursts)
        // coming up short says nothing about the ring, so it's only padded
        if (!isMuted && isPrimingMode && numFrames <= maxRepriminingCallbackFrames)
        {
            isPrimed = false;
            isRepriming = true;
            AudioDiagnostics::onReprime();
        }
    }

    updateOutputSkew(*currentInstance, numFrames, stream->getSampleRate(), isPrimed, isDiagnosticsEnabled);

    // When muted (fast-forward with the mute setting on), the SPU output was still read above and is
    // discarded here. Leaving it in the SPU buffer would play it as a stale burst once fast-forward ends
    if (num_in < 1 || isMuted)
    {
        memset(audioData, 0, len * sizeof(s16) * 2);
        return oboe::DataCallbackResult::Continue;
    }

    if (_volume < 256)
    {
        s16* samples = (s16*) audioData;
        for (int i = 0; i < num_in * 2; i++)
            samples[i] = ((s32) samples[i] * _volume) >> 8;
    }

    if (num_in < len_in)
    {
        int last = num_in - 1;

        for (int i = num_in; i < len_in; i++)
            ((u32*)audioData)[i] = ((u32*)audioData)[last];
    }

    if (_recordingStream) [[unlikely]]
        _recordingStream->write((char*) audioData, numFrames * sizeof(s16) * 2);

    return oboe::DataCallbackResult::Continue;
}

void OboeCallback::onErrorAfterClose(oboe::AudioStream* stream, oboe::Result result)
{
    AudioDiagnostics::onStreamError(result);
    if (result == oboe::Result::ErrorDisconnected && onErrorCallback != nullptr) {
        onErrorCallback(stream);
    }
}

int OboeCallback::getNumSamplesOut(int len)
{
    // TODO: adjust to game speed
    float f_len_in = len /* * (curFPS/60.0)*/;
    f_len_in += audioSampleFrac;
    int len_in = (int) floor(f_len_in);
    audioSampleFrac = f_len_in - len_in;

    return len_in;
}