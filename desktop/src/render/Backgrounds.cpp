#include "render/Backgrounds.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#include "Settings.h"
#include "layout/Layouts.h"
#include "render/SvgBackground.h"

// stb_image again, as a private copy: ArtImage.cpp's is built for the library's small art, capped at 4096 pixels a side. This one takes a
// wallpaper's size; its functions are static (STB_IMAGE_STATIC), so the ones not called would warn as unused, which isn't ours to change
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_MAX_DIMENSIONS 16384
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb/stb_image.h"
#pragma GCC diagnostic pop

namespace fs = std::filesystem;

namespace Frontend
{

namespace
{

struct EmbeddedBackgroundFile
{
    const char* Name;
    const unsigned char* Data;
    size_t Size;
};

#include "background_files.inc"

// BuiltInBackgrounds.all's order, which is how the app lists them; a file the exe has that isn't here goes after them
const char* const kBuiltInOrder[] = {"crimson.svg", "onyx.svg", "pearl.svg", "pearl-tech.svg", "atomic-purple.svg", "cobalt.svg",
    "glacier.svg", "kiwi.svg", "dandelion.svg", "gunmetal.svg", "coral.svg", "smoke.svg", "carbon.svg", "hex.svg"};

// what a user's background may be (rule 2): an 8K photo is 33 megapixels
constexpr uintmax_t MAX_IMAGE_FILE = 256u << 20;
// RasterizeSvg's own cap, checked before an SVG file is read whole and kept
constexpr uintmax_t MAX_SVG_FILE = 16u << 20;
constexpr long long MAX_IMAGE_PIXELS = 48ll << 20;
constexpr uintmax_t MAX_LIST_FILE = 4u << 20;
constexpr size_t MAX_USER_BACKGROUNDS = 10000;

std::string Lower(std::string text)
{
    for (char& c : text)
        c = (char)std::tolower((unsigned char)c);
    return text;
}

// UUID.nameUUIDFromBytes: a version 3 UUID from the MD5 of the bytes (CNG's MD5, a system DLL); empty when CNG fails
std::string NameUuid(const std::string& name)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    UCHAR md5[16] = {};
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0) >= 0;
    ok = ok && BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    ok = ok && BCryptHashData(hash, (PUCHAR)name.data(), (ULONG)name.size(), 0) >= 0;
    ok = ok && BCryptFinishHash(hash, md5, sizeof(md5), 0) >= 0;
    if (hash)
        BCryptDestroyHash(hash);
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok)
        return {};
    md5[6] = (UCHAR)((md5[6] & 0x0f) | 0x30);
    md5[8] = (UCHAR)((md5[8] & 0x3f) | 0x80);
    char text[37];
    snprintf(text, sizeof(text), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", md5[0], md5[1], md5[2], md5[3], md5[4],
        md5[5], md5[6], md5[7], md5[8], md5[9], md5[10], md5[11], md5[12], md5[13], md5[14], md5[15]);
    return text;
}

// BuiltInBackgrounds.displayName: pearl-tech.svg is Pearl Tech
std::string DisplayName(const std::string& fileName)
{
    std::string base = fileName.size() > 4 ? fileName.substr(0, fileName.size() - 4) : fileName;
    std::string out;
    bool start = true;
    for (char c : base)
    {
        if (c == '-')
        {
            out += ' ';
            start = true;
            continue;
        }
        out += start ? (char)std::toupper((unsigned char)c) : c;
        start = false;
    }
    return out;
}

// %XX decoded
std::string PercentDecoded(const std::string& text)
{
    std::string decoded;
    for (size_t i = 0; i < text.size(); i++)
    {
        if (text[i] == '%' && i + 2 < text.size() && isxdigit((unsigned char)text[i + 1]) && isxdigit((unsigned char)text[i + 2]))
        {
            decoded += (char)strtol(text.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        }
        else
            decoded += text[i];
    }
    return decoded;
}

// a backgrounds.json uri as a file: a path (a relative one is in the profile folder), or a file: URI, decoded first (file:///C:/x,
// file:///C%3A/x, file://localhost/C:/x; file://server/share/x and file:////server/share/x are the UNC path \\server\share\x); empty for a
// content:// one, which only the phone opens
fs::path UriPath(const std::string& uri, const fs::path& profileDir)
{
    if (uri.rfind("file:", 0) != 0)
    {
        if (uri.find("://") != std::string::npos)
            return {};
        fs::path path = PathFromUtf8(uri);
        return path.is_relative() && !profileDir.empty() ? profileDir / path : path;
    }
    std::string rest = PercentDecoded(uri.substr(5));
    auto withoutDriveSlash = [](std::string path)
    {
        // /C:/x is C:/x
        if (path.size() > 2 && path[0] == '/' && isalpha((unsigned char)path[1]) && path[2] == ':')
            path = path.substr(1);
        return path;
    };
    if (rest.rfind("//", 0) != 0)
        return PathFromUtf8(withoutDriveSlash(rest));
    rest = rest.substr(2);
    // file:////server/share: the host after the extra slashes
    if (rest.rfind("//", 0) == 0)
        return PathFromUtf8("\\\\" + rest.substr(2));
    // file:///path: no host
    if (rest.rfind("/", 0) == 0)
        return PathFromUtf8(withoutDriveSlash(rest));
    size_t slash = rest.find('/');
    std::string host = rest.substr(0, slash);
    std::string path = slash == std::string::npos ? std::string() : rest.substr(slash);
    std::string lowerHost = host;
    for (char& c : lowerHost)
        c = (char)std::tolower((unsigned char)c);
    if (lowerHost == "localhost")
        return PathFromUtf8(withoutDriveSlash(path));
    return PathFromUtf8("\\\\" + host + path);
}

bool ReadWhole(const fs::path& path, uintmax_t limit, std::vector<uint8_t>& out)
{
    std::error_code ec;
    uintmax_t size = fs::file_size(path, ec);
    if (ec || size > limit)
        return false;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.resize((size_t)size);
    in.read((char*)out.data(), (std::streamsize)size);
    return (uintmax_t)in.gcount() == size;
}

// PNG, JPEG, GIF or BMP by its first bytes
bool IsRasterImage(const uint8_t* data, size_t size)
{
    static const uint8_t png[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    return (size >= 8 && memcmp(data, png, 8) == 0) || (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
        || (size >= 4 && memcmp(data, "GIF8", 4) == 0) || (size >= 2 && data[0] == 'B' && data[1] == 'M');
}

// BitmapUtils.calculateMinimumSampleSize: the largest power of two that keeps both sides at least the target's
int SampleSize(int width, int height, int targetWidth, int targetHeight)
{
    int sample = 1;
    if (height > targetHeight || width > targetWidth)
    {
        int halfHeight = height / 2, halfWidth = width / 2;
        while (halfHeight / sample >= targetHeight && halfWidth / sample >= targetWidth)
            sample *= 2;
    }
    return sample;
}

// a raster image decoded, premultiplied, and shrunk by [sample] (each pixel the average of its sample x sample box)
bool DecodeRaster(const uint8_t* data, size_t size, int surfaceWidth, int surfaceHeight, int maxSize, BackgroundImage& out, std::string& error,
    const std::atomic<bool>* cancel)
{
    int w = 0, h = 0, comp = 0;
    if (size > (size_t)INT32_MAX || !stbi_info_from_memory(data, (int)size, &w, &h, &comp))
    {
        error = "not a PNG, JPEG, BMP or GIF image";
        return false;
    }
    if (w <= 0 || h <= 0 || (long long)w * h > MAX_IMAGE_PIXELS)
    {
        error = "bigger than " + std::to_string(MAX_IMAGE_PIXELS >> 20) + " megapixels";
        return false;
    }
    if (cancel && cancel->load(std::memory_order_relaxed))
    {
        error = "cancelled";
        return false;
    }
    stbi_uc* pixels = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 4);
    if (!pixels)
    {
        error = stbi_failure_reason() ? stbi_failure_reason() : "it doesn't decode";
        return false;
    }
    int sample = SampleSize(w, h, std::max(surfaceWidth, 1), std::max(surfaceHeight, 1));
    while (maxSize > 0 && ((w + sample - 1) / sample > maxSize || (h + sample - 1) / sample > maxSize))
        sample *= 2;
    int ow = (w + sample - 1) / sample, oh = (h + sample - 1) / sample;
    out.Rgba.assign((size_t)ow * oh * 4, 0);
    for (int oy = 0; oy < oh; oy++)
    {
        if (cancel && cancel->load(std::memory_order_relaxed))
        {
            stbi_image_free(pixels);
            error = "cancelled";
            return false;
        }
        for (int ox = 0; ox < ow; ox++)
        {
            uint64_t sum[4] = {0, 0, 0, 0};
            int count = 0;
            for (int y = oy * sample; y < std::min(h, (oy + 1) * sample); y++)
            {
                for (int x = ox * sample; x < std::min(w, (ox + 1) * sample); x++)
                {
                    const stbi_uc* p = &pixels[((size_t)y * w + x) * 4];
                    sum[0] += p[0] * p[3] / 255;
                    sum[1] += p[1] * p[3] / 255;
                    sum[2] += p[2] * p[3] / 255;
                    sum[3] += p[3];
                    count++;
                }
            }
            uint8_t* o = &out.Rgba[((size_t)oy * ow + ox) * 4];
            for (int c = 0; c < 4; c++)
                o[c] = (uint8_t)((sum[c] + (uint64_t)count / 2) / (uint64_t)count);
        }
    }
    stbi_image_free(pixels);
    out.Width = ow;
    out.Height = oh;
    out.IsSvg = false;
    return true;
}

}

std::optional<std::string> Backgrounds::AddUserFile(const fs::path& file, std::string& error)
{
    if (Dir.empty())
    {
        error = "no profile folder";
        return std::nullopt;
    }
    fs::path list = Dir / "backgrounds.json";
    picojson::array items;
    std::error_code ec;
    if (fs::is_regular_file(list, ec))
    {
        // the file's entries go back as they are; one that can't be read isn't written over
        std::vector<uint8_t> bytes;
        picojson::value root;
        error = ReadWhole(list, MAX_LIST_FILE, bytes) ? ParseJson(std::string(bytes.begin(), bytes.end()), root) : std::string("can't be read");
        if (error.empty() && !root.is<picojson::array>())
            error = "not a list";
        if (!error.empty())
            return std::nullopt;
        items = root.get<picojson::array>();
    }
    std::string id = Layouts::NewId();
    picojson::object entry;
    entry["id"] = picojson::value(id);
    entry["name"] = picojson::value(PathToUtf8(file.stem()));
    entry["uri"] = picojson::value(PathToUtf8(fs::absolute(file, ec)));
    items.push_back(picojson::value(entry));
    fs::path temp = list;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << picojson::value(items).serialize();
        out.close();
        if (out.fail())
        {
            fs::remove(temp, ec);
            error = "can't write " + PathToUtf8(temp);
            return std::nullopt;
        }
    }
    if (!MoveFileExW(temp.c_str(), list.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        fs::remove(temp, ec);
        error = "can't replace " + PathToUtf8(list);
        return std::nullopt;
    }
    printf("backgrounds: added %s as %s\n", PathToUtf8(file).c_str(), id.c_str());
    Load(Dir);
    return id;
}

void Backgrounds::Load(const fs::path& profileDir)
{
    Dir = profileDir;
    Entries.clear();
    SvgCache.clear();
    std::vector<const EmbeddedBackgroundFile*> files;
    for (const char* name : kBuiltInOrder)
    {
        for (const EmbeddedBackgroundFile& f : EMBEDDED_BACKGROUND_FILES)
        {
            if (strcmp(f.Name, name) == 0)
                files.push_back(&f);
        }
    }
    for (const EmbeddedBackgroundFile& f : EMBEDDED_BACKGROUND_FILES)
    {
        if (std::find(files.begin(), files.end(), &f) == files.end())
            files.push_back(&f);
    }
    for (const EmbeddedBackgroundFile* f : files)
    {
        Entry e;
        e.Id = NameUuid(std::string("melonade-builtin-background:") + f->Name);
        e.Name = DisplayName(f->Name);
        e.BuiltIn = true;
        e.Data = f->Data;
        e.Size = f->Size;
        e.FileName = f->Name;
        if (!e.Id.empty())
            Entries.push_back(std::move(e));
    }
    size_t builtIn = Entries.size();

    // the user's, as the app's backgrounds.json has them (a user one with a built-in's id is left out, as BuiltInBackgrounds.merge does)
    fs::path list = profileDir / "backgrounds.json";
    std::error_code ec;
    if (!profileDir.empty() && fs::is_regular_file(list, ec))
    {
        std::vector<uint8_t> bytes;
        picojson::value root;
        std::string error = ReadWhole(list, MAX_LIST_FILE, bytes) ? ParseJson(std::string(bytes.begin(), bytes.end()), root)
                                                                  : std::string("can't be read");
        if (error.empty() && !root.is<picojson::array>())
            error = "not a list";
        if (!error.empty())
            fprintf(stderr, "backgrounds: %s: %s\n", PathToUtf8(list).c_str(), error.c_str());
        else
        {
            for (const picojson::value& item : root.get<picojson::array>())
            {
                if (Entries.size() - builtIn >= MAX_USER_BACKGROUNDS)
                    break;
                if (!item.is<picojson::object>())
                    continue;
                const picojson::object& o = item.get<picojson::object>();
                auto text = [&](const char* key) -> std::string
                {
                    auto it = o.find(key);
                    return it != o.end() && it->second.is<std::string>() ? it->second.get<std::string>() : std::string();
                };
                Entry e;
                e.Id = Lower(text("id"));
                e.Name = text("name");
                e.Path = UriPath(text("uri"), profileDir);
                if (e.Id.empty() || Find(e.Id))
                    continue;
                Entries.push_back(std::move(e));
            }
        }
    }
    printf("backgrounds: %zu built in, %zu of the user's\n", builtIn, Entries.size() - builtIn);
}

const Backgrounds::Entry* Backgrounds::Find(const std::string& id) const
{
    std::string wanted = Lower(id);
    for (const Entry& e : Entries)
    {
        if (e.Id == wanted)
            return &e;
    }
    return nullptr;
}

bool Backgrounds::ResolveArgument(const std::string& text, std::string& key, std::string& error) const
{
    std::string wanted = Lower(text);
    if (wanted == "none")
    {
        key.clear();
        return true;
    }
    for (const Entry& e : Entries)
    {
        if (e.Id == wanted || Lower(e.Name) == wanted || (!e.FileName.empty() && (Lower(e.FileName) == wanted || Lower(e.FileName) == wanted + ".svg")))
        {
            key = e.Id;
            return true;
        }
    }
    std::error_code ec;
    fs::path path = PathFromUtf8(text);
    if (fs::is_regular_file(path, ec))
    {
        key = "file:" + PathToUtf8(fs::absolute(path, ec));
        return true;
    }
    error = "no background or image file called " + text + " (the built-in ones:";
    for (const Entry& e : Entries)
    {
        if (e.BuiltIn)
            error += " " + Lower(e.FileName.substr(0, e.FileName.size() - 4));
    }
    error += ")";
    return false;
}

std::string Backgrounds::NameOf(const std::string& key) const
{
    if (key.empty())
        return "none";
    if (key.rfind("file:", 0) == 0)
        return key.substr(5);
    const Entry* e = Find(key);
    return e ? e->Name : key + " (not found)";
}

bool Backgrounds::Source(const std::string& key, BackgroundSource& out, std::string& error) const
{
    out = BackgroundSource();
    out.Name = NameOf(key);
    const Entry* entry = key.rfind("file:", 0) == 0 ? nullptr : Find(key);
    if (entry && entry->BuiltIn)
    {
        out.Data = entry->Data;
        out.Size = entry->Size;
        out.IsSvg = true;
        return true;
    }
    if (!entry && key.rfind("file:", 0) != 0)
    {
        error = "no such background";
        return false;
    }
    auto it = SvgCache.find(key);
    if (it != SvgCache.end())
    {
        out.Bytes = it->second;
        out.Data = out.Bytes->data();
        out.Size = out.Bytes->size();
        out.IsSvg = true;
        return true;
    }
    out.Path = entry ? entry->Path : PathFromUtf8(key.substr(5));
    if (out.Path.empty())
    {
        error = "its uri isn't a file (one from the phone?)";
        return false;
    }
    return true;
}

void Backgrounds::CacheSvg(const std::string& key, std::shared_ptr<const std::vector<uint8_t>> bytes)
{
    if (bytes)
        SvgCache[key] = std::move(bytes);
}

void Backgrounds::Report(const std::string& key, const std::string& error)
{
    if (Reported[key])
        return;
    Reported[key] = true;
    fprintf(stderr, "background %s: %s\n", NameOf(key).c_str(), error.c_str());
}

namespace
{

// a user's file read into [source] and told apart: an image by its PNG, JPEG, GIF or BMP signature first (a photo's XMP can have "<svg" in
// it), else an SVG by its extension or its start; an SVG over RasterizeSvg's 16 MB is refused before it's kept
bool ReadSource(BackgroundSource& source, std::string& error)
{
    std::error_code ec;
    uintmax_t size = fs::file_size(source.Path, ec);
    std::string extension = PathToUtf8(source.Path.extension());
    for (char& c : extension)
        c = (char)std::tolower((unsigned char)c);
    if (ec || size > MAX_IMAGE_FILE || (extension == ".svg" && size > MAX_SVG_FILE))
    {
        error = ec ? "can't read " + PathToUtf8(source.Path) : "bigger than " + std::to_string((extension == ".svg" ? MAX_SVG_FILE : MAX_IMAGE_FILE) >> 20) + " MB";
        return false;
    }
    auto data = std::make_shared<std::vector<uint8_t>>();
    if (!ReadWhole(source.Path, MAX_IMAGE_FILE, *data))
    {
        error = "can't read " + PathToUtf8(source.Path);
        return false;
    }
    source.IsSvg = !IsRasterImage(data->data(), data->size()) && (extension == ".svg" || LooksLikeSvg(data->data(), data->size()));
    if (source.IsSvg && data->size() > MAX_SVG_FILE)
    {
        error = "an SVG bigger than " + std::to_string(MAX_SVG_FILE >> 20) + " MB";
        return false;
    }
    source.Bytes = std::move(data);
    source.Data = source.Bytes->data();
    source.Size = source.Bytes->size();
    return true;
}

// the worker's part: the file read if it has to be, then the decode; the result in [job], then Done
void RunDecode(BackgroundSource source, int surfaceWidth, int surfaceHeight, BackgroundMode mode, int maxSize, BackgroundJob& job)
{
    bool read = !source.Data && !source.Path.empty();
    if (read && !ReadSource(source, job.Error))
        job.Ok = false;
    else if (source.IsSvg)
    {
        std::string text((const char*)source.Data, source.Size);
        job.Image.IsSvg = true;
        job.Ok = RasterizeSvg(text, surfaceWidth, surfaceHeight, mode, maxSize, job.Image.Rgba, job.Image.Width, job.Image.Height, job.Error,
            &job.Cancel);
        if (read)
            job.SvgBytes = source.Bytes;
    }
    else
        job.Ok = DecodeRaster(source.Data, source.Size, surfaceWidth, surfaceHeight, maxSize, job.Image, job.Error, &job.Cancel);
    job.Cancelled = !job.Ok && job.Cancel.load(std::memory_order_relaxed);
    job.Done.store(true, std::memory_order_release);
}

}

std::shared_ptr<BackgroundJob> StartBackgroundDecode(BackgroundSource source, int surfaceWidth, int surfaceHeight, BackgroundMode mode, int maxSize)
{
    auto job = std::make_shared<BackgroundJob>();
    try
    {
        // detached: it holds its own share of the job, so nothing ever waits for it to end
        std::thread([source, surfaceWidth, surfaceHeight, mode, maxSize, job] { RunDecode(source, surfaceWidth, surfaceHeight, mode, maxSize, *job); })
            .detach();
    }
    catch (const std::system_error&)
    {
        // no thread for it: here, then
        RunDecode(std::move(source), surfaceWidth, surfaceHeight, mode, maxSize, *job);
    }
    return job;
}

}
