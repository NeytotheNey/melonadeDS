// The backgrounds a layout can show (its ScreenLayout.backgroundId), as the app's InternalBackgroundsRepository has them: the built-in ones
// first (the app's SVGs, app/src/main/assets/backgrounds, built into the exe; BuiltInBackgrounds.kt's order, names and ids, which are
// nameUUIDFromBytes of "melonade-builtin-background:<file>" so a phone's layout finds the same one), then the user's from backgrounds.json in
// the profile folder (the app's file: id, name, uri; here the uri is a file path or a file:// URI, while a phone's content:// ones can't
// be opened and show nothing). And their pixels for a surface: an SVG rasterized for its size (SvgBackground.h), a PNG, JPEG, BMP or GIF
// decoded with stb_image and shrunk by halves as the app's BitmapFactory inSampleSize does, on a worker thread of the decode's own
// (StartBackgroundDecode), which reads the file too. The list and the SVG cache are the UI thread's
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "layout/LayoutModel.h"

namespace Frontend
{

// what a background's pixels are made from, gathered on the UI thread and handed to the decode's worker as its own copy: a built-in one's
// SVG (in the exe), a user's SVG already read (Backgrounds' cache, shared and never changed), or a file the worker reads
struct BackgroundSource
{
    std::string Name;
    bool IsSvg = false;
    const unsigned char* Data = nullptr;
    size_t Size = 0;
    std::shared_ptr<const std::vector<uint8_t>> Bytes;
    std::filesystem::path Path;
};

// premultiplied RGBA rows, top row first
struct BackgroundImage
{
    std::vector<uint8_t> Rgba;
    int Width = 0, Height = 0;
    // an SVG, which is rasterized again when the surface's size changes (a raster image is only placed again)
    bool IsSvg = false;
};

// A decode running on a worker thread of its own. The worker and the renderer each hold the job, so either can let go first: the renderer
// never waits for the worker (it sets Cancel and drops the job; the worker stops at its next step). The worker only touches its own copy of
// the source and this job's result, written before Done is set (release) and read once it's seen set (acquire)
struct BackgroundJob
{
    std::atomic<bool> Cancel {false};
    std::atomic<bool> Done {false};
    BackgroundImage Image;
    bool Ok = false;
    bool Cancelled = false;
    std::string Error;
    // a user's SVG the worker read, for the UI thread to keep
    std::shared_ptr<const std::vector<uint8_t>> SvgBytes;
};

class Backgrounds
{
public:
    struct Entry
    {
        std::string Id, Name;
        bool BuiltIn = false;
        // a built-in one's SVG, in the exe
        const unsigned char* Data = nullptr;
        size_t Size = 0;
        std::string FileName;
        // a user's one's file (empty when its uri isn't a file)
        std::filesystem::path Path;
    };

    // the built-in backgrounds, and [profileDir]'s backgrounds.json
    void Load(const std::filesystem::path& profileDir);
    // [file] added to backgrounds.json as the user's background (named for the file, with a new id, the path as its uri; the entries there
    // kept as they are), and the list read again: its id, or none with [error] when backgrounds.json can't be read or written
    std::optional<std::string> AddUserFile(const std::filesystem::path& file, std::string& error);
    const std::vector<Entry>& All() const { return Entries; }
    const Entry* Find(const std::string& id) const;

    // --background: "none", a background's name (any case; a built-in's file name too, "pearl-tech"), its id, or an image file. [key] is
    // what a surface's background is then: an id, "file:<path>" for a file, or empty for none. False with [error] for one it can't find
    bool ResolveArgument(const std::string& text, std::string& key, std::string& error) const;
    // the name shown for a key
    std::string NameOf(const std::string& key) const;

    // what [key]'s pixels are made from, without reading anything (a user's file is read by the decode's worker); false with [error] when
    // there's no such background or its uri isn't a file
    bool Source(const std::string& key, BackgroundSource& out, std::string& error) const;
    // a user's SVG a decode read, kept for the next window size
    void CacheSvg(const std::string& key, std::shared_ptr<const std::vector<uint8_t>> bytes);
    // a background that couldn't be shown, logged once per key
    void Report(const std::string& key, const std::string& error);

private:
    std::filesystem::path Dir;
    std::vector<Entry> Entries;
    // the user's SVG files read so far, by key, so a resize doesn't read them again
    std::map<std::string, std::shared_ptr<const std::vector<uint8_t>>> SvgCache;
    std::map<std::string, bool> Reported;
};

// [source] decoded for a [surfaceWidth] x [surfaceHeight] surface showing it with [mode], no side over [maxSize], on a worker thread (or here
// when no thread can be made): a file read (at most 256 MB, an SVG at most 16 MB) and taken as an image by its signature first, an SVG
// rasterized for the surface, an image decoded and shrunk by halves. Never blocks
std::shared_ptr<BackgroundJob> StartBackgroundDecode(BackgroundSource source, int surfaceWidth, int surfaceHeight, BackgroundMode mode, int maxSize);

}
