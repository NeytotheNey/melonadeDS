// The game's sound through an SDL audio callback, ported from the app's Oboe output (OboeCallback.cpp and MelonDSAudio.cpp): the callback
// steers the buffered sound towards a target fill by nudging the SPU's resampling ratio (the app's AudioDriftController, RetroArch-style
// dynamic rate control), so the sound neither runs dry nor piles up whatever the sound card's clock and the emulator's pacing do. It
// primes before it plays (one clean gap rather than a run of tiny ones), fades in and out of gaps, and trims what fast forward left.
//
// Where it differs from the app, and why:
// - The emulator thread moves each frame's sound out of the SPU's ring (2048 frames) into a ring of its own here (8192 frames), which the
//   callback reads. A PC runs a frame in a few milliseconds and its frames come in bunches when one is slow (the timer limiter catches up),
//   so the sound arrives ~1500 frames at a time at worst, which the SPU's ring can't hold with a callback's worth on top.
// - The targets are larger to match (low 1500 frames, medium 2000, high 2800 at 48 kHz: 31 to 58 ms), and the drift control counts the
//   ring as starving below a quarter of the target rather than half, since the fill's sawtooth is deeper.
// - Every latency primes (the app's plain mode is for Bluetooth-sized bursts, which a PC's callbacks never are).
// As in the app, the emulator runs 60 frames a second (paced to the display or on the timer), a little faster than the DS's 59.83 Hz, so the
// base ratio is 60 / 59.83. Fast forward is the app's: sped up (and pitched up) at its speed, or silent when it's muted (the default) or unlimited.
//
// UI thread, apart from the emulator thread's hooks (BeforeFrame / AfterFrame) and the callback
#pragma once

#include <SDL.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace MelonDSAndroid { class MelonInstance; }

namespace Frontend
{

// the app's AudioDriftController (OboeCallback.h), with the starving fraction a setting
class AudioDriftController
{
public:
    static constexpr double FILL_FILTER_SECONDS = 0.5;
    static constexpr double PROPORTIONAL_GAIN = 0.01;
    static constexpr double INTEGRAL_SECONDS = 20.0;
    static constexpr double MAX_ADJUSTMENT = 0.005;

    void reset();
    void setTargetFill(double frames) { targetFill = frames; }
    double getTargetFill() const { return targetFill; }
    void setStarvingFraction(double fraction) { starvingFraction = fraction; }
    // [fill] is the ring's fill after this callback read [numFrames] from it; returns the new adjustment
    double update(int fill, int numFrames, int sampleRate);
    double getFilteredFill() const { return filteredFill; }

private:
    double targetFill = 960.0;
    double starvingFraction = 0.5;
    bool hasFill = false;
    double filteredFill = 0;
    double integral = 0;
    int filterFrames = 0;
    int filterSampleRate = 0;
    double filterCoefficient = 0;
};

class AudioOutput
{
public:
    struct Config
    {
        bool Enabled = true;
        int Volume = 256;               // 0-256, the app's volume
        std::string Latency = "medium"; // low, medium, high: the callback's size (256, 512, 1024 frames) and the target fill
        bool MuteFastForward = true;
    };

    static constexpr int kRingFrames = 8192;
    static constexpr int FADE_FRAMES = 72;

    ~AudioOutput();
    // the device opened (or closed, when sound is off or the volume is 0) for [config]; a running stream is reopened only when its latency
    // changes
    void Apply(const Config& config);
    // the device and the --audio-wav file closed
    void Close();
    bool IsOpen() const { return Device != 0; }

    // the game it plays (null: none, silence); waits for a callback that's running to finish, so the instance can go right after
    void SetInstance(MelonDSAndroid::MelonInstance* instance);
    // the game paused (a menu over it) or going again: paused, the callback fades out and plays silence; going again, it primes first
    void SetRunning(bool running);
    // fast forward on or off at [speed] (0 or less: unlimited), as the app's setAudioFastForwardActive
    void SetFastForward(bool active, float speed);

    // emulator thread, before and after each frame: the frame's sound into the ring
    void BeforeFrame(MelonDSAndroid::MelonInstance& instance);
    void AfterFrame(MelonDSAndroid::MelonInstance& instance);

    // --audio-wav FILE: everything the callback plays, before the volume, as a 48 kHz 16-bit stereo WAV (debugging)
    void RecordTo(const std::string& path);
    // --audio-log: once a second, from the UI thread: the ring's fill, its filtered fill, the ratio, the underruns and overflows
    void PrintStats();
    // --audio-trace FILE: the ring's fill at each callback (before it reads) and at each frame's start and end, with the times (CSV)
    void TraceTo(const std::string& path);

private:
    static void SDLCALL Callback(void* userdata, Uint8* stream, int len);
    void Fill(Sint16* samples, int numFrames);
    int RingFill() const { return (int)(WriteCount.load(std::memory_order_acquire) - ReadCount.load(std::memory_order_relaxed)); }
    // the callback's: frames out of the ring (fewer when it has fewer), or dropped
    int ReadRing(Sint16* out, int frames);
    void DropRing(int frames);
    void ApplyFades(Sint16* samples, int numRead, int numFrames);
    void UpdateSkew(MelonDSAndroid::MelonInstance& instance, int numFrames, bool canSteer, int fill);
    // a trim the callback runs at its next call: the ring down to [keep] when it holds more than [threshold]
    void RequestTrim(int threshold, int keep);
    void CloseDevice();
    void CloseWav();

    // written by Apply() on the UI thread, read by the emulator thread too (IsOpen, the trims after a frame)
    std::atomic<SDL_AudioDeviceID> Device = 0;
    Config Current;
    int SampleRate = 48000;
    std::atomic<int> Target = 2000;

    // the ring: the emulator thread writes, the callback reads
    std::vector<Sint16> Ring = std::vector<Sint16>(kRingFrames * 2);
    std::atomic<uint64_t> WriteCount = 0, ReadCount = 0;
    std::vector<Sint16> Scratch = std::vector<Sint16>(2048 * 2);

    // shared with the callback
    std::atomic<MelonDSAndroid::MelonInstance*> Instance = nullptr;
    std::atomic_bool Running = false;
    std::atomic_bool FastForward = false;
    std::atomic_bool MuteFastForward = true;
    std::atomic<float> FastForwardSpeed = 0;
    std::atomic<int> Volume = 256;
    std::atomic<uint32_t> RateGeneration = 0;
    std::atomic<uint32_t> StartGeneration = 0;
    std::atomic<uint64_t> TrimRequest = 0;
    std::atomic_bool TrimAfterFramePending = false;

    // the callback's own
    AudioDriftController Drift;
    const MelonDSAndroid::MelonInstance* DriftInstance = nullptr;
    uint32_t DriftRateGeneration = 0;
    bool DriftSteering = false;
    const MelonDSAndroid::MelonInstance* PrimeInstance = nullptr;
    uint32_t PrimeStartGeneration = 0;
    bool Primed = false;
    bool Repriming = false;
    Sint16 LastFrame[2] = {0, 0};
    int FadeGain = 0;

    // the stats
    std::atomic<int> StatFill = 0;
    std::atomic<int> StatFiltered = 0;
    std::atomic<double> StatSkew = 1.0;
    std::atomic<int> StatUnderrunFrames = 0;
    std::atomic<int> StatReprimes = 0;
    std::atomic<int> StatCallbacks = 0;
    std::atomic<int> StatMinFill = 1 << 30;
    std::atomic<int> StatMaxFill = 0;
    std::atomic<int> StatOverflowFrames = 0;

    struct TracePoint { double Ms; int Fill; char Who; };
    std::vector<TracePoint> CallbackTrace, FrameTrace;
    std::string TracePath;
    bool Tracing = false;

    FILE* Wav = nullptr;
    uint32_t WavBytes = 0;
    std::string WavPath;
};

}
