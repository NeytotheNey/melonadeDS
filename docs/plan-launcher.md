# Plan: game library views and RetroAchievements metadata

Status: mock-up. The grid and mosaic views are wired behind a view-mode menu on the ROM list, with previews and a unit test for the
mosaic packing. The metadata cards show what the app already knows (banner title and publisher line, plus the RetroAchievements icon
for games already in the local cache). The Web API cache below is designed but not built.

## 1. Views

The ROM list keeps its `LazyColumn` layout and gains two more, chosen from a view icon in the top bar and remembered in the
`rom_list_view_mode` preference (`PreferenceKeys.ROM_LIST_VIEW_MODE`, read and written only from code, like the sort order).

| Mode | Composable | Layout | Per-game actions |
|---|---|---|---|
| List | `RomList` + `ConfigurableRomItem` | one row per ROM, 48 dp banner icon, title, file name | tap plays, gear opens ROM settings |
| Grid | `RomGrid` + `RomGridTile` | `GridCells.Adaptive(110.dp)`: 3 columns on a phone, more when unfolded; 0.9 wide:tall card (a DS case), title (2 lines), "Publisher · Year" caption | tap plays, long press opens ROM settings |
| Mosaic | `RomMosaic` + `RomMosaicTile` | 3 fixed columns; the 2 most recently played games are 2:1 tiles with a "Continue" hint, the rest 1:1; art fills the tile, title on a scrim | tap plays, long press opens ROM settings |

Mosaic packing: a lazy grid places items in order and never back-fills, so `arrangeMosaic` (`ui/romlist/MosaicLayout.kt`) orders the
tiles up front: each featured tile starts a row and the next ordinary game completes it, so rows stay full. `MosaicLayoutTest` covers
the packing rules. The featured games lead regardless of the chosen sort; everything else keeps the user's order.

Every tile falls back to the banner icon centred on a tinted card when there is no art, which is also what every tile shows until its
metadata loads, so the views work with no network source at all. A trophy badge marks games that have a RetroAchievements set.

Open points for the real thing:
- D-pad and controller focus on the grid and mosaic tiles. The list has per-row focus handling that the tiles don't yet.
- A per-game gear is hidden behind a long press in the two card views. A controller needs an alternative (a menu on the long press of
  the confirm button, or a "details" item in the pause menu).
- Reconsider the mosaic column count in landscape and on tablets; `arrangeMosaic` takes a column count, the composable passes 3.
- The HTML mock-up explored 2x2 featured tiles and 2x1 "title screenshot" tiles with CSS dense packing. Compose's lazy grid cannot span
  rows, so the Compose mosaic uses 2:1 featured tiles only. Row spans would need a custom layout or a staggered grid.

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
local storage.

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
   `ImageTitle`, else `ImageIcon`). It backs off on HTTP 429 and marks `fetch_status` so a failure is retried on the next run, while a
   "not found" is not retried until a manual "Refresh metadata" action.
4. Manual actions in settings: "RetroAchievements Web API key" (optional, password field, test button), "Refresh metadata" (clears
   `fetched_at` so the worker re-fetches, weeks apart at most), and "Clear downloaded art".
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
