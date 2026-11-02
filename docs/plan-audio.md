# Audio pass

Measurements are in the local `PERF-NOTES.md` ("Audio"; see [README.md](README.md)). Before this pass it was about 44 ms from the SPU to the speaker (14 ms in the SPU ring, 30 ms in the AAudio buffer), and the only audible cuts came from folds draining the ring.

## How audio flows

1. The emulator thread runs `SPU::Mix` and feeds each sample into blip_buf (band-limited resampling from the 16.76 MHz SPU clock straight to 48 kHz). Every ~4 ms of SPU time `BufferAudio()` copies the result into a 2048-frame ring under a pthread mutex.
2. The Oboe callback (AAudio, Shared, LowLatency, MMAP, 96-frame bursts) locks the same mutex in `SPU::ReadOutput`, copies, applies volume, and pads a short read by repeating the last frame, or silence if nothing came.
3. The stream is 48 kHz I16, which matches the device, so Oboe never resamples. Oboe is 1.10.0, with the ADPF performance hint on.

## Done (steps 1 and 2, `1f181515` and `4146bf4f`)

- **Drift control:** a PI controller steers the ring towards 960 frames (20 ms) with at most ±0.5% skew, applied on the emulator thread.
- **Smaller AAudio buffer:** starts at 3 bursts and grows one burst per xrun.
- **Priming and trims:** silence until the ring reaches 960 at start/resume (480 after an underrun), then a trim to 840; only for bursts of 256 frames or less.
- **Logging:** skew, fill, buffer size and FF speed in the debug log, perf CSV and summary; summary totals exclude fast-forward.
- **Tried and parked:** scaling the fast-forward base rate by the measured FF speed made the pitch glide, so slow FF scenes still run dry until step 6.

Still owed: a phone run of the `4146bf4f` build (resumes and short pauses, checking the logged FF speed).

## Still to do

### 3. Lock-free ring

Replace `AudioLock` in the read and write paths with a single-producer, single-consumer ring (two atomic indexes). Same buffer, same overflow rule. It lives in the core (`SPU.cpp`), so it goes in the core submodule. Reason: if the emulator thread is preempted while holding the lock, the real-time callback waits on it.

### 4. Softer underruns

On a short read, fade the last frame to zero over ~1–2 ms instead of holding it, and fade back in on the next good read. Today repeat-last-frame then hard silence is a step in the waveform.

### 5. Newer APIs (small, optional)

- **Oboe 1.11.0** (Sept 2026): routing-changed callback and a callback-timing fix.
- **`notifyWorkloadSpike()`** (Oboe 1.10, API 36): tell ADPF when a fold or rotation starts so the audio thread's cores ramp up before the hitch.
- **Exclusive MMAP**: try `SharingMode::Exclusive` and fall back to Shared. Many Samsung builds deny it, so it needs a phone test.
- **Partial data callback** (API 37): only matters if underruns are still common.
- Not useful here: PCM offload, the power-saving offload mode, and Oboe's resampler (never runs at 48→48).

### 6. Later: time-stretching for slowdowns

When the emulator can't hold full speed (charging throttles, heavy scenes, slow fast-forward), the ring runs dry and ±0.5% can't fix it. DuckStation and PCSX2 use SoundTouch, Dolphin uses granular synthesis. Bigger piece (new library, more CPU), so only if throttled sessions show it's needed.

### 7. Decide the hidden latency setting

`settings_audio_latency_enabled` is false, so the audio latency setting is hidden, but `MelonDSAndroidConfiguration.cpp` still applies whatever value is saved. The polish pass left it as is (user's call, 2026-11-02). Decide from measurements whether the setting makes an audible or measurable difference now that drift control and the growing AAudio buffer size things: if it does, show it again; if not, remove the pref, its two arrays, two strings and the switch.

## Already fine

- Resampling quality: blip_buf is band-limited, no aliasing to fix.
- SPU interpolation and 10-bit "Auto" are already exposed.
- CPU: the audio thread is ~0.3 ms per frame and sound is ~3% of the emulator thread.

## Testing

Each step: build, phone run with the debug log and perf capture on, folds and rotations included. Compare fill p5, underruns outside fast-forward, xruns and buffer size against the last run.
