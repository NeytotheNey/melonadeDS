# Plan: a Vulkan 3D renderer, built on WatermelonDS's foundation, not ported from it

## Status (2026-11-01)

Plan only; no code yet. Phase 0 (the profile) is done and found no speed case on the Z Fold 8, so the renderer rests on the compatibility goal alone. By the gate below, Phase 2 needs one of two things that aren't settled yet: custom Adreno drivers (Goal 1) being wanted, or a named Mali tester. Phase 1 is cheap and harmless and can go ahead either way; its item 0 (lowering the OpenGL renderer to GLES 3.0) is useful without any Vulkan. Phase 3 (custom drivers) and Phase 4 (Vulkan presentation) are only worth it if the goal names them.

**Phase 0 result** (2026-11-01, Z Fold 8, White 2 overworld; numbers in the local `PERF-NOTES.md` "Phase 0 profile", see [README.md](README.md)). Goal 2 fails: GL renderer + driver is 10.6–11.8% of the emulator thread (~1.1 ms/frame), nearly all in `libGLESv2_adreno`, so even a perfect Vulkan path saves under ~1 ms. Goal 3 fails: the GPU stays at 160 MHz at 4x and only reaches ~342 MHz at 8x, with no change in frame time. WideMelon 768 couldn't be measured (wide width follows the layout, not the config). The phone has headroom (`work_avg` ~9.8 ms). The speed work the profile does point at is the bus/memory/interpreter bucket (~29%) and threaded 2D's `Wait2DJobs` spin (11% of the emulator thread), not the renderer.

Written after reading WatermelonDS (`SapphireRhodonite/WatermelonDS` at `9bd6a0d`, 2026-10-15) and its core fork (`SapphireRhodonite/melonDS-android-lib`, branch `DevelopSub` at `65f4b25`), against our app (`material3` at `97d1fa72`) and core (`melonade` at `1bd01df`). Paths marked `W/` are their core, `WA/` their app, `O/` our core, `OA/` our app.

## Context: what WatermelonDS actually has

Their Vulkan work is 33 commits by one author, 2026-04-10 to 2026-10-15, restarted midway from a compute rasterizer to a graphics-pipeline renderer, and the later commits are squashes with no per-change rationale. The sizes that decided it:

| Piece | Where | Lines | Here |
|---|---|---|---|
| Loader + device (dlopen'd `libvulkan.so`, `PFN_vk*` table, device profile) | `W/src/VulkanDispatch.*`, `VulkanContext.*` | ~2,500 | Borrowed, trimmed |
| Texture cache loader for the generic `Texcache<>` | `W/src/GPU3D_TexcacheVulkan.*` | 940 | Borrowed |
| The 3D renderer | `W/src/GPU3D_Vulkan.*` | **16,400** | Reference only |
| Shaders (raster vert/frag with 8 `#define`s, clear, edge, fog, capture export) and the SPIR-V build script | `W/src/GPU3D_Vulkan_*`, `WA/scripts/` | ~1,100 GLSL | Borrowed as the starting point |
| 2D rewrite that tags every pixel for GPU compositing, plus a copy of the old renderer | `W/src/GPU2D_Soft*` | ~15,000 | Not taken |
| Capture lineage tags on VRAM banks and main RAM | `W/src/GPU.*`, `NDS.cpp`, `DMA.cpp` | ~2,000 | Not taken |
| App compute compositor, swapchain presenter, frame queue | `WA/app/src/main/cpp/renderer/` | ~30,000 | Not taken |

Before deciding:

- **The renderer does not plug into melonDS's contract.** `Blit` is a no-op, `SetupAccelFrame` is empty, `SetOutputTexture` is ignored (`W/src/GPU3D_Vulkan.cpp:1708`, h:52-81). The frame leaves the core as a `VkImage` through non-virtual getters (`GetColorTargetImage`, `RetainPublishedColorTargetForPresentation`, `GetSubmittedRenderSourceByIdentity`, `SetFrameSubmissionDeferred`, cpp:744-791, 1943-2263) that only their `VulkanOutput` calls. 2D compositing happens in the app's compute shader. The core also needs 20 free functions in `namespace MelonDSAndroid` that the app must define or it does not link (`W/src/GPU3D_Vulkan.cpp:67-75`, `GPU.cpp:34-38`, `GPU2D_Soft.cpp:34-46`).
- **Their base is not ours.** Shared files differ from ours by thousands of lines (`GPU2D_Soft.cpp` 12.5k vs our 2.3k, `GPU.cpp` 2.8k vs 1.8k). A port would replace exactly the files we spent the last month on: threaded 2D, HASH2D-verified 2D accuracy, deferred capture, WideMelon's wings and wing hold, the blackmagic3 borrowings. This is the same trade [blackmagic3.md](blackmagic3.md) declined, with a bigger diff and no upstream behind it.
- **No WideMelon**, no 16:9 anywhere. `BetterPolygons` is stored but never read (`W/src/GPU3D_Vulkan.cpp:1825`, `GPU3D_AcceleratedFrontend.cpp` has no use). Anti-aliasing is the same alpha-0.5 edge trick as our GL. Captures at scale > 1 re-rasterize the whole scene at 1x (`submitFaithfulNativeProjection`, cpp:1395-1482), so a capture frame costs two renders.
- **Hard to maintain from outside.** Identifiers and env vars in Spanish (`ComposeFielPreciso3D`, `SolicitarSaltoFrameskip`, `MELON_FIEL_SIN_HILO`), ~100 game-case booleans in `VulkanCompositionInputs`, game fixes done as geometric heuristics (`isFlatDsUiPlaneTriangle`, cpp:10781) with no game-code check and no comment saying which game. Present mode prefers IMMEDIATE (tearing), there is no `VK_ERROR_DEVICE_LOST` handling, and the Gradle shader lists are stale.
- **It is GPLv3 with melonDS headers**, same as ours, so taking files is fine; keep the headers and note the source commit.
- **Our own numbers argue against expecting an fps win on the Z Fold 8.** The 10-31 A/B ([blackmagic3.md](blackmagic3.md), "GL texture cache") had the GPU at its lowest clock and at most 43% busy at OpenGL 4x in White 2. A faster GPU API cannot move a frame that is not GPU-bound. What Vulkan can buy on this device is listed under Goal below; the fps case exists for weaker GPUs (Mali, older Adreno) and for 8x.

**Decision: build our own Vulkan 3D renderer that honors the existing `Renderer3D` contract, and feed the unchanged `GLCompositor`, `FrameQueue`, `DSRenderer`, screenshot and rewind paths through an `AHardwareBuffer` shared between Vulkan and GL.** Borrow their loader, device setup, texture-cache loader, shader build script and shader sources as the foundation; take their renderer only as a reference for pipeline state and the parts that bit them (fence timeouts, descriptor paging, Adreno quirks). Do not take the 2D rewrite, capture lineage, compositor, presenter, frame-queue rewrite, librashader, frameskip or DRS.

Why this and not the 16k-line renderer with its outputs adapted: ours is a 1:1 transliteration of `O/src/GPU3D_OpenGL.cpp` (1,815 lines, every pass of which we have verified on the phone this month, with WideMelon and the clear bitmap built in), so it is ~3–4k lines we understand against 16k we don't, and the only genuinely new code is the GL/Vulkan handshake. If Phase 2 stalls, the fallback is their renderer behind the same handshake, with their identity/hint virtuals stubbed; it is written down under **Parked** so the option is not lost.

## Goal and gate (Phase 0, done)

The four possible goals, each of which changes the plan. Phase 0 measured 2 and 3; the results are in **Status** above.

1. **Custom drivers (Turnip / newer Adreno blobs) on Qualcomm.** Vulkan-only by nature. This is the one thing a GL renderer can never give, and it sidesteps vendor GLES bugs like the wing-hold word-0 bug in [MODS.md](../MODS.md#widemelon). If this is the goal, Phase 3 is in scope and the gitHub flavor needs `useLegacyPackaging`/extracted native libs for the adrenotools hook (`WA/app/build.gradle.kts`, `AdrenoVulkanDriverSupport.kt`).
2. **Driver CPU overhead on the emulator thread.** Measured with simpleperf at the usual spot, OpenGL 4x, summing the samples under `libGLESv2_adreno`/`libllvm-glnext`/`libgsl`; under ~10% of emu-thread cycles meant Vulkan would not change `work_avg_ms` here. It came out at 10.6–11.8%: fails.
3. **8x and wide 3D.** The question was whether the GPU leaves its idle clock at 8x or WideMelon 768. At 8x it reached only ~342 MHz with no change in frame time, so there is no GPU-side win on this device; WideMelon 768 couldn't be measured. Fails.
4. **Other people's devices.** If the target is Mali/PowerVR phones, we cannot measure it here; the gate becomes "parity with GL on the Z Fold 8 plus a tester with a Mali phone".

Gate: proceed to Phase 2 if (1) is wanted, or (2)/(3) show a measurable cost, or (4) has a named tester. Otherwise stop after Phase 1 (which is cheap and harmless) and park the rest.

### Stated goal (2026-11-01): compatibility with more hardware, and a modern API

Recorded as the reason. What it implies:

- **Vulkan does not reach devices GLES 3.2 misses.** Android devices that ship Vulkan 1.1 (2018 onward) all have GLES 3.2; the devices our settings screen hides the GL renderer from (`VideoPreferencesFragment.kt:22,43`, `reqGlEsVersion < 0x30002`) are older ones that have GLES 3.0/3.1 and at best Vulkan 1.0. The cheaper compatibility win is independent of Vulkan: our GL renderer uses nothing 3.2-specific. The shaders are `#version 320 es` by habit (`O/src/GPU3D_OpenGL_shaders.h:22`, `GPU_OpenGL_shaders.h:24,52`) and the only 3.2 entry point is `glFramebufferTexture`, which `glFramebufferTexture2D` replaces (`O/src/GPU3D_OpenGL.cpp:431-439, 1409, 1455`, `GPU_OpenGL.cpp:304, 390, 535`). Drop the GL renderer's gate to 3.0 (compute stays 3.1 and Qualcomm). That goes first, as **Phase 1 item 0**.
- **Where Vulkan does help compatibility** is driver quality, not device count: a native Vulkan path on devices whose vendor GLES is buggy (the Adreno wing-hold word-0 bug in [MODS.md](../MODS.md#widemelon) is the kind of thing), and custom Turnip drivers on Qualcomm phones whose stock driver is broken for a game. So Phase 3 stays in scope for the gitHub flavor. The counterweight: on cheap Mali and PowerVR the Vulkan driver is often worse than the GLES one, so Vulkan is offered next to OpenGL, never instead of it, and OpenGL stays the default.
- **"Modern" has a concrete form.** Google made Vulkan Android's official graphics API in 2025, with GLES increasingly served through ANGLE on top of Vulkan. On such devices our GL path runs through a translation layer and inherits ANGLE's bugs. Test this now, on the Z Fold 8: Developer options → "Enable ANGLE" for melonadeDS (Android 15+), run the Phase 2 checklist scenes on the OpenGL renderer and note what breaks. That is the best preview we have of future phones, and any breakage there is a point for the Vulkan path.
- **Design consequences for Phase 2:** Vulkan 1.1 core features only; no dependence on dynamic descriptor indexing or timeline semaphores (keep their fallbacks, `W/src/VulkanContext.cpp SetCapabilityOverrides`); no Adreno-only tuning; every pipeline must pass on the x86_64 emulator's SwiftShader, which is the strictest conformant implementation we can run without a tester; validation layers clean before any phone test.
- **Testers.** Compatibility cannot be shown on one Snapdragon phone. Before Phase 2 ends, name one Mali tester (a Samsung Exynos or MediaTek Dimensity phone) and, if possible, one PowerVR; parity screenshots on their devices are part of the Phase 2 exit.

### Profile recipe (Phase 0, one phone session)

One simpleperf capture of the emulator thread, split four ways. It answers Goals 2 and 3 above and the separate question of whether JIT or 2D work would pay. Numbers went into the local `PERF-NOTES.md` under "Phase 0 profile", and the conclusion into **Status** above. The same recipe is the before/after measurement for Phase 2.

Setup, same as the 10-31 A/B: fresh app start, White 2 at the usual overworld spot, OpenGL 4x, JIT on, fast-forward off, performance logging on so `_summary.txt` comes out of the same run. Debug build (`gitHubProdDebug`, package `me.magnum.melonds.dev`): it is debuggable, so simpleperf attaches without root, and native code is RelWithDebInfo with symbols (`app/build.gradle.kts:70`; keep the unstripped `.so` from `app/build/intermediates/merged_native_libs/` for `--symfs`). Two runs: threaded 2D off, then on.

```bash
adb shell pidof me.magnum.melonds.dev
```

```bash
adb shell "ps -T -p $(adb shell pidof me.magnum.melonds.dev) | grep -E 'EmulatorThread|GPU2DThread'"
```

```bash
adb shell simpleperf record --app me.magnum.melonds.dev -t <EmulatorThread tid>,<GPU2DThread tid> -e cpu-clock -f 4000 --duration 60 -o /data/local/tmp/melon.data
```

```bash
adb pull /data/local/tmp/melon.data && simpleperf report -i melon.data --symfs <dir with unstripped libmelonDS-android-frontend.so> --tids <EmulatorThread tid> --sort dso,symbol -n | head -80
```

In parallel during the 60 s, GPU clock and busy from the Adreno sysfs, once a second:

```bash
adb shell 'for i in $(seq 60); do echo "$(cat /sys/class/kgsl/kgsl-3d0/gpuclk) $(cat /sys/class/kgsl/kgsl-3d0/gpubusy)"; sleep 1; done'
```

(`gpubusy` prints busy and total cycles; busy % is the ratio. Also note which core `EmulatorThread` sits on from `ps -T -o tid,psr,comm`, and that core's `scaling_cur_freq`, so a clock drop is not read as a code change.)

Bucket the `EmulatorThread` report by dso and symbol prefix; everything should land in one of these:

| Bucket | What counts |
|---|---|
| JIT-generated code | samples in anonymous executable memory (dso shown as `[anon]`/unknown, no symbol); that is the recompiled ARM code |
| Bus, memory and interpreter fallbacks | `ARMJIT_Memory`, `NDS::ARM9Read/Write*`, `ARM7Read/Write*`, `ARMv5::`, `ARMv4::`, `ARMInterpreter::`, `CP15`, `DMA`, `ARMJIT::` (block lookup, invalidation, compile) |
| 2D | `GPU2D::SoftRenderer`, `GPU2D::Unit`, `GPU::Snapshot2D*`, `Sync2D`; with threaded 2D on, report `GPU2DThread` separately and count only what stays on `EmulatorThread` |
| 3D geometry (CPU) | `GPU3D::` except the renderer classes: `SubmitVertex`, `SubmitPolygon`, clipping, `ExecuteCommand`, `VCount144` |
| GL renderer + driver | `GLRenderer::`, `GLCompositor::`, `Texcache`, plus the vendor libraries `libGLESv2_adreno.so`, `libllvm-glnext.so`, `libgsl.so`, `libEGL_adreno.so`, `libEGL.so`, `libGLESv2.so` |
| Other | SPU, audio, JNI, Lua, `libc` memcpy (attribute to the bucket of its caller if `--call-graph fp` is added), kernel |

Reading it:

- **GL renderer + driver** ≥ 15% of `EmulatorThread`: Goal 2 holds, a Vulkan renderer can take a real slice. Under 10%: it cannot, and the renderer decision rests on compatibility alone.
- **GPU busy** under ~50% at the lowest `gpuclk` at 4x: Goal 3 fails at 4x; repeat at 8x and WideMelon 768 before concluding.
- **JIT-generated code** ≥ 50%: the ceiling is codegen quality, which is the hard project described in the chat of 2026-11-01 and not planned. Between 20 and 50% with a fat **bus/memory** bucket: the cheaper work is in the memory handlers and block lookup, not the backend.
- **2D** ≥ 25% with threaded 2D off and the `GPU2DThread` run showing it moved off the emulator thread with `work_avg_ms` down: make threaded 2D the default.
- `work_avg_ms` from `_summary.txt` under ~10 ms at 4x with `fps_1pct_low` at 60: the phone has headroom and none of this is needed for speed on it.

## Rules

- Same workflow as before: coder subagent → fresh reviewer → fixes → host build → phone. Core first (`melonDS-android-lib`, branch off `melonade`), then the submodule bump here.
- **No change to the GL, compute or software renderers, the compositor, `FrameQueue`, `DSRenderer` or the Kotlin presenter.** Vulkan is a fourth `Renderer3D` and nothing else sees it. If a step wants to touch those, it is the wrong step.
- New core option `ENABLE_VKRENDERER`, default ON only when `ANDROID`; OFF on the host so the host build, the 140 unit tests and `.claude/shadercheck` keep working without a Vulkan SDK. Everything Vulkan is behind `VKRENDERER_ENABLED`.
- The loader is dlopen'd, never linked (`VK_NO_PROTOTYPES`, `W/src/VulkanDispatch.cpp:966-1111`). The app must keep working on a device with no `libvulkan.so` or no suitable device: Vulkan is then hidden in settings and a stored `vulkan` pref falls back to OpenGL with a toast, like the compute renderer on non-Qualcomm.
- Shaders: GLSL sources in the repo, SPIR-V generated by a script into committed `*ShaderData.h` headers (their scheme, `WA/scripts/regenerate_vulkan_spirv.sh`), plus a check that the headers match the sources, run next to `.claude/shadercheck/check.sh`. No runtime GLSL compiler in the APK.
- Every borrowed file keeps its GPL header and gets one line naming the origin (`SapphireRhodonite/melonDS-android-lib 65f4b25`); list them in a new `ACKNOWLEDGEMENTS.md`, and move the WideMelon credit from [MODS.md](../MODS.md) there too.
- Bounded waits everywhere (`vkWaitForFences` with a timeout, their `kFenceWaitTimeoutNs` lesson from `e94937b`); a timeout or `VK_ERROR_DEVICE_LOST` reports `EVENT_RENDERER_INIT_FAILED`-style and drops to OpenGL instead of hanging the emulator thread.
- Measure before and after on the phone with the profile recipe above; parity screenshots against OpenGL at the same scale for every scene in the checklist.

## Phase 1: foundation (device-free)

Borrow, trim, compile. Nothing renders yet.

0. **Lower the GL renderer's gate to GLES 3.0** (see Stated goal): `#version 300 es` where nothing 3.1+ is used, `glFramebufferTexture2D`, gate `0x30000` for OpenGL and `0x30001` for compute; check the compositor's integer textures and `texelFetch` are 3.0 (they are). Its own commit, phone check that White 2 still renders identically.

1. **Loader and device.** Take `W/src/VulkanDispatch.{h,cpp}` and `VulkanContext.{h,cpp}` into `O/src/`. Trim `VulkanContext`: drop the hard requirement on `VK_KHR_android_surface`/`VK_KHR_swapchain` (`W/src/VulkanContext.cpp:19-26`; we do not present with Vulkan in this plan) and the dedicated present queue; keep Vulkan 1.1 (cpp:200), the device profile (Adreno/Mali/PowerVR flags), `FindMemoryType`, the queue mutex, timeline-semaphore detection, and the `DriverConfiguration` hook for Phase 3. Add detection for the three extensions Phase 2 needs: `VK_ANDROID_external_memory_android_hardware_buffer`, `VK_KHR_external_semaphore_fd`, `VK_KHR_external_fence_fd` (all mandatory on Android 9+ devices that ship Vulkan 1.1, but gate on them anyway). ~1,600 lines after trimming.
2. **Texture cache loader.** Take `W/src/GPU3D_TexcacheVulkan.{h,cpp}` as is (RGBA8UI array images, 8 staging slots with fences, CPU mirror). Take the dirty-mask index fix from `W/src/GPU3D_Texcache.h` (`vramSize / VRAMDirtyGranularity / 64`) **after checking it against upstream melonDS** `src/GPU3D_Texcache.h`; if upstream has the same bug it is a separate commit that also helps compute. Skip `UpdateWithInvalidationCallback` unless Phase 2 needs it.
3. **Pipeline policy.** Take the constants from `W/src/VulkanPipelinePolicy.h` that matter (descriptor paging for the texture cache, 3 contexts → 1 for us) and drop the rest.
4. **Shader build.** Take `WA/scripts/regenerate_vulkan_spirv.sh` (glslc from the NDK's `shader-tools`, `spirv-opt -O`, `xxd -i`), cut it to our shader list, and add a `check` mode. Hook it as a CMake custom target in the core, not Gradle's `preBuild` (their Gradle list rotted; one source of truth). Put the check into `.claude/shadercheck/check.sh`.
5. **CMake.** `ENABLE_VKRENDERER` in `O/CMakeLists.txt` next to `ENABLE_OGLRENDERER:68`; sources and `VK_NO_PROTOTYPES` PUBLIC in `O/src/CMakeLists.txt`; Vulkan headers from the NDK sysroot (no SDK). In `OA/app/CMakeLists.txt` (95 lines) nothing links `vulkan`; add `android` is already there for `AHardwareBuffer_*`, which lives in `libandroid` (API 26+, we are minSdk 29). Add the validation layer define for debug builds only (`MELONDS_VULKAN_ENABLE_VALIDATION`, `W/src/CMakeLists.txt:213`); the layer itself is pushed per-app with `adb` from the NDK; write the steps down next to this plan when it happens.
6. **Enum and config plumbing, no behavior.** `OA/app/src/main/cpp/renderer/Renderer.h`: `Vulkan = 3` (keep `Compute = 2`; the int crosses JNI and stored prefs; theirs uses 2 for Vulkan and 3 for Compute, don't copy that). `Configuration.h:13-30, 79-99`: `VulkanRenderSettings{scale}`; `MelonDSAndroidConfiguration.cpp:158-190` builds it. Kotlin: `VideoRenderer.VULKAN(3)`, value `vulkan` in `res/values/arrays.xml:32-47` and `strings.xml:642-656`, `SharedPreferencesSettingsRepository.kt:336` maps it by `valueOf` already. `VideoPreferencesFragment.kt` hides it unless the capability probe says yes.
7. **Capability probe.** One JNI call modeled on `WA/.../MelonDSAndroidInterface.cpp getRendererCapabilities()`: can dlopen, can create a 1.1 instance, some physical device has a graphics queue plus the three extensions from item 1. Cached per process. Used by the settings screen and by the launch precondition check (`WA/.../EmulatorLaunchPreconditionChecker.kt:135-150` is the pattern; ours has no such checker yet, so a sanitizer in `getVideoRenderer` that falls back to OpenGL is enough for now).

Done when: arm64 and x86_64 build with the option on, host build with it off, the probe returns true on the Z Fold 8 and on the x86_64 emulator (SwiftShader), `vulkan` shows in settings and selecting it falls back to OpenGL with a log line (the renderer does not exist yet).

## Phase 2: the renderer (`O/src/GPU3D_Vulkan.{h,cpp}`)

A transliteration of `O/src/GPU3D_OpenGL.cpp`, pass for pass, into Vulkan, behind the same `Renderer3D` virtuals. Keep its structure and names so the two can be diffed side by side.

1. **Scene build.** Reuse the GL renderer's own vertex and index building, including `WideVertexX`/`WideXAdjust`, `HiresPosition` with the `0x3FFF` clamp, the depth-equal margin and the shadow-mask sequence in `RenderSceneChunk`. Not `W/src/GPU3D_AcceleratedFrontend.cpp`: it has no WideMelon and expands lines into quads differently from what we verified. Vertex, index and the `ShaderConfig` UBO go into persistently mapped host-visible buffers (one set per in-flight frame, 2 frames).
2. **Passes.** Same order as GL: clear (plain or DISP3DCNT bit 14 bitmap from texture VRAM slots 2/3), opaque Z and W pipelines with `uRenderMode`/depth bias as push constants, translucent with the stencil shadow-mask sequence, edge marking, fog, the AA alpha-0.5 trick; attribute target for polygon ID/fog/edge, D24S8 depth-stencil. Cross-check each pass's pipeline state against the equivalent in `W/src/GPU3D_Vulkan.h:640-760` (they list every stencil variant: ShadowMask, ShadowClear, ShadowBlend, BgZero, DepthComplement, StencilBitClear); where ours differs, understand why before moving on.
3. **Textures.** Use the texture cache (Phase 1 item 2) with NEAREST samplers, not the GL in-shader decode. Our compute renderer already runs on the same `Texcache` logic in this core, so the dirty tracking is proven. Batches split on texture; that is the same batching our compute path does.
4. **Shaders.** Translate `O/src/GPU3D_OpenGL_shaders.h` (815 lines, already `#version 320 es` with `layout(location)` outputs) to Vulkan GLSL: descriptor sets and bindings, push constants, `gl_FragDepth` unchanged, keep the `.bgra`-equivalent channel order so the compositor's `texelFetch(_3DTex).bgra` (`O/src/GPU_OpenGL_shaders.h:83, 284`) needs no change. Their `GPU3D_Vulkan_GraphicsRasterShader.frag` (640 lines, 8 defines) is the reference for how they expressed the same passes; don't take the 13-variant matrix, two programs (Z/W) plus a toon switch is what we verified in Batch 2.
5. **Output image and the GL handshake** (the only new design in this plan). Allocate the color target as an `AHardwareBuffer` (`R8G8B8A8_UNORM`, `GPU_COLOR_OUTPUT | GPU_SAMPLED_IMAGE`), import it into Vulkan with `VK_ANDROID_external_memory_android_hardware_buffer` (dedicated allocation, `VK_QUEUE_FAMILY_FOREIGN_EXT` ownership release after the last pass), and into GL on the emulator thread's existing EGL context with `eglGetNativeClientBufferANDROID` → `eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID)` → `glEGLImageTargetTexture2DOES(GL_TEXTURE_2D)`. `SetupAccelFrame()` binds that texture on unit 1, exactly what `GLRenderer::SetupAccelFrame` does at `O/src/GPU3D_OpenGL.cpp:1810`; `GLCompositor::RenderFrame` (`O/src/GPU_OpenGL.cpp:465-529`) is untouched. Two color targets, alternating per frame.
   Sync, both directions, GPU-side only:
   - Vulkan → GL: export the submit's fence as a sync fd (`VK_KHR_external_fence_fd`), wrap it with `eglCreateSyncKHR(EGL_SYNC_NATIVE_FENCE_ANDROID)`, and `eglWaitSyncKHR` it in `SetupAccelFrame` before the compositor draws. No CPU wait.
   - GL → Vulkan: after `Blit`, `eglDupNativeFenceFDANDROID` on a GL fence and import it into a binary semaphore (`VK_KHR_external_semaphore_fd`, SYNC_FD) that the next submit into the same target waits on. This is why there are two targets.
   `FlushCommands()` becomes the `vkQueueSubmit` (the GL path's `glFlush`), so the existing `BlitFlushCapture` ordering in `GPU.cpp` keeps working.
6. **Capture.** `PrepareCaptureFrame()`: `vkCmdBlitImage` the center 256 columns down to 256×192 with linear filtering (what `O/src/GPU3D_OpenGL.cpp:1739-1754` does with `glBlitFramebuffer` + `glReadPixels`), then `vkCmdCopyImageToBuffer` into a host-visible buffer. `GetLine(0)` waits that fence with a bounded timeout, maps once, converts to the 6-bit format like `:1782`. Same contract the deferred-capture code in `GPU.cpp:1150-1175` relies on. Their compute pack shader (`GPU3D_Vulkan_CaptureLineExportShader.comp`) is the upgrade if the CPU convert shows in `work_avg_ms`; their 1x re-render is not wanted.
7. **Scale and WideMelon.** `SetRenderSettings(scale)`, `SetViewWidth`, `ResolveViewWidth`, `TargetFits` against `maxImageDimension2D`, `GetViewWidth`/`GetOutputWidth`. A change waits for the device to idle and reallocates both targets and the EGLImages. Wing hold needs nothing: it lives in the compositor.
8. **Pipelines and first-run.** Build pipelines through `NeedsShaderCompile`/`ShaderCompileStep` so the existing "compiling shaders" UI covers the first start; `VkPipelineCache` persisted with `Platform::OpenLocalFile` keyed by vendor/device/driver like `W/src/GPU3D_Vulkan.cpp:4307` and our `shadercache`.
9. **Errors.** Any `VK_ERROR_DEVICE_LOST`, timeout or failed allocation: log, tear down, return `nullptr` from `New()` or flag the instance; `MelonInstance::updateRenderer` (`OA/.../MelonInstance.cpp:946-1021`) already remembers a failed renderer and falls back to software; make it fall back to OpenGL for Vulkan. Validation layer on in debug builds for the whole phase.
10. **App glue.** `MelonInstance::updateRenderer`/`runFrame` get `Vulkan` switch arms (`static_cast<VkRenderer&>` like the others); `SetOutputTexture` stays as it is because the compositor still draws into the app's frame texture. `ScreenshotRenderer.cpp:72-137`, rewind readback, Lua overlay, external display: no change, they consume the frame texture after the compositor. `isWideRenderer` (`EmulatorViewModel.kt:977-983`) is already "not software".

Size: ~3–4k lines C++ plus ~900 lines GLSL and the generated headers. Order inside the phase: 5 first with a solid-color clear (proves the handshake end to end on the phone, including fold/unfold reallocation), then 2 opaque-only, then textures, then translucent/shadow/edge/fog, then 6, then 7.

Phone check, each against OpenGL at the same scale: White 2 overworld and battle (dual-screen 3D, capture), Phantom Hourglass top screen (wing hold with WideMelon on, the Adreno word-0 scene), a shadow-volume game (Zelda, Mario Kart), fog (Metroid Prime Hunters), toon/highlight, a DISP3DCNT bit 14 game if one was found, 1x/4x/8x, WideMelon 256→768, mid-game switch soft↔GL↔compute↔Vulkan, savestate, rewind thumbnails, screenshot, fold/unfold, external display, fast-forward (bounded fences, no deadlock), background/foreground (EGL and Vulkan targets survive). Then the profile recipe at 4x and 8x against the 10-31 OpenGL numbers, with simpleperf driver cycles and GPU busy %.

## Phase 3: custom Adreno drivers (only if Goal 1)

Borrow, mostly as is: `WA/.../AdrenoVulkanDriverSupport.kt` (36), `AdrenoVulkanDriverManager.kt` (189: SAF zip import with the zip-slip check, `meta.json`, `libvulkan_freedreno.so`/`vulkan.adreno.so` detection), `FreedrenoSettings.kt` (32: `FD_DEV_FEATURES`/`TU_DEBUG` env before the native lib loads), the `VulkanDriverConfiguration` model, the prefs `video_vulkan_driver_mode`/`video_vulkan_custom_drivers`/`video_vulkan_selected_driver_id`, the `setupNative(...)`/`configureVulkanDriverNative` JNI into `VulkanDispatch::ConfigureDriver`, and the CMake `FetchContent` of `bylaws/libadrenotools` at `8fae8ce2` with `MELONDS_HAS_ADRENOTOOLS` for arm64 in the gitHub flavor only. Requirements they hit: API 28+, arm64, `/sys/class/kgsl/kgsl-3d0/gpu_model` starts with `Adreno`, and native libs extracted to `nativeLibraryDir` so the hook `.so`s have a path (ours only sets `useLegacyPackaging` for dex, `app/build.gradle.kts:124`; `jniLibs.useLegacyPackaging` would be new). Not Play Store.

Phone check: system driver vs a Turnip zip, same checklist subset, plus a driver that fails to load (must fall back to the system driver, not crash).

## Phase 4: Vulkan presentation (parked; needs a measured reason)

Replacing `DSRenderer` + `VideoFilterPipeline` + the EGL window surfaces with a swapchain presenter for Vulkan sessions. Their version is `WA/.../VulkanSurfacePresenter.cpp` (6,088 lines plus a 1,490-line fragment shader) and it took them the extra frame-queue machinery to pace it. It buys: no GL compositor pass, no EGL/AHB handshake, `VK_GOOGLE_display_timing`. It costs: a second copy of the video filters (xBR, HQx, LCD, scanlines… 981 lines of GLSL in `ShaderProgramSource.kt`), the external display and cover screen paths, and the overlay. Open it only if Phase 2's measurement shows the compositor+presenter pass is a real cost on this device. If it opens, the first version is the presenter only (sample the compositor's output texture through the same AHB trick in reverse), not their 2D compute compositor.

## Parked

- **Their renderer behind our handshake.** If Phase 2 is not at parity: take `W/src/GPU3D_Vulkan.{h,cpp}`, `GPU3D_AcceleratedFrontend.*` and the 13 shader variants, stub the identity/hint virtuals it added to `Renderer3D` (`W/src/GPU3D.h:88-104` and the virtuals from `FrameskipPlaceholderServido` to `UsesStructured2DMetadata`), define the `MelonDSAndroid::*` externs as no-ops, replace its published-image API with Phase 2 item 5's AHB target, and keep its CPU `LineCache` for `GetLine`. WideMelon would then be a later project inside 16k lines of someone else's code. To first picture, unknown to parity.
- **Faithful 2D + capture lineage** (`W/src/GPU2D_Soft*`, `GPU.cpp` tags): the GPU-2D decision from [blackmagic3.md](blackmagic3.md) stands; it would also replace threaded 2D and HASH2D.
- **librashader / RetroArch presets**: a Rust toolchain in the build, two patches, unrelated to the renderer. Separate plan if wanted.
- **Frameskip, dynamic render scale, ADPF hints**: not renderer work.
- **Vulkan on armeabi-v7a**: the probe can say no on 32-bit; nobody with a 32-bit phone is running 3D at scale.

## Verification

- Host: core builds with `ENABLE_VKRENDERER=OFF`, unit tests pass; `.claude/shadercheck/check.sh` validates the GLES shaders and checks the SPIR-V headers match their sources.
- Emulator (x86_64, SwiftShader Vulkan): the probe, settings gating, and a start with Vulkan selected that reaches the first frame without a validation error. Not for performance.
- Phone: the Phase 2 checklist, plus the standing set (White 2 capture scene holds 60 fps at 4x, renderer switch mid-game, fold/unfold, screenshot, savestate, rewind), and the perf recipe at 4x and 8x. Record the numbers in the local `PERF-NOTES.md` under "Vulkan Phase 2" with the Goal they answer.
