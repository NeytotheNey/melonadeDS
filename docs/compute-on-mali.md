# Compute renderer on Mali

## Status: fixed and shipped

The Mali "missing blocks" bug is fixed in the core (`4b58305d`, 11-12; app `8c20cd3a`), and since app `06923682` (11-13) the settings screen offers Compute on every GLES 3.2 GPU, not just Qualcomm (`VideoPreferencesFragment.kt`). On the Pixel 8 it renders cleanly but costs more than OpenGL: at 8x it falls to about 41 FPS at White 2's title while OpenGL holds 60 (PERF-NOTES "Pixel 8 baseline").

## The cause

A Mali driver bug in atomics. On driver `v1.r54p3` (Pixel 8 and Pixel 9 Pro Fold), an atomic on one component of a `uvec4` array element in a storage buffer always hits element 0, whatever the index. Compute kept its per-variant counters in `uvec4 VariantWorkCount[]`, so two atomics went wrong:

- `CalcOffsets`: `atomicAdd(VariantWorkCount[1].w, …)` added into `[0].w`. The work total doubled, and every sorted offset was shifted by the real count. SortWork then ran twice the threads over empty entries. At `MaxWorkTiles` 12288 (1x, or `computetile 32` at 4x) the doubled range wrote past the WorkDesc buffer and hung the GPU, which took down SurfaceFlinger and soft-restarted the phone.
- `BinCombined`: `atomicAdd(VariantWorkCount[variantIdx].z, 1)` gave every count to variant 0. The other variants rasterized from wrong offsets, which made the block noise.

Atomics on plain `uint` arrays work fine on the same driver. Keep it that way in new compute code: never an atomic on a component of a `uvec4` element.

The fix: `VariantWorkCount` is declared as `uint VariantWorkCountFlat[MaxVariants*4]` (same bytes) behind `VariantCountX/Y/Z/W(i)` macros, and `CalcOffsets` hands out the offsets as a running sum in one invocation, with no atomics. On the desktop it's byte-identical at 1x and 4x.

How it was found: a `computecheck N` debug switch reads the binning and sort buffers back every Nth render and checks them on the CPU (`COMPCHECK` lines). On the Pixel, `total` was exactly 2× `maskbits`, `offsetsum` was 0, and `badvaroffset` was about 33k out of 38k. COMPPEAK's `pastbound` had already hinted at it: it was nonzero every frame on the Pixel and 0 in all 865 Fold lines. The texture buffer, X span setup and binning masks all checked out clean.

Debug switches added for this, all off by default:
- `computecheck N`: the readback checks.
- `computeflat 0`: the old uvec4 layout, the Mali control.
- `computecalc 1`: the old atomic `CalcOffsets`.
- `computebin 1/2/3`: BinCombined variants. They made no difference.
- `computessbo 1`: setup indices from an SSBO. It crashes the Mali driver inside the check's dispatch, so don't use it there.

## Testing

- **Pixel 8:** Mario Kart at 4x matches OpenGL, and 1x and `computetile 32` run 60 s with no hang (`perf_logs/mali-pixel8/flat/`). Phantom Hourglass, New Super Mario Bros, Platinum and SoulSilver at 4x had every COMPCHECK count at 0 and matched OpenGL; Phantom Hourglass at 1x ran 62 s with no hang (`perf_logs/mali-pixel8/games/`).
- **`computecheck` is flaky on Mali:** two of five check runs crashed inside Mali's driver (`gles_texturep_bind_image_instance_nx`) during RunCheck's extra dispatch. None of the plain Compute runs crashed, so it's the check, not the renderer.
- **OnePlus 7 Pro (Adreno 640):** Mario Kart at 4x with `computecheck 30`, all 55 COMPCHECK lines 0. New default 59.97 FPS, 12.77 ms average work, against the old code (`computeflat 0` + `computecalc 1`) at 59.92 and 13.18 ms, so no slowdown. New Super Mario Bros, Phantom Hourglass, Dark Dawn and White 2 passed all 212 COMPCHECK lines and matched OpenGL (`perf_logs/mali-pixel8/oneplus/`, `games/`). One `COMPGLERR 0x0502` showed up before the tile memory allocation in the check run only, with no visible effect.
- **Pixel 9 Pro Fold (user, 11-13):** Compute works with the fix. No numbers taken.

**Upstream port:** the same fix on rafaelvcaetano's code fixes the blocks there too, in Phantom Hourglass and New Super Mario Bros on the Pixel 8, and 1x runs with no hang. See [compute-on-mali-upstream.md](shelved/compute-on-mali-upstream.md) and `perf_logs/mali-pixel8/upstream/`.

## Background from the first attempt (11-06)

Compute was hidden unless `Build.HARDWARE` was `qcom`. The upstream commit that added the check (`012832e0`) says Compute "presented multiple issues on non-Adreno GPUs" but doesn't list them; the uvec4 atomics are the only one found so far.

The first try was on the Pixel 9 Pro Fold (Tensor G4, Mali-G715) over APKs on the NAS. With Mario Kart the 3D was full of tile-sized square blocks (missing polygons, clear color, or bits of kart in the sky) while 2D was clean. Ruled out then: WideMelon, `memoryBarrierShared()` before the `barrier()` calls in `BinCombined`, `glMemoryBarrier(GL_ALL_BARRIER_BITS)` plus `glFinish()` after every dispatch, and indirect dispatches. The driver's limits are well above what Compute needs (1024 threads per work group, 32 KB shared memory, 35 storage buffers, 2 GB largest storage buffer).

One Mali limit still worth knowing: an [Arm employee says](https://community.arm.com/support-forums/f/mobile-graphics-and-gaming-forum/53228/compute-shader-mali-gpu-bug) texture buffers wrap at 65,536 texels on older Mali GPUs, and on newer ones with older drivers, and recommends storage buffers. It wasn't the cause here, and `GL_MAX_TEXTURE_BUFFER_SIZE` was never logged.

WatermelonDS's Vulkan renderer runs fine on the Pixel 9 Pro Fold, but it's a graphics-pipeline renderer, not a compute one ([plan-vulkan.md](plan-vulkan.md)).
