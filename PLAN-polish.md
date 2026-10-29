# Plan: polish pass

## Context

All 49 commits since upstream base `c42995ca` are fork work: 402 files, +12.3k / −3.9k lines, written fast across two days. Before threaded 2D lands (`PLAN-threaded-2d.md`), we tidy it up. That means fixing the real bugs the reviews found, removing leftovers, and cleaning the code that threaded 2D will touch (`emulate()`, the frame queue, constants).

Sources: three read-only reviews (layout editor; native/perf/rendering; settings/UI) and Android lint (`app/build/reports/lint-results-gitHubProdDebug.html`). Two of the bugs were spot-checked by hand and are confirmed: the screen stacking in `rearrangeScreens()` and the missing `perf_log_dir` restore key. The rest are the reviewers' findings, and the coder re-checks each one before changing it.

**Rules:** no new features, and behaviour stays the same unless an item is marked as a visible change. One batch is one commit (or a few). Each batch goes coder subagent → fresh reviewer subagent → fixes → build → phone check where noted. Paths below are under `app/src/main/`.

## Batch 1: bugs (do first)

Layout editor:
- `LayoutEditorView.rearrangeScreens()`: add order is reversed compared to `LayoutView.loadLayout()`, which adds screens at index 0. With "above other screen" on, the editor draws it underneath. **Confirmed.**
- `setSelectedScreenOnTop()`: clear `onTop` on the other screen, so both can't be saved as on top.
- `handleKeyDown()`: D-pad nudge should call `moveSelectedView` so the handle anchor updates.
- `DefaultLayoutProvider.buildUtilityButtons()`: `first { isScreen }` becomes `firstOrNull` with a fallback.
- Phone check only: after the first Save of a new layout, does the editor deselect the item? Fix only if it does.

Rendering:
- `DSRenderer.onSurfaceCreated()` runs again on render-mode switches and size changes. It leaks the background texture, 3 VBOs/VAOs and the background/shadow shaders. The background then probably shows black, because `isBackgroundLoaded` stays true for a new empty texture. Delete or reuse the old objects, and reload the background.

Settings:
- `impl/PendingSettingsRestore.kt` `KEPT_PREF_KEYS`: add `perf_log_dir`. **Confirmed.**
- Backup JSON has no types, so a float of exactly 0 or 1 comes back as an int. That can crash `getFloat` (layout editor menu-button position). Write backups in the typed `{type,value}` format `preferencesToJson()` already uses, and keep reading old untyped files.
- `GameMasteredPopupUi.kt:73`: text on the gold card in dark mode turns light. Give the card an explicit dark content colour.
- `SoftInputBehaviourPreferencesFragment.kt:78`: `entries[indexOf(...)]` crashes on an unknown stored value. Use `getOrElse`.

Lint errors (the Spanish rewind string goes away with the translations in Batch 2):
- `repeatOnLifecycle` misuse in `CheatsImportProgressDialog.kt:42`.
- `android:tint` → `app:tint` in `preference_top_level.xml` and `preference_widget_chevron.xml`.
- `RomsPreferencesFragment.kt:125` should use `viewLifecycleOwner.lifecycleScope`.
- `RetroAchievementsPreferencesFragment` can open a duplicate login dialog after a stop/restart.

Phone check: layout editor stacking and save, render-mode switch with a background image set, settings backup → restore, and the dark-mode mastery popup if easy to trigger.

## Batch 2: dead code and leftovers

- Delete `ui/emulator/render/ExternalScreenRender.kt` and `ExternalLayoutRender.kt`. They're never instantiated, but three fork commits kept updating them.
- Remove `SaveManager.cpp`'s unused memory-flush branch (`dst`/`dstLength`).
- Update checker (unreachable since `973f70ee`): **keep it dormant**. Remove only the empty `pref_general_updates.xml` screen load in `AppearancePreferencesFragment`, and add a short comment at `UpdatesViewModel` / `GitHub*UpdatesRepository` saying it's kept switched off on purpose.
- **Remove the other languages** (English only):
  - delete the translated `strings.xml` files in `res/values-b+*/` and any other locale `values-*` folders that only hold strings (keep `values-night`, `values-v*` and the like);
  - remove any in-app language picker or `locales_config` entries that list them;
  - set `androidResources.localeFilters` to `en` if needed.

  This also clears the lint `MissingTranslation`, `MissingQuantity`, `UnusedQuantity`, `PrivateResource` and Spanish rewind-string issues.
- Inline `ui/common/Colors.kt` wrappers that now return the M3 defaults (41 call sites).
- Unused code:
  - `VideoPreferencesFragment` injections and its two one-item renderer lists
  - `PreferenceFragmentCompat` imports
  - `LayoutEditorView.getSelectedView()`, the duplicate `deselectSelectedView`, `ScreenEditorState.isMenuShown`
  - `LayoutComponentView.setSize/getPosition`
  - the `SwitchPreferenceCompat` style and layout
  - `front_rendering`
  - the `performanceHintSession != nullptr` check after `new`
- Unused resources from lint (100 in fork files), including the old layout editor strings (`label_alpha`, `aspect_ratio_*`, …) and their translations.
- Fix stale comments: `view_layout_editor_manager.xml` "cutout insets", and `HANDOFF.md`, which still says minSdk 37 (it's 29 now) and "30 commits".

## Batch 3: native and pacing tidy (before threaded 2D)

- `MelonDSAndroidJNI.cpp`:
  - make the UI-written globals `std::atomic` (`limitFps`, `isFastForwardEnabled`, `targetFps`, `fastForwardSpeedMultiplier`, `paused`, `started`);
  - make the new fork globals `static`;
  - hoist the duplicated "unlock from running" block;
  - split `emulate()` (~190 lines) into `schedulePacedFrame` / `runTimerLimiter` / `pushPerfSample`;
  - cache the `renderFrame` `jmethodID` instead of looking it up every vsync;
  - rename `presentFrame`'s `nextFrameDueNs` to `emulatingFrameDueNs`.
- Named constants:
  - `FramePacing.h` inline values (0.005, /4, /8, 2, 2/100) and the JNI `/2` tolerance;
  - one 60 Hz constant per language, with "must match" comments between C++ and Kotlin;
  - perf sample field count and state flags shared between `MelonDSAndroidJNI.cpp` and `MelonEmulator.kt`;
  - `FLOATS_PER_VERTEX` in `DSRenderer`;
  - the 2-row screen gap in one domain constant.
- `FrameRenderCoordinator`: stop allocating a `Bundle` per vsync.
- `DSRenderer.applyRendererConfiguration()`: only rebuild the filter pipeline when the filter or colours change, not on every fold or layout change.
- `ShaderFactory`: a failed compile or link falls back to the no-filter shader instead of drawing black.
- Two smaller `VideoFilterPipeline` changes:
  - trim `saveState()` to just the framebuffer and viewport;
  - share one `PRECISION_HEADER` across the 9 shader copies.
- `SaveManager.cpp`: read the flush state under `SecondaryBufferLock`. `MelonInstance.cpp`: `localtime` → `localtime_r`. `MelonDSAudio.cpp`: add a `recreateAudioOutputStream()` helper.
- `build.gradle.kts`: move the frontend to `-std=c++20` (the code already uses C++20 features), and scope `useLegacyPackaging` to debug.

Phone check: a perf log at the White 2 spot. Pacing judder and `work_avg_ms` should match the last run.

## Batch 4: layout editor cleanup

- Duplication:
  - the seek bar listeners and 1x/2x/3x wiring in `LayoutEditorItemControls`;
  - the "panel or pill" reposition;
  - the width and height range calls;
  - clamp-inside and params-rect helpers in `LayoutEditorView`;
  - the Save / Save-and-exit paths in `LayoutEditorManagerView` and `LayoutEditorActivity`;
  - repeated `onStoreLayoutChanges()`;
  - `RaisedButtons` builders and the lip/travel pairs;
  - `Rect.overlaps` moved into `domain/model/Rect.kt`.
- Magic numbers and consistency:
  - drag threshold `25` px → `touchSlop`;
  - name the 0.5 unselected alpha;
  - the handle fractions and the minimum grid cell;
  - `DS_SCREEN_*` → `NDS.SCREEN_*`;
  - one `@dimen/layout_editor_margin`;
  - the floating button alpha set in one place;
  - `actionMasked` everywhere, and `applyRect` for both cancels;
  - plain vals instead of pointless `lazy`.
- Leaks: remove the global layout listener in `LayoutEditorManagerView`, and the `updateAnchorRunnable` callback on detach.
- Renames: `cutoutInsets` → `controlInsets`; `ButtonColors` → `ControllerButtonColors`.
- Locale-aware number formatting in `LayoutEditorItemControls` (lint `SetTextI18n`). Mark word-free strings `translatable="false"`.
- Skipped for now: splitting `LayoutEditorView` (838 lines) and pulling the floating ⋮ button out of `LayoutEditorManagerView`.

Phone check: a full editor pass covering drag, handles, snap grid, pill, panel, Save and back gesture.

## Batch 5: settings and theme consistency

- Keys and defaults:
  - a `PreferenceKeys` object for the keys the fork added;
  - reference the existing RA constants in `KEPT_PREF_KEYS`;
  - store controller style as `name.lowercase()` and read it with `enumValueOfIgnoreCase` (it's mapped in 4 places today);
  - merge `ControllerStylePreference` and `SoftwareInputBehaviourPreference`;
  - align the theme default between XML and the repository;
  - read `video_renderer` through the repository.
- Shared helpers:
  - `settingsContentPadding()` for the 5 Compose settings pages;
  - `SettingsActivity.showPage()`;
  - `ProvideEnabledContentColor`;
  - a slider summary formatter;
  - the `RomListScreen` focus wrapper;
  - the `RomHeaderUi` title row;
  - one icon table for the button builders.
- `SettingsBackupManager`: parse once, and move the shared names into `SettingsBackupFormat`. Move backup/restore out of `AppearancePreferencesFragment` into a helper that lets cancellation through.
- `iconSpaceReserved=false` set once in the card styles (72 repeats).
- **Visible changes, check in light and dark:**
  - drop the M2 status-bar `Box` around the 8 top bars;
  - drop the redundant tab and slider colour overrides;
  - rename the `Preference` style overlay;
  - remove the `buttonContrasted` tint;
  - repoint the leftover M2 colours (splash, notifications, save-slot button, colour picker) to M3, then delete the old palette;
  - build the Compose scheme from the `m3*` colour resources so it isn't typed twice.
- Uppercase buttons and the AppCompat preference dialogs stay as they are.

## Batch 6: tests (small, cheap)

- Unit tests for `LayoutEditorSnapping.kt` (pure functions, no tests today).
- `PendingSettingsRestore` round-trip: typed floats, kept keys, old untyped files.

## Decisions (10-27)

1. Update checker: keep it dormant.
2. Release `applicationId` stays `me.magnum.melonds` for now (only debug builds get installed).
3. M3 visuals: cleanup only. No sentence case, and the dialogs stay as they are.
4. Big layout editor split: skipped unless it comes up later.
5. Translations: remove the other languages, so the app is English only.
6. Hidden vibration strength slider (upstream): left alone.

Not in scope: dependency bumps from lint (camera 1.6.2, compose-bom, aboutlibraries, commons-compress). They're a separate small commit if wanted.

## Verification

Build after every batch (`HANDOFF.md` build steps), install over wireless adb, and run that batch's phone check. After Batch 3, also run a perf log at the same White 2 spot. After everything: a full lint run, where the error count in fork files should drop to 0, and one longer play session.
