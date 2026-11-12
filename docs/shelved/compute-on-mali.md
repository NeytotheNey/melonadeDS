# Compute renderer on Mali

Tried on 2026-11-06 and shelved. The goal was to see if the Compute renderer could run on a Pixel 9 Pro Fold, which the app currently doesn't allow. It runs, but the 3D comes out broken, and the cause isn't known yet. Nothing was committed, and Compute is still Qualcomm-only (`VideoPreferencesFragment.kt:54`).

Code at the time: app `1481b16c`, core `4c8a9f46`.

## Why Compute is hidden

The settings screen removes Compute unless `Build.HARDWARE` is `qcom`. The Pixel reports `comet`, so it never gets the option. The upstream commit that added the check (`012832e0`) says Compute "presented multiple issues on non-Adreno GPUs" but doesn't list them, so there may be more than one problem to fix.

## Test phone

Pixel 9 Pro Fold, Tensor G4, Mali-G715, Android 17, OpenGL ES 3.2 driver `v1.r54p3-00eac0`. It isn't on hand for ADB, so tests went through APKs on the NAS, with screenshots and the in-app debug log coming back.

**Next test phone (added 11-11, check still to do):** a Pixel 8 (`shiba`, Tensor G3, also Mali-G715, Android 14, driver `v1.r46p0`, older than the 9 Pro Fold's), on USB adb, serial `39071FDJH002W8`. It's set aside for this bug, so the next step here is the first Mali check on it: install a throwaway APK with the Qualcomm check forced on, push a game, and see if the missing blocks show up on this driver too. Until this work starts, it only runs OpenGL tests.

## What goes wrong

Every test used Mario Kart DS (AMCE), mainly the character select screen and the Figure-8 Circuit intro.

- It holds about 60 FPS with no crash, but with the 3D this broken that number doesn't mean much.
- The 3D is full of small square blocks. Some are missing polygons, some show the clear color or black, and some show things that should be hidden or belong elsewhere, like bits of kart in the sky.
- 2D is clean, including the HUD, text and bottom-screen map.
- At 1x each block is 8 DS pixels, which is exactly one tile, since `TileSizeForScale` gives 8 hi-res pixels at both 1x and 4x. At 4x they looked about a quarter of that size, as they should (judged by eye from screenshots).
- The log has no shader compile or link errors and no `Failed to create` line, so Compute really is running and didn't fall back to software. The only error is `Failed to allocate memory using ftruncate!` just before the game starts. It doesn't look related, but it wasn't checked.

## What we tested

| Test | How | Result |
|---|---|---|
| Remove the check | `isAdrenoGpu = true`, 4x, widescreen 308 | Runs, with broken 3D |
| Lower resolution | 1x in settings | Blocks get bigger, still broken |
| Widescreen off | Per-game setting | Still broken, so WideMelon isn't the cause |
| Shared memory sync | `memoryBarrierShared()` before both `barrier()` calls in `BinCombined` | No change |
| GPU timing | `glMemoryBarrier(GL_ALL_BARRIER_BITS)` and `glFinish()` after every dispatch in `RenderFrame` | No change |
| `computedirect` | Forced off in the same APK as the timing test | No change |

The last two ran together, so one APK ruled out both. Waiting for every step to finish didn't help, so this doesn't look like a timing problem. The steps seem to produce wrong results by themselves.

The driver's limits are also well above what Compute needs:

| Limit | Mali-G715 |
|---|---|
| Threads per work group | 1024 |
| Shared memory | 32 KB |
| Storage buffers per shader | 35 |
| Largest storage buffer | 2 GB |
| Images per compute shader | 8 |
| Work group count | 65535 on each axis |
| Work group size | 1024 on each axis |

`GL_MAX_TEXTURE_BUFFER_SIZE` wasn't logged, which matters for the first idea below.

WatermelonDS's Vulkan renderer runs fine on this phone (reported by the user). That doesn't clear our shaders, because per [plan-vulkan.md](../plan-vulkan.md) it's a graphics-pipeline renderer, not a compute one. Its 0.7.0.rc3 release notes say it dropped its old Vulkan Compute backend, without saying why.

## What we can look at next

In rough order:

1. **Check OpenGL on the Pixel.** Run Mario Kart on the OpenGL renderer at 4x. If that looks right and holds speed, the phone already has hi-res 3D, and Compute would mainly add Smooth 30 FPS and color dithering. This needs no build, and it decides how much the rest is worth.
2. **Make a log-only build.** This tells us where to look without anyone judging by eye. After a frame:
   - Log `GL_MAX_TEXTURE_BUFFER_SIZE` and the peak `numSetupIndices`.
   - Read `BinResultMemory` and `WorkDescs` back on the CPU and check them: work counts, polygon indexes in range, tile positions in bounds, and that the sorted list holds each work index once.
   - If the lists are wrong, the problem is in the steps that build them. If they're right, it's in `Rasterise` or `DepthBlend`.
3. **Try a storage buffer for `YSpanIndicesTexture`.** It's a texture buffer (`GPU3D_Compute.cpp:602-609`) sized for more than 65,536 entries. An [Arm employee says](https://community.arm.com/support-forums/f/mobile-graphics-and-gaming-forum/53228/compute-shader-mali-gpu-bug) texture buffers wrap at 65,536 on older Mali GPUs, and on newer ones with older drivers, and recommends storage buffers. This probably isn't the whole answer: a Mario Kart frame at 1x likely doesn't get that high, and this driver may not have the limit.
4. **Follow up on what step 2 finds:**
   - In binning, skip the shared mask in `BinCombined` and test every polygon group in every tile. That removes `shared` and `barrier()` altogether, since adding `memoryBarrierShared()` wasn't a full test of them.
   - Clear the tile buffers every frame, to see if the misplaced content is left over from earlier frames. Mario Kart's intro draws 3D on both screens, so old tiles could be from the other screen.
   - In `DepthBlend`, color tiles by state (no polygons, polygons but no pixels) to see where they get lost.
5. **Check code that may behave differently on Mali.** These are guesses from reading the shaders, none of them tested:
   - `CalcOffsets` writes `.y` and `.z` of a `uvec4` in a storage buffer while another invocation runs `atomicAdd` on `.w` of the same `uvec4`.
   - `BinCombined` uses signed `bitfieldInsert` and packs values into the top bits.
   - `BinCombined` redeclares `polygonIdx` in nested scopes.
6. **Widen the test.** Try other games and turn hi-res capture off, since it was on in the logged runs. Only Mario Kart was tried, and it draws 3D on both screens through capture in some scenes.
7. **Consider Vulkan instead.** [plan-vulkan.md](../plan-vulkan.md) was waiting for a Mali tester before Phase 2, and this phone could be it. That's a separate renderer of about 3,000–4,000 lines, so only worth it if OpenGL falls short here.

ANGLE would help separate a Mali OpenGL driver bug from a problem in our shaders, but it wasn't tried. The user doesn't want to enable it while it's experimental.

## What we found online

Nothing describes this bug, and there's no Pixel-specific graphics guide for developers. Apart from the Arm post above:

- A [search summary of an Android Authority article](https://www.androidauthority.com/pixel-phones-gpu-performance-boost-3537248) says Pixels don't ship the newest Mali driver. The article itself wouldn't load.
- [A downhill-revived issue](https://github.com/skuza-oss/downhill-revived/issues/47) reports r54 drivers crashing while compiling shaders on a Mali-G57. Ours compiles fine, so it's probably different.
- [Arm's Mali best practices guide](https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/arm-mali-best-practices) covers atomics and shared memory. Only a summary was read, and our shaders haven't been checked against it.
- [Android's Vulkan tools page](https://developer.android.com/games/develop/vulkan/tools-and-advanced-features) lists Android GPU Inspector, RenderDoc and Arm Performance Studio. They'd need the phone connected, and Arm's tool is aimed at performance, not wrong output.

## Before shipping any fix

- Keep testing on the Pixel itself. Fake GL and desktop GPUs have missed phone-only bugs before.
- Any shader change also needs a run on the Z Fold 8, so it doesn't hurt Adreno rendering or speed.
- Only then replace the `qcom` check, and go by GPU rather than `Build.HARDWARE`.
- Test a few more games too, since upstream's commit suggests more than one problem.

## To rebuild the test APKs

The patches weren't saved, but they're small:

- **Show Compute:** set `isAdrenoGpu = true` at `VideoPreferencesFragment.kt:54`.
- **Force indirect dispatches:** use `const bool directDispatch = false;` in `ComputeRenderer::RenderFrame`.
- **Max sync:** add `glMemoryBarrier(GL_ALL_BARRIER_BITS); glFinish();` after each dispatch in `RenderFrame`.
- **Shared sync:** add `memoryBarrierShared();` before both `barrier();` calls in `BinCombined` in `GPU3D_Compute_shaders.h`.

Then build `assembleGitHubProdDebug` for `arm64-v8a` with `-Pandroid.injected.testOnly=false`. The debug log is at Settings > Performance > Diagnostics > Record debug log.
