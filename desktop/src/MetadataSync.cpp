#include "MetadataSync.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <utility>

#include "ArtImage.h"
#include "Library.h"
#include "LibraryDb.h"
#include "RomFile.h"
#include "Settings.h"
#include "miniz/miniz.h"
#include "picojson/picojson.h"

namespace fs = std::filesystem;

namespace Frontend
{

namespace
{

constexpr int kConsoleDs = 18, kConsoleDsi = 78;
constexpr int64_t kDayMs = 24ll * 60 * 60 * 1000;
// the app's MIN_AGE_TO_REFETCH: a manual refresh fetches again what's older than this
constexpr int64_t kRefreshAgeMs = 14 * kDayMs;
constexpr int kGamesPerBatch = 20;
constexpr int kMaxConcurrentDownloads = 2;
constexpr int kMaxConsecutiveArtFailures = 3;
constexpr int kMaxConsecutiveNotFounds = 3;
// libretro: this many missing images in a row with none found this run, and it stops (a blanket 404 mustn't mark the whole library)
constexpr int kMaxLibretroMissingInARow = 10;
// a run that stopped early tries again after 15 minutes, then 30, 60, ... (the app's exponential back-off); this many runs in all, as the
// app's MAX_RUN_ATTEMPTS (runAttemptCount + 1 < 5: four retries)
constexpr int kMaxRunAttempts = 5;
constexpr int64_t kControlGameId = 1;

int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string Lower(std::string text)
{
    for (char& c : text)
        c = (char)tolower((unsigned char)c);
    return text;
}

std::string Trim(const std::string& text)
{
    size_t a = text.find_first_not_of(" \t\r\n"), b = text.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : text.substr(a, b - a + 1);
}

// libretro's thumbnail names: the No-Intro name with each of &*/:`<>?\|" made a _
std::string LibretroFileName(const std::string& name)
{
    std::string out = name;
    for (char& c : out)
    {
        if (strchr("&*/:`<>?\\|\"", c))
            c = '_';
    }
    return out;
}

uint32_t Fnv32(const std::string& text)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : text)
        h = (h ^ c) * 16777619u;
    return h;
}

const char* const kLibretroKinds[] = {"box", "title"};
const char* const kLibretroFolders[] = {"Named_Boxarts", "Named_Titles"};

// "/Images/012345.png" -> "012345.png"; empty for anything but a plain image name, which then can't point anywhere else (the app's IMAGE_PATH)
std::string ImageName(const std::string& path)
{
    const std::string prefix = "/Images/";
    if (path.compare(0, prefix.size(), prefix) != 0)
        return "";
    std::string name = path.substr(prefix.size());
    size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0)
        return "";
    for (size_t i = 0; i < dot; i++)
    {
        char c = name[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-')
            return "";
    }
    std::string ext = Lower(name.substr(dot + 1));
    if (ext != "png" && ext != "jpg" && ext != "jpeg" && ext != "gif" && ext != "webp")
        return "";
    return name;
}

// what RetroAchievements returns for a game with no icon or no other image
bool IsPlaceholder(const std::string& path)
{
    return path == "/Images/000001.png" || path == "/Images/000002.png";
}

const std::string& ImagePath(const RaGameRow& row, int kind)
{
    return kind == kArtBox ? row.ImageBoxArt : kind == kArtTitle ? row.ImageTitle : row.ImageIcon;
}

// the one image a game's tile uses: box art, else the title screen, else the icon (the app's chooseArt); -1 for none
int ChooseArt(const RaGameRow& row)
{
    for (int kind = 0; kind < kArtKinds; kind++)
    {
        if (!ImagePath(row, kind).empty())
            return kind;
    }
    return -1;
}

// the app's withoutImage: an image that will never work forgotten, and the fetch time cleared so the next manual refresh fetches the game again
RaGameRow WithoutImage(RaGameRow row, int kind)
{
    row.FetchedAt = 0;
    (kind == kArtBox ? row.ImageBoxArt : kind == kArtTitle ? row.ImageTitle : row.ImageIcon).clear();
    return row;
}

// the app's isArtOutdated
bool IsArtOutdated(const RaGameRow* previous, const RaGameRow& next)
{
    if (!previous)
        return false;
    bool titleOutdated = !previous->ImageTitle.empty() && previous->ImageTitle != next.ImageTitle;
    int before = ChooseArt(*previous);
    if (before < 0)
        return titleOutdated;
    int after = ChooseArt(next);
    return titleOutdated || after < 0 || ImagePath(*previous, before) != ImagePath(next, after);
}

// a refresh that fails or no longer finds the game keeps what the card shows (the app's withStatus)
RaGameRow WithStatus(const RaGameRow* previous, int64_t gameId, int status, int64_t now)
{
    RaGameRow row = previous ? *previous : RaGameRow();
    row.GameId = gameId;
    row.FetchStatus = status;
    row.FetchedAt = now;
    return row;
}

// the region a game code's last letter stands for, as No-Intro names it
const char* RegionWord(const std::string& gameCode)
{
    if (gameCode.size() < 4)
        return "";
    switch (gameCode[3])
    {
    case 'E': case 'L': case 'O': return "USA";
    case 'P': case 'W': case 'X': case 'Y': case 'Z': case 'V': return "Europe";
    case 'J': return "Japan";
    case 'K': return "Korea";
    case 'C': return "China";
    case 'U': return "Australia";
    case 'D': return "Germany";
    case 'F': return "France";
    case 'I': return "Italy";
    case 'S': return "Spain";
    case 'H': return "Netherlands";
    default: return "";
    }
}

// a DAT entry that isn't the retail game
bool IsOddRelease(const std::string& name)
{
    for (const char* tag : {"(Demo", "(Kiosk", "(Beta", "(Proto", "(Sample", "(Pirate", "(Unl"})
    {
        if (name.find(tag) != std::string::npos)
            return true;
    }
    return false;
}

std::string Hex(const uint8_t* data, size_t size)
{
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < size; i++)
    {
        out += hex[data[i] >> 4];
        out += hex[data[i] & 15];
    }
    return out;
}

// MD5 or SHA1 through Windows' CNG (bcrypt, a system DLL)
class Digest
{
public:
    explicit Digest(LPCWSTR algorithm)
    {
        if (BCryptOpenAlgorithmProvider(&Alg, algorithm, nullptr, 0) != 0 || BCryptCreateHash(Alg, &Hash, nullptr, 0, nullptr, 0, 0) != 0)
            Hash = nullptr;
        DWORD got = 0;
        if (Alg)
            BCryptGetProperty(Alg, BCRYPT_HASH_LENGTH, (PUCHAR)&Length, sizeof(Length), &got, 0);
    }
    ~Digest()
    {
        if (Hash)
            BCryptDestroyHash(Hash);
        if (Alg)
            BCryptCloseAlgorithmProvider(Alg, 0);
    }
    void Add(const uint8_t* data, size_t size)
    {
        if (Hash)
            BCryptHashData(Hash, (PUCHAR)data, (ULONG)size, 0);
    }
    std::string Finish()
    {
        if (!Hash || Length == 0 || Length > 64)
            return "";
        uint8_t out[64];
        if (BCryptFinishHash(Hash, out, Length, 0) != 0)
            return "";
        return Hex(out, Length);
    }

private:
    BCRYPT_ALG_HANDLE Alg = nullptr;
    BCRYPT_HASH_HANDLE Hash = nullptr;
    DWORD Length = 0;
};

bool WriteFileAtomically(const fs::path& file, const void* data, size_t size)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    fs::path temp = file;
    temp += ".part";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write((const char*)data, (std::streamsize)size);
        if (!out)
        {
            out.close();
            fs::remove(temp, ec);
            return false;
        }
    }
    fs::rename(temp, file, ec);
    if (ec)
    {
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

std::string JsonText(const picojson::object& o, const char* key, bool& malformed)
{
    auto it = o.find(key);
    if (it == o.end() || it->second.is<picojson::null>())
        return "";
    if (it->second.is<std::string>())
        return Trim(it->second.get<std::string>());
    // (a number is taken as text, as the app's lenient parser does; anything else doesn't fit the field)
    if (it->second.is<double>() || it->second.is<bool>())
        return Trim(it->second.to_str());
    malformed = true;
    return "";
}

// runs [work] on each of [items], two at a time
template <typename T, typename F> void TwoAtATime(const std::vector<T>& items, F work)
{
    std::atomic<size_t> next = 0;
    auto loop = [&]
    {
        for (size_t i; (i = next++) < items.size();)
        {
            try
            {
                work(items[i]);
            }
            catch (const std::exception& e)
            {
                fprintf(stderr, "metadata: a download failed: %s\n", e.what());
            }
        }
    };
    std::vector<std::thread> threads;
    for (int t = 1; t < kMaxConcurrentDownloads; t++)
        threads.emplace_back(loop);
    loop();
    for (std::thread& t : threads)
        t.join();
}

}

// what a download attempt says about the image and about the host (the app's ArtOutcome)
enum class ArtOutcome { Saved, Inconclusive, ImageUnavailable, NotAnImage, HostProblem, RateLimited };
enum class ArtAction { Done, TryNextKind, StopDownloads };

// The app's ArtDownloadTracker: outcomes turned into actions over a run, since one image proves nothing about the host while several in a row
// do. Stopping keeps a CDN block or a blanket 404 from clearing the art paths of the whole library. Any thread
struct MetadataSync::ArtTracker
{
    std::mutex M;
    int ConsecutiveFailures = 0;
    bool SavedAny = false;
    std::atomic<bool> Stopped = false;

    bool ShouldRetry()
    {
        std::lock_guard g(M);
        return Stopped && SavedAny;
    }

    ArtAction ActionFor(ArtOutcome outcome)
    {
        std::lock_guard g(M);
        if (outcome == ArtOutcome::Saved)
            SavedAny = true;
        if (Stopped)
            return ArtAction::StopDownloads;
        auto countFailure = [&]
        {
            ConsecutiveFailures++;
            Stopped = ConsecutiveFailures >= kMaxConsecutiveArtFailures;
            return Stopped.load();
        };
        switch (outcome)
        {
        case ArtOutcome::Saved: ConsecutiveFailures = 0; return ArtAction::Done;
        case ArtOutcome::Inconclusive: return ArtAction::Done;
        case ArtOutcome::ImageUnavailable:
        case ArtOutcome::NotAnImage: return countFailure() ? ArtAction::StopDownloads : ArtAction::TryNextKind;
        case ArtOutcome::HostProblem: return countFailure() ? ArtAction::StopDownloads : ArtAction::Done;
        case ArtOutcome::RateLimited: Stopped = true; return ArtAction::StopDownloads;
        }
        return ArtAction::Done;
    }
};

// The app's NotFoundHoldBack: "not found" answers held until something shows the API is working. Game ids come from RetroAchievements' own
// hash library, so several in a row almost certainly mean a bad response, and "not found" isn't retried by itself
struct MetadataSync::NotFoundHoldBack
{
    std::vector<RaGameRow> Held;
    bool Hold(const RaGameRow& row)
    {
        Held.push_back(row);
        return (int)Held.size() >= kMaxConsecutiveNotFounds;
    }
    std::set<int64_t> HeldIds() const
    {
        std::set<int64_t> ids;
        for (const RaGameRow& r : Held)
            ids.insert(r.GameId);
        return ids;
    }
    std::vector<RaGameRow> Release() { return std::exchange(Held, {}); }
    std::vector<RaGameRow> ReleaseAsFailed()
    {
        std::vector<RaGameRow> rows = Release();
        for (RaGameRow& r : rows)
            r.FetchStatus = RaGameRow::kFailed;
        return rows;
    }
};

// API_GetGame's answer, as the app's RAWebApi gives it
struct MetadataSync::GameResult
{
    enum Kind { Ok, NotFound, InvalidKey, RateLimited, Network, Unexpected, Malformed, HttpError, Cancelled } Type = Network;
    int Code = 0;
    RaGameRow Row;
};

std::string MetadataSync::LibretroFolderName(const std::string& name)
{
    // the server's file name, then whatever Windows wouldn't take as a folder of its own: control characters, leading spaces, trailing dots
    // and spaces (which also rules out "." and ".."), a reserved device name (CON, NUL, COM1, LPT1, ..., with or without an extension),
    // and length. The name's own hash goes at the end whatever it was, so two names can't share a folder (case, or the characters made _)
    std::string out = LibretroFileName(name);
    for (char& c : out)
    {
        if ((unsigned char)c < 0x20 || c == 0x7f)
            c = '_';
    }
    size_t lead = out.find_first_not_of(' ');
    out.erase(0, lead == std::string::npos ? out.size() : lead);
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();
    // at most 120 bytes, cut on a UTF-8 character's start
    if (out.size() > 120)
    {
        size_t cut = 120;
        while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80)
            cut--;
        out.resize(cut);
        while (!out.empty() && (out.back() == '.' || out.back() == ' '))
            out.pop_back();
    }
    if (out.empty())
        return "";
    std::string stem = out.substr(0, out.find('.'));
    while (!stem.empty() && stem.back() == ' ')
        stem.pop_back();
    for (char& c : stem)
        c = (char)toupper((unsigned char)c);
    bool reserved = stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" || stem == "CONOUT$"
        || ((stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0)
            && (stem.size() == 4 ? isdigit((unsigned char)stem[3]) != 0
                : stem == "COM\xc2\xb9" || stem == "COM\xc2\xb2" || stem == "COM\xc2\xb3" || stem == "LPT\xc2\xb9" || stem == "LPT\xc2\xb2"
                    || stem == "LPT\xc2\xb3"));
    if (reserved)
        out = "_" + out;
    char hash[16];
    snprintf(hash, sizeof(hash), " ~%08x", Fnv32(name));
    return out + hash;
}

const char* ArtKindName(int kind)
{
    return kind == kArtBox ? "box" : kind == kArtTitle ? "title" : "icon";
}

std::string FormatReleaseDate(const std::string& released, const std::string& granularity)
{
    // YYYY, YYYY-MM or YYYY-MM-DD, then anything that doesn't start with a digit (the app's RELEASE_DATE)
    std::string t = Trim(released);
    auto digits = [&](size_t at, size_t n)
    {
        if (t.size() < at + n)
            return false;
        for (size_t i = at; i < at + n; i++)
        {
            if (!isdigit((unsigned char)t[i]))
                return false;
        }
        return true;
    };
    if (!digits(0, 4))
        return "";
    size_t end = 4;
    int month = 0, day = 0;
    if (t.size() > 4 && t[4] == '-' && digits(5, 2))
    {
        month = atoi(t.substr(5, 2).c_str());
        end = 7;
        if (t.size() > 7 && t[7] == '-' && digits(8, 2))
        {
            day = atoi(t.substr(8, 2).c_str());
            end = 10;
        }
    }
    if (end < t.size() && isdigit((unsigned char)t[end]))
        return "";
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::string year = t.substr(0, 4);
    bool hasMonth = month >= 1 && month <= 12, hasDay = day >= 1 && day <= 31;
    if (granularity == "day" && hasMonth && hasDay)
        return std::string(kMonths[month - 1]) + " " + std::to_string(day) + ", " + year;
    if ((granularity == "day" || granularity == "month") && hasMonth)
        return std::string(kMonths[month - 1]) + " " + year;
    return year;
}

void ApplyMetadata(const MetadataIndex& index, bool libretro, LibraryEntry& e)
{
    e.RaGameId = 0;
    e.RaGame = nullptr;
    e.ArtFile = {};
    e.TitleArtFile = {};
    e.ArtSource = kArtNone;
    e.LibretroName.clear();
    e.Crc32.clear();
    e.RaHashState = kRaHashUnknown;
    if (e.State != LibraryEntry::kRead)
        return;
    auto hashes = index.Hashes.find(e.Id);
    if (hashes != index.Hashes.end() && hashes->second.FileSize == e.FileSize && hashes->second.FileTime == e.FileTime)
        e.Crc32 = hashes->second.Crc32;

    if (e.RaHash.empty())
        e.RaHashState = kRaHashMissing;
    else if (!index.HasHashLibrary)
        e.RaHashState = kRaHashUnknown;
    else
    {
        auto id = index.GameIds.find(Lower(e.RaHash));
        e.RaHashState = id != index.GameIds.end() ? kRaHashMatched : kRaHashUnmatched;
        if (id != index.GameIds.end())
            e.RaGameId = id->second;
    }
    ArtRef raIcon;
    if (e.RaGameId)
    {
        auto game = index.Games.find(e.RaGameId);
        if (game != index.Games.end())
            e.RaGame = game->second;
        auto art = index.RaArt.find(e.RaGameId);
        if (art != index.RaArt.end())
        {
            // box art, else the title screen (the icon, a small badge, only when nothing better is on disk)
            e.ArtFile = !art->second[kArtBox].empty() ? art->second[kArtBox] : art->second[kArtTitle];
            e.TitleArtFile = art->second[kArtTitle];
            raIcon = art->second[kArtIcon];
            if (!e.ArtFile.empty())
                e.ArtSource = kArtRetroAchievements;
        }
    }
    if (libretro)
    {
        auto name = index.LibretroNames.find(e.Id);
        if (name != index.LibretroNames.end())
        {
            e.LibretroName = name->second;
            auto art = index.LibretroArt.find(name->second);
            if (art != index.LibretroArt.end())
            {
                if (e.ArtFile.empty())
                {
                    e.ArtFile = !art->second[0].empty() ? art->second[0] : art->second[1];
                    if (!e.ArtFile.empty())
                        e.ArtSource = kArtLibretro;
                }
                if (e.TitleArtFile.empty())
                    e.TitleArtFile = art->second[1];
            }
        }
    }
    if (e.ArtFile.empty() && !raIcon.empty())
    {
        e.ArtFile = raIcon;
        e.ArtSource = kArtRetroAchievements;
    }
}

void MetadataSync::Open(Library& library, const Config& config, const std::string& apiKey, bool libretro)
{
    Lib = &library;
    Db = &library.Database();
    Cfg = config;
    Key = apiKey;
    LibretroOn = libretro;
    ForceHashLibrary = config.SyncNow;
    Net = std::make_unique<Http::Client>(config.UserAgent);
    MediaDir = (library.ProfileDir() / "media").make_preferred();
    // new files' stamps start past any file time on disk
    StampCounter = (uint64_t)fs::file_time_type::clock::now().time_since_epoch().count();
    DatFile = (library.ProfileDir() / "libretro" / "Nintendo - Nintendo DS.dat").make_preferred();

    // what's stored: the hash library, the games, the ROMs' hashes, and the art on disk
    {
        std::lock_guard guard(Data);
        Db->LoadHashLibrary(HashLib);
        HasHashLib = !HashLib.empty();
        Db->LoadGames(Games);
        std::vector<LibraryDb::RomHashRow> hashes;
        Db->LoadRomHashes(hashes);
        for (const LibraryDb::RomHashRow& h : hashes)
            Hashes[h.Id] = {h.Crc32, h.Md5, h.Sha1, h.FileSize, h.FileTime};
        std::error_code ec;
        for (const fs::directory_entry& game : fs::directory_iterator(MediaDir / "ra", ec))
        {
            int64_t id = _wtoi64(game.path().filename().c_str());
            for (int kind = 0; id > 0 && kind < kArtKinds; kind++)
            {
                fs::path file = game.path() / (std::string(ArtKindName(kind)) + ".png");
                if (fs::is_regular_file(file, ec))
                    RaFiles[{id, kind}] = (uint64_t)fs::last_write_time(file, ec).time_since_epoch().count();
            }
        }
        for (const fs::directory_entry& name : fs::directory_iterator(MediaDir / "libretro", ec))
        {
            for (int kind = 0; kind < 2; kind++)
            {
                fs::path file = name.path() / (std::string(kLibretroKinds[kind]) + ".png");
                if (fs::is_regular_file(file, ec))
                    LibretroFiles[{PathToUtf8(name.path().filename()), kind}] = (uint64_t)fs::last_write_time(file, ec).time_since_epoch().count();
            }
        }
    }
    LoadDat();
    std::string last = Db->GetState("last_sync");
    LastSync = last.empty() ? 0 : _atoi64(last.c_str());
    printf("metadata: %zu hashes in the hash library, %zu games, %zu RetroAchievements and %zu libretro images on disk, %zu ROM hashes, %s,"
        " %s, libretro thumbnails %s%s\n", HashLib.size(), Games.size(), RaFiles.size(), LibretroFiles.size(), Hashes.size(),
        DatGames.empty() ? "no DAT" : (std::to_string(DatGames.size()) + " games in the DAT").c_str(), Key.empty() ? "no Web API key" : "a Web API key",
        LibretroOn ? "on" : "off", Cfg.NoSync ? ", --no-sync: no requests this run" : "");
    fflush(stdout);
    Publish(true);
    Worker = std::thread([this] { Run(); });
}

void MetadataSync::Close()
{
    {
        std::lock_guard guard(Lock);
        if (!Worker.joinable() && !Tester.joinable())
            return;
        Stopping = true;
    }
    Abort = true;
    if (Net)
        Net->Cancel();
    if (TestClient)
        TestClient->Cancel();
    Wake.notify_all();
    if (Worker.joinable())
        Worker.join();
    if (Tester.joinable())
        Tester.join();
    printf("metadata: requests this run: %s\n", RequestCounts().c_str());
    fflush(stdout);
}

void MetadataSync::SetApiKey(const std::string& key)
{
    bool restart;
    {
        std::lock_guard guard(Lock);
        if (key == Key)
            return;
        Key = key;
        restart = !key.empty();
        // no key: only the RetroAchievements part stops (the app cancels its worker, which does nothing else); the ROM hashes, the hash
        // library and the libretro thumbnails go on
        if (!restart)
            RaStop = true;
    }
    printf("metadata: %s\n", key.empty() ? "the Web API key removed" : "a new Web API key");
    fflush(stdout);
    Publish(true);
    if (restart)
        Restart();
}

bool MetadataSync::HasApiKey() const
{
    std::lock_guard guard(Lock);
    return !Key.empty();
}

void MetadataSync::SetLibretro(bool on)
{
    {
        std::lock_guard guard(Lock);
        if (LibretroOn == on)
            return;
        LibretroOn = on;
        if (!on)
            LibretroStop = true;
    }
    Publish(true);
    if (on)
        Request();
}

void MetadataSync::Request()
{
    {
        std::lock_guard guard(Lock);
        // the app's KEEP: a sync that's running, or waiting out a back-off, stays as it is (it or the next scan sees the new games)
        if (Running || RetryPending)
        {
            printf("metadata: a sync is %s: kept as it is\n", Running ? "running" : "waiting to try again");
            fflush(stdout);
            return;
        }
        Requested = true;
    }
    Wake.notify_all();
}

void MetadataSync::Restart()
{
    {
        std::lock_guard guard(Lock);
        Requested = true;
        RetryAttempts = 0;
        if (Running)
        {
            Abort = true;
            Net->Cancel();
        }
    }
    Wake.notify_all();
}

void MetadataSync::GameStarted()
{
    std::string counts = RequestCounts();
    std::lock_guard guard(Lock);
    RunningGames++;
    printf("metadata: a game started (requests so far: %s)%s\n", counts.c_str(), Running ? ": the sync stops until it ends" : "");
    fflush(stdout);
    // (a key test too)
    if (TestClient && Test == KeyTest::Testing)
        TestClient->Cancel();
    if (Running)
    {
        Abort = true;
        Net->Cancel();
    }
    Wake.notify_all();
}

void MetadataSync::GameEnded()
{
    {
        std::lock_guard guard(Lock);
        RunningGames = std::max(0, RunningGames - 1);
        if (RunningGames > 0)
            return;
        int total = 0;
        for (const auto& [kind, n] : Requests)
            total += n;
        printf("metadata: the game ended (requests so far: %d): a sync is asked for\n", total);
        fflush(stdout);
        // what was stopped for the game, and anything that asked meanwhile (the app enqueues it again with KEEP)
        Requested = true;
    }
    Wake.notify_all();
}

void MetadataSync::RefreshMetadata()
{
    int64_t cutoff = NowMs() - kRefreshAgeMs;
    int games = Db->MarkGamesForRefetch(cutoff);
    int images = Db->ForgetLibretroMissing(cutoff);
    {
        std::lock_guard guard(Data);
        for (auto& [id, row] : Games)
        {
            if (row.FetchedAt <= cutoff)
                row.FetchStatus = RaGameRow::kFailed;
        }
    }
    std::string dat = Db->GetState("libretro_dat");
    if (!dat.empty() && _atoi64(dat.c_str()) <= cutoff)
        Db->SetState("libretro_dat_due", "1");
    printf("metadata: refresh: %d games and %d missing libretro images to ask for again\n", games, images);
    fflush(stdout);
    Publish(true);
    Restart();
}

void MetadataSync::ClearArt()
{
    {
        std::lock_guard guard(Lock);
        // or the sync would just put the art back; the next scan starts it again (as the app)
        Requested = false;
        RetryPending = false;
        ClearQueued = true;
        ClearCount++;
        if (Running)
        {
            Abort = true;
            Net->Cancel();
        }
    }
    {
        std::lock_guard guard(Data);
        RaFiles.clear();
        LibretroFiles.clear();
    }
    Publish(true);
    Wake.notify_all();
}

void MetadataSync::TestKey(const std::string& key)
{
    std::string k = Trim(key);
    {
        std::lock_guard guard(Lock);
        if (k.empty())
            k = Key;
        if (k.empty())
        {
            Test = KeyTest::Empty;
            return;
        }
        if (Test == KeyTest::Testing)
            return;
        Test = KeyTest::Testing;
    }
    if (Tester.joinable())
        Tester.join();
    if (!TestClient)
        TestClient = std::make_unique<Http::Client>(Cfg.UserAgent);
    if (Cfg.NoSync)
    {
        std::lock_guard guard(Lock);
        Test = KeyTest::Failed;
        printf("metadata: key test: --no-sync, no request made\n");
        fflush(stdout);
        return;
    }
    TestClient->Resume();
    Tester = std::thread([this, k]
    {
        GameResult r = GetGame(kControlGameId, k, *TestClient);
        KeyTest result = r.Type == GameResult::Ok ? KeyTest::Valid : r.Type == GameResult::InvalidKey ? KeyTest::Rejected
            : r.Type == GameResult::Cancelled ? KeyTest::Idle : KeyTest::Failed;
        printf("metadata: key test: %s\n", result == KeyTest::Valid ? "valid" : result == KeyTest::Rejected ? "rejected"
            : result == KeyTest::Idle ? "cancelled (a game started)" : "failed");
        fflush(stdout);
        std::lock_guard guard(Lock);
        Test = result;
    });
}

void MetadataSync::ResetKeyTest()
{
    std::lock_guard guard(Lock);
    if (Test != KeyTest::Testing)
        Test = KeyTest::Idle;
}

MetadataSync::Status MetadataSync::GetStatus() const
{
    std::lock_guard guard(Lock);
    Status s;
    s.Off = Cfg.NoSync;
    s.Running = Running;
    s.Waiting = (Requested || RetryPending) && RunningGames > 0;
    s.RetryLater = RetryPending;
    s.Activity = Activity;
    s.LastStop = LastStop;
    s.LastSync = LastSync;
    s.Test = Test;
    return s;
}

bool MetadataSync::Busy() const
{
    std::lock_guard guard(Lock);
    return Running || ClearQueued || (Requested && RunningGames == 0) || Test == KeyTest::Testing;
}

std::string MetadataSync::RequestCounts() const
{
    std::lock_guard guard(Lock);
    std::string out;
    int total = 0;
    for (const auto& [kind, n] : Requests)
    {
        out += (out.empty() ? "" : ", ") + kind + " " + std::to_string(n);
        total += n;
    }
    return std::to_string(total) + (out.empty() ? "" : " (" + out + ")");
}

void MetadataSync::Count(const char* kind)
{
    std::lock_guard guard(Lock);
    Requests[kind]++;
}

void MetadataSync::SetActivity(const std::string& text)
{
    std::lock_guard guard(Lock);
    Activity = text;
}

bool MetadataSync::Sleep(int ms)
{
    std::unique_lock lock(Lock);
    Wake.wait_for(lock, std::chrono::milliseconds(ms), [&] { return Stopping || Abort.load(); });
    return !Stopping && !Abort;
}

void MetadataSync::Run()
{
    std::unique_lock lock(Lock);
    for (;;)
    {
        auto ready = [&]
        {
            return Stopping || ClearQueued || (RunningGames == 0 && (Requested || (RetryPending && std::chrono::steady_clock::now() >= RetryAt)));
        };
        // a retry's time only counts while no game runs: with one running (or nothing pending) it waits for a wake-up, not a deadline,
        // which would otherwise pass during the game and wake it over and over
        if (RetryPending && RunningGames == 0)
            Wake.wait_until(lock, RetryAt, ready);
        else
            Wake.wait(lock, ready);
        if (Stopping)
            return;
        if (ClearQueued)
        {
            ClearQueued = false;
            lock.unlock();
            std::error_code ec;
            uintmax_t removed = fs::remove_all(MediaDir, ec);
            printf("metadata: downloaded art cleared (%llu files and folders)%s\n", (unsigned long long)removed,
                ec ? (" with an error: " + ec.message()).c_str() : "");
            fflush(stdout);
            {
                // (an image saved just before the clear, which the folder's removal took too)
                std::lock_guard guard(Data);
                RaFiles.clear();
                LibretroFiles.clear();
            }
            if (OnArtCleared)
                OnArtCleared();
            Publish(true);
            lock.lock();
            continue;
        }
        if (RunningGames > 0)
            continue;
        if (!Requested && !(RetryPending && std::chrono::steady_clock::now() >= RetryAt))
            continue;
        Requested = false;
        RetryPending = false;
        Running = true;
        Abort = false;
        RaStop = false;
        LibretroStop = !LibretroOn;
        LastStop.clear();
        lock.unlock();
        Net->Resume();
        auto start = std::chrono::steady_clock::now();
        Outcome outcome = Outcome::Aborted;
        try
        {
            outcome = RunOnce();
        }
        catch (const std::exception& e)
        {
            fprintf(stderr, "metadata: the sync stopped: %s\n", e.what());
            outcome = Outcome::RetryLater;
        }
        Publish(true);
        // (--no-sync made no requests: that's no sync to show as the last one)
        if (outcome == Outcome::Done && !Cfg.NoSync)
        {
            int64_t now = NowMs();
            Db->SetState("last_sync", std::to_string(now));
            std::lock_guard guard(Lock);
            LastSync = now;
        }
        printf("metadata: sync %s in %.1f s; requests so far: %s\n", outcome == Outcome::Done ? "done" : outcome == Outcome::RetryLater
            ? "stopped early (it tries again later)" : "stopped", std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
            RequestCounts().c_str());
        fflush(stdout);
        lock.lock();
        Running = false;
        Activity.clear();
        if (outcome == Outcome::RetryLater && RetryAttempts + 1 < kMaxRunAttempts)
        {
            // the app's WorkManager back-off: 15 minutes (Config::BackoffMs), doubling
            RetryPending = true;
            RetryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds((int64_t)Cfg.BackoffMs << RetryAttempts);
            RetryAttempts++;
            printf("metadata: run %d of at most %d stopped early; the next in %lld s\n", RetryAttempts, kMaxRunAttempts,
                (long long)(((int64_t)Cfg.BackoffMs << (RetryAttempts - 1)) / 1000));
            fflush(stdout);
        }
        else if (outcome == Outcome::RetryLater)
        {
            printf("metadata: %d runs stopped early in a row: no more until something asks for one\n", kMaxRunAttempts);
            fflush(stdout);
            RetryAttempts = 0;
        }
        else if (outcome == Outcome::Done)
            RetryAttempts = 0;
    }
}

MetadataSync::Outcome MetadataSync::RunOnce()
{
    HashRoms();
    if (Aborted())
        return Outcome::Aborted;
    Publish(true);
    if (Cfg.NoSync)
    {
        printf("metadata: --no-sync: no requests\n");
        fflush(stdout);
        return Outcome::Done;
    }
    bool force;
    {
        std::lock_guard guard(Lock);
        force = ForceHashLibrary;
        ForceHashLibrary = false;
    }
    if (!UpdateHashLibrary(force) && force)
    {
        // (nothing to look up yet: kept for the sync after the scan)
        std::lock_guard guard(Lock);
        ForceHashLibrary = true;
    }
    if (Aborted())
        return Outcome::Aborted;
    Publish(true);
    Outcome ra = RetroAchievementsPhase();
    if (Aborted())
        return Outcome::Aborted;
    Publish(true);
    Outcome libretro = LibretroPhase();
    if (Aborted())
        return Outcome::Aborted;
    return ra == Outcome::RetryLater || libretro == Outcome::RetryLater ? Outcome::RetryLater : Outcome::Done;
}

// Each ROM's CRC32, MD5 and SHA1, once per file: one pass over the decompressed ROM (a zip's entry read as it goes, a 7z's decoded), on
// this thread. Online-only files aren't read (that would download them); a game starting stops it between two blocks
void MetadataSync::HashRoms()
{
    std::shared_ptr<const std::vector<LibraryEntry>> entries = Lib->Entries();
    std::vector<const LibraryEntry*> todo;
    {
        std::lock_guard guard(Data);
        std::set<int64_t> seen;
        for (const LibraryEntry& e : *entries)
        {
            if (e.State != LibraryEntry::kRead || e.Id <= 0 || !seen.insert(e.Id).second)
                continue;
            auto h = Hashes.find(e.Id);
            if (h == Hashes.end() || h->second.FileSize != e.FileSize || h->second.FileTime != e.FileTime)
                todo.push_back(&e);
        }
    }
    if (todo.empty())
        return;
    auto start = std::chrono::steady_clock::now();
    uint64_t bytes = 0;
    int done = 0;
    std::vector<uint8_t> buffer(1 << 20);
    for (const LibraryEntry* e : todo)
    {
        if (Aborted())
            return;
        SetActivity("Reading ROMs " + std::to_string(done + 1) + " of " + std::to_string(todo.size()));
        fs::path path = PathFromUtf8(e->Path);
        std::unique_ptr<RomData> data;
        std::string error;
        try
        {
            if (IsArchiveName(path))
            {
                ArchiveEntry entry;
                if (FindRomInArchive(path, entry, error))
                    data = OpenArchiveRom(path, entry, &Abort);
            }
            else
                data = OpenRomFile(path);
        }
        catch (const std::exception& x)
        {
            error = x.what();
        }
        if (!data)
        {
            fprintf(stderr, "metadata: can't read %s for its hashes%s%s\n", e->FileName.c_str(), error.empty() ? "" : ": ", error.c_str());
            continue;
        }
        uint32_t crc = MZ_CRC32_INIT;
        Digest md5(BCRYPT_MD5_ALGORITHM), sha1(BCRYPT_SHA1_ALGORITHM);
        uint64_t size = data->Size(), at = 0;
        bool ok = true;
        while (at < size)
        {
            if (Aborted())
                return;
            size_t n = data->Read(at, buffer.data(), (size_t)std::min<uint64_t>(buffer.size(), size - at));
            if (n == 0)
            {
                ok = false;
                break;
            }
            crc = (uint32_t)mz_crc32(crc, buffer.data(), n);
            md5.Add(buffer.data(), n);
            sha1.Add(buffer.data(), n);
            at += n;
        }
        if (!ok || data->Failed())
        {
            fprintf(stderr, "metadata: reading %s for its hashes failed\n", e->FileName.c_str());
            continue;
        }
        char crcText[16];
        snprintf(crcText, sizeof(crcText), "%08x", crc);
        LibraryDb::RomHashRow row {e->Id, e->FileSize, e->FileTime, crcText, md5.Finish(), sha1.Finish()};
        bool stored = Db->StoreRomHashes(row);
        {
            std::lock_guard guard(Data);
            Hashes[e->Id] = {row.Crc32, row.Md5, row.Sha1, row.FileSize, row.FileTime};
        }
        printf("metadata: %s: crc32 %s, md5 %s, sha1 %s%s\n", e->FileName.c_str(), row.Crc32.c_str(), row.Md5.c_str(), row.Sha1.c_str(),
            stored ? "" : " (not stored: the file changed or the database is busy)");
        bytes += size;
        done++;
        Publish(false);
    }
    printf("metadata: hashed %d ROMs (%.0f MB) in %.1f s\n", done, bytes / 1048576.0,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    fflush(stdout);
}

// RetroAchievements' hash library for DS and DSi: replaced when it's over a day old (or [force]), kept as it is when the call fails
bool MetadataSync::UpdateHashLibrary(bool force)
{
    std::shared_ptr<const std::vector<LibraryEntry>> entries = Lib->Entries();
    bool anyHash = std::any_of(entries->begin(), entries->end(), [](const LibraryEntry& e) { return !e.RaHash.empty(); });
    if (!anyHash)
        return false;
    int64_t now = NowMs();
    for (int console : {kConsoleDs, kConsoleDsi})
    {
        std::string key = "hash_library_" + std::to_string(console);
        std::string last = Db->GetState(key);
        if (!force && !last.empty() && now - _atoi64(last.c_str()) < kDayMs)
            continue;
        if (Aborted())
            return true;
        SetActivity("Updating RetroAchievements' hash library");
        Count("hashlibrary");
        Http::Request req;
        req.Url = Cfg.RaBase + "/dorequest.php?r=hashlibrary&c=" + std::to_string(console);
        req.FollowRedirects = false;
        req.MaxBytes = 32u << 20;
        req.TimeoutMs = 30000;
        Http::Response r = Net->Get(req);
        if (r.Err == Http::Response::Error::Cancelled)
            return true;
        std::vector<std::pair<std::string, int64_t>> hashes;
        bool ok = r.Ok();
        if (ok)
        {
            picojson::value v;
            std::string error = ParseJson(r.BodyText(), v);
            ok = error.empty() && v.is<picojson::object>() && v.contains("Success") && v.get("Success").is<bool>() && v.get("Success").get<bool>()
                && v.contains("MD5List") && v.get("MD5List").is<picojson::object>();
            if (ok)
            {
                for (const auto& [hash, id] : v.get("MD5List").get<picojson::object>())
                {
                    if (id.is<double>() && hash.size() == 32 && id.get<double>() > 0)
                        hashes.emplace_back(Lower(hash), (int64_t)id.get<double>());
                }
            }
        }
        if (!ok)
        {
            printf("metadata: the hash library for console %d didn't come (%s%s); the stored one stays\n", console, Http::Describe(r).c_str(),
                r.Ok() ? ", not the answer expected" : "");
            fflush(stdout);
            continue;
        }
        if (Db->StoreHashLibrary(console, hashes))
            Db->SetState(key, std::to_string(now));
        printf("metadata: hash library for console %d: %zu hashes\n", console, hashes.size());
        fflush(stdout);
        std::lock_guard guard(Data);
        HashLib.clear();
        Db->LoadHashLibrary(HashLib);
        HasHashLib = !HashLib.empty();
    }
    return true;
}

MetadataSync::GameResult MetadataSync::GetGame(int64_t gameId, const std::string& key, Http::Client& client)
{
    GameResult result;
    Http::Request req;
    req.Url = Cfg.RaBase + "/API/API_GetGame.php?i=" + std::to_string(gameId) + "&y=" + Http::UrlEncode(key);
    // a redirect could take the key to another host
    req.FollowRedirects = false;
    req.MaxBytes = 1u << 20;
    Count("API_GetGame");
    Http::Response r = client.Get(req);
    if (r.Err == Http::Response::Error::Cancelled)
    {
        result.Type = GameResult::Cancelled;
        return result;
    }
    if (r.Err != Http::Response::Error::None)
    {
        result.Type = GameResult::Network;
        return result;
    }
    result.Code = r.Status;
    if (r.Status == 401 || r.Status == 403)
    {
        result.Type = GameResult::InvalidKey;
        return result;
    }
    if (r.Status == 429)
    {
        result.Type = GameResult::RateLimited;
        return result;
    }
    if (r.Status != 200)
    {
        result.Type = GameResult::HttpError;
        return result;
    }
    picojson::value v;
    if (!ParseJson(r.BodyText(), v).empty())
    {
        result.Type = GameResult::Unexpected;
        return result;
    }
    // only an empty answer means an unknown game; an object about something else (an error message) isn't an answer about it at all
    if ((v.is<picojson::array>() && v.get<picojson::array>().empty()) || (v.is<picojson::object>() && v.get<picojson::object>().empty()))
    {
        result.Type = GameResult::NotFound;
        return result;
    }
    if (!v.is<picojson::object>() || !v.contains("Title"))
    {
        result.Type = GameResult::Unexpected;
        return result;
    }
    const picojson::object& o = v.get<picojson::object>();
    bool malformed = false;
    RaGameRow& row = result.Row;
    row.GameId = gameId;
    row.Title = JsonText(o, "Title", malformed);
    row.Publisher = JsonText(o, "Publisher", malformed);
    row.Developer = JsonText(o, "Developer", malformed);
    row.Genre = JsonText(o, "Genre", malformed);
    row.Released = JsonText(o, "Released", malformed);
    row.ReleasedGranularity = JsonText(o, "ReleasedAtGranularity", malformed);
    auto image = [&](const char* field)
    {
        std::string path = JsonText(o, field, malformed);
        return IsPlaceholder(path) ? std::string() : path;
    };
    row.ImageIcon = image("ImageIcon");
    row.ImageTitle = image("ImageTitle");
    row.ImageIngame = image("ImageIngame");
    row.ImageBoxArt = image("ImageBoxArt");
    if (malformed)
    {
        result.Type = GameResult::Malformed;
        return result;
    }
    result.Type = row.Title.empty() ? GameResult::NotFound : GameResult::Ok;
    return result;
}

void MetadataSync::StoreGame(const RaGameRow& row)
{
    Db->StoreGame(row);
    {
        std::lock_guard guard(Data);
        Games[row.GameId] = row;
    }
    Publish(false);
}

void MetadataSync::DeleteRaArt(int64_t gameId)
{
    std::error_code ec;
    fs::remove_all(MediaDir / "ra" / std::to_string(gameId), ec);
    std::lock_guard guard(Data);
    for (int kind = 0; kind < kArtKinds; kind++)
        RaFiles.erase({gameId, kind});
}

MetadataSync::Outcome MetadataSync::RetroAchievementsPhase()
{
    std::string key;
    {
        std::lock_guard guard(Lock);
        key = Key;
    }
    if (key.empty())
        return Outcome::Done;
    // the distinct games the library's ROMs are
    std::vector<int64_t> gameIds;
    {
        std::shared_ptr<const std::vector<LibraryEntry>> entries = Lib->Entries();
        std::lock_guard guard(Data);
        std::set<int64_t> ids;
        for (const LibraryEntry& e : *entries)
        {
            auto id = e.RaHash.empty() ? HashLib.end() : HashLib.find(Lower(e.RaHash));
            if (id != HashLib.end() && ids.insert(id->second).second)
                gameIds.push_back(id->second);
        }
    }
    if (gameIds.empty())
        return Outcome::Done;
    ArtTracker art;
    NotFoundHoldBack notFounds;

    // first the art an earlier run left missing, which needs no API calls
    DownloadMissingRaArt(gameIds, art);
    if (RaAborted())
        return Outcome::Aborted;

    // read after that, since that can clear image paths, which storing these rows again later would otherwise bring back
    std::map<int64_t, RaGameRow> rows;
    {
        std::lock_guard guard(Data);
        rows = Games;
    }
    // games with no row yet, then those whose last fetch failed, longest ago first; "not found" waits for a manual refresh
    std::vector<int64_t> toFetch;
    for (int64_t id : gameIds)
    {
        auto row = rows.find(id);
        if (row == rows.end() || row->second.FetchStatus == RaGameRow::kFailed)
            toFetch.push_back(id);
    }
    std::sort(toFetch.begin(), toFetch.end(), [&](int64_t a, int64_t b)
    {
        auto ra = rows.find(a), rb = rows.find(b);
        int64_t ta = ra == rows.end() ? -1 : ra->second.FetchedAt, tb = rb == rows.end() ? -1 : rb->second.FetchedAt;
        return ta != tb ? ta < tb : a < b;
    });
    printf("metadata: RetroAchievements: %zu games, %zu to fetch\n", gameIds.size(), toFetch.size());
    fflush(stdout);
    for (size_t at = 0; at < toFetch.size(); at += kGamesPerBatch)
    {
        std::vector<int64_t> batch(toFetch.begin() + at, toFetch.begin() + std::min(toFetch.size(), at + kGamesPerBatch));
        int stop = FetchBatch(batch, rows, notFounds);
        if (stop == 3 || RaAborted())
            return Outcome::Aborted;
        // even after a stop: being told to slow down by the API says nothing about the media host, and these rows are stored
        std::set<int64_t> held = notFounds.HeldIds();
        std::vector<int64_t> artFor;
        for (int64_t id : batch)
        {
            if (!held.count(id))
                artFor.push_back(id);
        }
        DownloadMissingRaArt(artFor, art);
        if (RaAborted())
            return Outcome::Aborted;
        if (stop == 1)
            return Outcome::Done;
        if (stop == 2)
            return Outcome::RetryLater;
    }
    // every game got its turn, so what's still held was never contradicted, and this few can really be missing
    for (const RaGameRow& row : notFounds.Release())
        StoreGame(row);
    if (art.ShouldRetry())
    {
        std::lock_guard guard(Lock);
        LastStop = "Too many art downloads failed in a row";
        return Outcome::RetryLater;
    }
    return Outcome::Done;
}

// API_GetGame for each game of [batch], as the app's fetchMetadata. Returns 0 when every game got its turn, 1 to stop for good (the key),
// 2 to stop and try again later, 3 when the sync was stopped
int MetadataSync::FetchBatch(const std::vector<int64_t>& batch, std::map<int64_t, RaGameRow>& rows, NotFoundHoldBack& notFounds)
{
    std::string key;
    {
        std::lock_guard guard(Lock);
        key = Key;
    }
    for (int64_t gameId : batch)
    {
        if (RaAborted())
            return 3;
        {
            std::lock_guard guard(Lock);
            Activity = "Fetching game details (game " + std::to_string(gameId) + ")";
        }
        auto prevIt = rows.find(gameId);
        const RaGameRow* previous = prevIt == rows.end() ? nullptr : &prevIt->second;
        GameResult result = GetGame(gameId, key, *Net);
        // the key was removed meanwhile: its answer isn't stored
        if (RaStop)
        {
            printf("metadata: the Web API key was removed: no more RetroAchievements calls this run\n");
            fflush(stdout);
            return 3;
        }
        int64_t now = NowMs();
        auto stopLater = [&](const char* why, bool markFailed)
        {
            if (markFailed)
            {
                // being marked puts it behind the others next time, so one game the server chokes on can't hold up the rest every run
                RaGameRow failed = WithStatus(previous, gameId, RaGameRow::kFailed, now);
                StoreGame(failed);
                rows[gameId] = failed;
            }
            printf("metadata: game %lld: %s; the API waits for a later sync\n", (long long)gameId, why);
            fflush(stdout);
            std::lock_guard guard(Lock);
            LastStop = why;
            return 2;
        };
        switch (result.Type)
        {
        case GameResult::Cancelled:
            return 3;
        case GameResult::Ok:
        {
            RaGameRow row = result.Row;
            row.FetchedAt = now;
            row.FetchStatus = RaGameRow::kOk;
            // a real answer shows the API is working, so the "not found" before it were real too
            for (const RaGameRow& held : notFounds.Release())
                StoreGame(held);
            if (IsArtOutdated(previous, row))
                DeleteRaArt(gameId);
            StoreGame(row);
            rows[gameId] = row;
            printf("metadata: game %lld: \"%s\"%s%s%s\n", (long long)gameId, row.Title.c_str(), row.ImageBoxArt.empty() ? "" : ", box art",
                row.ImageTitle.empty() ? "" : ", title screen", row.ImageIcon.empty() ? "" : ", icon");
            break;
        }
        case GameResult::NotFound:
        {
            RaGameRow row = WithStatus(previous, gameId, RaGameRow::kNotFound, now);
            printf("metadata: game %lld: not found\n", (long long)gameId);
            if (notFounds.Hold(row))
            {
                // they could really be missing, which only an answer about a game that exists can tell
                if (!Sleep(Cfg.ApiDelayMs))
                    return 3;
                GameResult control = GetGame(kControlGameId, key, *Net);
                if (control.Type == GameResult::Cancelled)
                    return 3;
                if (control.Type == GameResult::Ok)
                {
                    printf("metadata: the control call for game 1 answered: the games not found stand\n");
                    for (const RaGameRow& held : notFounds.Release())
                        StoreGame(held);
                }
                else
                {
                    for (const RaGameRow& held : notFounds.ReleaseAsFailed())
                    {
                        StoreGame(held);
                        rows[held.GameId] = held;
                    }
                    return stopLater("three games not found in a row, and the control call for game 1 didn't answer either", false);
                }
            }
            break;
        }
        case GameResult::InvalidKey:
        {
            printf("metadata: RetroAchievements rejected the Web API key (HTTP %d): no more API calls this run\n", result.Code);
            fflush(stdout);
            std::lock_guard guard(Lock);
            LastStop = "RetroAchievements rejected the Web API key";
            return 1;
        }
        case GameResult::RateLimited:
            return stopLater("RetroAchievements asked to slow down (HTTP 429)", false);
        case GameResult::Network:
            return stopLater("no answer from RetroAchievements", true);
        case GameResult::Unexpected:
            return stopLater("an answer that isn't game data", true);
        case GameResult::HttpError:
        {
            // only a 4xx is about this request in particular (an unknown game is a 200 with nothing in it, not a 404); anything else,
            // such as a 503, would hit every game the same way
            if (result.Code >= 400 && result.Code <= 499 && result.Code != 404)
            {
                RaGameRow failed = WithStatus(previous, gameId, RaGameRow::kFailed, now);
                StoreGame(failed);
                rows[gameId] = failed;
                printf("metadata: game %lld: HTTP %d, marked failed\n", (long long)gameId, result.Code);
                break;
            }
            char why[64];
            snprintf(why, sizeof(why), "RetroAchievements answered HTTP %d", result.Code);
            return stopLater(why, true);
        }
        case GameResult::Malformed:
        {
            RaGameRow failed = WithStatus(previous, gameId, RaGameRow::kFailed, now);
            StoreGame(failed);
            rows[gameId] = failed;
            printf("metadata: game %lld: a malformed answer, marked failed\n", (long long)gameId);
            break;
        }
        }
        fflush(stdout);
        if (!Sleep(Cfg.ApiDelayMs))
            return 3;
    }
    return 0;
}

// what a finished download attempt says (the app's decideArtOutcome); [status]: the HTTP status, or a negative Http error
static ArtOutcome DecideArtOutcome(int status, bool saved, bool notAnImage)
{
    if (status == 200)
        return saved ? ArtOutcome::Saved : notAnImage ? ArtOutcome::NotAnImage : ArtOutcome::Inconclusive;
    switch (status)
    {
    case -100: return ArtOutcome::Inconclusive;     // cancelled
    case -101: return ArtOutcome::ImageUnavailable; // too large, or not a plain image path
    case -102: return ArtOutcome::HostProblem;      // no connection, timed out, or a page instead of an image
    case 404: case 410: return ArtOutcome::ImageUnavailable;
    case 429: return ArtOutcome::RateLimited;
    // a 403 can be a block, and a redirect is never followed, so it would otherwise just fail quietly on every run
    default: return ArtOutcome::HostProblem;
    }
}

int MetadataSync::DownloadRaImage(const std::string& imagePath, std::vector<uint8_t>& bytes)
{
    std::string name = ImageName(imagePath);
    if (name.empty())
        return -101;
    Http::Request req;
    req.Url = Cfg.MediaBase + "/Images/" + Http::UrlEncode(name);
    req.FollowRedirects = false;
    Count("image");
    Http::Response r = Net->Get(req);
    if (r.Err == Http::Response::Error::Cancelled)
        return -100;
    if (r.Err == Http::Response::Error::TooLarge || r.Err == Http::Response::Error::BadUrl)
        return -101;
    if (r.Err != Http::Response::Error::None)
        return -102;
    if (r.Status != 200)
        return r.Status;
    // a page instead of an image says something about the host, as a block or a challenge page would, and nothing about the image
    if (Lower(r.ContentType).rfind("text", 0) == 0)
        return -102;
    bytes = std::move(r.Body);
    return 200;
}

MetadataSync::SaveResult MetadataSync::SaveArt(const fs::path& file, const std::vector<uint8_t>& bytes, int clearCount)
{
    // (a WebP this Windows can't decode says nothing about the image: not a failure, and its path stays for a Windows that can)
    ImageCheck check = CheckImage(bytes.data(), bytes.size());
    if (check == ImageCheck::Unsupported)
    {
        printf("metadata: %s is WebP, which this Windows can't decode; skipped, not counted as a failure\n", PathToUtf8(file.filename()).c_str());
        fflush(stdout);
        return SaveResult::Unsupported;
    }
    if (check != ImageCheck::Ok)
        return SaveResult::NotAnImage;
    std::lock_guard guard(SaveLock);
    // cleared while it downloaded: the art is meant to be gone
    if (ClearCount != clearCount)
        return SaveResult::Discarded;
    return WriteFileAtomically(file, bytes.data(), bytes.size()) ? SaveResult::Saved : SaveResult::Failed;
}

void MetadataSync::DownloadMissingRaArt(const std::vector<int64_t>& gameIds, ArtTracker& tracker)
{
    if (tracker.Stopped || RaAborted() || gameIds.empty())
        return;
    std::set<int64_t> missingArt, missingTitle;
    std::map<int64_t, RaGameRow> rows;
    {
        std::lock_guard guard(Data);
        for (int64_t id : gameIds)
        {
            auto row = Games.find(id);
            if (row == Games.end())
                continue;
            int kind = ChooseArt(row->second);
            if (kind >= 0 && !RaFiles.count({id, kind}))
                missingArt.insert(id);
            if (!row->second.ImageTitle.empty() && !RaFiles.count({id, kArtTitle}))
                missingTitle.insert(id);
            if (missingArt.count(id) || missingTitle.count(id))
                rows[id] = row->second;
        }
    }
    std::vector<int64_t> jobs;
    for (const auto& [id, row] : rows)
        jobs.push_back(id);
    if (jobs.empty())
        return;
    // shuffled, so images that always fail (all that stays on the list from one run to the next) don't line up and stop every run
    std::shuffle(jobs.begin(), jobs.end(), std::mt19937(std::random_device()()));
    SetActivity("Downloading art for " + std::to_string(jobs.size()) + (jobs.size() == 1 ? " game" : " games"));
    TwoAtATime(jobs, [&](int64_t id)
    {
        if (RaAborted())
            return;
        // one at a time for a game, since each works from the row the last one left
        RaGameRow row = rows.at(id);
        if (missingArt.count(id) && !tracker.Stopped)
            row = DownloadRaArt(row, tracker);
        if (missingTitle.count(id) && !tracker.Stopped && !RaAborted())
            DownloadRaTitle(row, tracker);
    });
    if (tracker.Stopped)
    {
        printf("metadata: RetroAchievements art stopped for this run (failures in a row, or asked to slow down)\n");
        fflush(stdout);
    }
}

RaGameRow MetadataSync::DownloadRaArt(RaGameRow row, ArtTracker& tracker)
{
    for (;;)
    {
        int kind = ChooseArt(row);
        if (kind < 0 || RaAborted())
            return row;
        {
            std::lock_guard guard(Data);
            if (RaFiles.count({row.GameId, kind}))
                return row;
        }
        int clearCount = ClearCount;
        std::vector<uint8_t> bytes;
        int status = DownloadRaImage(ImagePath(row, kind), bytes);
        SaveResult saved = status == 200 ? SaveArt(MediaDir / "ra" / std::to_string(row.GameId) / (std::string(ArtKindName(kind)) + ".png"), bytes,
            clearCount) : SaveResult::Failed;
        if (saved == SaveResult::Saved)
        {
            std::lock_guard guard(Data);
            RaFiles[{row.GameId, kind}] = NewStamp();
        }
        ArtAction action = tracker.ActionFor(DecideArtOutcome(status, saved == SaveResult::Saved, saved == SaveResult::NotAnImage));
        printf("metadata: game %lld %s art: %s\n", (long long)row.GameId, ArtKindName(kind), saved == SaveResult::Saved ? "saved"
            : action == ArtAction::TryNextKind ? "unavailable, the next kind" : action == ArtAction::StopDownloads ? "failed, stopping" : "failed");
        fflush(stdout);
        if (saved == SaveResult::Saved)
            Publish(false);
        if (action != ArtAction::TryNextKind)
            return row;
        row = WithoutImage(row, kind);
        StoreGame(row);
    }
}

// the title screen, which the game may have as its art already; if it fails for good only its own path goes
void MetadataSync::DownloadRaTitle(const RaGameRow& row, ArtTracker& tracker)
{
    if (row.ImageTitle.empty())
        return;
    {
        std::lock_guard guard(Data);
        if (RaFiles.count({row.GameId, kArtTitle}))
            return;
    }
    int clearCount = ClearCount;
    std::vector<uint8_t> bytes;
    int status = DownloadRaImage(row.ImageTitle, bytes);
    SaveResult saved = status == 200 ? SaveArt(MediaDir / "ra" / std::to_string(row.GameId) / "title.png", bytes, clearCount) : SaveResult::Failed;
    if (saved == SaveResult::Saved)
    {
        std::lock_guard guard(Data);
        RaFiles[{row.GameId, kArtTitle}] = NewStamp();
    }
    ArtAction action = tracker.ActionFor(DecideArtOutcome(status, saved == SaveResult::Saved, saved == SaveResult::NotAnImage));
    printf("metadata: game %lld title screen: %s\n", (long long)row.GameId, saved == SaveResult::Saved ? "saved" : "failed");
    fflush(stdout);
    if (saved == SaveResult::Saved)
        Publish(false);
    if (action == ArtAction::TryNextKind)
    {
        // the row as it is now (the box art's own may have gone meanwhile)
        RaGameRow now;
        {
            std::lock_guard guard(Data);
            auto it = Games.find(row.GameId);
            now = it != Games.end() ? it->second : row;
        }
        StoreGame(WithoutImage(now, kArtTitle));
    }
}

// The libretro DAT (clrmamepro format) into DatGames: each game's name, region, serial and its ROM's CRC32
bool MetadataSync::LoadDat()
{
    std::ifstream in(DatFile, std::ios::binary);
    if (!in)
        return false;
    std::vector<DatGame> games;
    std::unordered_map<std::string, size_t> byCrc;
    std::unordered_map<std::string, std::vector<size_t>> bySerial;
    std::string line;
    bool inGame = false;
    DatGame game;
    std::string crc;
    auto quoted = [](const std::string& text, size_t at)
    {
        size_t a = text.find('"', at);
        size_t b = a == std::string::npos ? a : text.find('"', a + 1);
        return b == std::string::npos ? std::string() : text.substr(a + 1, b - a - 1);
    };
    while (std::getline(in, line))
    {
        std::string t = Trim(line);
        if (t.rfind("game (", 0) == 0 || t == "game(")
        {
            inGame = true;
            game = {};
            crc.clear();
        }
        else if (!inGame)
            continue;
        else if (t == ")")
        {
            if (!game.Name.empty())
            {
                size_t index = games.size();
                games.push_back(game);
                if (!crc.empty())
                    byCrc.emplace(Lower(crc), index);
                if (game.Serial.size() == 4)
                    bySerial[game.Serial].push_back(index);
            }
            inGame = false;
        }
        else if (t.rfind("name \"", 0) == 0)
            game.Name = quoted(t, 4);
        else if (t.rfind("region \"", 0) == 0)
            game.Region = quoted(t, 6);
        else if (t.rfind("serial \"", 0) == 0)
            game.Serial = quoted(t, 6);
        else if (t.rfind("rom (", 0) == 0 || t.rfind("rom(", 0) == 0)
        {
            size_t at = t.find(" crc ");
            if (at != std::string::npos && crc.empty())
                crc = t.substr(at + 5, 8);
            size_t serial = t.find(" serial \"");
            if (serial != std::string::npos && game.Serial.empty())
                game.Serial = quoted(t, serial + 7);
        }
    }
    if (games.size() < 100)
        return false;
    std::lock_guard guard(Data);
    DatGames = std::move(games);
    DatByCrc = std::move(byCrc);
    DatBySerial = std::move(bySerial);
    return true;
}

bool MetadataSync::DownloadDat()
{
    SetActivity("Downloading the libretro DAT");
    Count("dat");
    Http::Request req;
    req.Url = Cfg.DatUrl;
    req.MaxBytes = 64u << 20;
    req.TimeoutMs = 60000;
    req.TotalTimeoutMs = 600000;
    Http::Response r = Net->Get(req);
    std::string text = r.Ok() ? r.BodyText() : std::string();
    // the DAT at all: clrmamepro's header first
    bool ok = r.Ok() && Trim(text.substr(0, 64)).rfind("clrmamepro", 0) == 0;
    if (!ok)
    {
        printf("metadata: the libretro DAT didn't come (%s%s)\n", Http::Describe(r).c_str(), r.Ok() ? ", not a DAT" : "");
        fflush(stdout);
        return false;
    }
    fs::path keep = DatFile;
    keep += ".old";
    std::error_code ec;
    if (fs::exists(DatFile, ec))
        fs::rename(DatFile, keep, ec);
    if (!WriteFileAtomically(DatFile, text.data(), text.size()) || !LoadDat())
    {
        // a DAT this can't read: the old one back
        fs::remove(DatFile, ec);
        if (fs::exists(keep, ec))
            fs::rename(keep, DatFile, ec);
        LoadDat();
        printf("metadata: the libretro DAT that came can't be read; %s\n", DatGames.empty() ? "no DAT" : "the old one stays");
        fflush(stdout);
        return false;
    }
    fs::remove(keep, ec);
    Db->SetState("libretro_dat", std::to_string(NowMs()));
    Db->SetState("libretro_dat_due", "");
    printf("metadata: libretro DAT: %zu games (%zu bytes)\n", DatGames.size(), text.size());
    fflush(stdout);
    return true;
}

// the No-Intro name for [e] (Data held): its CRC32's entry, else its serial's, the one for its region first and the retail release over a
// demo or a beta (a trimmed ROM's CRC32 is never the full-size dump's)
std::string MetadataSync::LibretroNameFor(const LibraryEntry& e) const
{
    auto h = Hashes.find(e.Id);
    if (h != Hashes.end() && h->second.FileSize == e.FileSize && h->second.FileTime == e.FileTime)
    {
        auto crc = DatByCrc.find(h->second.Crc32);
        if (crc != DatByCrc.end())
            return DatGames[crc->second].Name;
    }
    auto serial = DatBySerial.find(e.GameCode);
    if (serial == DatBySerial.end())
        return "";
    std::string region = RegionWord(e.GameCode);
    int best = -1, bestScore = -1;
    for (size_t index : serial->second)
    {
        const DatGame& g = DatGames[index];
        int score = (IsOddRelease(g.Name) ? 0 : 2) + (!region.empty() && (g.Region.find(region) != std::string::npos
            || g.Name.find(region) != std::string::npos) ? 1 : 0);
        if (score > bestScore)
        {
            best = (int)index;
            bestScore = score;
        }
    }
    return best >= 0 ? DatGames[best].Name : "";
}

MetadataSync::Outcome MetadataSync::LibretroPhase()
{
    {
        std::lock_guard guard(Lock);
        if (!LibretroOn)
            return Outcome::Done;
    }
    // the games RetroAchievements has no box art or title screen for (by their row), which libretro may have
    std::shared_ptr<const std::vector<LibraryEntry>> entries = Lib->Entries();
    auto needs = [&](const LibraryEntry& e, bool& box, bool& title)
    {
        box = title = true;
        auto id = e.RaHash.empty() ? HashLib.end() : HashLib.find(Lower(e.RaHash));
        if (id == HashLib.end())
            return;
        auto row = Games.find(id->second);
        if (row == Games.end())
            return;
        box = row->second.ImageBoxArt.empty() && row->second.ImageTitle.empty();
        title = row->second.ImageTitle.empty();
    };
    bool any = false;
    {
        std::lock_guard guard(Data);
        for (const LibraryEntry& e : *entries)
        {
            bool box, title;
            needs(e, box, title);
            any = any || (e.State == LibraryEntry::kRead && (box || title));
        }
    }
    if (!any)
        return Outcome::Done;
    bool haveDat;
    {
        std::lock_guard guard(Data);
        haveDat = !DatGames.empty();
    }
    bool due = Db->GetState("libretro_dat_due") == "1";
    if (!haveDat || due)
        DownloadDat();
    if (LibretroAborted())
        return Outcome::Aborted;
    std::map<std::string, int64_t> missing;
    Db->LoadLibretroMissing(missing);

    // what to download: (name, kind) for each game's name, once
    struct Job { std::string Name; int Kind; };
    std::vector<Job> jobs;
    {
        std::lock_guard guard(Data);
        if (DatGames.empty())
            return Outcome::Done;
        std::set<std::pair<std::string, int>> queued;
        for (const LibraryEntry& e : *entries)
        {
            if (e.State != LibraryEntry::kRead)
                continue;
            bool box, title;
            needs(e, box, title);
            std::string name = LibretroNameFor(e);
            if (name.empty())
                continue;
            for (int kind = 0; kind < 2; kind++)
            {
                if (!(kind == 0 ? box : title) || missing.count(name + "|" + kLibretroKinds[kind])
                    || LibretroFolderName(name).empty() || LibretroFiles.count({LibretroFolderName(name), kind}) || !queued.insert({name, kind}).second)
                    continue;
                jobs.push_back({name, kind});
            }
        }
    }
    Publish(true);
    if (jobs.empty())
        return Outcome::Done;
    std::shuffle(jobs.begin(), jobs.end(), std::mt19937(std::random_device()()));
    printf("metadata: libretro: %zu images to ask for\n", jobs.size());
    fflush(stdout);
    SetActivity("Downloading libretro art (" + std::to_string(jobs.size()) + (jobs.size() == 1 ? " image)" : " images)"));

    // The app's rules, with one change: libretro has no title screen or box art for many games, so a missing image (404) doesn't count
    // toward stopping. Instead they're held until an image of this run is saved, which shows the host is answering for real; a run with
    // none saved stores none of them, and ten in a row with none saved stops it, so a blanket 404 can't mark the whole library
    std::mutex m;
    int failures = 0, missingInARow = 0;
    bool savedAny = false, stopped = false, retry = false;
    std::vector<std::pair<std::string, int>> held;
    int64_t now = NowMs();
    TwoAtATime(jobs, [&](const Job& job)
    {
        {
            std::lock_guard g(m);
            if (stopped || LibretroAborted())
                return;
        }
        std::string file = LibretroFileName(job.Name), folder = LibretroFolderName(job.Name);
        // (on disk only under media\libretro, whatever the DAT's name is)
        fs::path dir = (MediaDir / "libretro" / PathFromUtf8(folder)).lexically_normal();
        if (folder.empty() || dir.parent_path() != (MediaDir / "libretro").lexically_normal())
        {
            fprintf(stderr, "metadata: no folder for the libretro name \"%s\"; skipped\n", job.Name.c_str());
            return;
        }
        Http::Request req;
        req.Url = Cfg.ThumbnailsBase + "/" + kLibretroFolders[job.Kind] + "/" + Http::UrlEncode(file) + ".png";
        int clearCount = ClearCount;
        Count("libretro");
        Http::Response r = Net->Get(req);
        int status = r.Err == Http::Response::Error::Cancelled ? -100 : r.Err == Http::Response::Error::TooLarge ? -101
            : r.Err != Http::Response::Error::None ? -102 : r.Status != 200 ? r.Status
            : Lower(r.ContentType).rfind("text", 0) == 0 ? -102 : 200;
        SaveResult saved = status == 200 ? SaveArt(dir / (std::string(kLibretroKinds[job.Kind]) + ".png"), r.Body, clearCount)
            : SaveResult::Failed;
        ArtOutcome outcome = DecideArtOutcome(status, saved == SaveResult::Saved, saved == SaveResult::NotAnImage);
        std::lock_guard g(m);
        if (saved == SaveResult::Saved)
        {
            {
                std::lock_guard guard(Data);
                LibretroFiles[{folder, job.Kind}] = NewStamp();
            }
            savedAny = true;
            failures = missingInARow = 0;
            for (const auto& [name, kind] : held)
                Db->StoreLibretroMissing(name, kLibretroKinds[kind], now);
            held.clear();
        }
        else if (status == 404 || status == 410)
        {
            missingInARow++;
            if (savedAny)
                Db->StoreLibretroMissing(job.Name, kLibretroKinds[job.Kind], now);
            else
                held.emplace_back(job.Name, job.Kind);
            if (!savedAny && missingInARow >= kMaxLibretroMissingInARow)
                stopped = true;
        }
        else if (outcome == ArtOutcome::RateLimited)
            stopped = retry = true;
        else if (outcome == ArtOutcome::HostProblem || outcome == ArtOutcome::NotAnImage || outcome == ArtOutcome::ImageUnavailable)
        {
            failures++;
            if (failures >= kMaxConsecutiveArtFailures)
                stopped = retry = true;
        }
        printf("metadata: libretro %s of \"%s\": %s\n", kLibretroKinds[job.Kind], job.Name.c_str(), saved == SaveResult::Saved ? "saved"
            : status == 404 || status == 410 ? "none there" : (status > 0 ? "HTTP " + std::to_string(status) : std::string("failed")).c_str());
        fflush(stdout);
        if (saved == SaveResult::Saved)
            Publish(false);
    });
    if (stopped)
    {
        printf("metadata: libretro art stopped for this run (%s)\n", retry ? "failures in a row, or asked to slow down" : "nothing found");
        fflush(stdout);
        std::lock_guard guard(Lock);
        LastStop = "libretro art downloads failed";
    }
    if (!held.empty())
    {
        printf("metadata: %zu libretro images not found, not remembered (no image came this run)\n", held.size());
        fflush(stdout);
    }
    return retry && savedAny ? Outcome::RetryLater : Outcome::Done;
}

void MetadataSync::Publish(bool now)
{
    {
        std::lock_guard guard(Lock);
        auto t = std::chrono::steady_clock::now();
        if (!now && t - LastPublish < std::chrono::seconds(1))
        {
            PublishPending = true;
            return;
        }
        LastPublish = t;
        PublishPending = false;
    }
    // (built and handed over together, so a publish built before a clear can't land after the clear's)
    std::lock_guard order(PublishLock);
    std::shared_ptr<const std::vector<LibraryEntry>> entries = Lib->Entries();
    auto index = std::make_shared<MetadataIndex>();
    bool libretro;
    {
        std::lock_guard guard(Lock);
        libretro = LibretroOn;
    }
    {
        std::lock_guard guard(Data);
        index->HasHashLibrary = HasHashLib;
        index->Hashes.insert(Hashes.begin(), Hashes.end());
        for (const LibraryEntry& e : *entries)
        {
            if (e.State != LibraryEntry::kRead)
                continue;
            if (!e.RaHash.empty())
            {
                auto id = HashLib.find(Lower(e.RaHash));
                if (id != HashLib.end())
                {
                    index->GameIds[Lower(e.RaHash)] = id->second;
                    auto game = Games.find(id->second);
                    if (game != Games.end() && !index->Games.count(id->second))
                        index->Games[id->second] = std::make_shared<RaGameRow>(game->second);
                    std::array<ArtRef, kArtKinds> files;
                    for (int kind = 0; kind < kArtKinds; kind++)
                    {
                        auto file = RaFiles.find({id->second, kind});
                        if (file != RaFiles.end())
                            files[kind] = {PathToUtf8(MediaDir / "ra" / std::to_string(id->second) / (std::string(ArtKindName(kind)) + ".png")),
                                file->second};
                    }
                    index->RaArt[id->second] = files;
                }
            }
            if (!DatGames.empty())
            {
                std::string name = LibretroNameFor(e);
                if (!name.empty())
                {
                    index->LibretroNames[e.Id] = name;
                    std::string folder = LibretroFolderName(name);
                    std::array<ArtRef, 2> files;
                    for (int kind = 0; kind < 2 && !folder.empty(); kind++)
                    {
                        auto file = LibretroFiles.find({folder, kind});
                        if (file != LibretroFiles.end())
                            files[kind] = {PathToUtf8(MediaDir / "libretro" / PathFromUtf8(folder) / (std::string(kLibretroKinds[kind]) + ".png")),
                                file->second};
                    }
                    index->LibretroArt[name] = files;
                }
            }
        }
    }
    Lib->SetMetadata(index, libretro);
}

}
