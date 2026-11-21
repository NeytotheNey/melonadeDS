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
// Step C's metadata tables come as version 3 and up: a new entry in kMigrations (LibraryDb.cpp), run once on open
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

private:
    bool Exec(const char* sql);
    bool Migrate();

    std::mutex Lock;
    sqlite3* Db = nullptr;
    int Version = 0;
};

}
