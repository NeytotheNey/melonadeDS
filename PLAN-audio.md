# Audio pass

Baseline numbers are in PERF-NOTES.md ("Audio"). Short version: about 44 ms from the SPU to the speaker (14 ms in the SPU ring, 30 ms in the AAudio buffer), 0 xruns, and the only audible cuts come from folds draining the ring.

## How audio flows today

1. The emulator thread runs `SPU::Mix` and feeds each sample into blip_buf (band-limited resampling from the 16.76 MHz SPU clock straight to 48 kHz). Every ~4 ms of SPU time `BufferAudio()` copies the result into a 2048-frame ring under a pthread mutex.
2. The Oboe callback (AAudio, Shared, LowLatency, MMAP, 96-frame bursts) locks the same mutex in `SPU::ReadOutput`, copies, applies volume, and pads a short read by repeating the last frame, or silence if nothing came.
3. The stream is 48 kHz I16, which matches the device, so Oboe never resamples. Oboe is 1.10.0, with the ADPF performance hint on.

## What's wrong or weak

- **The AAudio buffer is 15× bigger than it needs to be.** `setBufferSizeInFrames(min(capacity, 2048))` gives 1440 frames (30 ms) on the Fold. The burst is 2 ms, the worst callback gap 4.7 ms, and xruns are 0.
- **Nothing steers the ring.** The callback sets a skew every time, but it's a constant (`60 / 59.826`, i.e. "the DS runs slightly slower than 60 Hz"). So the fill wanders (+3 frames/s, ±100–400 per fold) and a fold that lands while the fill is low cuts the sound.
- **`SetOutputSkew` is a data race.** The callback calls `blip_set_rates` on the blip buffers while the emulator thread writes into them. It's harmless only because the value never changes, and it stops being harmless as soon as the skew moves.
- **A mutex in the real-time callback.** If the emulator thread is preempted while it holds `AudioLock`, the audio thread waits on it. It hasn't shown up as an xrun yet, but it's exactly the kind of thing that does on a busy phone.
- **Underruns click.** Repeat-last-frame then hard silence is a step in the waveform.
- **Fast-forward with sound on is choppy.** The ring overflows and drops the oldest audio (`getNumSamplesOut` has a "TODO: adjust to game speed").
- Logger quirks from PERF-NOTES are still open.

## Plan

### 1. Drift control (biggest win)

Steer the ring towards a target fill with a small, smooth change to the blip_buf rate, the way RetroArch's dynamic rate control does: `skew = base × (1 + d × (fill − target) / target)`, with `d` around 0.005 (±0.5%, about 9 cents, not audible) and the fill low-pass filtered like PPSSPP does so the per-frame sawtooth doesn't wobble the pitch.

- The callback computes the skew and stores it in an atomic. The emulator thread applies it in `BufferAudio()`, between blip frames. That also fixes the data race.
- Target fill around 15 ms to start, tuned from the logs. Log the skew and filtered fill in the debug and perf logs.
- During fast-forward, scale the base by the speed instead (that's what desktop melonDS does), so unmuted fast-forward plays pitched-up instead of choppy.

### 2. Smaller AAudio buffer

Start at 2–4 bursts (4–8 ms) and grow by one burst each time the xrun count goes up, capped at the old size. Oboe's `LatencyTuner` does this, or it's ~15 lines by hand using the xrun polling that already exists. Together with step 1 this should take the total from ~44 ms to ~20–25 ms. It only makes sense after step 1, because a tight buffer with an unsteered ring would just move the cuts around.

### 3. Lock-free ring

Replace `AudioLock` in the read and write paths with a single-producer, single-consumer ring (two atomic indexes). Same buffer, same overflow rule. It lives in the core (`SPU.cpp`), so it goes in the core submodule.

### 4. Softer underruns

On a short read, fade the last frame to zero over ~1–2 ms instead of holding it, and fade back in on the next good read. Cheap, and with step 1 it should rarely fire anyway.

### 5. Newer APIs (small, optional)

- **Oboe 1.11.0** (Sept 2026): routing-changed callback and a callback-timing fix. Worth bumping to.
- **`notifyWorkloadSpike()`** (Oboe 1.10, API 36): tell ADPF when a fold or rotation starts so the audio thread's cores ramp up before the hitch.
- **Exclusive MMAP**: try `SharingMode::Exclusive` and fall back to Shared. It skips the system mixer, so it can shave a few ms. Many Samsung builds deny it, so it needs a phone test.
- **Partial data callback** (API 37): lets the callback hand back fewer frames instead of padding. Only matters if underruns are still common after step 1.
- Not useful here: PCM offload and the power-saving offload mode (they buffer seconds), and Oboe's resampler (never runs at 48→48).

### 6. Later: time-stretching for slowdowns

When the emulator can't hold full speed (charging throttles, heavy scenes), the ring keeps running dry and drift control alone can't fix it, since ±0.5% isn't enough. DuckStation and PCSX2 use SoundTouch time-stretching for this, and Dolphin uses granular synthesis. Either keeps the sound continuous at the same pitch. It's a bigger piece (new library, more CPU), so only if throttled sessions show it's needed.

### Already fine, no change needed

- Resampling quality: blip_buf is band-limited, so there's no aliasing to fix.
- The SPU interpolation setting (None/Linear/Cosine/Cubic/Gaussian) and 10-bit "Auto" are already exposed. "None" is what the hardware does; the others are a matter of taste.
- CPU: the audio thread is ~0.3 ms per frame and sound is ~3% of the emulator thread. There's no real performance to gain.

## Testing

Each step: build, phone run with the debug log and perf capture on, folds and rotations included, then compare against the 10-29 20:29 baseline (fill p5, underruns outside fast-forward, xruns, buffer size).

## Status

- 2026-10-29: steps 1 and 2 built and installed, not committed, waiting on a phone run. Target fill 20 ms (960 frames), ±0.5% skew with a PI controller, AAudio buffer starts at 3 bursts and grows one burst per xrun. The ring is trimmed back to ~21 ms when fast-forward ends. New log fields: `skew_ppm`, `fill_filtered`, `buffer_size` (debug log), `audio_skew_ppm` (perf CSV), skew and buffer size stats in the summary.
- 2026-10-29 (round 3, installed, not committed): trim and controller reset whenever steering starts (stream open/reopen, resume, FF on/off, unmute mid-FF) via `SPU::TrimOutputIfAbove(1200, 960)` under one lock; no draining while the raw fill is under 480; proportional gain 0.01 (settles in ~3 s); priming: the callback plays silence until the ring reaches 960 at start/resume, and 480 after a real underrun. New counters `prime_wait_frames` and `reprimes`. `cleanupAudio()` now drops `activeInstance`.
- 2026-10-29 (round 4, committed, installed, not yet played): the trim moved to priming end in the callback, so resumes and stream reopens no longer refill to full before it runs. Priming and trims only run for bursts of 256 frames or less; larger-burst streams get plain drift control. A deferred trim catches the last fast-forward frame. Summary totals now exclude all fast-forward, and the prime counters cover the whole session.
- Next: a phone run of round 4 (resume and headset in/out especially), then steps 3 (lock-free ring) and 4 (softer underruns).
