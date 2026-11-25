#include "Audio.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "Configuration.h"
#include "MelonInstance.h"

namespace Frontend
{

namespace
{

double NowMs()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// the desktop's ring is deeper than the app's (Audio.h says why): the starving part of the target
constexpr double kStarvingFraction = 0.25;
// the DS's refresh rate, 33513982 Hz / (6 * 355 * 263), as OboeCallback.cpp has it
constexpr double INTERNAL_FRAME_RATE = 59.8260982880808;

}

// ---- the app's AudioDriftController (OboeCallback.cpp)

void AudioDriftController::reset()
{
    hasFill = false;
    filteredFill = 0;
    integral = 0;
}

double AudioDriftController::update(int fill, int numFrames, int sampleRate)
{
    if (numFrames <= 0 || sampleRate <= 0)
        return std::clamp(integral, -MAX_ADJUSTMENT, MAX_ADJUSTMENT);

    // the filter works on elapsed time rather than on the callback count, so it doesn't depend on the burst size
    if (numFrames != filterFrames || sampleRate != filterSampleRate)
    {
        filterFrames = numFrames;
        filterSampleRate = sampleRate;
        filterCoefficient = 1.0 - std::exp(-(double)numFrames / (FILL_FILTER_SECONDS * sampleRate));
    }
    if (hasFill)
        filteredFill += filterCoefficient * (fill - filteredFill);
    else
    {
        filteredFill = fill;
        hasFill = true;
    }

    // the filter lags, so a ring that's running dry could still look too full: recover from where the fill really is
    bool isStarving = fill < targetFill * starvingFraction;
    if (isStarving)
        filteredFill = std::min(filteredFill, (double)fill);

    double error = (filteredFill - targetFill) / targetFill;
    double proportional = PROPORTIONAL_GAIN * error;
    double integralStep = PROPORTIONAL_GAIN / INTEGRAL_SECONDS * error * numFrames / sampleRate;

    // anti-windup: at a limit the integral only moves back towards the middle, and it holds while starving (a stall, not a rate error)
    double unclamped = proportional + integral + integralStep;
    if (!isStarving && (unclamped < MAX_ADJUSTMENT || integralStep < 0) && (unclamped > -MAX_ADJUSTMENT || integralStep > 0))
        integral = std::clamp(integral + integralStep, -MAX_ADJUSTMENT, MAX_ADJUSTMENT);

    double adjustment = std::clamp(proportional + integral, -MAX_ADJUSTMENT, MAX_ADJUSTMENT);
    // a starving ring must never be drained faster
    if (isStarving)
        adjustment = std::min(adjustment, 0.0);
    return adjustment;
}

// ---- the output

AudioOutput::~AudioOutput()
{
    Close();
}

void AudioOutput::Apply(const Config& config)
{
    Volume = std::clamp(config.Volume, 0, 256);
    bool wasMuted = MuteFastForward.load();
    // unmuted in the middle of fast forward: what's there is trimmed and the drift control starts over, as the app does
    if (FastForward && wasMuted && !config.MuteFastForward)
    {
        RequestTrim(Target + 256, Target);
        RateGeneration++;
    }
    MuteFastForward = config.MuteFastForward;

    bool want = config.Enabled && config.Volume > 0;
    if (!want)
    {
        CloseDevice();
        Current = config;
        return;
    }
    if (Device && config.Latency == Current.Latency)
    {
        Current = config;
        return;
    }
    CloseDevice();
    Current = config;
    int samples = config.Latency == "low" ? 256 : config.Latency == "high" ? 1024 : 512;
    SDL_AudioSpec wanted {}, have {};
    wanted.freq = 48000;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = (Uint16)samples;
    wanted.callback = Callback;
    wanted.userdata = this;
    // the format, rate and callback size as asked (SDL converts and buffers), so the latency setting is what the callback sees
    Device = SDL_OpenAudioDevice(nullptr, 0, &wanted, &have, 0);
    if (!Device)
    {
        fprintf(stderr, "audio: can't open the output: %s\n", SDL_GetError());
        return;
    }
    SampleRate = have.freq;
    Target = config.Latency == "low" ? 1500 : config.Latency == "high" ? 2800 : 2000;
    Drift.setTargetFill(Target);
    Drift.setStarvingFraction(kStarvingFraction);
    // the callback primes again
    StartGeneration++;
    printf("audio: %s, %d Hz, %d frames a callback, target fill %d (%.0f ms)\n", SDL_GetCurrentAudioDriver(), have.freq, have.samples, Target.load(),
        Target.load() * 1000.0 / have.freq);
    fflush(stdout);
    SDL_PauseAudioDevice(Device, 0);
}

void AudioOutput::CloseDevice()
{
    if (Device)
    {
        SDL_CloseAudioDevice(Device);
        Device = 0;
    }
}

void AudioOutput::Close()
{
    CloseDevice();
    CloseWav();
    if (Tracing)
    {
        Tracing = false;
        if (FILE* f = fopen(TracePath.c_str(), "w"))
        {
            fprintf(f, "ms,fill,who\n");
            std::vector<TracePoint> all = CallbackTrace;
            all.insert(all.end(), FrameTrace.begin(), FrameTrace.end());
            std::sort(all.begin(), all.end(), [](const TracePoint& a, const TracePoint& b) { return a.Ms < b.Ms; });
            for (const TracePoint& p : all)
                fprintf(f, "%.3f,%d,%c\n", p.Ms, p.Fill, p.Who);
            fclose(f);
        }
    }
}

void AudioOutput::SetInstance(MelonDSAndroid::MelonInstance* instance)
{
    if (Device)
        SDL_LockAudioDevice(Device);
    Instance = instance;
    // a new game starts from an empty ring
    ReadCount.store(WriteCount.load());
    if (Device)
        SDL_UnlockAudioDevice(Device);
}

void AudioOutput::SetRunning(bool running)
{
    if (running && !Running)
        StartGeneration++;
    Running = running;
}

void AudioOutput::RequestTrim(int threshold, int keep)
{
    TrimRequest.store(((uint64_t)threshold << 32) | (uint32_t)keep);
}

void AudioOutput::SetFastForward(bool active, float speedMultiplier)
{
    float speed = active && speedMultiplier > 0 ? speedMultiplier : 0;
    bool wasActive = FastForward.load();
    if (active == wasActive && speed == FastForwardSpeed.load())
        return;
    // fast forward ending (unlimited above all) leaves the ring full, so it's trimmed, and once more after the frame running now
    RequestTrim(Target + 256, Target);
    FastForwardSpeed = speed;
    FastForward = active;
    RateGeneration++;
    if (wasActive && !active)
        TrimAfterFramePending.store(true, std::memory_order_release);
}

void AudioOutput::BeforeFrame(MelonDSAndroid::MelonInstance&)
{
    if (Tracing && FrameTrace.size() < FrameTrace.capacity())
        FrameTrace.push_back({NowMs(), RingFill(), 'b'});
}

void AudioOutput::AfterFrame(MelonDSAndroid::MelonInstance& instance)
{
    // the frame's sound out of the SPU's ring into this one; what doesn't fit (unlimited fast forward) is dropped, the newest first
    int got;
    while ((got = instance.readAudioOutput(Scratch.data(), (int)Scratch.size() / 2)) > 0)
    {
        uint64_t write = WriteCount.load(std::memory_order_relaxed);
        uint64_t read = ReadCount.load(std::memory_order_acquire);
        int room = kRingFrames - (int)(write - read);
        int n = std::min(got, std::max(room, 0));
        for (int i = 0; i < n; i++)
        {
            size_t at = (size_t)((write + i) % kRingFrames) * 2;
            Ring[at] = Scratch[i * 2];
            Ring[at + 1] = Scratch[i * 2 + 1];
        }
        WriteCount.store(write + n, std::memory_order_release);
        if (n < got)
            StatOverflowFrames += got - n;
    }
    if (TrimAfterFramePending.load(std::memory_order_relaxed) && TrimAfterFramePending.exchange(false, std::memory_order_acquire)
        && !FastForward)
        RequestTrim(Target + 400, Target);
    if (Tracing && FrameTrace.size() < FrameTrace.capacity())
        FrameTrace.push_back({NowMs(), RingFill(), 'f'});
}

int AudioOutput::ReadRing(Sint16* out, int frames)
{
    uint64_t read = ReadCount.load(std::memory_order_relaxed);
    uint64_t write = WriteCount.load(std::memory_order_acquire);
    int n = std::min(frames, (int)(write - read));
    for (int i = 0; i < n; i++)
    {
        size_t at = (size_t)((read + i) % kRingFrames) * 2;
        out[i * 2] = Ring[at];
        out[i * 2 + 1] = Ring[at + 1];
    }
    ReadCount.store(read + n, std::memory_order_release);
    return n;
}

void AudioOutput::DropRing(int frames)
{
    uint64_t read = ReadCount.load(std::memory_order_relaxed);
    uint64_t write = WriteCount.load(std::memory_order_acquire);
    ReadCount.store(read + std::min<uint64_t>((uint64_t)std::max(frames, 0), write - read), std::memory_order_release);
}

void SDLCALL AudioOutput::Callback(void* userdata, Uint8* stream, int len)
{
    static_cast<AudioOutput*>(userdata)->Fill((Sint16*)stream, len / 4);
}

void AudioOutput::UpdateSkew(MelonDSAndroid::MelonInstance& instance, int numFrames, bool canSteer, int fill)
{
    // The emulator runs 60 DS frames a second, slightly more than the DS's own rate (the app's OboeCallback::updateOutputSkew)
    double baseSkew = std::clamp(EMULATOR_TARGET_FPS / INTERNAL_FRAME_RATE, 0.995, 1.005);
    uint32_t rateGeneration = RateGeneration.load(std::memory_order_acquire);
    bool steering = canSteer;
    if (FastForward.load(std::memory_order_relaxed))
    {
        // paced fast forward plays its audio sped up (and pitched up); muted fast forward discards it and unlimited has no known rate, so both
        // keep the plain base without steering
        float speed = FastForwardSpeed.load(std::memory_order_relaxed);
        if (speed > 0 && !MuteFastForward.load(std::memory_order_relaxed))
            baseSkew *= speed;
        else
            steering = false;
    }
    if (&instance != DriftInstance || rateGeneration != DriftRateGeneration || steering != DriftSteering)
    {
        Drift.reset();
        DriftInstance = &instance;
        DriftRateGeneration = rateGeneration;
        DriftSteering = steering;
    }
    double adjustment = steering ? Drift.update(fill, numFrames, SampleRate) : 0;
    double skew = baseSkew * (1.0 + adjustment);
    // only stored here; the emulator thread applies it between two blip frames
    instance.setAudioOutputSkew(skew);
    StatFill = fill;
    StatFiltered = steering ? (int)Drift.getFilteredFill() : -1;
    StatSkew = skew;
}

void AudioOutput::Fill(Sint16* samples, int numFrames)
{
    StatCallbacks++;
    int target = Target.load();
    // a trim asked for (fast forward ended, unmuted): down to its keep when the ring holds more than its threshold
    if (uint64_t request = TrimRequest.exchange(0))
    {
        int threshold = (int)(request >> 32), keep = (int)(request & 0xFFFFFFFF), fill = RingFill();
        if (fill > threshold)
            DropRing(fill - keep);
    }
    MelonDSAndroid::MelonInstance* instance = Instance.load();
    bool muted = FastForward.load(std::memory_order_relaxed) && MuteFastForward.load(std::memory_order_relaxed);
    int fill = RingFill();
    if (Tracing && CallbackTrace.size() < CallbackTrace.capacity())
        CallbackTrace.push_back({NowMs(), fill, 'c'});
    if (!instance || !Running.load(std::memory_order_relaxed))
    {
        DriftInstance = nullptr;
        PrimeInstance = nullptr;
        ApplyFades(samples, 0, numFrames);
    }
    else
    {
        uint32_t startGeneration = StartGeneration.load(std::memory_order_relaxed);
        if (instance != PrimeInstance || startGeneration != PrimeStartGeneration)
        {
            PrimeInstance = instance;
            PrimeStartGeneration = startGeneration;
            Primed = false;
            Repriming = false;
        }
        if (muted)
        {
            // muted fast forward: the sound is dropped as it comes, so it can't play as a stale burst once fast forward ends, which primes
            // again
            DropRing(fill);
            Primed = false;
            Repriming = false;
            UpdateSkew(*instance, numFrames, false, 0);
            ApplyFades(samples, 0, numFrames);
        }
        else
        {
            bool play = true;
            if (!Primed)
            {
                // the target first (half of it after the ring ran dry) with this callback's worth on top, since the target is the fill
                // after a read, then trimmed to it, so the drift control starts on it
                if (fill < (Repriming ? target / 2 : target) + numFrames)
                {
                    UpdateSkew(*instance, numFrames, false, fill);
                    ApplyFades(samples, 0, numFrames);
                    play = false;
                }
                else
                {
                    Primed = true;
                    Repriming = false;
                    if (fill > target + numFrames + 256)
                        DropRing(fill - target - numFrames);
                    Drift.reset();
                }
            }
            if (play)
            {
                int numIn = ReadRing(samples, numFrames);
                if (numIn < numFrames)
                {
                    StatUnderrunFrames += numFrames - numIn;
                    // the ring ran dry: one clean gap until it holds half the target again, rather than a stutter
                    Primed = false;
                    Repriming = true;
                    StatReprimes++;
                }
                UpdateSkew(*instance, numFrames, Primed, RingFill());
                ApplyFades(samples, numIn, numFrames);
            }
        }
        if (fill < StatMinFill) StatMinFill = fill;
        if (fill > StatMaxFill) StatMaxFill = fill;
    }
    // the volume after the --audio-wav copy, which keeps the game's sound at full scale
    if (Wav)
    {
        fwrite(samples, 4, numFrames, Wav);
        WavBytes += numFrames * 4;
    }
    int volume = Volume.load(std::memory_order_relaxed);
    if (volume < 256)
    {
        for (int i = 0; i < numFrames * 2; i++)
            samples[i] = (Sint16)(((int)samples[i] * volume) >> 8);
    }
}

void AudioOutput::ApplyFades(Sint16* samples, int numRead, int numFrames)
{
    if (numRead > 0)
    {
        LastFrame[0] = samples[(numRead - 1) * 2];
        LastFrame[1] = samples[(numRead - 1) * 2 + 1];
    }
    for (int i = 0; i < numRead && FadeGain < FADE_FRAMES; i++)
    {
        FadeGain++;
        samples[i * 2] = (Sint16)(samples[i * 2] * FadeGain / FADE_FRAMES);
        samples[i * 2 + 1] = (Sint16)(samples[i * 2 + 1] * FadeGain / FADE_FRAMES);
    }
    int i = numRead;
    for (; i < numFrames && FadeGain > 0; i++)
    {
        FadeGain--;
        samples[i * 2] = (Sint16)(LastFrame[0] * FadeGain / FADE_FRAMES);
        samples[i * 2 + 1] = (Sint16)(LastFrame[1] * FadeGain / FADE_FRAMES);
    }
    if (i < numFrames)
        memset(samples + i * 2, 0, (size_t)(numFrames - i) * 4);
}

// ---- debugging

namespace
{

void WriteWavHeader(FILE* f, uint32_t dataBytes)
{
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(2);
    u32(48000);
    u32(48000 * 4);
    u16(4);
    u16(16);
    fwrite("data", 1, 4, f);
    u32(dataBytes);
}

}

void AudioOutput::RecordTo(const std::string& path)
{
    if (Device)
        SDL_LockAudioDevice(Device);
    CloseWav();
    Wav = fopen(path.c_str(), "wb");
    WavBytes = 0;
    WavPath = path;
    if (Wav)
        WriteWavHeader(Wav, 0);
    else
        fprintf(stderr, "audio: can't write %s\n", path.c_str());
    if (Device)
        SDL_UnlockAudioDevice(Device);
}

void AudioOutput::CloseWav()
{
    if (!Wav)
        return;
    fseek(Wav, 0, SEEK_SET);
    WriteWavHeader(Wav, WavBytes);
    fclose(Wav);
    Wav = nullptr;
    printf("audio: wrote %s (%.1f s)\n", WavPath.c_str(), WavBytes / (48000.0 * 4));
}

void AudioOutput::TraceTo(const std::string& path)
{
    TracePath = path;
    CallbackTrace.reserve(200000);
    FrameTrace.reserve(400000);
    Tracing = true;
}

void AudioOutput::PrintStats()
{
    if (!Device)
        return;
    int minFill = StatMinFill.exchange(1 << 30), maxFill = StatMaxFill.exchange(0);
    printf("audio: fill %d (before reads: min %d, max %d), filtered %d of %d, ratio %.5f, underrun frames %d, reprimes %d, overflow frames %d, "
        "callbacks %d%s\n", StatFill.load(), minFill == (1 << 30) ? -1 : minFill, maxFill, StatFiltered.load(), Target.load(), StatSkew.load(),
        StatUnderrunFrames.exchange(0), StatReprimes.exchange(0), StatOverflowFrames.exchange(0), StatCallbacks.exchange(0),
        FastForward ? (MuteFastForward ? " (fast forward, muted)" : " (fast forward)") : "");
    fflush(stdout);
}

}
