#include "Library.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "RomFile.h"
#include "Settings.h"

namespace fs = std::filesystem;

namespace Frontend
{

namespace
{

std::string Lower(std::string text)
{
    for (char& c : text)
        c = (char)tolower((unsigned char)c);
    return text;
}

int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// a folder as the start of the paths under it, lowercased, with one backslash at the end (a drive's root "E:\" has it already)
std::string FolderPrefix(const std::string& folder)
{
    std::string prefix = Lower(folder);
    if (prefix.empty() || prefix.back() != '\\')
        prefix += '\\';
    return prefix;
}

bool Under(const std::string& lowerPath, const std::string& prefix)
{
    return lowerPath.size() > prefix.size() && lowerPath.compare(0, prefix.size(), prefix) == 0;
}

// a file the walk found
struct FoundFile
{
    std::string Path;
    uint64_t Size = 0;
    int64_t Time = 0;
    bool Online = false; // online-only (OneDrive and the like): reading it would download it
};

// FILE_ATTRIBUTE_RECALL_ON_OPEN and FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS, which older MinGW headers lack: a cloud file not on the disk
constexpr DWORD kRecallOnOpen = 0x00040000, kRecallOnDataAccess = 0x00400000;

std::wstring Join(const std::wstring& dir, const std::wstring& name)
{
    return !dir.empty() && dir.back() == L'\\' ? dir + name : dir + L"\\" + name;
}

// every .nds, .dsi, .zip and .7z under [dir], through FindFirstFileEx (which gives the size, time and attributes without opening the
// files); folders that are links or junctions aren't followed, so a loop can't trap it. A name UTF-8 can't hold (a lone surrogate, which
// NTFS allows) is skipped, since its path couldn't be opened again from the database
void Walk(const std::wstring& dir, std::vector<FoundFile>& out, const std::atomic<bool>& stop, int depth)
{
    if (depth > 32 || stop)
        return;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(Join(dir, L"*").c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE)
        return;
    std::vector<std::wstring> subdirs;
    do
    {
        std::wstring name = data.cFileName;
        if (name == L"." || name == L"..")
            continue;
        std::wstring full = Join(dir, name);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                subdirs.push_back(full);
            continue;
        }
        fs::path path(full);
        if (!IsRomFileName(path) && !IsArchiveName(path))
            continue;
        FoundFile f;
        f.Path = PathToUtf8(path);
        if (PathFromUtf8(f.Path).native() != full)
        {
            fprintf(stderr, "library: skipped %s: its name can't be written in UTF-8\n", f.Path.c_str());
            continue;
        }
        f.Size = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        f.Time = (int64_t)(((uint64_t)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime);
        f.Online = (data.dwFileAttributes & (FILE_ATTRIBUTE_OFFLINE | kRecallOnOpen | kRecallOnDataAccess)) != 0;
        out.push_back(std::move(f));
    } while (!stop && FindNextFileW(find, &data));
    FindClose(find);
    std::sort(subdirs.begin(), subdirs.end());
    for (const std::wstring& sub : subdirs)
        Walk(sub, out, stop, depth + 1);
}

// the icon's average color, for the card it sits on
uint32_t IconTint(const std::vector<uint8_t>& icon)
{
    uint64_t r = 0, g = 0, b = 0, n = 0;
    for (size_t i = 0; i + 3 < icon.size(); i += 4)
    {
        if (icon[i + 3] == 0)
            continue;
        r += icon[i];
        g += icon[i + 1];
        b += icon[i + 2];
        n++;
    }
    if (!n)
        return 0;
    return (uint32_t)(r / n) | ((uint32_t)(g / n) << 8) | ((uint32_t)(b / n) << 16) | 0xFF000000u;
}

void Derive(LibraryEntry& e)
{
    e.FileName = PathToUtf8(PathFromUtf8(e.Path).filename());
    e.Tint = e.Icon ? IconTint(*e.Icon) : 0;
}

// FNV-1a, for the unpacked files' names
uint64_t Fnv(const std::string& text)
{
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : text)
    {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

// a ROM's file in the cache: kept for the last few games, the oldest going first
constexpr int kCachedRoms = 3;

}

std::string LibraryEntry::Name() const
{
    if (!Config.CustomName.empty())
        return Config.CustomName;
    if (!Title.empty())
        return Title;
    if (RaGame && !RaGame->Title.empty())
        return RaGame->Title;
    return PathToUtf8(PathFromUtf8(FileName).stem());
}

bool LibraryEntry::IsArchive() const
{
    return IsArchiveName(PathFromUtf8(Path));
}

std::string Library::NormalizePath(const std::string& path)
{
    std::error_code ec;
    fs::path p = PathFromUtf8(path);
    fs::path absolute = fs::absolute(p, ec);
    if (!ec)
        p = absolute;
    p = p.lexically_normal().make_preferred();
    std::string text = PathToUtf8(p);
    // no backslash at the end, but for a drive's root ("E:\", which without it would mean the drive's current folder)
    while (text.size() > 3 && (text.back() == '\\' || text.back() == '/'))
        text.pop_back();
    return text;
}

bool Library::SameFolder(const std::string& a, const std::string& b)
{
    return FolderPrefix(NormalizePath(a)) == FolderPrefix(NormalizePath(b));
}

void Library::Open(const fs::path& profileDir, int bannerLanguage)
{
    Language = bannerLanguage;
    Profile = profileDir;
    CacheDir = (profileDir / "rom-cache").make_preferred();
    if (!Db.Open(profileDir / "library.db"))
        fprintf(stderr, "library: the library is kept in memory for this run\n");
    Db.LoadRoms(Rows);
    for (LibraryEntry& e : Rows)
        Derive(e);
    {
        std::lock_guard guard(Lock);
        Db.LoadPlays(Plays);
        std::map<std::string, std::map<std::string, std::string>> configs;
        Db.LoadConfigs(configs);
        for (const auto& [key, fields] : configs)
        {
            RomConfig config = RomConfig::FromFields(fields);
            if (!config.IsDefault())
                Configs[key] = config;
        }
    }
    printf("library: %zu files in library.db (schema version %d), %zu with settings of their own, banner language %d\n", Rows.size(),
        Db.SchemaVersion(), Configs.size(), Language);
    fflush(stdout);
    Publish();
    Worker = std::thread([this] { Run(); });
}

void Library::Close()
{
    {
        std::lock_guard guard(Lock);
        Stopping = true;
    }
    Wake.notify_all();
    if (Worker.joinable())
        Worker.join();
    Db.Close();
}

void Library::SetFolders(const std::vector<std::string>& folders, bool forRun)
{
    std::lock_guard guard(Lock);
    FolderList.clear();
    for (const std::string& folder : folders)
    {
        if (folder.empty())
            continue;
        std::string norm = NormalizePath(folder);
        bool known = std::any_of(FolderList.begin(), FolderList.end(), [&](const std::string& f) { return FolderPrefix(f) == FolderPrefix(norm); });
        if (!known)
            FolderList.push_back(norm);
    }
    FoldersForRun = forRun;
}

std::vector<std::string> Library::Folders() const
{
    std::lock_guard guard(Lock);
    return FolderList;
}

void Library::Rescan()
{
    {
        std::lock_guard guard(Lock);
        ScanQueued = true;
        FirstScanAsked = true;
    }
    Wake.notify_all();
}

void Library::EnsureScanned()
{
    bool ask;
    {
        std::lock_guard guard(Lock);
        ask = !FirstScanAsked;
    }
    if (ask)
        Rescan();
}

Library::Progress Library::GetProgress() const
{
    std::lock_guard guard(Lock);
    Progress p = State;
    p.Scanning = ScanQueued || ScanRunning;
    p.Preparing = State.Preparing || !Prepares.empty();
    return p;
}

std::shared_ptr<const std::vector<LibraryEntry>> Library::Entries() const
{
    std::lock_guard guard(Lock);
    return Snapshot;
}

bool Library::Busy() const
{
    std::lock_guard guard(Lock);
    return ScanQueued || ScanRunning || State.Preparing || !Prepares.empty();
}

void Library::MarkPlayed(const std::string& path)
{
    std::string norm = NormalizePath(path);
    int64_t now = NowMs();
    {
        std::lock_guard guard(Lock);
        Plays[Lower(norm)] = now;
        auto copy = std::make_shared<std::vector<LibraryEntry>>(*Snapshot);
        for (LibraryEntry& e : *copy)
        {
            if (_stricmp(e.Path.c_str(), norm.c_str()) == 0)
                e.LastPlayed = now;
        }
        Snapshot = copy;
    }
    Gen++;
    Db.SetPlayed(norm, now);
    printf("library: played %s\n", norm.c_str());
    fflush(stdout);
}

std::string Library::ConfigKey(const std::string& raHash, const std::string& path)
{
    // the hash's 32 hex digits, or a path (which has a backslash, so the two can't meet)
    return raHash.empty() ? Lower(NormalizePath(path)) : Lower(raHash);
}

RomConfig Library::GetConfig(const std::string& key) const
{
    std::lock_guard guard(Lock);
    auto it = Configs.find(Lower(key));
    return it != Configs.end() ? it->second : RomConfig();
}

bool Library::SetConfig(const std::string& key, const RomConfig& config)
{
    std::string k = Lower(key);
    {
        std::lock_guard guard(Lock);
        if (config.IsDefault())
            Configs.erase(k);
        else
            Configs[k] = config;
        auto copy = std::make_shared<std::vector<LibraryEntry>>(*Snapshot);
        for (LibraryEntry& e : *copy)
        {
            if (ConfigKey(e) == k)
                e.Config = config;
        }
        Snapshot = copy;
    }
    Gen++;
    bool ok = Db.StoreConfig(k, config.ToFields(), RomConfig::Keys());
    printf("library: settings of %s: %s\n", k.c_str(), config.Describe().c_str());
    fflush(stdout);
    return ok;
}

void Library::SetMetadata(std::shared_ptr<const MetadataIndex> index, bool libretro)
{
    {
        std::lock_guard guard(Lock);
        Meta = std::move(index);
        MetaLibretro = libretro;
    }
    Publish();
}

Library::Identity Library::IdentityFor(const std::string& path, const std::string& romFile)
{
    std::string norm = NormalizePath(path);
    {
        std::lock_guard guard(Lock);
        for (const LibraryEntry& e : *Snapshot)
        {
            if (e.State == LibraryEntry::kRead && _stricmp(e.Path.c_str(), norm.c_str()) == 0)
                return {e.RaHash, e.DsiWare, true};
        }
    }
    Identity id;
    std::unique_ptr<RomData> data = OpenRomFile(PathFromUtf8(romFile));
    RomInfo info;
    std::lock_guard guard(HashLock);
    if (data && ReadRomInfo(*data, Language, info))
        id = {info.RaHash, info.DsiWare, true};
    return id;
}

void Library::Publish()
{
    auto list = std::make_shared<std::vector<LibraryEntry>>();
    list->reserve(Rows.size());
    std::lock_guard guard(Lock);
    // the rows of the folders in use: a saved folder the command line's --rom-folder left out keeps its rows, hidden
    std::vector<std::string> prefixes;
    for (const std::string& folder : FolderList)
        prefixes.push_back(FolderPrefix(folder));
    for (const LibraryEntry& row : Rows)
    {
        if (row.State == LibraryEntry::kNotRom)
            continue;
        std::string lower = Lower(row.Path);
        if (!std::any_of(prefixes.begin(), prefixes.end(), [&](const std::string& p) { return Under(lower, p); }))
            continue;
        list->push_back(row);
        auto played = Plays.find(lower);
        list->back().LastPlayed = played != Plays.end() ? played->second : 0;
        auto config = Configs.find(ConfigKey(row));
        list->back().Config = config != Configs.end() ? config->second : RomConfig();
        if (Meta)
            ApplyMetadata(*Meta, MetaLibretro, list->back());
        list->back().HasTitleArt = PretendTitleArt || !list->back().TitleArtFile.empty();
    }
    Snapshot = list;
    Gen++;
}

void Library::Run()
{
    std::unique_lock lock(Lock);
    for (;;)
    {
        Wake.wait(lock, [&] { return Stopping || ScanQueued || !Prepares.empty(); });
        if (Stopping)
            return;
        if (!Prepares.empty())
        {
            lock.unlock();
            RunPrepares();
            lock.lock();
            continue;
        }
        ScanQueued = false;
        ScanRunning = true;
        lock.unlock();
        try
        {
            Scan();
        }
        catch (const std::exception& e)
        {
            fprintf(stderr, "library: the scan stopped: %s\n", e.what());
        }
        lock.lock();
        ScanRunning = false;
        State.Scanned = true;
        State.Current.clear();
    }
}

void Library::RunPrepares()
{
    for (;;)
    {
        PrepareJob job;
        {
            std::lock_guard guard(Lock);
            if (Prepares.empty() || Stopping)
                return;
            job = std::move(Prepares.front());
            Prepares.pop_front();
            State.Preparing = true;
            State.PreparingName = PathToUtf8(PathFromUtf8(job.Path).filename());
        }
        std::string error;
        std::string file;
        try
        {
            file = PrepareFile(job.Path, error);
        }
        catch (const std::exception& e)
        {
            file.clear();
            error = e.what();
        }
        {
            std::lock_guard guard(Lock);
            State.Preparing = false;
            State.PreparingName.clear();
        }
        if (PostToUi)
            PostToUi([done = std::move(job.Done), file, error] { done(file, error); });
    }
}

void Library::Prepare(const std::string& path, std::function<void(const std::string&, const std::string&)> done)
{
    if (!IsArchiveName(PathFromUtf8(path)))
    {
        done(path, "");
        return;
    }
    {
        std::lock_guard guard(Lock);
        Prepares.push_back({path, std::move(done)});
    }
    Wake.notify_all();
}

std::string Library::PrepareNow(const std::string& path, std::string& error)
{
    if (!IsArchiveName(PathFromUtf8(path)))
        return path;
    try
    {
        return PrepareFile(path, error);
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return "";
    }
}

std::string Library::PrepareFile(const std::string& path, std::string& error)
{
    fs::path archive = PathFromUtf8(path);
    ArchiveEntry entry;
    if (!FindRomInArchive(archive, entry, error))
        return "";
    std::error_code ec;
    uint64_t size = fs::file_size(archive, ec);
    auto time = fs::last_write_time(archive, ec);
    // one name per archive, entry and version of the file, so a changed archive is unpacked again
    char key[32];
    snprintf(key, sizeof(key), "%016llx", (unsigned long long)Fnv(Lower(NormalizePath(path)) + "|" + entry.Name + "|" + std::to_string(size)
        + "|" + std::to_string(time.time_since_epoch().count())));
    std::string base = PathToUtf8(PathFromUtf8(entry.Name).filename());
    for (char& c : base)
    {
        if (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    }
    fs::create_directories(CacheDir, ec);
    fs::path dest = CacheDir / PathFromUtf8(std::string(key) + "-" + base);
    if (fs::exists(dest, ec) && fs::file_size(dest, ec) == entry.Size)
    {
        // used again: the newest, for the pruning below
        fs::last_write_time(dest, fs::file_time_type::clock::now(), ec);
        printf("library: %s from the cache (%s)\n", entry.Name.c_str(), PathToUtf8(dest).c_str());
        fflush(stdout);
        return PathToUtf8(dest);
    }
    auto start = std::chrono::steady_clock::now();
    if (!ExtractArchiveRom(archive, entry, dest, error, &Stopping))
        return "";
    printf("library: unpacked %s from %s (%llu bytes, %.0f ms)\n", entry.Name.c_str(), PathToUtf8(archive.filename()).c_str(),
        (unsigned long long)entry.Size, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    fflush(stdout);

    // the cache keeps the last few
    std::vector<std::pair<fs::file_time_type, fs::path>> cached;
    for (const fs::directory_entry& f : fs::directory_iterator(CacheDir, ec))
    {
        if (f.is_regular_file(ec) && f.path() != dest)
            cached.emplace_back(f.last_write_time(ec), f.path());
    }
    std::sort(cached.begin(), cached.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = kCachedRoms - 1; i < cached.size(); i++)
        fs::remove(cached[i].second, ec);
    return PathToUtf8(dest);
}

void Library::Scan()
{
    auto start = std::chrono::steady_clock::now();
    std::vector<std::string> folders;
    bool forRun;
    {
        std::lock_guard guard(Lock);
        folders = FolderList;
        forRun = FoldersForRun;
        State.Found = State.Done = 0;
        State.Current.clear();
    }
    printf("library: scan of %zu folder%s%s\n", folders.size(), folders.size() == 1 ? "" : "s", forRun ? " (--rom-folder)" : "");
    fflush(stdout);

    // the files first (quick: names and sizes only), so the progress has a total
    std::vector<FoundFile> found;
    std::vector<std::string> scanned, unavailable;
    for (const std::string& folder : folders)
    {
        std::error_code ec;
        fs::path dir = PathFromUtf8(folder);
        if (!fs::is_directory(dir, ec))
        {
            // a drive that isn't there now: its games stay, as the app keeps a folder's it can't open
            fprintf(stderr, "library: %s isn't there; its games stay listed\n", folder.c_str());
            unavailable.push_back(FolderPrefix(folder));
            continue;
        }
        scanned.push_back(FolderPrefix(folder));
        Walk(dir.native(), found, Stopping, 0);
    }
    {
        std::lock_guard guard(Lock);
        State.Found = (int)found.size();
    }

    std::map<std::string, size_t> byPath;
    for (size_t i = 0; i < Rows.size(); i++)
        byPath[Lower(Rows[i].Path)] = i;
    std::vector<bool> seen(Rows.size(), false);
    int read = 0, unchanged = 0, online = 0, notRoms = 0, failed = 0;
    auto lastPublish = std::chrono::steady_clock::now();
    for (const FoundFile& f : found)
    {
        if (Stopping)
            break;
        {
            std::lock_guard guard(Lock);
            State.Current = PathToUtf8(PathFromUtf8(f.Path).filename());
        }
        RunPrepares();
        auto it = byPath.find(Lower(f.Path));
        if (it != byPath.end())
        {
            const LibraryEntry& row = Rows[it->second];
            // the same file as before, read in the same language: not read again. A file that was online-only is read once it's on
            // the disk, and one whose read failed is tried again
            bool same = row.FileSize == f.Size && row.FileTime == f.Time && row.State != LibraryEntry::kFailed
                && !(row.State == LibraryEntry::kNotRead && !f.Online) && (row.State != LibraryEntry::kRead || row.TitleLanguage == Language);
            if (same)
            {
                seen[it->second] = true;
                unchanged++;
                std::lock_guard guard(Lock);
                State.Done++;
                continue;
            }
        }

        LibraryEntry e;
        // a file read again keeps its row and its id (the menus' focus and icon go by it)
        e.Id = it != byPath.end() ? Rows[it->second].Id : 0;
        e.Path = f.Path;
        e.FileSize = f.Size;
        e.FileTime = f.Time;
        e.ScannedAt = NowMs();
        e.TitleLanguage = Language;
        fs::path path = PathFromUtf8(f.Path);
        if (f.Online)
        {
            e.State = LibraryEntry::kNotRead;
            online++;
        }
        else
        {
            RomInfo info;
            std::unique_ptr<RomData> data;
            std::string error;
            bool ok = false, broken = false;
            try
            {
                if (IsArchiveName(path))
                {
                    ArchiveEntry entry;
                    if (FindRomInArchive(path, entry, error))
                    {
                        e.Entry = entry.Name;
                        data = OpenArchiveRom(path, entry, &Stopping);
                        broken = !data;
                    }
                }
                else
                {
                    data = OpenRomFile(path);
                    // (locked, or no permission: tried again next time)
                    broken = !data;
                    if (broken)
                        error = "can't open it";
                }
                {
                    std::lock_guard hashGuard(HashLock);
                    ok = data && ReadRomInfo(*data, Language, info);
                }
                broken = broken || (!ok && data && data->Failed());
            }
            catch (const std::exception& x)
            {
                ok = false;
                broken = true;
                error = x.what();
            }
            if (ok)
            {
                e.State = LibraryEntry::kRead;
                e.RomSize = data->Size();
                e.GameCode = info.GameCode;
                e.Region = RegionName(info.GameCode);
                e.HeaderTitle = info.HeaderTitle;
                e.Title = info.Title;
                e.Developer = info.Developer;
                e.DsiWare = info.DsiWare;
                e.RaHash = info.RaHash;
                if (!info.Icon.empty())
                    e.Icon = std::make_shared<std::vector<uint8_t>>(std::move(info.Icon));
                read++;
            }
            else if (broken)
            {
                e.State = LibraryEntry::kFailed;
                failed++;
                printf("library: couldn't read %s%s%s; it's listed without details and read again at the next scan\n",
                    PathToUtf8(path.filename()).c_str(), error.empty() ? "" : ": ", error.c_str());
            }
            else
            {
                e.State = LibraryEntry::kNotRom;
                notRoms++;
                printf("library: %s isn't a DS ROM%s%s\n", PathToUtf8(path.filename()).c_str(), error.empty() ? "" : ": ", error.c_str());
            }
        }
        Derive(e);
        int64_t oldId = e.Id;
        if (!Db.StoreRom(e))
        {
            // not in the database (locked by another copy of the app, or none at all): an id of its own for this run
            if (e.Id <= 0)
                e.Id = NextTempId--;
        }
        if (e.State == LibraryEntry::kRead)
            printf("library: read %s (id %lld%s): %s, %s, \"%s\"%s\n", PathToUtf8(path.filename()).c_str(), (long long)e.Id,
                oldId ? ", again" : "", e.GameCode.c_str(), e.RaHash.empty() ? "no RA hash" : e.RaHash.c_str(), e.Name().c_str(),
                e.Icon ? "" : ", no icon");
        fflush(stdout);
        {
            std::lock_guard guard(Lock);
            if (it != byPath.end())
            {
                Rows[it->second] = std::move(e);
                seen[it->second] = true;
            }
            else
            {
                byPath[Lower(f.Path)] = Rows.size();
                Rows.push_back(std::move(e));
                seen.push_back(true);
            }
            State.Done++;
        }
        if (std::chrono::steady_clock::now() - lastPublish > std::chrono::milliseconds(250))
        {
            Publish();
            lastPublish = std::chrono::steady_clock::now();
        }
    }

    // Gone: what a scanned folder didn't have. Rows under a folder that isn't there now (a drive unplugged) stay, and so do the rows of
    // the saved folders while --rom-folder sets others for the run; a folder taken out of the list for good loses its rows. A scan that
    // stopped early removes nothing
    int removed = 0;
    if (!Stopping)
    {
        std::vector<int64_t> ids;
        std::vector<LibraryEntry> kept;
        for (size_t i = 0; i < Rows.size(); i++)
        {
            std::string lower = Lower(Rows[i].Path);
            auto under = [&](const std::vector<std::string>& prefixes)
            {
                return std::any_of(prefixes.begin(), prefixes.end(), [&](const std::string& p) { return Under(lower, p); });
            };
            bool inScanned = under(scanned), away = under(unavailable);
            bool gone = !seen[i] && !away && (inScanned || !forRun);
            if (!gone)
                kept.push_back(std::move(Rows[i]));
            else
            {
                ids.push_back(Rows[i].Id);
                removed++;
                printf("library: %s is gone\n", Rows[i].Path.c_str());
            }
        }
        {
            std::lock_guard guard(Lock);
            Rows = std::move(kept);
        }
        if (!ids.empty())
            Db.DeleteRoms(ids);
    }
    Publish();
    size_t games = Entries()->size();
    printf("library: scan done in %.0f ms: %zu games, %d files read, %d unchanged, %d online-only, %d failed, %d not ROMs, %d removed\n",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(), games, read, unchanged, online, failed,
        notRoms, removed);
    fflush(stdout);
    if (OnScanDone && !Stopping)
        OnScanDone();
}

}
