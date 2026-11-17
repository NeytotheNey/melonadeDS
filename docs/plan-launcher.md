# Plan: game library views and RetroAchievements metadata

## Status (2026-11-17)

- Built on `material3` (`3df6d29d`): the List / Grid / Mosaic views, the metadata sheet, controller focus on the tiles, `RAWebApi`, the `ra_game_metadata` table (database version 9, `Migration8to9`), `RomMetadataRepository`, the art store, `RomMetadataSyncWorker`, the settings entries and the ROM details header. Nothing has changed in it since.
- It builds and the unit tests pass, and the library views have run on the emulator, but nothing has run on a phone yet. Still to try there: the look, scrolling and press feedback of the sheet and the mosaic, focus and holding the confirm button with a real controller (including that letting go on the details screen doesn't start the game), a real key against the real API, and `Migration8to9Test`.
- The API details are checked against RetroAchievements' docs and source, not against a live call: `ReleasedAtGranularity` is `day`, `month`, `year` or null, `/Images/000001.png` and `/Images/000002.png` are the placeholder badge and image (the media-restricted case also returns the second), `Released` is `YYYY-MM-DD` or `YYYY-MM-DD HH:MM:SS`, and an unknown game looks like HTTP 200 with `[]`.
- Visual mock-up of the three modes with the metadata sheet, light and dark, and the degraded "unknown ROM" state: [mockups/launcher-mockup.html](mockups/launcher-mockup.html), a single self-contained page (also at https://claude.ai/artifact/AbrpUXUoQ9YJ8KJqMTCQ2y). Fictional games and art. The library screen is built to match it.
- Open: picking a match by hand and a sync status line (section 4).

## 1. Views

The library has three layouts, chosen from a List / Grid / Mosaic segmented control under the app bar and remembered in the `rom_list_view_mode` preference (`PreferenceKeys.ROM_LIST_VIEW_MODE`, read and written only from code, like the sort order). Below it are a sort chip and a Filter chip (All, Played, Not played, Has achievements), which works on all three layouts together with the search and isn't saved between launches. The app bar only keeps search and the overflow menu; settings, the DSiWare manager, the rescan and booting the firmware are in the menu.

| Mode | Composable | Layout |
|---|---|---|
| List | `RomList` + `ConfigurableRomItem` | one row per ROM, 48 dp banner icon, 18 sp title, file name, hairline dividers, a menu button for ROM settings |
| Grid | `RomGrid` + `RomGridTile` | `GridCells.Adaptive(96.dp)`: 3 columns on a phone and the Fold's cover screen, more when unfolded; 9:10 card, banner icon badge, RA pill, title (2 lines), publisher and year chips |
| Mosaic | `RomMosaic` + `RomMosaicTile` | 4 columns on a phone, more when there's room (`mosaicColumnsFor`), 88 dp rows, 6 dp gaps; art fills the tile, title on a scrim |

Tapping a tile opens the metadata sheet (`RomMetadataSheet`), and Play on the sheet starts the game. A long press, or holding the confirm button, opens the ROM settings. In the mosaic the first featured tile's Continue pill starts the game directly, and a featured tile's menu button opens the ROM settings.

Mosaic packing (`ui/romlist/MosaicLayout.kt`): `chooseMosaicSizes` makes the two most recently played games 2x2 "featured" tiles, leading whatever the sort, and games with title-screen art on disk 2x1 (about one in six of those, decided by the ROM hash in `isWideMosaicGame`, so filters and sorts don't reshuffle them). `packMosaic` packs them like CSS's dense grid and cuts the result into bands that no tile crosses, one band per `LazyColumn` item, since a lazy grid can't span rows. New title art only re-packs at the top of the list or when the list or width changes, and the position is kept by a tile rather than an index, so a sync or a rotation doesn't move what's on screen. A tile with nothing found shrinks to a single cell instead of leaving a hole.

Every tile without art falls back to the banner icon on a tinted card, which is also what it shows when its image fails to load, so the views work with no network source at all. With nothing but the file it shows the file name and an italic "No metadata". The RA pill and the Has achievements filter only know about games opened with a RetroAchievements login, since they read the local achievements cache.

The sheet (`buildRomSheetContent`) only reads the shared snapshot and shows nothing it doesn't know: achievements are counted from the local cache, "earned" only appears once the user's data was fetched for that game, and a game never opened with a login shows no achievements fact, since API_GetGame doesn't say. The credit line tells apart a cached row, "not found", a hash the hash library has no entry for, no Web API key, and not downloaded yet.

Controller: the D-pad moves between tiles with a focus border and reaches the view switch and chips by going up from the first row; left and right stop at the library's edges. The sheet focuses Play, left and right move between Play and Settings, up and down scroll, and Back closes it and gives the focus back to the tile. `CarriedOverKeyFilter` keeps the press that opened the sheet from counting as a press of Play. On a featured tile, down moves into the pill and menu button, since a D-pad search doesn't look inside a tile, and up comes back out.

## 2. What RetroAchievements can tell us

| Source | Needs | Gives | Coverage |
|---|---|---|---|
| Connect API `dorequest.php` (`rcheevos-api`, already in the app) | `hashlibrary`: nothing; `achievementsets`: a signed-in RA user | hash to game id for every registered DS/DSi hash; per game: title, icon, console id, sets | games with a registered hash |
| Web API `API_GetGame.php?i=<id>&y=<key>` | the user's own Web API key, pasted into settings and stored like a password | title, console, `ImageIcon`, `ImageTitle`, `ImageIngame`, `ImageBoxArt`, `Publisher`, `Developer`, `Genre`, `Released` (+ granularity) | same games; many entries have blank publisher/developer/genre or no box art |
| Web API `API_GetGameList.php?i=18&h=1` (and `i=78`) | Web API key | one bulk call: every DS title with id, title, icon, achievement count and hashes | useful to replace `hashlibrary` if we ever want to drop the connect API; no box art or credits |

Facts that shape the design:
- Console ids: Nintendo DS is 18, DSi is 78. The app's ROM hash (`RomProcessor.getRomMetadata`) is the rcheevos `rc_hash_nintendo_ds` MD5 (header, ARM9, ARM7, banner) as 32 lowercase hex digits, so trimmed ROMs still match and ROM hacks and translations do not unless RA registered the patch.
- Images live at `https://media.retroachievements.org` + the path in the field (`/Images/NNNNNN.png`). There are no size parameters; box art comes at whatever size it was uploaded, so downscale on device.
- RA publishes no numeric rate limit. Its docs ask clients to cache static game data, to avoid frequent bulk calls, and say heavy users get their bandwidth reviewed. RetroArch caches every badge forever on disk and downloads at most two at a time; we do the same for art.
- The connect API has no box art or credits, so an RA login alone gets us title + icon. The richer card needs the Web API key.

## 3. Cache and sync

Goal: at most one Web API call per distinct game for the life of the install, zero calls while scrolling, and everything shown from local storage. The one exception is the `ra_game` icon on a tile with no downloaded art, which still loads through Coil on any network, metered included. For a library of N ROMs that's one `hashlibrary` a day (already happening) and N `API_GetGame` calls once.

- The repository keeps one in-memory map of the library, rebuilt at most once a second when a table or the art changes, and collected with the lifecycle, so no database work runs for it during a game. To know whether a hash is in the hash library, it looks up the library's own ROM hashes when it rebuilds, since that table holds every hash on RetroAchievements.
- Art is in `externalCacheDir/ra_art/<game id>_<kind>.png`, not Coil's LRU disk cache, which could evict box art. Two downloads at a time, skipped when the file exists.
- `RomMetadataSyncWorker` (WorkManager, unmetered network, one at a time) resolves game ids through `ra_game_hash_library` and, for each id with no `ra_game_metadata` row, calls `API_GetGame`, then downloads `ImageBoxArt`, else `ImageTitle`, else `ImageIcon`, plus the title screen for the mosaic's wide tiles, in batches of 20 games. With no key it does nothing.
- Failure rules: an image that fails for good (404, too large, not an image) has its path cleared from the row, which moves the game to its next kind. Three failures in a row with nothing saved in between stop the art for that run instead, so a CDN block or a blanket 404 can't clear the library's paths. Downloads go in random order. It backs off on HTTP 429, a 5xx, a 404 or a response that isn't game data, and marks `fetch_status` (0 ok, 1 not found, 2 failed) so a failure is retried next run, while "not found" waits for a manual refresh. Since the ids come from RA's own hash library, three "not found" in a row trigger one control call for game 1: real data there means they stand, anything else stops the run and counts them as failures.
- While a game runs the sync doesn't: `EmulatorViewModel` cancels it when created and enqueues it again when cleared, and the scheduler refuses every other trigger until then.
- Settings: "RetroAchievements Web API key" (optional, password field, test button), "Refresh metadata" (sets `fetch_status` to 2 on rows fetched over two weeks ago, and always on a row with a cleared image, so the worker fetches them again while the cards keep what they have), and "Clear downloaded art" (stops a running sync; the next one downloads the art again, with no API calls).

## 4. Later: fixing a game that doesn't match (not built)

Games match RetroAchievements by ROM hash, so hacks, translations and some dumps get no art or details. Let the user pick the right game by hand.

- **Where:** a "Fix match" action on the metadata sheet, and "Change match" on a game that matched the wrong entry.
- **Finding the game:** search by title, with results showing art, console and year. `API_GetGameList.php?i=18` and `i=78` give every DS and DSi title in one call per console, so cache that list and search it locally instead of calling the API per keystroke.
- **Storing it:** a new table mapping the ROM's hash to the chosen game id, which takes priority over the hash library in `RomMetadataSyncWorker` and `RomMetadataRepository`. It needs a Room migration. Keep it out of `ra_game_hash_library`, which gets replaced on refresh. Decide whether settings export should include it.
- **Display only:** a manual match must never change the hash sent to RetroAchievements. Achievements stay tied to the ROM's real hash, so a matched hack doesn't award or look like it has a set unless RA registered that hash. The sheet should say the match is manual, and the RA pill and Achievements fact shouldn't imply otherwise.
- **Undo:** a "Remove match" action that returns the game to automatic matching and removes art no other ROM uses.
- **Sync status:** the user can't currently tell whether art is downloading. Show "Art: N of M, last synced ..." under the Game library settings, and say why a game has no art on its sheet (no hash match, RA has no art, waiting for sync).

Needs a key, like the rest of the Web API work. Test with a hack, a translation, a wrong match and an undo.
