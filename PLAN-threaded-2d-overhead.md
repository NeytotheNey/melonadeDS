# Plan: cut the threaded 2D overhead

## Context

Threaded 2D (`PLAN-threaded-2d.md`, step 4) works and is hash-identical to sync, but on White 2 it doesn't pay for itself yet. The clean A/B on 10-28 (fresh start each run, same spot, standing still, 60 s, OpenGL 4x, JIT; details in `PERF-NOTES.md`, "Clean A/B", and the data is in `perf_logs/simpleperf/fresh_{off,on}/`):

| | OFF | ON |
|---|---|---|
| EmulatorThread CPU | 10.27 ms/frame (2D ≈ 2.72) | 8.78 ms/frame |
| GPU2DThread CPU | — | 4.11 ms/frame (drawing ≈ 3.07, `Wait2DWork` + syscall ≈ 0.83) |
| Total app CPU | | +25% |
| work_ms avg / p95 / max | 10.05 / 11.85 / 12.88 | 9.37 / 12.36 / 15.53 |
| Clocks | cpu0-5 ~1.6 GHz, cpu6-7 idle at 883 MHz | same |

The questions:
- The emulator thread saves only 1.5 of the 2.7 ms of 2D. Where does the other ~1.2 ms go?
- Why does the worker draw ~13% slower than the inline code did?
- The worker spins ~0.8 ms per frame. Can that come down?

Clocks are the same in both runs, so the answer isn't frequency. That leaves code we added, syscalls, and cache or memory effects.

**Goal:** the emulator thread saves close to the full 2D cost (≥ 2 ms), with total CPU at most ~10% above OFF. If that can't be reached, keep the setting opt-in and park it.

## Rules

- Same workflow as before: a coder subagent, then a fresh reviewer subagent, then fixes, then a build.
- Every change must stay hash-identical. Check it on the emulator with `debug.melonade.threaded2d` = 1 vs 0 and HASH2D, the same way the step 4 check was done (see HANDOFF section 0).
- **Measuring:** the user plays and stands at the same spot. A subagent records, so the main session's context stays small. Every run starts fresh (exit the game, change the setting, start again), because switching live skewed an earlier run. The recording recipe is `perf_logs/simpleperf/fresh_on/loop.sh` plus a 60 s `simpleperf record` (see `PERF-NOTES.md`). That loop takes ~1.5 s per pass, so for exact CPU per thread use start/end snapshots.
- Step 4 should be committed before this starts, so every experiment compares against a fixed base.

## Step 1: find where the time goes (no code changes)

1. **Symbol diff.** Use the existing `fresh_off`/`fresh_on` profiles. For each EmulatorThread symbol, compute ms per frame (share × thread ms/frame) and diff OFF against ON. Put each symbol in one bucket:
   - new hand-off code: `Begin2DJob`/`End2DJob`, `Snapshot2D*`, coherence helpers, `Sync2D`/`Wait2DJobs`, `State()` copies;
   - syscalls and futex;
   - code that existed before and got slower.

   The live-switch A/B showed every old symbol +15–25%. Check whether that holds in the clean data. Do the same for the worker: compare its drawing symbols with the same symbols inline in OFF. Run `simpleperf report` on the device (`adb push` the `.data` back if it's gone), or on the host with the NDK's simpleperf and `--symfs` pointing at the unstripped `.so`.
2. **Hardware counters.** Run `simpleperf stat --app me.magnum.melonds.dev --per-thread -e cpu-cycles,instructions,cache-misses,cache-references --duration 30` (check `simpleperf list` for L2/L3 refill events on this SoC), once OFF and once ON. Compare instructions per frame against cycles per frame, for each thread:
   - Same instructions with more cycles means stalls (cache/memory).
   - More instructions means it's our code.

   This step decides which experiments in step 3 matter.

## Step 2: counters in the build

This is plan step 6 of `PLAN-threaded-2d.md`. Add to the perf logger's `_summary.txt`, behind the perf logging setting so off costs nothing:
- per frame: time the emulator thread waits in `Sync2D`, plus a sync count per call site (palette, OAM, LCDC, mapping, coherence, frame end);
- futex wakes sent by the emulator thread;
- the worker's spin time vs sleep time;
- lines queued vs run inline.

## Step 3: experiments

Run them in the order step 1 points to. Each one is its own small change plus an A/B against the committed step 4 build.

- **False sharing.** `Jobs2DQueued`/`Jobs2DDone`, the sleeping flags and the waiter count probably share cache lines. If so, every push/pop bounces lines between the two cores. Put the producer-written and consumer-written fields on separate 64-byte lines (`alignas(64)`), and align ring slots to cache lines.
- **Job size.** A job is ~1–2 KB, and ~193 a frame cross cores (~400 KB/frame). Ideas:
  - don't copy `Line3DBuffer` unless soft 3D needs it (check that it's already skipped for GL);
  - point at the FIFO line instead of copying it when the FIFO isn't in use;
  - check whether `UnitState` can shrink;
  - maybe write the snapshot straight into the slot instead of building it and copying.
- **Wake and spin.** Measure how many futex wakes happen per frame, then:
  - try a shorter worker spin (100 → 20 µs, and pure sleep);
  - try waking the worker only when it's actually asleep (check that this is already the case);
  - try letting it run lines in batches.

  The aim is to cut the ~0.8 ms worker spin and the emulator thread's syscalls without adding barrier waits.
- **Core placement.** Both threads run on the 3.63 GHz cores at ~1.6 GHz, and the 4.74 GHz cores sit idle. As separate runs:
  - pin the worker to cpu6-7 with `sched_setaffinity`;
  - pin the emulator thread there instead.

  Check ADPF still covers the threads. This trades power for speed, so measure both.
- **Shared data traffic.** If step 1 shows cache stalls on old code, see what both cores touch: palette, OAM and `VRAMFlat_*` copies (written by the emulator, read by the worker), the framebuffers (written by the worker, read at frame end for the GL upload), and `GPU`/`Unit` fields that sit next to hot emulator fields. Moving the worker-read data off cache lines the emulator writes often may help.

## Decision

After each experiment, update the table in `PERF-NOTES.md`. Stop when the goal is met, or when the remaining ideas are each worth less than ~0.2 ms. If the goal isn't met, leave threaded 2D off by default, and note in HANDOFF which games it might help (CPU-heavy scenes over budget).
