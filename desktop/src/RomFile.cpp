#include "RomFile.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <new>

#include "RomIconBuilder.h"
#include "Settings.h"
#include "rc_consoles.h"
#include "rc_hash.h"

#include "miniz/miniz.h"

extern "C"
{
#include "lzma/7z.h"
#include "lzma/7zAlloc.h"
#include "lzma/7zCrc.h"
#include "lzma/7zFile.h"
}

namespace fs = std::filesystem;

namespace Frontend
{

namespace
{

uint32_t Read32(const uint8_t* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t Read16(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

std::string LowerExtension(const fs::path& path)
{
    std::string ext = PathToUtf8(path.extension());
    for (char& c : ext)
        c = (char)tolower((unsigned char)c);
    return ext;
}

// a name inside an archive that the app takes for a ROM
bool IsRomEntryName(const std::string& name)
{
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    std::string ext = name.substr(dot + 1);
    for (char& c : ext)
        c = (char)tolower((unsigned char)c);
    return ext == "nds" || ext == "dsi" || ext == "ids";
}

bool Cancelled(const std::atomic<bool>* cancel)
{
    return cancel && cancel->load();
}

// a plain file, read at offsets without moving a shared position
class FileData : public RomData
{
public:
    explicit FileData(HANDLE handle) : Handle(handle)
    {
        LARGE_INTEGER size {};
        if (GetFileSizeEx(handle, &size))
            Length = (uint64_t)size.QuadPart;
    }
    ~FileData() override { CloseHandle(Handle); }

    size_t Read(uint64_t offset, void* out, size_t size) override
    {
        size_t done = 0;
        while (done < size && offset + done < Length)
        {
            OVERLAPPED at {};
            uint64_t pos = offset + done;
            at.Offset = (DWORD)pos;
            at.OffsetHigh = (DWORD)(pos >> 32);
            DWORD chunk = (DWORD)std::min<size_t>(size - done, 1u << 24), got = 0;
            if (!ReadFile(Handle, (uint8_t*)out + done, chunk, &got, &at) || got == 0)
            {
                ReadError = true;
                break;
            }
            done += got;
        }
        return done;
    }
    uint64_t Size() const override { return Length; }
    bool Failed() const override { return ReadError; }

private:
    HANDLE Handle;
    uint64_t Length = 0;
    bool ReadError = false;
};

// ---------- zip (miniz) ----------

class ZipFile
{
public:
    ~ZipFile()
    {
        if (Open)
            mz_zip_reader_end(&Zip);
    }
    bool OpenFile(const fs::path& path)
    {
        memset(&Zip, 0, sizeof(Zip));
        // miniz opens UTF-8 names with _wfopen on Windows
        Open = mz_zip_reader_init_file(&Zip, PathToUtf8(path).c_str(), 0) != 0;
        return Open;
    }
    mz_zip_archive Zip;
    bool Open = false;
};

// a zip entry's name as UTF-8: as it is with the UTF-8 flag (general purpose bit 11), otherwise in code page 437, zip's original one
// (what Windows' own zip folders and Shift-JIS tools without the flag write differs, but 437 at least never fails)
std::string ZipName(const mz_zip_archive_file_stat& stat)
{
    std::string raw = stat.m_filename;
    if (stat.m_bit_flag & (1 << 11))
        return raw;
    int len = MultiByteToWideChar(437, 0, raw.data(), (int)raw.size(), nullptr, 0);
    if (len <= 0)
        return raw;
    std::wstring wide(len, L'\0');
    MultiByteToWideChar(437, 0, raw.data(), (int)raw.size(), wide.data(), len);
    return PathToUtf8(fs::path(wide));
}

// a zip entry, decompressed as far as the reads have gone (a megabyte at a time, so a damaged size can't make it allocate more than
// the entry really holds)
class ZipData : public RomData
{
public:
    explicit ZipData(const std::atomic<bool>* cancel) : Cancel(cancel) {}
    ~ZipData() override
    {
        if (Iter)
            mz_zip_reader_extract_iter_free(Iter);
    }
    bool Start(const fs::path& path, const ArchiveEntry& entry)
    {
        if (!Archive.OpenFile(path))
            return false;
        Iter = mz_zip_reader_extract_iter_new(&Archive.Zip, entry.Index, 0);
        Length = entry.Size;
        return Iter != nullptr;
    }
    size_t Read(uint64_t offset, void* out, size_t size) override
    {
        uint64_t need = std::min<uint64_t>(offset + size, Length);
        try
        {
            while (Iter && Bytes.size() < need)
            {
                if (Cancelled(Cancel))
                {
                    Stop(true);
                    break;
                }
                size_t old = Bytes.size();
                size_t chunk = (size_t)std::min<uint64_t>(need - old, 1 << 20);
                Bytes.resize(old + chunk);
                size_t got = mz_zip_reader_extract_iter_read(Iter, Bytes.data() + old, chunk);
                Bytes.resize(old + got);
                if (got == 0)
                    Stop(Bytes.size() < Length);
            }
        }
        catch (const std::bad_alloc&)
        {
            Bytes.clear();
            Bytes.shrink_to_fit();
            Stop(true);
        }
        if (offset >= Bytes.size())
            return 0;
        size_t n = (size_t)std::min<uint64_t>(size, Bytes.size() - offset);
        memcpy(out, Bytes.data() + offset, n);
        return n;
    }
    uint64_t Size() const override { return Length; }
    bool Failed() const override { return Error; }

private:
    void Stop(bool error)
    {
        if (Iter)
            mz_zip_reader_extract_iter_free(Iter);
        Iter = nullptr;
        Error = Error || error;
    }

    const std::atomic<bool>* Cancel;
    ZipFile Archive;
    mz_zip_reader_extract_iter_state* Iter = nullptr;
    uint64_t Length = 0;
    bool Error = false;
    std::vector<uint8_t> Bytes;
};

bool FindInZip(const fs::path& path, ArchiveEntry& entry, std::string& error)
{
    ZipFile zip;
    if (!zip.OpenFile(path))
    {
        error = "not a zip file miniz can read";
        return false;
    }
    mz_uint count = mz_zip_reader_get_num_files(&zip.Zip);
    for (mz_uint i = 0; i < count; i++)
    {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip.Zip, i, &stat) || stat.m_is_directory)
            continue;
        std::string name = ZipName(stat);
        if (IsRomEntryName(name))
        {
            if (stat.m_uncomp_size > kMaxRomSize)
            {
                error = name + " claims " + std::to_string(stat.m_uncomp_size) + " bytes, more than a DS ROM can be";
                return false;
            }
            entry.Name = name;
            entry.Size = stat.m_uncomp_size;
            entry.Index = i;
            return true;
        }
    }
    error = "no .nds, .dsi or .ids file in it";
    return false;
}

// ---------- 7z (LZMA SDK) ----------

void* SzAllocImpl(ISzAllocPtr, size_t size) { return size ? malloc(size) : nullptr; }
void SzFreeImpl(ISzAllocPtr, void* address) { free(address); }
const ISzAlloc kSzAlloc = {SzAllocImpl, SzFreeImpl};

// the archive's file stream, which fails its reads once [Cancel] is set, so a long decode stops partway (the SDK has no other way in)
struct CancelStream
{
    ISeekInStream vt;
    const ISeekInStream* Inner;
    const std::atomic<bool>* Cancel;
};

SRes CancelRead(ISeekInStreamPtr p, void* buf, size_t* size)
{
    const CancelStream* s = (const CancelStream*)p;
    if (Cancelled(s->Cancel))
    {
        *size = 0;
        return SZ_ERROR_READ;
    }
    return s->Inner->Read(s->Inner, buf, size);
}

SRes CancelSeek(ISeekInStreamPtr p, Int64* pos, ESzSeek origin)
{
    const CancelStream* s = (const CancelStream*)p;
    return s->Inner->Seek(s->Inner, pos, origin);
}

// a block the SDK decoded (freed with its allocator), and where the entry is in it
struct SevenZipBlock
{
    Byte* Buffer = nullptr;
    size_t Offset = 0, Size = 0;
    ~SevenZipBlock()
    {
        if (Buffer)
            ISzAlloc_Free(&kSzAlloc, Buffer);
    }
};

class SevenZipFile
{
public:
    explicit SevenZipFile(const std::atomic<bool>* cancel = nullptr) { Cancel.Cancel = cancel; }
    SevenZipFile(const SevenZipFile&) = delete;
    ~SevenZipFile()
    {
        if (DbOpen)
            SzArEx_Free(&Db, &kSzAlloc);
        if (Look.buf)
            ISzAlloc_Free(&kSzAlloc, Look.buf);
        if (FileOpen)
            File_Close(&Stream.file);
    }
    bool OpenFile(const fs::path& path)
    {
        static std::once_flag crc;
        std::call_once(crc, [] { CrcGenerateTable(); });
        File_Construct(&Stream.file);
        if (InFile_OpenW(&Stream.file, path.c_str()) != 0)
            return false;
        FileOpen = true;
        FileInStream_CreateVTable(&Stream);
        Stream.wres = 0;
        Cancel.vt.Read = CancelRead;
        Cancel.vt.Seek = CancelSeek;
        Cancel.Inner = &Stream.vt;
        LookToRead2_CreateVTable(&Look, False);
        Look.buf = (Byte*)ISzAlloc_Alloc(&kSzAlloc, kBufSize);
        if (!Look.buf)
            return false;
        Look.bufSize = kBufSize;
        Look.realStream = &Cancel.vt;
        LookToRead2_INIT(&Look)
        SzArEx_Init(&Db);
        DbOpen = true;
        return SzArEx_Open(&Db, &Look.vt, &kSzAlloc, &kSzAlloc) == SZ_OK;
    }
    std::string Name(uint32_t index)
    {
        size_t len = SzArEx_GetFileNameUtf16(&Db, index, nullptr);
        std::vector<UInt16> name(len + 1);
        SzArEx_GetFileNameUtf16(&Db, index, name.data());
        std::wstring wide(name.begin(), name.begin() + (len ? len - 1 : 0));
        return PathToUtf8(fs::path(wide));
    }
    // the entry's block decoded into memory (the SDK decodes a block whole); false when that fails (out of memory, damaged, cancelled)
    bool Extract(uint32_t index, SevenZipBlock& out)
    {
        UInt32 block = 0xFFFFFFFF;
        size_t bufferSize = 0;
        SRes res = SzArEx_Extract(&Db, &Look.vt, index, &block, &out.Buffer, &bufferSize, &out.Offset, &out.Size, &kSzAlloc, &kSzAlloc);
        return res == SZ_OK && out.Buffer;
    }

    static constexpr size_t kBufSize = 1 << 18;
    CFileInStream Stream {};
    CancelStream Cancel {};
    CLookToRead2 Look {};
    CSzArEx Db {};
    bool FileOpen = false, DbOpen = false;
};

bool FindIn7z(const fs::path& path, ArchiveEntry& entry, std::string& error)
{
    SevenZipFile file;
    if (!file.OpenFile(path))
    {
        error = "not a 7z file the LZMA SDK can read";
        return false;
    }
    for (UInt32 i = 0; i < file.Db.NumFiles; i++)
    {
        if (SzArEx_IsDir(&file.Db, i))
            continue;
        std::string name = file.Name(i);
        if (IsRomEntryName(name))
        {
            uint64_t size = SzArEx_GetFileSize(&file.Db, i);
            if (size > kMaxRomSize)
            {
                error = name + " claims " + std::to_string(size) + " bytes, more than a DS ROM can be";
                return false;
            }
            entry.Name = name;
            entry.Size = size;
            entry.Index = i;
            return true;
        }
    }
    error = "no .nds, .dsi or .ids file in it";
    return false;
}

// a 7z entry, its block decoded on the first read and kept as the SDK gave it (no copy)
class SevenZipData : public RomData
{
public:
    SevenZipData(const fs::path& path, const ArchiveEntry& entry, const std::atomic<bool>* cancel)
        : Path(path), Index(entry.Index), Length(entry.Size), Cancel(cancel) {}

    size_t Read(uint64_t offset, void* out, size_t size) override
    {
        Fill();
        if (!Block || offset >= Block->Size)
            return 0;
        size_t n = (size_t)std::min<uint64_t>(size, Block->Size - offset);
        memcpy(out, Block->Buffer + Block->Offset + offset, n);
        return n;
    }
    uint64_t Size() const override { return Length; }
    bool Failed() const override { return Error; }

private:
    void Fill()
    {
        if (Filled)
            return;
        Filled = true;
        try
        {
            auto block = std::make_unique<SevenZipBlock>();
            SevenZipFile file(Cancel);
            if (file.OpenFile(Path) && file.Extract(Index, *block))
                Block = std::move(block);
        }
        catch (const std::bad_alloc&)
        {
        }
        Error = !Block;
    }

    fs::path Path;
    uint32_t Index;
    uint64_t Length;
    const std::atomic<bool>* Cancel;
    bool Filled = false, Error = false;
    std::unique_ptr<SevenZipBlock> Block;
};

// ---------- the banner ----------

std::string Utf16ToUtf8(const uint16_t* text, size_t count)
{
    std::wstring wide;
    for (size_t i = 0; i < count && text[i]; i++)
        wide.push_back((wchar_t)text[i]);
    if (wide.empty())
        return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), out.data(), len, nullptr, nullptr);
    return out;
}

std::string Trim(const std::string& text)
{
    size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

// a banner title as the app splits it: the lines but the last are the title (joined with spaces), the last is the developer
void SplitTitle(const std::string& raw, std::string& title, std::string& developer)
{
    std::string text = Trim(raw);
    size_t last = text.find_last_of('\n');
    if (last == std::string::npos)
    {
        title = text;
        developer.clear();
        return;
    }
    title = Trim(text.substr(0, last));
    developer = Trim(text.substr(last + 1));
    for (char& c : title)
    {
        if (c == '\n' || c == '\r')
            c = ' ';
    }
    // (a \r\n leaves two spaces)
    size_t twice;
    while ((twice = title.find("  ")) != std::string::npos)
        title.erase(twice, 1);
}

// rc_hash reads the ROM through these, from the RomData the hashing thread set
thread_local RomData* tHashSource = nullptr;
struct HashFile { RomData* Data; int64_t Pos; };

void* HashOpen(const char*)
{
    return tHashSource ? new HashFile {tHashSource, 0} : nullptr;
}
void HashSeek(void* handle, int64_t offset, int origin)
{
    HashFile* f = (HashFile*)handle;
    if (origin == SEEK_SET) f->Pos = offset;
    else if (origin == SEEK_CUR) f->Pos += offset;
    else f->Pos = (int64_t)f->Data->Size() + offset;
    if (f->Pos < 0)
        f->Pos = 0;
}
int64_t HashTell(void* handle)
{
    return ((HashFile*)handle)->Pos;
}
size_t HashRead(void* handle, void* buffer, size_t bytes)
{
    HashFile* f = (HashFile*)handle;
    size_t got = f->Data->Read((uint64_t)f->Pos, buffer, bytes);
    f->Pos += (int64_t)got;
    return got;
}
void HashClose(void* handle)
{
    delete (HashFile*)handle;
}

std::string RaHash(RomData& data)
{
    rc_hash_iterator_t iterator;
    memset(&iterator, 0, sizeof(iterator));
    iterator.path = "rom.nds";
    iterator.callbacks.filereader.open = HashOpen;
    iterator.callbacks.filereader.seek = HashSeek;
    iterator.callbacks.filereader.tell = HashTell;
    iterator.callbacks.filereader.read = HashRead;
    iterator.callbacks.filereader.close = HashClose;
    tHashSource = &data;
    char hash[33] = {};
    int ok = rc_hash_generate(hash, RC_CONSOLE_NINTENDO_DS, &iterator);
    tHashSource = nullptr;
    return ok ? std::string(hash) : std::string();
}

}

std::unique_ptr<RomData> OpenRomFile(const fs::path& path)
{
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return nullptr;
    return std::make_unique<FileData>(handle);
}

bool ReadRomInfo(RomData& data, int language, RomInfo& out)
{
    out = RomInfo();
    // the DSi header is 0x1000; a DS one is 0x200, of which the app reads 0x160
    uint8_t header[0x1000] = {};
    size_t got = data.Read(0, header, sizeof(header));
    if (got < 0x160)
        return false;
    uint32_t arm9Size = Read32(header + 0x2C), arm7Size = Read32(header + 0x3C);
    // the app's limits (RomProcessor.getRomMetadata), as signed ints there
    if (arm9Size > 0x3BFE00 || arm7Size > 0x3BFE00)
        return false;

    for (int i = 0; i < 4; i++)
    {
        char c = (char)header[0x0C + i];
        out.GameCode.push_back(c >= 0x20 && c < 0x7F ? c : '?');
    }
    for (int i = 0; i < 12 && header[i]; i++)
    {
        char c = (char)header[i];
        out.HeaderTitle.push_back(c >= 0x20 && c < 0x7F ? c : '?');
    }
    out.HeaderTitle = Trim(out.HeaderTitle);
    char category = out.GameCode[0];
    if ((category == 'H' || category == 'K') && got >= 0x238)
        out.DsiWare = Read32(header + 0x234) == 0x00030004;

    // the banner: version, CRCs, the icon's tiles and palette, then the titles (0x100 bytes each: Japanese, English, French, German,
    // Italian, Spanish, then Chinese from version 2 and Korean from version 3)
    uint32_t bannerOffset = Read32(header + 0x68);
    if (bannerOffset >= 0x160 && bannerOffset < data.Size())
    {
        std::vector<uint8_t> banner(0xA40, 0);
        size_t bannerSize = data.Read(bannerOffset, banner.data(), banner.size());
        if (bannerSize >= 0x840)
        {
            uint16_t version = Read16(banner.data());
            auto has = [&](int lang)
            {
                if (lang < 0 || lang > 7) return false;
                if (lang == 6) return (version & 0xFF) >= 2 && bannerSize >= 0x940;
                if (lang == 7) return (version & 0xFF) >= 3 && bannerSize >= 0xA40;
                return true;
            };
            auto titleIn = [&](int lang)
            {
                const uint8_t* p = banner.data() + 0x240 + lang * 0x100;
                uint16_t text[128];
                for (int i = 0; i < 128; i++)
                    text[i] = Read16(p + i * 2);
                return Utf16ToUtf8(text, 128);
            };
            for (int lang : {language, 1})
            {
                if (!has(lang))
                    continue;
                std::string title, developer;
                SplitTitle(titleIn(lang), title, developer);
                if (!title.empty())
                {
                    out.Title = title;
                    out.Developer = developer;
                    out.TitleLanguage = lang;
                    break;
                }
            }

            melonDS::u8 tiles[512];
            melonDS::u16 palette[16];
            memcpy(tiles, banner.data() + 0x20, sizeof(tiles));
            for (int i = 0; i < 16; i++)
                palette[i] = Read16(banner.data() + 0x220 + i * 2);
            melonDS::u32 icon[32 * 32];
            MelonDSAndroid::BuildRomIcon(tiles, palette, icon);
            // a ROM with no real banner still decodes to an icon of one color, which the app doesn't show (isBlankIcon)
            bool blank = std::all_of(icon, icon + 32 * 32, [&](melonDS::u32 p) { return p == icon[0]; });
            if (!blank)
            {
                out.Icon.resize(32 * 32 * 4);
                memcpy(out.Icon.data(), icon, out.Icon.size());
            }
        }
    }

    out.RaHash = RaHash(data);
    return true;
}

int BannerLanguage()
{
    const char* forced = getenv("MELONADE_BANNER_LANGUAGE");
    if (forced && *forced >= '0' && *forced <= '7' && !forced[1])
        return *forced - '0';
    switch (PRIMARYLANGID(GetUserDefaultUILanguage()))
    {
    case LANG_JAPANESE: return 0;
    case LANG_FRENCH: return 2;
    case LANG_GERMAN: return 3;
    case LANG_ITALIAN: return 4;
    case LANG_SPANISH: return 5;
    case LANG_CHINESE: return 6;
    case LANG_KOREAN: return 7;
    default: return 1;
    }
}

std::string RegionName(const std::string& gameCode)
{
    if (gameCode.size() < 4)
        return "";
    switch (gameCode[3])
    {
    case 'A': return "All regions";
    case 'C': return "China";
    case 'D': return "Germany";
    case 'E': return "USA";
    case 'F': return "France";
    case 'H': return "Netherlands";
    case 'I': return "Italy";
    case 'J': return "Japan";
    case 'K': return "Korea";
    case 'L': return "USA";
    case 'M': return "Sweden";
    case 'N': return "Norway";
    case 'O': return "USA and Europe";
    case 'P': return "Europe";
    case 'Q': return "Denmark";
    case 'R': return "Russia";
    case 'S': return "Spain";
    case 'T': return "USA and Australia";
    case 'U': return "Australia";
    case 'V': return "Europe and Australia";
    case 'W': return "Europe";
    case 'X': case 'Y': case 'Z': return "Europe";
    default: return std::string("Region ") + gameCode[3];
    }
}

bool IsRomFileName(const fs::path& path)
{
    std::string ext = LowerExtension(path);
    return ext == ".nds" || ext == ".dsi";
}

bool IsArchiveName(const fs::path& path)
{
    std::string ext = LowerExtension(path);
    return ext == ".zip" || ext == ".7z";
}

bool FindRomInArchive(const fs::path& archive, ArchiveEntry& entry, std::string& error)
{
    try
    {
        std::string ext = LowerExtension(archive);
        if (ext == ".zip")
            return FindInZip(archive, entry, error);
        if (ext == ".7z")
            return FindIn7z(archive, entry, error);
        error = "not a zip or 7z file";
    }
    catch (const std::bad_alloc&)
    {
        error = "out of memory reading its list of files";
    }
    return false;
}

std::unique_ptr<RomData> OpenArchiveRom(const fs::path& archive, const ArchiveEntry& entry, const std::atomic<bool>* cancel)
{
    if (LowerExtension(archive) == ".zip")
    {
        auto data = std::make_unique<ZipData>(cancel);
        if (!data->Start(archive, entry))
            return nullptr;
        return data;
    }
    return std::make_unique<SevenZipData>(archive, entry, cancel);
}

bool ExtractArchiveRom(const fs::path& archive, const ArchiveEntry& entry, const fs::path& dest, std::string& error,
    const std::atomic<bool>* cancel)
{
    fs::path temp = dest;
    temp += ".tmp";
    bool ok = false;
    try
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            error = "can't write " + PathToUtf8(temp);
            return false;
        }
        if (LowerExtension(archive) == ".zip")
        {
            ZipFile zip;
            mz_zip_reader_extract_iter_state* iter = zip.OpenFile(archive) ? mz_zip_reader_extract_iter_new(&zip.Zip, entry.Index, 0) : nullptr;
            if (iter)
            {
                std::vector<char> buffer(1 << 20);
                uint64_t total = 0;
                bool stopped = false;
                for (;;)
                {
                    if (Cancelled(cancel))
                    {
                        stopped = true;
                        break;
                    }
                    size_t got = mz_zip_reader_extract_iter_read(iter, buffer.data(), buffer.size());
                    if (got == 0)
                        break;
                    out.write(buffer.data(), (std::streamsize)got);
                    total += got;
                }
                // (frees the state and checks the entry's CRC)
                ok = mz_zip_reader_extract_iter_free(iter) && !stopped && total == entry.Size;
            }
            if (!ok)
                error = Cancelled(cancel) ? "stopped" : "the zip entry didn't unpack";
        }
        else
        {
            SevenZipFile file(cancel);
            SevenZipBlock block;
            ok = file.OpenFile(archive) && file.Extract(entry.Index, block) && block.Size == entry.Size;
            if (ok)
                out.write((const char*)block.Buffer + block.Offset, (std::streamsize)block.Size);
            else
                error = Cancelled(cancel) ? "stopped" : "the 7z entry didn't unpack (damaged, or not enough memory)";
        }
        ok = ok && out.flush().good();
    }
    catch (const std::bad_alloc&)
    {
        error = "not enough memory to unpack it";
        ok = false;
    }
    std::error_code ec;
    if (ok && !MoveFileExW(temp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        error = "can't rename the unpacked file";
        ok = false;
    }
    if (!ok)
        fs::remove(temp, ec);
    return ok;
}

}
