# Findings: Kotlin emulator area (ui/emulator/**, common/opengl/**, impl/emulator/**)

Scope reviewed: 62 files from `git diff c42995ca HEAD` in this area (3818+/1518-). All paths below are repo-relative under
`app/src/main/java/me/magnum/melonds/`; line numbers are in the current `material3` checkout.

## Note on the "debug switches" question

The native debug switches named in the task (deferred capture, compute direct dispatch, capture readback, thread pinning,
2D frame hash) are **not plumbed through this Kotlin area at all**: `grep` finds them only in `app/src/main/cpp/MelonInstance.cpp`
(debug-only toggles). The only switch-like settings that pass through here are:

- `RendererConfiguration` -> `RuntimeRendererConfiguration` (3 fields: filtering, dsScreenColors, screenShadow) in
  `ui/emulator/EmulatorViewModel.kt:917-924` -- already a single mapping, not per-switch.
- Threaded rendering / threaded 2D / hi-res (upscaled) capture: ride on `EmulatorConfiguration.rendererConfiguration` straight
  from `SharedPreferencesSettingsRepository` into `MelonEmulator.setupEmulator`/`updateEmulatorConfiguration`; nothing in this
  area touches them individually (the JNI marshalling is in `MelonEmulator.kt` / cpp, outside this area).
- WideMelon: `wideViewWidth` goes `WidescreenLaunch` -> `loadRom(..., wideViewWidth)` -> `romWideViewWidth` field ->
  `getRomEmulatorConfiguration`, plus `setWideViewWidth` live updates. One path, not duplicated.
- Screen swap: UI-only (`RuntimeLayoutView`/`DSRenderer`/`LuaUiOverrides`), persisted via `RomConfig.screensSwapped`.

So there is no per-switch settings->viewmodel->JNI copy-paste to table-drive here. What *is* repeated is the Activity-side
"read a setting imperatively on resume/settings-return" pattern (five one-line passthroughs in the VM and four `setupX()`
functions in the Activity), see Consolidation C1.

---

## 1. Dead code

- `ui/emulator/DSRenderer.kt:15` -- `import me.magnum.melonds.domain.model.SCREEN_HEIGHT` is no longer used (the UV maths now uses `FRAME_TEXTURE_HEIGHT`) -- delete the import -- S
- `ui/emulator/EmulatorViewModel.kt:4` -- `import android.util.Log` is unused (pre-existing, but the file was heavily edited) -- delete -- S
- `ui/emulator/DSRenderer.kt:395` -- leftover `// TODO: Apply rotation. Apply to background as well. Rename this class to something more generic` (edited on the branch only to drop the ExternalScreenRender mention; the class was not renamed) -- do it or drop the TODO -- S
- `common/opengl/ShaderProgramSource.kt:268-269` -- two `// What is this?` trailing comments in the scanlines vertex shader were kept while the lines were rewritten -- delete both -- S
- `common/opengl/ShaderProgramSource.kt:22` and `common/opengl/VideoFilterPipeline.kt:41` -- `LOGICAL_TEXTURE_HEIGHT` and `NATIVE_TEXTURE_HEIGHT` are both private aliases of `FRAME_TEXTURE_HEIGHT` -- use `FRAME_TEXTURE_HEIGHT` directly and drop both aliases -- S
- `ui/emulator/EmulatorViewModel.kt:1345` -- pre-existing `// TODO: Should we pause the session if the app goes to background? If so, how?` -- out of branch scope, listed for completeness -- S

## 2. Duplication and reuse

- `ui/emulator/render/ChoreographerOldFrameRenderer.kt:7-8` and `ui/emulator/render/ChoreographerVSyncFrameRenderer.kt:8-9` -- `MIN_VSYNC_PERIOD_NS = 4_000_000L` / `MAX_VSYNC_PERIOD_NS = 50_000_000L` (and their 2-line comments) are declared twice -- move to `ChoreographerFrameRenderer`'s companion next to `DEADLINE_FRAME_TIME_THRESHOLD` as a `VSYNC_PERIOD_RANGE` plus a shared `Long.asVsyncPeriodOrZero()` helper -- S
- `ui/emulator/EmulatorViewModel.kt:482-500` -- `stopEmulator()` and `stopEmulatorAndExit()` repeat the same four-line teardown (`endLuaUiSession(); performanceLogger.stopSession(); sessionCoroutineScope.cancel(); emulatorManager.stopEmulator()`), and `resetEmulatorState():763-766` repeats the first two -- extract `private fun endSession()` and call it from all three -- S
- `ui/emulator/EmulatorViewModel.kt:1083` -- `setLuaScriptOptions` refreshes the ROM with `romsRepository.getRomAtUri(rom.uri) ?: rom` whereas `onPlayerSwappedScreens:570` and `getSyncedLuaScripts:1000` use the new `getLibraryRom(rom)` whose KDoc explicitly says it exists to avoid `getRomAtUri()` re-reading/unpacking the file -- use `getLibraryRom(rom) ?: return@launch` here too -- S
- `ui/emulator/EmulatorViewModel.kt:1046,1068,1080,1122` -- `emulatorManager.getLuaScripts().getOrNull(scriptId)` builds the full status list four times just to pick one script -- add `EmulatorManager.getLuaScript(scriptId): LuaScriptStatus?` (or a private VM helper) -- S
- `ui/emulator/DSRenderer.kt:577-579` -- `renderScreenShadows()` hand-writes the three `glVertexAttribPointer` calls that `setScreenVertexAttributes(shader):532` already encapsulates (same layout, same stride) -- call `setScreenVertexAttributes(shadowShader)` -- S
- `ui/emulator/DSRenderer.kt:505-530` -- `useScreenShader` and `useOverlayShader` share the bind/filter/attrib/uniformTex preamble -- fold into one `bindScreenTexture(shader, textureId)` and keep only the extra uniform calls in `useScreenShader` -- S
- `common/opengl/ShaderProgramSource.kt:121-133` -- `SCREEN_BAND_FUNCTIONS` and `SCREEN_BAND_FUNCTIONS_300` differ only by `texture2D` vs `texture` -- generate both from one `screenBandFunctions(sampler: String)` helper, or add `#define texture2D texture` to the 300 shaders -- S
- `common/opengl/ShaderProgramSource.kt` (LinearShader:197, Xbr:329, Hq2x:421, Hq4x:510, Quilez:614, Scale2x:632, Fxaa:681, Sharp:770, Mmpx:833) -- every filter repeats the same uniform preamble `"uniform sampler2D tex;\nuniform vec2 texSize;\nuniform float topScreenWidth;\n"` -- add a `SCREEN_UNIFORMS` const -- S
- `ui/emulator/EmulatorActivity.kt:1112-1140` and `ui/emulator/render/ExternalPresentation.kt:95-120` -- `updateRendererScreenAreas()` is a near-verbatim copy (pre-existing, but the branch extended both with `isWideLayout` and `areScreensSwapped`, and both keep a parallel `isWideLayout` field: Activity:191, Presentation:42) -- move it into `RuntimeLayoutView` (`fun applyScreenAreasTo(renderer: DSRenderer)` using its own `currentRuntimeLayout.isWideLayout`), which also removes both cached `isWideLayout` fields -- M
- `ui/emulator/EmulatorActivity.kt:812-813,846-847` -- each state change is logged twice with identical strings, once to `performanceLogger.logStateChange` and once to `DebugLog.stateChange` -- add a single `logStateChange(key, value)` helper, or have `PerformanceLogger.logStateChange` forward to `DebugLog` (see C3) -- S
- `impl/emulator/EmulatorSession.kt:64-77` -- `areSaveStateLoadsAllowed()`, `areCheatsEnabled()` and the new `areLuaScriptsAllowed()` have the identical body `!isRetroAchievementsHardcoreModeEnabled || !areRetroAchievementsEnabled` -- add `private val isHardcoreActive get() = ...` and make the three delegates one-liners (keep the names: the call sites document intent) -- S
- `ui/emulator/input/TouchscreenInputHandler.kt:26,29` -- `getScreenArea(v) ?: Rect(0, 0, v.width, v.height)` is repeated for DOWN and MOVE and allocates a `Rect` per event when the fallback is taken -- extract `private fun screenArea(v: View)` and reuse a cached fallback `Rect` -- S
- `ui/emulator/EmulatorActivity.kt:1266-1275` -- the Lua scripts dialog hand-builds rows with `android.R.layout.simple_list_item_multiple_choice` + manual ripple resolution; the Compose `BaseDialog`/`DialogButton` used by `LuaScriptOptionsDialog` already provides the themed container -- rebuild as a Compose dialog next to `LuaScriptOptionsDialog` (see C2) -- M

## 3. Consolidation

- **C1** `ui/emulator/EmulatorViewModel.kt:1137-1155` + `ui/emulator/EmulatorActivity.kt:954-1000` -- five one-line setting passthroughs (`isSustainedPerformanceModeEnabled`, `isFramePacingEnabled`, `isDisplayLock60HzEnabled`, `getFpsCounterPosition`, `showStatusBar`) are each re-read by a matching `setupX()` in the Activity from three call sites (`settingsLauncher`, state -> Running, `onWindowFocusChanged`) -- expose one `data class EmulatorScreenSettings(...)` via `viewModel.screenSettings()` (or a `StateFlow` collected once) and one `applyScreenSettings()` in the Activity -- M
- **C2** `ui/emulator/EmulatorActivity.kt:1257-1340` -- `showLuaScriptsDialog` + `getLuaScriptLabel` + `closeLuaScriptOptionsDialog` are ~90 lines of view-building inside a 1443-line Activity -- move to `ui/emulator/ui/LuaScriptsDialog.kt` (Compose, driven by a `mutableStateOf` like `luaScriptOptionsDialogState`) -- M
- **C3** `ui/emulator/EmulatorActivity.kt` (lines 458-471, 647, 812-813, 846-847, 860, 920, 1409, 1434) and `impl/emulator/AndroidEmulatorManager.kt` (14 `DebugLog.event { }` calls) -- two parallel logging facilities (`PerformanceLogger.logEvent/logStateChange` and `DebugLog.event/stateChange`) are sprinkled through the same lifecycle methods with overlapping content -- decide one owner: route `DebugLog.stateChange` through `PerformanceLogger` (or vice versa) so each site logs once -- M
- `impl/emulator/AndroidEmulatorManager.kt:66-88` -- six `@Volatile` fields (`luaScriptNames`, `loadedLuaScriptCount`, `runningLuaScripts`, `luaScriptErrors`, `luaScriptHeaders`, `luaScriptOptionValues`) guarded by `luaScriptsLock` and reset together in `clearLuaScripts():531` -- replace with one immutable `private data class LuaScriptsState(...)` held in a single `@Volatile var`, updated by `copy()` under the lock; `clearLuaScripts()` becomes `state = LuaScriptsState.EMPTY` and `getLuaScripts()` needs no lock -- M
- `ui/emulator/model/LuaUiEvent.kt:3` -- the `model` package imports `LuaUiSetting` from its parent `ui.emulator` package (`LuaUiOverrides.kt:15`) -- move the `LuaUiSetting` enum into `ui/emulator/model/` next to `LuaUiEvent` -- S
- `ui/emulator/LuaUiOverrides.kt` -- one file holds `LuaUiSetting`, `LuaUiOverrides`, `LuaLayoutRequests`, `LuaLayoutRequest`, `getLuaLayoutRequest()` and `getPlayerLayoutPicks()` (the last two are free functions used only by the VM) -- split layout-request types into `LuaLayoutRequests.kt` and move `getPlayerLayoutPicks` into the VM as a private fun -- S
- `ui/emulator/EmulatorViewModel.kt:171-203` -- Lua UI state (`_luaUiEvent` channel, `luaUiEventSession`, `luaUiOverrides`, `areScreensSwapped`, `luaLayoutRequests`, `_luaLayoutId`, `luaExistingLayoutIds`) plus 7 Lua functions at 1036-1130 and the Lua branch of `startObservingLayoutForRom` make the VM 1431 lines -- consider a `LuaSessionController` collaborator owning the event channel/session counter and layout requests, exposing `luaUiEvent`/`luaLayoutId` flows -- L
- `impl/emulator/EmulatorMessageQueue.kt:25` -- `DATA_SIZE_BYTES = 512 // Must match kMaxDataLength in EmulatorMessageQueueJNI.cpp` is a cross-language magic number -- read it from native once (`external fun getMaxDataLength()`), or at least name it in `EmulatorEventType` where the per-event sizes are documented -- S

## 4. Comments (longer than one sentence, history-narrating, or stale)

The branch adds ~110 multi-sentence comments in this area (full machine-generated list with file:line:
`the comment scan (not kept)`). The ones below most need a
rewrite; the proposed replacement is the single sentence to keep. Everything in `comments.txt` not listed here should be cut
to its first sentence, which in nearly every case already carries the intent.

### Cut to one sentence (proposed replacement given)

- `common/opengl/ShaderProgramSource.kt:15-21` (7-line block on `LOGICAL_TEXTURE_HEIGHT`) -> "Height of the frame texture in DS pixels; filters that need the real texel size read `texSize` instead." (move the scaler-vs-DS-grid rationale to the commit message)
- `common/opengl/ShaderProgramSource.kt:53-59` (COLOR_FUNCTIONS) -> "`finishColor()` applies Pokefan531's nds-color profile when `COLOR_CORRECTION` is defined; both `pow()` inputs are clamped because filters can output negatives."
- `common/opengl/ShaderProgramSource.kt:81-88` -> "`screenColumns(p)` is the u range a wide frame lets the non-wide screen sample; needs `texSize` and `topScreenWidth` declared first."
- `common/opengl/ShaderProgramSource.kt:106-111` -> "Helpers that clamp samples to the screen a fragment belongs to, so the gap and the other screen never bleed in."
- `common/opengl/ShaderProgramSource.kt:153-155`, `169-171` -> "Soft shadow around each screen; `uv` is the distance outside the screen in radii." / "Draws the premultiplied Lua overlay texture over a screen quad."
- `common/opengl/ShaderProgramSource.kt:624-627`, `670-676`, `746-751`, `823-828` (Scale2x / FXAA / Sharp / MMPX prose) -> keep author + licence + URL, cut the algorithm essays to one line each ("Pre-pass; runs once per frame texel.").
- `common/opengl/VideoFilterPipeline.kt:77-88` (8-sentence KDoc on `renderPrePasses`) -> "Runs the pre-passes and returns the texture the screen shader should draw; call it before drawing to the surface and for invalid frames too."
- `common/opengl/VideoFilterPipeline.kt:186-189` -> "Full-target quad whose UVs map target texel centres onto source texel (quadrant) centres."
- `common/opengl/ShaderFactory.kt:9-14` -> "Compiles and links [source]; with [colorCorrection] `COLOR_CORRECTION` is defined in the fragment shader, and a failed build is logged but still returned."
- `ui/emulator/DSRenderer.kt:52-53`, `67-69`, `70-73`, `83-91` (field comments) -> one sentence each, e.g. `frameTopScreenWidth`: "Top-screen width of the frame the quads were built for; GL thread writes, touch input reads."
- `ui/emulator/DSRenderer.kt:264-266` -> "Lay out both screens for the current frame width, swap and wide-layout flags (see `getScreensDrawing`)."
- `ui/emulator/DSRenderer.kt:348-352` -> "3x3 grid of cells around the screen minus the centre; each vertex carries its distance outside the screen in radii."
- `ui/emulator/DSRenderer.kt:449-450` -> "Pre-passes must run before anything is drawn to the surface; also called for invalid frames to clear the cache."
- `ui/emulator/EmulatorViewModel.kt:175-177` -> "Unbounded, since a script's `on_load` can send several requests at once; events are tagged with their session so stale ones are dropped."
- `ui/emulator/EmulatorViewModel.kt:186-188`, `192-195`, `198-200` -> "Script UI overrides, kept in the VM to survive activity recreation." / "Current swap state as shown, restored after recreation." / "Layout a script asked for, or null for the player's."
- `ui/emulator/EmulatorViewModel.kt:845-847`, `869-871`, `947-949`, `958-960`, `995-997`, `1000-1002`, `1015-1018` -> first sentence only.
- `ui/emulator/EmulatorViewModel.kt:1050-1052`, `1063-1065` -> move the "Returns false if..." part to `@return`.
- `ui/emulator/EmulatorActivity.kt:274-277` (`resetFastForward`) -> "Turns fast-forward off when a session starts, re-applying any request this session's scripts already made."
- `ui/emulator/EmulatorActivity.kt:401-404` -> "Pause behind the pause menu when something else takes the audio; the player resumes from the menu."
- `ui/emulator/EmulatorActivity.kt:229-231`, `284-286`, `420-422`, `477-479`, `768-770`, `1081-1084`, `1253-1256` -> first sentence only.
- `ui/emulator/EmulatorActivity.kt:977-982` (`updateDisplayModeLock`) -> "Pins the window to the display's 60 Hz mode while frame pacing is on and fast-forward is off." Also stale/hedging: "that hasn't been verified on every device" must go.
- `ui/emulator/LuaUiOverrides.kt:21-32` (6-sentence class KDoc with bullet list) -> "Per-setting stack of script requests over a player baseline; the newest running script wins and the player's own change clears them."
- `ui/emulator/LuaUiOverrides.kt:141-148` -> "Same rules as [LuaUiOverrides] for the layout a script asked for; no baseline because null means the player's layout." (The "Kept apart because..." sentence narrates design history.)
- `ui/emulator/LuaUiOverrides.kt:233-239` -> "Emits when the player picks a layout by hand mid-session (global-setting change only; edits and deletions don't count)."
- `ui/emulator/LuaUiOverrides.kt:99-102`, `132-135`, `213-216`, `220-223` -> first sentence only.
- `ui/emulator/RuntimeLayoutView.kt:169-171`, `224-229`, `251-253`, `364-367`, `441-444` -> first sentence only (e.g. 224: "Replays the touches still down onto the new views and then sends the releases the old views made, except for inputs held again.").
- `ui/emulator/component/EmulatorAudioFocusManager.kt:10-19` -> "Holds audio focus for the Oboe stream, which doesn't handle focus itself, so a call or another media app pauses the game." Keep `@param` lines but cut each to one sentence.
- `ui/emulator/component/EmulatorAudioFocusManager.kt:25-28`, `42-44`, `57-60` -> first sentence only.
- `ui/emulator/input/MultiButtonInputHandler.kt:12-19` (7 sentences) -> "Four buttons with overlapping circular hit areas; [spacing] (0..1) narrows the band in which a touch presses two of them."
- `ui/emulator/input/MultiButtonInputHandler.kt:127-129` -> "Max distance difference inside an overlap is `2r - centreDistance`; opposite circles never overlap."
- `ui/emulator/render/FrameRenderCallback.kt:7-19` (`@param` block, 1107 chars) -> one clause per `@param`; say "0 if the frame is not valid" once in the function doc.
- `ui/emulator/render/FrameRenderCoordinator.kt:21-24`, `119-121` -> first sentence only; `65-70` -> `@param vsyncPeriodNanos The vsync period, or 0 if unknown.`
- `ui/emulator/EmulatorSurfaceView.kt:52-54`, `125-126`; `ui/emulator/render/EmulatorRenderer.kt:13-15`; `common/opengl/Shader.kt:46,55,65`; `common/opengl/VideoFilter.kt:3-9,14-17`; `common/opengl/VideoFilterPipeline.kt:10-12,29-33,243-244,279-282`; `impl/emulator/EmulatorMessageQueue.kt:50-53`; `impl/emulator/AndroidEmulatorManager.kt:66-69,400-401,425-427,458-460,498-500`; `ui/emulator/model/LuaUiEvent.kt:5-7,10-12`; `ui/emulator/render/ChoreographerOldFrameRenderer.kt:47-48`; `ui/emulator/render/ChoreographerVSyncFrameRenderer.kt:36-38`; `ui/emulator/ui/LuaScriptOptionsDialog.kt:26-29` -> cut to first sentence each.

### History / plan-doc narration or stale wording

- `ui/emulator/EmulatorActivity.kt:977-982` -- "that hasn't been verified on every device" (test-status narration) -- delete the clause -- S
- `ui/emulator/LuaUiOverrides.kt:146-147` -- "Kept apart from [LuaUiOverrides] because..." explains a past design decision -- delete -- S
- `ui/emulator/EmulatorActivity.kt:1265` -- "The checkbox row the old list used" refers to a previous implementation -- delete -- S
- `ui/emulator/EmulatorActivity.kt:1267` -- "A list gave its rows a touch ripple, which plain views don't have" narrates the old implementation -- "Plain views have no ripple, so one is set." -- S
- `common/opengl/ShaderProgramSource.kt:268-269` -- `// What is this?` -- delete -- S
- `ui/emulator/render/GlContext.kt:34` -- edited from "Used for front-buffer rendering" to a 2-clause sentence -- "Pbuffer so the context can be made current to free GL objects without a window surface." -- S
- `domain/model/EmulatorConfiguration.kt:26-28` (outside area, noted in passing) -- "WideMelon:" product-name prefix and 3-sentence comment on a field -- S

## 5. Polish

- `ui/emulator/EmulatorActivity.kt:33-36, 75, 111-114, 139` -- new imports inserted out of alphabetical order (`mutableStateOf` after `remember`, `impl.debug.DebugLog` after `impl.layout.*`, `model.LuaUiEvent`/`ui.LuaScriptOptionsDialog*` in the middle of the `model.*` block) -- run the IDE import optimiser -- S
- `ui/emulator/EmulatorActivity.kt:1266,1268,1271` -- fully-qualified `android.util.TypedValue()`, `android.R.layout.simple_list_item_multiple_choice`, `androidx.appcompat.R.attr.borderlessButtonStyle` inline -- import them (or remove with C2) -- S
- `ui/emulator/EmulatorActivity.kt:288-301` -- `releaseFastForwardHold()` is a public wrapper around private `setFastForwardHoldActive(false)` with its own 2-sentence KDoc -- make `setFastForwardHoldActive` the public entry point or inline the wrapper -- S
- `ui/emulator/EmulatorActivity.kt:1104-1108` -- `swapScreen()` (singular) vs `swapScreensForPlayer()` / `RuntimeLayoutView.swapScreens()` (plural) -- rename to `swapScreens()` -- S
- `ui/emulator/input/FrontendInputHandler.kt:24-27` -- `when (key) { FAST_FORWARD_HOLD -> ...; else -> {} }` for a single case -- `if (key == Input.FAST_FORWARD_HOLD) onFastForwardHoldReleased()` -- S
- `ui/emulator/input/MelonTouchHandler.kt:18-25` -- two sequential `if`s on `key` -- a single `when (key) { HINGE -> {}; TOUCHSCREEN -> { onInputUp; onScreenRelease }; else -> onInputUp }` -- S
- `impl/emulator/AndroidEmulatorManager.kt:136,390` -- `?: "?"` placeholders surface in a toast (`lua_script_switched_off`) and in logs -- use `"script #$scriptId"` / the full uri string -- S
- `impl/emulator/AndroidEmulatorManager.kt:259` -- `getRewindWindow()` hops to `Dispatchers.Default` with a comment about the emulator thread parking; all other blocking JNI calls in the file use `Dispatchers.IO` -- use `IO` for consistency (or document why `Default`) -- S
- `common/opengl/ShaderFactory.kt:16` -- `buildShaderProgram(...)!!` to satisfy the nullable helper -- invert: make `buildShaderProgram` non-null and have `createShaderProgramOrNull` do the failure check/cleanup -- S
- `common/opengl/ShaderProgramSource.kt:329-334 vs 421-430` -- Xbr computes `columns` per fragment while Hq2x/Hq4x pass it as a varying ("same for the whole quad"); Quilez:614 recomputes `screenColumns(uv)` inside `getTexel` per call -- pick one convention (varying) -- S
- `ui/emulator/EmulatorViewModel.kt:1153` -- `showStatusBar()` is a boolean getter without `is`/`should` prefix among `isFramePacingEnabled()` etc. -- rename `isStatusBarShown()` -- S
- `ui/emulator/model/RuntimeInputLayoutConfiguration.kt:16` -- `isWideLayout: Boolean = false` is the only defaulted field in the data class and the default is never relied on -- drop the default -- S
- `ui/emulator/ui/LuaScriptOptionsDialog.kt:26-29` -- KDoc line wrapped mid-sentence ("restarts the script\n * with the new values") -- rewrap -- S
- `ui/emulator/ui/info/ServerCommunicationFailedUi.kt:59` -- hard-coded `"Keep playing while we retry in the background"` (pre-existing, line touched by the M3 migration) -- move to `strings.xml` -- S
- `ui/emulator/EmulatorActivity.kt:458-471` -- anonymous `SurfaceHolder.Callback` added solely for perf-log events next to `EmulatorSurfaceView`, which already implements `SurfaceHolder.Callback` -- log from `EmulatorSurfaceView`'s existing callbacks (inject the logger or a lambda) -- S
- `impl/emulator/EmulatorEventType.kt:78-121` -- event ids jump from 213 to 300 with no comment on the numbering scheme -- one line: "300+: Lua events" -- S

## 6. Correctness smells (confident only)

- `ui/emulator/EmulatorViewModel.kt:1083` -- `getRomAtUri(rom.uri)` for a ROM outside the library may return a *new* ROM built from the file, whose `updateRomConfig` is a no-op per `getLibraryRom`'s own KDoc; using `getLibraryRom` (see section 2) also fixes the inconsistency with `onPlayerSwappedScreens`.

---

## Top 5 highest-value items in this area

1. **Extract `updateRendererScreenAreas()` into `RuntimeLayoutView`** (`EmulatorActivity.kt:1112`, `ExternalPresentation.kt:95`): kills a 30-line copy that the branch grew by two more parameters, plus the two parallel `isWideLayout` caches. (M)
2. **Collapse the six `@Volatile` Lua fields in `AndroidEmulatorManager.kt:66-88` into one immutable `LuaScriptsState`**: removes the lock-per-field pattern and `clearLuaScripts()`, and makes `getLuaScripts()` lock-free. (M)
3. **One `endSession()` in `EmulatorViewModel.kt:482-500/763`** and use `getLibraryRom` in `setLuaScriptOptions:1083`: removes a 3-way copy of the teardown and a real behavioural inconsistency (one path re-reads/unpacks the ROM file). (S)
4. **Move the Lua scripts dialog (`EmulatorActivity.kt:1257-1340`) to Compose next to `LuaScriptOptionsDialog`** and sort the Activity imports: ~90 lines of hand-built widgets with fully-qualified `android.*` names leave the 1443-line Activity. (M)
5. **Comment pass**: ~110 multi-sentence comments were added (list in `kd/comments.txt`); cut each to its first sentence, delete the history / "not verified on every device" / "What is this?" remarks, and dedupe the `MIN/MAX_VSYNC_PERIOD_NS` and `FRAME_TEXTURE_HEIGHT` alias constants while there. (M, mechanical)
