# Reporting the Mali fix upstream

Drafts for rafaelvcaetano's repos. Nothing is posted yet. The background is in [compute-on-mali.md](../compute-on-mali.md).

Before posting:
- The fix is ported onto upstream as lib branch `mali-uvec4-fix` (`af6834c9`, off lib `431ab4bd`, app `aaf263d2`), in a local clone at `C:\Dev\upstream-test\melonDS-android`. It changes one file, with 25 lines added and 10 removed, and a review found no issues. The branch is pushed to the local Forgejo (`ney/melonDS-android-lib-upstream`, remote `fork`), not to GitHub. A PR still needs a public GitHub fork of melonDS-android-lib, since ours is private.
- Attach `perf_logs/mali-pixel8/upstream/sheet_nsmb.png` (before | OpenGL | after) to the PR. `sheet_ph.png` is the Phantom Hourglass one.

---

## PR: rafaelvcaetano/melonDS-android-lib

**Title:** Fix compute renderer on Mali: no atomics on uvec4 components

On Mali (driver r54p3, Pixel 8 and Pixel 9 Pro Fold), an atomic on one component of a `uvec4` array element in an SSBO always hits element 0, whatever the index. Atomics on plain `uint` arrays work.

The compute renderer does this twice on `VariantWorkCount`:

- **BinCombined:** `atomicAdd(VariantWorkCount[variantIdx].z, 1u)` puts every variant's count in variant 0, so the other variants rasterize from the wrong offsets. That's the tile-sized blocks of missing or wrong 3D.
- **CalcOffsets:** `atomicAdd(VariantWorkCount[1].w, ...)` lands in `[0].w` and doubles the work total. SortWork then runs past the end of the work list. At 1x this hung the GPU and soft-rebooted the phone.

**Fix:** declare the counters as `uint VariantWorkCountFlat[MaxVariants*4]`. The bytes are the same, so the indirect dispatch offsets don't change. CalcOffsets also now uses a running sum in one invocation instead of the atomic.

**Tested** this branch in the current app on a Pixel 8 (Mali-G715), Compute at 4x. Before the fix, the Phantom Hourglass intro and the New Super Mario Bros title screen show the blocks. After it, they match OpenGL (screenshots below). At 1x, the game ran 60 s without a hang. Before the fix, 1x hung the GPU and the phone soft-rebooted.

The same fix in our fork also renders cleanly on Mario Kart, Pokemon Platinum and SoulSilver. On an Adreno 640 it runs at the same speed (59.9 fps both). On desktop the output is byte-identical.

**To see it:** remove the `qcom` check in `VideoPreferencesFragment` and pick Compute on a Pixel 8.

---

## Issue: rafaelvcaetano/melonDS-android

**Title:** Compute renderer on Mali: cause found, fix in melonDS-android-lib#N

Compute is hidden on non-Qualcomm devices because it had issues on non-Adreno GPUs (012832e0). On a Pixel 8, the 3D corruption and GPU hangs come from a Mali driver bug: an atomic on a `uvec4` array component in an SSBO always lands in element 0. melonDS-android-lib#N avoids it.

With the fix, Compute renders correctly on the Pixel 8 (driver r54p3) in the five games tried so far. That's still one GPU family, so the `qcom` check should probably stay until more Mali devices are tested. Happy to help with that.

**To test on a Mali device:** build with the PR, set `isAdrenoGpu = true` in `VideoPreferencesFragment`, then pick Compute at 4x in any 3D game. Without the fix you get tile-sized holes in the 3D. With it, the picture should match OpenGL.
