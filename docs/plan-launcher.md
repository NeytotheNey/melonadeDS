# Plan: game library views and RetroAchievements metadata

## Status (2026-11-05)

- Section 4 is built on `material3`: controller focus on the tiles, `RAWebApi`, the `ra_game_metadata` table (database version 9),
  `RomMetadataRepository`, the art store, `RomMetadataSyncWorker`, the settings entries, the tile wiring and the ROM details header.
- It builds and the unit tests pass, and the library views have run on the emulator, but nothing has run on a phone yet. Still to try there: focus
  and holding the confirm button with a real controller (including that letting go on the details screen doesn't start the game), a real key against
  the real API, and `Migration8to9Test`.
- The API details are checked against RetroAchievements' docs and source, not against a live call: `ReleasedAtGranularity` is `day`, `month`, `year`
  or null, `/Images/000001.png` and `/Images/000002.png` are the placeholder badge and image (the media-restricted case also returns the second),
  `Released` is `YYYY-MM-DD` or `YYYY-MM-DD HH:MM:SS`, and an unknown game looks like HTTP 200 with `[]`.
- Visual mock-up of the three modes with the metadata sheet, light and dark, and the degraded "unknown ROM" state:
  [mockups/launcher-mockup.html](mockups/launcher-mockup.html), a single self-contained page; open it in any browser, or resize it to
  phone width. Also published at https://claude.ai/artifact/AbrpUXUoQ9YJ8KJqMTCQ2y. Fictional games and art. The library screen is built to match
  it, metadata sheet included (section 1). That pass builds and its logic has unit tests, but it hasn't run on a phone or an emulator yet, so the look,
  scrolling, press feedback and all the controller behavior of the sheet and the mosaic are still to try.
- Short answer on RetroAchievements: the connect API the app already uses gives title and icon only; box art and credits need the
  user's own Web API key and one `API_GetGame` call per game, cached for good (section 2 and 3).

## 1. Views

The library has three layouts, chosen from a List / Grid / Mosaic segmented control under the app bar and remembered in the `rom_list_view_mode`
preference (`PreferenceKeys.ROM_LIST_VIEW_MODE`, read and written only from code, like the sort order). Below it are a chip with the current sort, which opens
the sort options, and a Filter chip (All, Played, Not played, Has achievements), which works on all three layouts together with the search and isn't saved
between launches. The app bar only keeps search and the overflow menu; settings, the DSiWare manager, the rescan and booting the firmware are in the menu.

| Mode | Composable | Layout |
|---|---|---|
| List | `RomList` + `ConfigurableRomItem` | one row per ROM, 48 dp banner icon, 18 sp title, file name, hairline dividers, a menu button for ROM settings |
| Grid | `RomGrid` + `RomGridTile` | `GridCells.Adaptive(96.dp)`: 3 columns on a phone and the Fold's cover screen, more when unfolded; 9:10 card, banner icon badge, RA pill, title (2 lines), publisher and year chips |
| Mosaic | `RomMosaic` + `RomMosaicTile` | 4 columns on a phone, more when there's room (`mosaicColumnsFor`), 88 dp rows, 6 dp gaps; art fills the tile, title on a scrim |

Tapping a tile in any layout opens the metadata sheet (`RomMetadataSheet`), and Play on the sheet starts the game. A long press, or holding the confirm
button, opens the ROM settings. In the mosaic the first featured tile's Continue pill starts the game directly, and a featured tile's menu button opens the
ROM settings.

Mosaic packing: `chooseMosaicSizes` (`ui/romlist/MosaicLayout.kt`) makes the two most recently played games 2x2 "featured" tiles, leading whatever the sort, and
games with title-screen art on disk 2x1. `packMosaic` then packs them the way CSS's dense grid does and cuts the result into bands, stretches of rows that no
tile crosses the edge of. A `LazyColumn` shows one band per item with a small custom `Layout`, since a lazy grid can't span rows; that also keeps it lazy, and
D-pad focus goes by where the tiles really are. New title art only re-packs at the top of the list or when the list or width changes, and the position is kept by a
tile rather than an index, so a sync or a rotation doesn't move what's on screen. A tile that nothing can be found for shrinks to a single cell instead of leaving a hole, so free cells only come
after the last tile. The label above the tiles names the real sort ("Recently played", "Least recently played", "A to Z", "Z to A"), or isn't there when
sorting by play time and nothing was played.

Wide tiles need `_title.png` on disk, so the sync downloads the title screen too, on top of a game's main art and with no extra API calls. A title that fails for
good only clears its own path. Of those games about one in six goes wide, decided by the ROM hash (`isWideMosaicGame`), so filters and sorts don't reshuffle them.

Every tile without art falls back to the banner icon centered on a tinted card, which is also what a tile shows when its image fails to load, so the views work
with no network source at all. With a real title or publisher (from the banner or RetroAchievements) the card shows those. With nothing but the file it shows
the file name and an italic "No metadata". The RA pill marks games whose achievements are cached locally, meaning the game was opened with a RetroAchievements
login. It doesn't say anything about games that were never opened, and neither does the Has achievements filter.

The sheet (`buildRomSheetContent` decides what it says) only reads the shared snapshot and shows nothing it doesn't know. Achievements are counted from the local
cache, "earned" only appears once the user's data was fetched for that game, and a game never opened with a login shows no achievements fact at all, since API_GetGame
doesn't say. The credit line tells apart a cached row, a row saying "not found", a hash the loaded hash library has no entry for, no Web API key, and not
downloaded yet. To know about that hash, the snapshot looks up the library's own ROM hashes in the hash library when it rebuilds, as the table holds every hash on
RetroAchievements and can't be kept in memory. Like everything else in the snapshot, that only runs while something on screen collects it.

A controller moves between tiles with the D-pad, with a border on what has the focus, and reaches the view switch and chips by going up from the first row. Left
and right stop at the edges of the library. The sheet puts the focus
on Play, left and right move between Play and Settings, up and down scroll what doesn't fit, and Back closes it and gives the focus back to the tile.
`CarriedOverKeyFilter` keeps the press that opened the sheet from counting as a press of Play. On a featured tile, down moves into the pill and menu button, since
a D-pad search doesn't look inside a tile, and up comes back out.

## 2. What RetroAchievements can tell us

Two different APIs, with different reach. The app already uses the first one.

| Source | Needs | Gives | Coverage |
|---|---|---|---|
| Connect API `dorequest.php` (`rcheevos-api`, already in the app) | `hashlibrary`: nothing; `achievementsets`: a signed-in RA user | hash to game id for every registered DS/DSi hash; per game: title, icon, console id, sets | games with a registered hash |
| Web API `API_GetGame.php?i=<id>&y=<key>` | the user's own Web API key, pasted into settings and stored like a password | title, console, `ImageIcon`, `ImageTitle`, `ImageIngame`, `ImageBoxArt`, `Publisher`, `Developer`, `Genre`, `Released` (+ granularity) | same games; many entries have blank publisher/developer/genre or no box art |
| Web API `API_GetGameList.php?i=18&h=1` (and `i=78`) | Web API key | one bulk call: every DS title with id, title, icon, achievement count and hashes | useful to replace `hashlibrary` if we ever want to drop the connect API; no box art or credits |

Facts that shape the design:
- Console ids: Nintendo DS is 18, DSi is 78. The app's ROM hash (`RomProcessor.getRomMetadata`) is the rcheevos `rc_hash_nintendo_ds`
  MD5 (header, ARM9, ARM7, banner) as 32 lowercase hex digits, so trimmed ROMs still match and ROM hacks and translations do not
  unless RA registered the patch.
- Images live at `https://media.retroachievements.org` + the path in the field (`/Images/NNNNNN.png`). There are no size parameters;
  box art comes at whatever size it was uploaded, so downscale on device.
- RA publishes no numeric rate limit. Its docs ask clients to cache static game data, to avoid frequent bulk calls, and say heavy
  users get their bandwidth reviewed. RetroArch caches every badge forever on disk and downloads at most two at a time; we should do the
  same for art.
- The connect API fields have no box art or credits, so an RA login alone gets us title + icon. The richer card needs the Web API key.

## 3. Cache design

Goal: at most one Web API call per distinct game for the life of the install, zero calls while scrolling, and everything shown from
local storage. The one exception is the `ra_game` icon on a tile with no downloaded art, which still loads through Coil on any network, metered
included.

The repository keeps one in-memory map of the library, rebuilt at most once a second when a table or the art changes. Tiles only look themselves up
in it, and it's collected with the lifecycle, so no database work runs for it during a game.

Storage, in the existing Room database (next version with a manual migration, see `Migration7to8` for the pattern):

```
ra_game_metadata
  game_id        INTEGER PK   -- RA game id, joins ra_game and ra_game_hash_library
  publisher      TEXT NULL
  developer      TEXT NULL
  genre          TEXT NULL
  released       TEXT NULL    -- "YYYY-MM-DD", plus
  released_granularity TEXT NULL  -- day | month | year
  image_box_art  TEXT NULL    -- "/Images/....png", null when RA has none
  image_title    TEXT NULL
  image_ingame   TEXT NULL
  image_icon     TEXT NULL    -- the art of last resort below, kept so missing art never needs another API call
  fetched_at     INTEGER      -- epoch ms
  fetch_status   INTEGER      -- 0 ok, 1 not found, 2 failed (retry later)
```

Images: Coil's disk cache already persists `AsyncImage` downloads, but it is an LRU that can evict box art. Add a dedicated
`externalCacheDir/ra_art/<game id>_<kind>.png` store written by a small downloader (two at a time, skipped when the file exists) and
have the tiles load from the file, like `RomIconProvider` does for banner icons.

Flow:
1. On scan, the app already stores each ROM's RA hash. The hash library (`ra_game_hash_library`) maps it to a game id; it refreshes at
   most once a day and needs no login.
2. `RomLibraryMetadata` for a card is assembled from: the banner (`Rom.developerName` as publisher), `ra_game` (title, icon) when the
   game has been opened with an RA login, and `ra_game_metadata` when present. Missing fields stay null and the card degrades.
3. A `RomMetadataSyncWorker` (WorkManager, unmetered network, one at a time) walks the scanned ROMs, resolves game ids, and for each id
   with no `ra_game_metadata` row calls `API_GetGame` with the user's key, then downloads the chosen art (`ImageBoxArt`, else
   `ImageTitle`, else `ImageIcon`) and the title screen for the mosaic's wide tiles after every 20 games, so a big library doesn't need one run to get through everything. An image that fails for good
   (404, too large, not an image) has its path cleared from the row, which moves the game to its next kind and keeps later syncs from trying it again.
   Three failures in a row with nothing saved in between (those, or a 403, a redirect, a 5xx, a lost connection or a page instead of an image) stop
   the art for that run instead, so a CDN block or a blanket 404 can't clear the library's paths, and a lone failure only costs that image a try. Downloads
   go in random order so images that always fail don't line up, and a run that stops with nothing saved waits for the next trigger rather than retrying. It backs off on HTTP 429, a 5xx, a 404 or a response that isn't game data, and marks `fetch_status` so a failure is retried on the next run, while a
   "not found" is not retried until a manual "Refresh metadata" action. "Not found" only means an empty answer. Since the ids come from RetroAchievements' own hash
   library, three in a row trigger one control call for game 1: real data there means they stand, anything else stops the run and counts them as failures.
   While a game runs the sync doesn't: `EmulatorViewModel` cancels it when created and enqueues it again when cleared, the scheduler refuses every other trigger until then, and the next run carries on from the saved rows.
4. Manual actions in settings: "RetroAchievements Web API key" (optional, password field, test button), "Refresh metadata" (sets `fetch_status` to
   2 on rows fetched over two weeks ago, found or not, so the worker fetches them again while the cards keep what they have; a row with an image cleared
   after failing always counts, which brings the image back), and "Clear downloaded art" (stops a running sync; the next one downloads the art again, with no API calls).
5. With no key the worker does nothing, and cards show banner data plus whatever `ra_game` already has. With no login and no key the
   views still work with banner icons.

Requests for a library of N ROMs: one `hashlibrary` a day (already happening) and N `API_GetGame` calls once, then nothing.

## 4. Implementation order

1. Keep the mock-up views, add tile focus handling for controllers, decide the long-press gear alternative.
2. `RAWebApi` client in `rcheevos-api` (own class: the Web API has no `Success` envelope), `API_GetGame` DTO, key storage.
3. Room entity, DAO, migration, `RomMetadataRepository` that merges banner + `ra_game` + `ra_game_metadata`, replacing
   `RomListViewModel.getRomMetadata`.
4. Art store and sync worker, settings entries, tile wiring.
5. Reuse the metadata on the ROM details header.

## 5. Later: fixing a game that doesn't match (not built)

Games match RetroAchievements by ROM hash, so hacks, translations and some dumps get no art or details. Let the user pick the right game by hand.

- **Where:** a "Fix match" action on the metadata sheet, and "Change match" on a game that matched the wrong entry.
- **Finding the game:** search by title, with results showing art, console and year. `API_GetGameList.php?i=18` and `i=78` give every DS and DSi title in one call per console, so cache that list and search it locally instead of calling the API per keystroke.
- **Storing it:** a new table mapping the ROM's hash to the chosen game id, which takes priority over the hash library in `RomMetadataSyncWorker` and `RomMetadataRepository`. It needs a Room migration. Keep it out of `ra_game_hash_library`, which gets replaced on refresh. Decide whether settings export should include it.
- **Display only:** a manual match must never change the hash sent to RetroAchievements. Achievements stay tied to the ROM's real hash, so a matched hack doesn't award or look like it has a set unless RA registered that hash. The sheet should say the match is manual, and the RA pill and Achievements fact shouldn't imply otherwise.
- **Undo:** a "Remove match" action that returns the game to automatic matching and removes art no other ROM uses.
- **Sync status:** the user can't currently tell whether art is downloading. Show "Art: N of M, last synced ..." under the Game library settings, and say why a game has no art on its sheet (no hash match, RA has no art, waiting for sync).

Needs a key, like the rest of the Web API work. Test with a hack, a translation, a wrong match and an undo.
