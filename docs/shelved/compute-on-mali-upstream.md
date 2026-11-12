# Reporting the Mali fix upstream

Drafts for rafaelvcaetano's repos. Nothing is posted yet. The background is in [compute-on-mali.md](compute-on-mali.md).

Before posting:
- The results below come from our fork. Upstream has the same three uvec4 atomics, but its CalcOffsets is simpler than ours, so the fix has to be ported onto their code and checked on the Pixel 8 before the PR's claims hold. The PR carries only the fix, without our diagnostic switches.
- The Mario Kart scene differs by phone. On the Pixel 8 it was the mid-race state, but on the OnePlus it was the results-screen replay. Both scenes draw moving 3D, so the comparison still holds.

---

## PR: rafaelvcaetano/melonDS-android-lib

**Title:** Fix compute renderer on Mali: no atomics on uvec4 components

On Mali (driver r54p3, Pixel 8 and Pixel 9 Pro Fold), an atomic on one component of a `uvec4` array element in an SSBO always hits element 0, whatever the index. Atomics on plain `uint` arrays work.

The compute renderer does this twice on `VariantWorkCount`:

- **BinCombined:** `atomicAdd(VariantWorkCount[variantIdx].z, 1u)` puts every variant's count in variant 0, so the other variants rasterize from the wrong offsets. That's the tile-sized blocks of missing or wrong 3D.
- **CalcOffsets:** `atomicAdd(VariantWorkCount[1].w, ...)` lands in `[0].w` and doubles the work total. SortWork then runs past the end of the work list. At 1x this hung the GPU and soft-rebooted the phone.

**Fix:** declare the counters as `uint VariantWorkCountFlat[MaxVariants*4]`. The bytes are the same, so the indirect dispatch offsets don't change. CalcOffsets also now uses a running sum in one invocation instead of the atomic.

**Tested** on Mario Kart DS:
- Pixel 8 (Mali-G715): at 4x it matches the OpenGL renderer, and 1x and 32 px tiles run without hangs. Before: block noise at every scale and a GPU hang at 1x.
- OnePlus 7 Pro (Adreno 640): same picture and same speed (59.9 fps both, 12.8 vs 13.2 ms of frame work).
- Desktop (NVIDIA): the output is byte-identical before and after.

**To see it:** remove the `qcom` check in `VideoPreferencesFragment` and pick Compute on a Pixel 8.

---

## Issue: rafaelvcaetano/melonDS-android

**Title:** Compute renderer on Mali: cause found, fix in melonDS-android-lib#N

Compute is hidden on non-Qualcomm devices because it had issues on non-Adreno GPUs (012832e0). On a Pixel 8, the 3D corruption and GPU hangs come from a Mali driver bug: an atomic on a `uvec4` array component in an SSBO always lands in element 0. melonDS-android-lib#N avoids it.

With the fix, Mario Kart DS renders correctly on Compute on the Pixel 8 (driver r54p3). That's one game and one GPU family so far, so the `qcom` check should probably stay until more Mali devices are tested. Happy to help with that.

**To test on a Mali device:** build with the PR, set `isAdrenoGpu = true` in `VideoPreferencesFragment`, then pick Compute at 4x in any 3D game. Without the fix you get tile-sized holes in the 3D. With it, the picture should match OpenGL.
