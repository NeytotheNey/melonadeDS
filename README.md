# melonadeDS
A fork of the [melonDS Android port](https://github.com/rafaelvcaetano/melonDS-android), itself an Android port of [melonDS](https://melonds.kuribo64.net/), a DS and DSi emulator.

|Settings|Layout Editor|Button Styles|
|---|---|---|
|![Settings](./.github/images/settings.png)|![Layout editor](./.github/images/layout_editor.png)|![Button styles](./.github/images/button_styles.png)|

# What this fork adds
*  Material 3 settings, with Material You colors
*  Controller button styles, in game and in the layout editor
*  A layout editor with snapping, a grid and aspect-ratio locks
*  WideMelon: a widened 3D view for wide layouts, set per game (see [MODS.md](MODS.md))
*  Lua scripts per game that can read and change input and memory and draw on the screens (see [MODS.md](MODS.md))
*  Upscaled display capture, so screens that games capture and show again stay sharp at higher internal resolutions
*  Smooth gradients (compute renderer): dithers 3D lighting and color gradients instead of showing hard bands
*  Frame pacing to the display's vsync, with an optional 60 Hz lock
*  Settings backup and restore
*  Diagnostics logging: a performance log and a debug log
*  English-only UI

# Requirements
Android 10 or newer.

# Missing Features
*  Local Multiplayer
*  DSi SD card support

# Performance
Performance is solid on 64 bit devices with thread rendering and JIT enabled, and should run at full speed on flagship devices. Performance on older devices, especially
32 bit devices, is very poor due to the lack of JIT support.

# Integration with third-party frontends
It's possible to launch melonadeDS from third-party frontends. For that, you will need to have the ROMs you want to launch already scanned by melonadeDS. Then, you can configure your
third-party frontend with the following configuration:
*  Package name: `me.magnum.melonds` (`me.magnum.melonds.nightly` for nightly builds)
*  Activity name: `me.magnum.melonds.ui.emulator.EmulatorActivity`
*  Parameters (choose one):
    * Intent data (preferred) - a URI of the NDS ROM (ZIP and 7z files are supported). Ensure [read permission is granted](https://developer.android.com/reference/android/content/Intent#FLAG_GRANT_READ_URI_PERMISSION)
    * `uri` (deprecated) - a string with the [SAF](https://developer.android.com/guide/topics/providers/create-document-provider) URI of the NDS ROM (ZIP and 7z files are supported)
    * `PATH` (deprecated) - a string with the absolute path to the NDS ROM (ZIP and 7z files are supported)

### Pegasus metadata files
* [melonds.metadata.txt](./.github/pegasus/melonds.metadata.txt)
* [melonds-nightly.metadata.txt](./.github/pegasus/melonds-nightly.metadata.txt)

### Info regarding save files
When launching ROMs from third-party frontends, if melonadeDS hasn't scanned that particular ROM previously, it won't be able to create the save file next to the ROM file if the
option "Save next to ROM file" is enabled in the settings or the save file directory is not set. Instead, melonadeDS will create a save file in
`Android/data/me.magnum.melonds/files/saves`

# Building
To build the project you will need a JDK (21 or newer), the Android SDK, NDK and CMake.

## Build steps:
1.  Clone the project, including submodules with:

    `git clone --recurse-submodules https://github.com/NeytotheNey/melonadeDS.git`
2.  Install the Android SDK, NDK and CMake
3.  Build a debug APK for one ABI (`arm64-v8a` for phones, `x86_64` for the emulator) with:
    1.  Unix: `./gradlew :app:assembleGitHubProdDebug -Pandroid.injected.build.abi=arm64-v8a`
    2.  Windows: `gradlew.bat :app:assembleGitHubProdDebug -Pandroid.injected.build.abi=arm64-v8a`
4.  The generated APK can be found at `app/build/intermediates/apk/gitHubProd/debug/`. If you install it with `adb install`, add `-Pandroid.injected.testOnly=false` to the Gradle command in step 3, otherwise the install is refused.

The flavors are `gitHub` or `playStore` (the `gitHub` one also builds for 32 bit ARM) crossed with `prod` or `nightly`. Debug builds install beside the release app as `me.magnum.melonds.dev` (`me.magnum.melonds.nightly.dev` for nightly). Their native code is optimized (RelWithDebInfo) so they can be used for performance tests; add `-PnativeBuildType=Debug` for an unoptimized native build.

If you want to create a release build, you will need to modify your `local.properties` file to include the following fields:
*  `MELONDS_KEYSTORE=<path_to_your_keystore>`
*  `MELONDS_KEYSTORE_PASSWORD=<keystore_password>`
*  `MELONDS_KEY_ALIAS=<name_of_your_key_alias>`
*  `MELONDS_KEY_PASSWORD=<key_alias_password>`
