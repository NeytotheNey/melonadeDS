// What the library knows about its games beyond the ROM files (phase 2 step C), as MetadataSync (MetadataSync.h) fills it in and the menus
// read it: RetroAchievements' game ids and API_GetGame rows (the app's ra_game_metadata), the art on disk, and the libretro thumbnails'
// No-Intro names. The sync publishes a MetadataIndex; Library::Publish puts what each game gets from it into its LibraryEntry
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Frontend
{

struct LibraryEntry;

// One game's API_GetGame answer, as the app's RAGameMetadataEntity, plus its title (the app has that from the connect API's ra_game). The
// image fields are the API's paths ("/Images/012345.png"), empty when it has none or only RetroAchievements' placeholder
struct RaGameRow
{
    enum Status { kOk = 0, kNotFound = 1, kFailed = 2 };
    int64_t GameId = 0;
    std::string Title, Publisher, Developer, Genre, Released, ReleasedGranularity;
    std::string ImageBoxArt, ImageTitle, ImageIngame, ImageIcon;
    int64_t FetchedAt = 0;   // ms since 1970; 0 after an image of it failed for good, so the next manual refresh fetches it again
    int FetchStatus = kOk;
    // something came from RetroAchievements for it (a failed refresh keeps what an earlier fetch stored)
    bool HasData() const { return !Title.empty(); }
};

// the kinds of art a game can have on disk, in the order a tile prefers them (as the app's chooseArt)
enum ArtKind { kArtBox = 0, kArtTitle = 1, kArtIcon = 2, kArtKinds = 3 };
const char* ArtKindName(int kind); // "box", "title", "icon": the file names (box.png, ...)

// where a game's tile art came from
enum ArtSource { kArtNone = 0, kArtRetroAchievements = 1, kArtLibretro = 2 };

// Whether the RetroAchievements hash library knows a ROM's hash
enum RaHashState
{
    kRaHashUnknown = 0,   // no hash library yet (never synced)
    kRaHashMissing = 1,   // rc_hash refused the ROM: no hash to look up
    kRaHashUnmatched = 2, // the hash library has no game for it (a hack, a translation, a bad dump)
    kRaHashMatched = 3,
};

// an image on disk: its path (UTF-8) and a stamp that changes whenever the file is saved again, so the menus read it again
struct ArtRef
{
    std::string Path;
    uint64_t Stamp = 0;
    bool empty() const { return Path.empty(); }
};

struct MetadataIndex
{
    bool HasHashLibrary = false;
    // the library's ROMs' hashes (lowercase) that RetroAchievements knows, to their game ids
    std::unordered_map<std::string, int64_t> GameIds;
    std::unordered_map<int64_t, std::shared_ptr<const RaGameRow>> Games;
    // art on disk: a RetroAchievements game's files by kind (empty where there's none), and a libretro name's (box, title)
    std::unordered_map<int64_t, std::array<ArtRef, kArtKinds>> RaArt;
    std::unordered_map<std::string, std::array<ArtRef, 2>> LibretroArt;
    // a ROM's No-Intro name in the libretro DAT, by its row id (the CRC32's match, or its serial's)
    std::unordered_map<int64_t, std::string> LibretroNames;
    // the ROMs' own hashes (lowercase hex), by row id, with the file's size and time they're for
    struct RomHashes { std::string Crc32, Md5, Sha1; uint64_t FileSize = 0; int64_t FileTime = 0; };
    std::unordered_map<int64_t, RomHashes> Hashes;
};

// what [index] says about [entry]'s game, into its metadata fields (LibraryEntry::RaGame, ArtFile, ...); [libretro]: the libretro
// thumbnails are in use (Settings > Library)
void ApplyMetadata(const MetadataIndex& index, bool libretro, LibraryEntry& entry);

// "Nov 18, 2010", "Nov 2010" or "2010" by RetroAchievements' granularity (day, month, year or none), as the app's formatReleaseDate;
// empty when [released] isn't a date
std::string FormatReleaseDate(const std::string& released, const std::string& granularity);

}
