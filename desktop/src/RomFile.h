// What the library reads from a ROM, as the app's RomProcessor and ROM file processors do: the header (game code, region, DSiWare), the
// banner's title in a language with English to fall back on, its 32x32 icon (the shared RomIconBuilder), and the RetroAchievements hash
// (rcheevos' rc_hash Nintendo DS method, which the app's RomProcessor.getRomMetadata copies). ROMs are plain .nds / .dsi files or the first
// .nds, .dsi or .ids file in a .zip (miniz) or .7z (the LZMA SDK's decoder), as the app's ZipRomFileProcessor and SevenZRomFileProcessor
// pick it. The shared ROM loader only reads plain files, so a game in an archive is unpacked to a file before it boots. Any thread; the
// RetroAchievements hash only one thread at a time (rc_hash's in-memory reader is a global). Nothing here throws: a failed allocation
// (a damaged archive claiming gigabytes) is a failed read
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Frontend
{

// the biggest ROM an archive's entry may claim to be: DS cartridges top out at 512 MB, so anything over this is a damaged archive or not a
// game, and isn't decompressed
constexpr uint64_t kMaxRomSize = 1ull << 30;

// a ROM's bytes, read at any offset
class RomData
{
public:
    virtual ~RomData() = default;
    // up to [size] bytes at [offset]; fewer at the end
    virtual size_t Read(uint64_t offset, void* out, size_t size) = 0;
    virtual uint64_t Size() const = 0;
    // a read failed (a damaged archive, out of memory, cancelled), as opposed to a file that just isn't a ROM; worth trying again later
    virtual bool Failed() const { return false; }
};

// a plain ROM file (UTF-8 path); null when it can't be opened
std::unique_ptr<RomData> OpenRomFile(const std::filesystem::path& path);

struct RomInfo
{
    std::string GameCode;     // the header's four letters ("IRDO")
    std::string HeaderTitle;  // the header's 12-character title ("POKEMON W2")
    std::string Title;        // the banner's title in the language asked for, or English, or empty (no banner, or a blank one)
    std::string Developer;    // the banner title's last line, when it has more than one
    int TitleLanguage = -1;   // which banner language Title is in (0 Japanese, 1 English, ... 7 Korean), -1 with no title
    bool DsiWare = false;
    std::string RaHash;       // 32 lowercase hex digits; empty when rc_hash refused the ROM
    std::vector<uint8_t> Icon; // 32 x 32 RGBA, empty when the banner has none (or one of a single color, as the app treats it)
};

// The header, banner and RetroAchievements hash. False when it isn't a DS ROM by the app's rules: a header too short, or ARM9 or ARM7 code
// bigger than 0x3BFE00 bytes (or when the data couldn't be read: RomData::Failed says which). [language] is the banner language wanted
// (BannerLanguage())
bool ReadRomInfo(RomData& data, int language, RomInfo& out);

// the banner language for the user's Windows display language: 0 Japanese, 1 English, 2 French, 3 German, 4 Italian, 5 Spanish,
// 6 Chinese, 7 Korean (English for anything else). MELONADE_BANNER_LANGUAGE (0-7) sets it for a test
int BannerLanguage();

// the region a game code's last letter stands for ("USA", "Europe", "Japan", ...), or "Region X" for one it doesn't know
std::string RegionName(const std::string& gameCode);

// .nds and .dsi files, and the archives the app reads them from
bool IsRomFileName(const std::filesystem::path& path);
bool IsArchiveName(const std::filesystem::path& path);

// The ROM in a .zip or .7z: the first file (not a folder) whose extension is .nds, .dsi or .ids, in the archive's order, as the app takes it
struct ArchiveEntry
{
    std::string Name;    // its path inside the archive, UTF-8 (a zip's name without the UTF-8 flag is read as code page 437, as zip says)
    uint64_t Size = 0;   // uncompressed
    uint32_t Index = 0;  // the archive's own index for it
};
// false when the archive doesn't open, has no ROM in it, or its ROM claims more than kMaxRomSize ([error] says which)
bool FindRomInArchive(const std::filesystem::path& archive, ArchiveEntry& entry, std::string& error);
// the entry, read as it's needed: a zip entry is decompressed as far as the reads go, a 7z one all at once on the first read (the LZMA
// SDK decodes whole blocks). [cancel], when given and set, stops a decompression partway (the read then fails)
std::unique_ptr<RomData> OpenArchiveRom(const std::filesystem::path& archive, const ArchiveEntry& entry,
    const std::atomic<bool>* cancel = nullptr);
// the entry unpacked into [dest] (through a temporary file next to it, so a failed unpack leaves nothing)
bool ExtractArchiveRom(const std::filesystem::path& archive, const ArchiveEntry& entry, const std::filesystem::path& dest, std::string& error,
    const std::atomic<bool>* cancel = nullptr);

}
