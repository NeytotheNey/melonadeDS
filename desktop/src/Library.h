// The game library (phase 2 step A): the ROM folders (rom_search_dirs, as the app names them) walked on a worker thread for .nds, .dsi,
// .zip and .7z files, each read once (RomFile.h) and kept in library.db (LibraryDb.h) with its size and last write time, so a rescan only
// reads what's new or changed and drops what's gone, the way the app's FileSystemRomsRepository does. The UI thread reads a snapshot of
// the list and the scan's progress; the worker also unpacks a game in an archive to the profile's rom-cache folder before it boots
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "LibraryDb.h"

namespace Frontend
{

struct LibraryEntry
{
    // what a row of the database holds
    enum Status { kRead = 0, kNotRom = 1, kNotRead = 2, kFailed = 3 };
    int64_t Id = 0;            // the row's id, kept when the file is read again (negative: a row the database couldn't store)
    std::string Path;          // the file, absolute (UTF-8)
    std::string Entry;         // the ROM inside a zip or 7z; empty for a plain file (and for an archive not read yet)
    uint64_t FileSize = 0;     // the file's size on disk
    int64_t FileTime = 0;      // its last write time (FILETIME, 100 ns since 1601)
    // kNotRom: not a DS ROM (hidden); kNotRead: an online-only file (OneDrive) listed without reading it; kFailed: reading it failed
    // (a damaged archive, out of memory, locked), listed without metadata and read again at the next scan
    int State = kRead;
    uint64_t RomSize = 0;      // the ROM's own size (an archive's entry uncompressed)
    std::string GameCode, Region, HeaderTitle, Title, Developer, RaHash;
    int TitleLanguage = -1;    // the banner language asked for when it was read
    bool DsiWare = false;
    std::shared_ptr<const std::vector<uint8_t>> Icon; // 32 x 32 RGBA, null without one
    int64_t ScannedAt = 0;

    // for the menus
    std::string FileName;      // the file's name (the archive's for a ROM in one)
    int64_t LastPlayed = 0;    // ms since 1970, 0 when never played
    uint32_t Tint = 0;         // the icon's average color (RGBA), 0 without an icon

    // the banner's title, or the file name without its extension when there's none
    std::string Name() const;
    // a banner title was read (otherwise there's nothing but the file: "No metadata")
    bool HasMetadata() const { return !Title.empty(); }
    bool IsArchive() const;
};

class Library
{
public:
    ~Library() { Close(); }

    // the database in [profileDir], its rows loaded, and the worker started. [banner language]: BannerLanguage()
    void Open(const std::filesystem::path& profileDir, int bannerLanguage);
    void Close();

    // The folders to scan, from the next scan on. [forRun]: the command line's (--rom-folder) for this run only, so the rows of the
    // saved folders it leaves out stay in the database, only hidden; otherwise a folder that's no longer in the list loses its rows
    void SetFolders(const std::vector<std::string>& folders, bool forRun);
    std::vector<std::string> Folders() const;
    // a scan, queued on the worker (one already queued covers it)
    void Rescan();
    // the first scan of the run, once: when the library is first shown, so a game started from the command line doesn't scan
    void EnsureScanned();

    struct Progress
    {
        bool Scanning = false;   // a scan is queued or running
        bool Scanned = false;    // one has finished this run
        int Found = 0, Done = 0; // files found in the folders, and looked at so far
        std::string Current;     // the file being read
        bool Preparing = false;  // a game is being unpacked
        std::string PreparingName;
    };
    Progress GetProgress() const;
    // the list as the worker last published it (the ROMs in the current folders, kNotRom rows left out), and a number that changes with it
    std::shared_ptr<const std::vector<LibraryEntry>> Entries() const;
    uint64_t Generation() const { return Gen.load(); }
    // a scan or an unpack is queued or running (what --ui-script's wait-scan waits for)
    bool Busy() const;

    // a game booted from [path] (the archive's path for one in an archive): its last-played time now, in play_history
    void MarkPlayed(const std::string& path);
    // the absolute, normalized form paths are stored in (a drive's root keeps its backslash: "E:\")
    static std::string NormalizePath(const std::string& path);
    // the same folder, however it's written
    static bool SameFolder(const std::string& a, const std::string& b);

    // The ROM file to boot for [path] (UTF-8): the path itself for a plain file; for a zip or 7z, its ROM unpacked into the rom-cache
    // folder (on the worker, [done] then runs on the UI thread through PostToUi). [done] gets the file, or an empty one and the reason
    void Prepare(const std::string& path, std::function<void(const std::string& file, const std::string& error)> done);
    // the same, on the calling thread (the command line's game)
    std::string PrepareNow(const std::string& path, std::string& error);

    // runs a task on the UI thread (main.cpp sets it: the event messenger's Post)
    std::function<void(std::function<void()>)> PostToUi;
    // the game the library's focus was on last, by id, so the library can go back to it after a game
    int64_t LastFocused = 0;

private:
    void Run();
    void Scan();
    // a prepare queued while a scan runs, between two of its files
    void RunPrepares();
    void Publish();
    std::string PrepareFile(const std::string& path, std::string& error);

    LibraryDb Db;
    std::filesystem::path CacheDir;
    int Language = 1;

    mutable std::mutex Lock;
    std::condition_variable Wake;
    std::thread Worker;
    // set by Close(): the scan and an unpack stop at the next file, directory or block of data they read
    std::atomic<bool> Stopping = false;
    bool ScanQueued = false, ScanRunning = false, FirstScanAsked = false;
    struct PrepareJob { std::string Path; std::function<void(const std::string&, const std::string&)> Done; };
    std::deque<PrepareJob> Prepares;
    std::vector<std::string> FolderList;
    bool FoldersForRun = false;
    Progress State;
    std::shared_ptr<const std::vector<LibraryEntry>> Snapshot = std::make_shared<std::vector<LibraryEntry>>();
    std::atomic<uint64_t> Gen = 1;

    // the worker's own copy of every row (Publish reads it under Lock), and the last-played times (under Lock)
    std::vector<LibraryEntry> Rows;
    std::map<std::string, int64_t> Plays;
    // ids for rows the database couldn't store, unique for the run
    int64_t NextTempId = -1;
};

}
