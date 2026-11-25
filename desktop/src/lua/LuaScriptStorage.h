// Reads a game's Lua scripts, their headers, modules and images from its linked folder, ported from the app's impl/LuaScriptStorage.kt
// (listScripts, readScripts, readHeader and readFolderContent) over std::filesystem instead of the Storage Access Framework. The app never
// writes to the folder and native never opens script files. Nothing here logs: what the app logs comes back in the results (the read
// results, the left-out reasons, FolderContent::GaveUp) for the caller to log
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "lua/LuaScriptFolderContent.h"
#include "lua/LuaScriptOption.h"

namespace Frontend::LuaScriptStorage
{

constexpr int MAX_SCRIPT_SIZE = 1024 * 1024;

// a .lua file at the top of a linked folder
struct ScriptFile
{
    std::string Name;
    std::filesystem::path Path;
};

struct ReadResult
{
    enum class Kind
    {
        Success,
        FolderNotFound,
        FileNotFound,
        TooLarge,
        Failed,
    };
    Kind ResultKind = Kind::Failed;
    // the script, for Success
    std::string Source;
};

// the result's name, as the app logs it ("Skipping Lua script x.lua: FileNotFound")
const char* ResultName(ReadResult::Kind kind);

// top-level .lua files, unordered, or none when the folder can't be read (missing, not a folder, or the listing failed)
std::optional<std::vector<ScriptFile>> ListScripts(const std::filesystem::path& folder);

// reads the given scripts in order from [folder], listing it once; with no folder every result is FolderNotFound, and so with an empty
// listing, which is taken as a hidden folder (see LuaScriptFolderSync::Sync)
std::vector<std::pair<std::string, ReadResult>> ReadScripts(const std::optional<std::filesystem::path>& folder,
    const std::vector<std::string>& fileNames);

// one script file by its path, outside any linked folder (the command line's --lua-script): FileNotFound when it isn't a file
ReadResult ReadScriptFile(const std::filesystem::path& file);

// the script's header from the start of the file, or none if it can't be read
std::optional<LuaScriptHeader> ReadHeader(const std::filesystem::path& scriptFile);

// up to [limit] bytes from the start of [file], or none if it can't be opened or read
std::optional<std::string> ReadAtMost(const std::filesystem::path& file, size_t limit);

// one folder's entries for LuaScriptFolderContent::Walk, by path; none when it can't be listed
std::optional<std::vector<LuaScriptFolderContent::Child<std::filesystem::path>>> ListChildren(const std::filesystem::path& folder);

// A module scripts can require by [Name], with its [Source] or none and the reason it was left out (empty when it wasn't)
struct FolderModule
{
    std::string Name;
    std::string Path;
    std::optional<std::string> Source;
    std::string LeftOutReason;
};

// An image scripts can load by [Path], as unpremultiplied 0xAARRGGBB [Pixels] (Width x Height, top row first), or none with the reason it
// was left out. MelonDSAndroid::LuaFolderContent wants its pixels premultiplied (premultiplyColor, as LuaScriptingJNI.cpp does)
struct FolderImage
{
    std::string Path;
    int Width = 0;
    int Height = 0;
    std::vector<uint32_t> Pixels;
    std::string LeftOutReason;
};

struct FolderContent
{
    std::vector<FolderModule> Modules;
    std::vector<FolderImage> Images;
    // a cap on folders or files stopped the walk, so some were left out (the app logs a warning)
    bool GaveUp = false;
};

// How images are read: [ReadSize] gives the image [file]'s size from its header without decoding it, and [Decode] decodes it into
// unpremultiplied 0xAARRGGBB [pixels], width x height of them; each false if it isn't an image it can read
struct ImageDecoder
{
    std::function<bool(const std::filesystem::path& file, int& width, int& height)> ReadSize;
    std::function<bool(const std::filesystem::path& file, int& width, int& height, std::vector<uint32_t>& pixels)> Decode;
};

// Reads the scripts' modules and decodes their images with [decoder] (rules in LuaScriptFolderContent; an image's size is read first, so
// one over the caps is never decoded), or none if the folder can't be read; it lists up to 65 folders and reads a few MB, so not on the UI
// or emulator thread's frame loop
std::optional<FolderContent> ReadFolderContent(const std::filesystem::path& folder, const ImageDecoder& decoder);

}
