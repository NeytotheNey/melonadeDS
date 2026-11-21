// The library's metadata and art sync (phase 2 step C), a port of the app's RomMetadataSyncWorker, RomMetadataSyncPlanner and
// RomMetadataSyncGate, plus the libretro thumbnails as the fallback that needs no account. One worker thread runs a sync when asked (after
// each library scan, a new key, Refresh metadata, --sync-now) and never while a game runs: a game starting stops it (requests in flight are
// cancelled) and the game ending asks for it again, as the app's EmulatorViewModel does. A sync:
//   1. computes each ROM's CRC32, MD5 and SHA1 once (one pass over the decompressed ROM) into library.db's roms
//   2. replaces RetroAchievements' hash library (connect API hashlibrary, DS 18 and DSi 78) when it's over a day old; it needs no key
//   3. with a Web API key: API_GetGame once per distinct game with no usable row (fetch_status 0 ok, 1 not found, 2 failed), in batches of
//      20 with the art after each batch (box art, else title, else icon, plus the title screen for the mosaic's wide tiles), two downloads
//      at a time in random order, with the app's back-off and failure rules (see MetadataSync.cpp)
//   4. with the libretro thumbnails on: the No-Intro DS DAT from libretro-database (downloaded the first time), each ROM's name by its
//      CRC32 or else its serial and region, and the box art and title screen of the games RetroAchievements has no art for
// Art goes in <profile>\media\ra\<game id>\<kind>.png and <profile>\media\libretro\<name>\<kind>.png and stays until it's cleared. What the
// sync knows goes to the library as a MetadataIndex (Metadata.h), at most once a second while it runs. --no-sync makes no requests at all
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Http.h"
#include "Metadata.h"

namespace Frontend
{

class Library;
class LibraryDb;

class MetadataSync
{
public:
    struct Config
    {
        bool NoSync = false;    // --no-sync: no requests at all this run (ROM hashes and the art on disk still)
        bool SyncNow = false;   // --sync-now: a sync at the start, with the hash library due whatever its age
        // where the requests go (--metadata-base-url, --thumbnails-base-url, --dat-url point them at a test server)
        std::string RaBase = "https://retroachievements.org";             // dorequest.php and /API/
        std::string MediaBase = "https://media.retroachievements.org";    // /Images/
        std::string ThumbnailsBase = "https://raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_Nintendo_DS/master";
        std::string DatUrl = "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/no-intro/Nintendo%20-%20Nintendo%20DS.dat";
        std::string UserAgent = "melonadeDS-desktop";
        int ApiDelayMs = 500;   // between two API calls, as the app
        int BackoffMs = 15 * 60 * 1000; // the first wait after a run that stopped early, doubling (--sync-backoff for tests)
    };

    ~MetadataSync() { Close(); }

    // loads what library.db and the media folder hold, publishes it to [library], and starts the worker (which waits to be asked)
    void Open(Library& library, const Config& config, const std::string& apiKey, bool libretro);
    // the worker stopped (requests in flight cancelled) and joined
    void Close();

    // The Web API key in use ("" for none). A new one starts the sync over (the app's REPLACE); none stops it
    void SetApiKey(const std::string& key);
    bool HasApiKey() const;
    // the libretro thumbnails on or off: off, their art isn't shown or downloaded
    void SetLibretro(bool on);

    // a sync, unless one is waiting or running (after a scan: the app's KEEP)
    void Request();
    // a sync from the start: one running now stops and starts over (a new key, Refresh metadata: the app's REPLACE)
    void Restart();
    // A game started: a running sync stops and none starts until every game has ended, which then asks for one (counted, as the app's
    // gate, so a game started before the last one's end can't let it in)
    void GameStarted();
    void GameEnded();

    // Refresh metadata: RetroAchievements rows fetched over two weeks ago (and any whose image failed) fetched again, libretro images found
    // missing over two weeks ago asked for again, the DAT downloaded again when it's that old; then a sync from the start
    void RefreshMetadata();
    // Clear downloaded art: the sync stopped and every file in the media folder deleted (on the worker); the art goes from the menus at once.
    // The next sync downloads it again, with no API calls
    void ClearArt();

    // the key tried with one API_GetGame call (for game 1), on a thread of its own; the result in GetStatus().Test
    enum class KeyTest { Idle, Testing, Valid, Rejected, Failed, Empty };
    void TestKey(const std::string& key);
    void ResetKeyTest();

    struct Status
    {
        bool Off = false;        // --no-sync
        bool Running = false;
        bool Waiting = false;    // asked for, but a game is running
        bool RetryLater = false; // stopped early; it tries again later in the run
        std::string Activity;    // what it's doing ("Fetching game details 3 of 20")
        std::string LastStop;    // why the last sync stopped early, when it did ("RetroAchievements rejected the Web API key")
        int64_t LastSync = 0;    // when a sync last ran to its end (ms since 1970), 0 never
        KeyTest Test = KeyTest::Idle;
    };
    Status GetStatus() const;
    // a sync is running or will start as soon as it can, or the art is being cleared (what --ui-script's wait-sync waits for); false while
    // a game holds it off
    bool Busy() const;
    // the requests made this run, by kind ("hashlibrary", "API_GetGame", "image", "libretro", "dat"), for the log and the checks
    std::string RequestCounts() const;

private:
    enum class Outcome { Done, RetryLater, Aborted };
    struct ArtTracker;
    struct NotFoundHoldBack;
    struct GameResult;
    enum class SaveResult { Saved, NotAnImage, Unsupported, Discarded, Failed };

    void Run();
    Outcome RunOnce();
    void HashRoms();
    // false when no ROM has a RetroAchievements hash (nothing asked for)
    bool UpdateHashLibrary(bool force);
    Outcome RetroAchievementsPhase();
    Outcome LibretroPhase();
    GameResult GetGame(int64_t gameId, const std::string& key, Http::Client& client);
    int FetchBatch(const std::vector<int64_t>& batch, std::map<int64_t, RaGameRow>& rows, NotFoundHoldBack& notFounds);
    void DownloadMissingRaArt(const std::vector<int64_t>& gameIds, ArtTracker& tracker);
    RaGameRow DownloadRaArt(RaGameRow row, ArtTracker& tracker);
    void DownloadRaTitle(const RaGameRow& row, ArtTracker& tracker);
    // the HTTP status (200 with [bytes]), or -100 cancelled, -101 not to be had (too large, a bad path), -102 the host's fault
    int DownloadRaImage(const std::string& imagePath, std::vector<uint8_t>& bytes);
    SaveResult SaveArt(const std::filesystem::path& file, const std::vector<uint8_t>& bytes, int clearCount);
    void StoreGame(const RaGameRow& row);
    void DeleteRaArt(int64_t gameId);
    bool LoadDat();
    bool DownloadDat();
    std::string LibretroNameFor(const LibraryEntry& entry) const;

    // what the library is shown from: built from the caches below and published (at most once a second while a sync runs, unless [now])
    void Publish(bool now);
    bool Aborted() const { return Abort.load() || (Net && Net->Cancelled()); }
    // the RetroAchievements part stopped (the key removed) or the libretro part (turned off), the rest of the run going on
    bool RaAborted() const { return Aborted() || RaStop.load(); }
    bool LibretroAborted() const { return Aborted() || LibretroStop.load(); }
    // the folder a libretro name's art goes in under media\libretro: the thumbnail's file name made safe for Windows (no "." or "..",
    // no reserved device name, no trailing dot or space, a hash of the name added when that changed it); empty when there's none
    static std::string LibretroFolderName(const std::string& name);
    // a stamp for a file saved now, which changes with each save, so the menus know to read it again
    uint64_t NewStamp() { return ++StampCounter; }
    // waits [ms], false when the sync was stopped meanwhile
    bool Sleep(int ms);
    void SetActivity(const std::string& text);
    void Count(const char* kind);

    Library* Lib = nullptr;
    LibraryDb* Db = nullptr;
    Config Cfg;
    std::filesystem::path MediaDir, DatFile;
    std::unique_ptr<Http::Client> Net, TestClient;

    mutable std::mutex Lock;
    std::condition_variable Wake;
    std::thread Worker, Tester;
    bool Stopping = false, Requested = false, Running = false, ClearQueued = false, ForceHashLibrary = false;
    int RunningGames = 0;
    int RetryAttempts = 0;
    std::chrono::steady_clock::time_point RetryAt {};
    bool RetryPending = false;
    std::atomic<bool> Abort = false, RaStop = false, LibretroStop = false;
    std::atomic<uint64_t> StampCounter = 0;
    std::atomic<int> ClearCount = 0;
    std::string Key;
    bool LibretroOn = true;
    std::string Activity, LastStop;
    int64_t LastSync = 0;
    KeyTest Test = KeyTest::Idle;
    std::map<std::string, int> Requests;

    // the caches (under Data)
    mutable std::mutex Data;
    std::mutex SaveLock;
    bool HasHashLib = false;
    std::map<std::string, int64_t> HashLib;
    std::map<int64_t, RaGameRow> Games;
    // the art on disk, with each file's stamp: (game id, kind), and (folder name, kind) with 0 box, 1 title
    std::map<std::pair<int64_t, int>, uint64_t> RaFiles;
    std::map<std::pair<std::string, int>, uint64_t> LibretroFiles;
    std::unordered_map<int64_t, MetadataIndex::RomHashes> Hashes;
    struct DatGame { std::string Name, Region, Serial; };
    std::vector<DatGame> DatGames;
    std::unordered_map<std::string, size_t> DatByCrc;            // lowercase CRC32 -> DatGames index
    std::unordered_map<std::string, std::vector<size_t>> DatBySerial;
    std::chrono::steady_clock::time_point LastPublish {};
    // one publish at a time, built and handed over together, so an older one can't land after a newer one
    std::mutex PublishLock;
    bool PublishPending = false;
};

}
