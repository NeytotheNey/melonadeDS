// The game library's database: library.db in the profile folder (SQLite, third_party/sqlite). One connection, used by the library's
// worker thread and, for the last-played times, by the UI thread, so every call takes the lock.
//
// Schema version 1 (PRAGMA user_version):
//   roms          one row per ROM file found in the ROM folders (and the files that turned out not to be ROMs, so a rescan doesn't read
//                 them again): its path, the ROM's entry when it's in a zip or 7z, the file's size and last write time (what a rescan
//                 compares), what was read from it, and the banner icon (32x32 RGBA)
//   play_history  when each file was last played, by path, kept apart from roms so a rescan, a folder removed and added again, or a
//                 game booted from the command line keeps it
// Schema version 2 (phase 2 step B) adds:
//   rom_config    each game's own settings (RomConfig.h), by the ROM's RetroAchievements hash (its path when it has none), so they
//                 survive a move or a rename: one row per field that differs from the default, under the app's RomConfigDto names
// Schema version 3 (phase 2 step C, the metadata sync) adds:
//   ra_hash_library   RetroAchievements' hash library for DS (18) and DSi (78): every registered ROM hash to its game id, from the connect
//                     API's hashlibrary, replaced at most once a day (the app's ra_game_hash_library)
//   ra_game_metadata  one row per game id: what API_GetGame said (the app's ra_game_metadata, plus the title), when, and fetch_status
//                     (0 ok, 1 not found, 2 failed or due for a refresh)
//   libretro_art      a libretro thumbnail (No-Intro name and kind) that wasn't there (404), so it isn't asked for again until a refresh
//   sync_state        when the hash library, the libretro DAT and the last sync were done
// and fills roms' crc32, md5 and sha1 (the whole ROM's, for the libretro DAT), which the sync computes once per file
// Later versions: a new entry in kMigrations (LibraryDb.cpp), run once on open
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace Frontend
{

struct LibraryEntry;
struct RaGameRow;

class LibraryDb
{
public:
    ~LibraryDb() { Close(); }

    // opens (or makes) [file] and brings its schema up to date; false when it can't, and the library then lives in memory for the run
    bool Open(const std::filesystem::path& file);
    void Close();
    bool IsOpen() const { return Db != nullptr; }
    int SchemaVersion() const { return Version; }

    // every row of roms (icons included)
    bool LoadRoms(std::vector<LibraryEntry>& out);
    // play_history, by lowercased path
    bool LoadPlays(std::map<std::string, int64_t>& out);
    // [entry] stored: in its own row when its Id names one, or the row with its path (a file read again keeps its id), else a new row,
    // whose id goes into Id. False when the database is closed or the write failed (busy, read-only), with Id as it was
    bool StoreRom(LibraryEntry& entry);
    // all of them or none
    bool DeleteRoms(const std::vector<int64_t>& ids);
    bool SetPlayed(const std::string& path, int64_t when);
    // rom_config: each game's fields (key, value), by its lowercased game key (Library::ConfigKey)
    bool LoadConfigs(std::map<std::string, std::map<std::string, std::string>>& out);
    // [game]'s rows for [keys] replaced by [fields] (the ones not in [keys] stay), in one transaction
    bool StoreConfig(const std::string& game, const std::map<std::string, std::string>& fields, const std::vector<std::string>& keys);

    // ra_hash_library: hash (lowercase) to game id; replaced for one console id at a time, all or nothing
    bool LoadHashLibrary(std::map<std::string, int64_t>& out);
    bool StoreHashLibrary(int consoleId, const std::vector<std::pair<std::string, int64_t>>& hashes);
    // ra_game_metadata
    bool LoadGames(std::map<int64_t, RaGameRow>& out);
    bool StoreGame(const RaGameRow& row);
    // fetch_status 2 on the rows fetched at or before [cutoff] (ms since 1970), so the next sync fetches them again; how many
    int MarkGamesForRefetch(int64_t cutoff);
    // roms' hashes, by row id, with the file's size and time they're for (only the rows that have them)
    struct RomHashRow { int64_t Id; uint64_t FileSize; int64_t FileTime; std::string Crc32, Md5, Sha1; };
    bool LoadRomHashes(std::vector<RomHashRow>& out);
    // stored only if the row still has that size and time (a rescan that read the file again in the meantime cleared them)
    bool StoreRomHashes(const RomHashRow& row);
    // libretro_art: "name|kind" to when it was found missing
    bool LoadLibretroMissing(std::map<std::string, int64_t>& out);
    bool StoreLibretroMissing(const std::string& name, const std::string& kind, int64_t when);
    // the marks made at or before [cutoff] forgotten (Refresh metadata); how many
    int ForgetLibretroMissing(int64_t cutoff);
    // sync_state
    std::string GetState(const std::string& key);
    bool SetState(const std::string& key, const std::string& value);

private:
    bool Exec(const char* sql);
    bool Migrate();

    std::mutex Lock;
    sqlite3* Db = nullptr;
    int Version = 0;
};

}
