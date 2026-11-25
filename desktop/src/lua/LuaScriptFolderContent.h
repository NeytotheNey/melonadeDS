// Rules for which files in a linked folder are scripts, and for the modules (require) and PNG images (melon.image.load) it offers them,
// ported from the app's impl/LuaScriptFolderContent.kt: the folder walk with its caps and name rules, kept apart from the file system (LuaScriptStorage.h lists
// and reads the real folder) so the tests can walk a made-up one, and the budgets that leave out modules and images past the caps. Paths
// and names are UTF-8, with `/` between a path's parts
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lua/LuaScriptHeaderParser.h"

namespace Frontend::LuaScriptFolderContent
{

constexpr int MAX_DEPTH = 4;
constexpr int MAX_FOLDERS = 64;
// modules and images the walk finds before it gives up, so a folder with thousands of files can't hold up the launch
constexpr int MAX_FOUND_FILES = 4096;
// subfolder .lua files whose first line a listing reads to tell scripts from modules
constexpr int MAX_SUBFOLDER_SCRIPT_CANDIDATES = 256;

constexpr int MEGA = 1024 * 1024;

constexpr int MAX_MODULE_SIZE = MEGA;
constexpr int MAX_MODULES_SIZE = 2 * MEGA;
constexpr int MAX_MODULE_COUNT = 256;

constexpr int MAX_IMAGE_SIDE = 1024;
constexpr int MAX_IMAGE_COUNT = 128;
constexpr int MAX_IMAGE_PIXELS = 4 * MEGA;

// shown after "module 'x' wasn't loaded: " and "image 'x' wasn't loaded: "; the numbers are the caps above
constexpr const char* REASON_MODULE_TOO_LARGE = "modules are limited to 1 MB each";
constexpr const char* REASON_MODULES_TOO_LARGE = "modules are limited to 2 MB in total";
constexpr const char* REASON_TOO_MANY_MODULES = "modules are limited to 256 files";
constexpr const char* REASON_IMAGE_TOO_LARGE = "images are limited to 1024x1024 pixels each";
constexpr const char* REASON_IMAGES_TOO_LARGE = "images are limited to 4M pixels in total";
constexpr const char* REASON_TOO_MANY_IMAGES = "images are limited to 128 files";
constexpr const char* REASON_UNREADABLE = "the file couldn't be read";
constexpr const char* REASON_NOT_AN_IMAGE = "the file couldn't be read as an image";

constexpr const char* LUA_EXTENSION = ".lua";

bool IsNamePartAllowed(std::string_view part);
// whether [fileName] is a script or module file
bool IsLuaScriptName(std::string_view fileName);
// the module name require uses for [path] (lib/util.lua is lib.util, lib/init.lua is lib), or none for a top-level script or a path part
// outside [A-Za-z0-9_-]
std::optional<std::string> GetModuleName(std::string_view path);
// whether the file at [path] is an image scripts can load
bool IsImagePath(std::string_view path);
// whether [path]'s file is a folder's init.lua
bool IsInitModulePath(std::string_view path);

// NAME_ORDER, how scripts, modules and images are ordered by name: case-insensitive, then by exact name for ties (<0, 0 or >0)
int CompareNames(std::string_view a, std::string_view b);
inline bool NameLess(std::string_view a, std::string_view b)
{
    return CompareNames(a, b) < 0;
}

// an entry of a folder listing, with whatever [Handle] the lister needs to list or read it later; [Size] when the listing knows it
template <class T>
struct Child
{
    std::string Name;
    bool IsFolder = false;
    std::optional<int64_t> Size;
    T Handle;
};

// a script, module or image found by Walk, with its [Path] relative to the linked folder
template <class T>
struct FoundFile
{
    std::string Path;
    std::optional<int64_t> Size;
    T Handle;
};

template <class T>
struct Module
{
    std::string Name;
    FoundFile<T> File;
};

// What Walk found, each list in path order: the modules and images, the top-level .lua files (always scripts), and every .lua file in the
// subfolders it listed (scripts only if their first line says so, see ListScripts). [GaveUp] is set when a cap stopped it early and
// [SkippedFolder] when a subfolder couldn't be listed
template <class T>
struct WalkResult
{
    std::vector<Module<T>> Modules;
    std::vector<FoundFile<T>> Images;
    std::vector<FoundFile<T>> TopLevelScripts;
    std::vector<FoundFile<T>> ScriptCandidates;
    bool GaveUp = false;
    bool SkippedFolder = false;
};

// the scripts a listing found, in path order, with [IsComplete] false when some could be missing (see ListScripts)
template <class T>
struct ScriptListing
{
    std::vector<FoundFile<T>> Scripts;
    bool IsComplete = true;
};

template <class T>
void SortByPath(std::vector<FoundFile<T>>& files)
{
    std::stable_sort(files.begin(), files.end(), [](const FoundFile<T>& a, const FoundFile<T>& b) { return NameLess(a.Path, b.Path); });
}

// one file per module name: x.lua wins over x/init.lua like in Lua's own search order, otherwise the first in path order wins
template <class T>
std::vector<Module<T>> PickModules(std::vector<Module<T>> modules)
{
    std::stable_sort(modules.begin(), modules.end(), [](const Module<T>& a, const Module<T>& b) {
        bool aIsInit = IsInitModulePath(a.File.Path), bIsInit = IsInitModulePath(b.File.Path);
        if (aIsInit != bIsInit)
            return !aIsInit;
        return NameLess(a.File.Path, b.File.Path);
    });
    std::vector<Module<T>> picked;
    std::set<std::string> names;
    for (Module<T>& module : modules)
    {
        if (names.insert(module.Name).second)
            picked.push_back(std::move(module));
    }
    return picked;
}

// Walks breadth first from [root], at most MAX_DEPTH deep and MAX_FOLDERS folders. [listChildren] takes a handle and gives its folder's
// std::optional<std::vector<Child<T>>>: a folder it can't read (none) is skipped, an unreadable root returns none. A subfolder .lua file in
// [scriptPaths] (the known scripts) isn't a module and doesn't count toward MAX_FOUND_FILES
template <class T, class ListChildren>
std::optional<WalkResult<T>> Walk(const T& root, ListChildren&& listChildren, const std::set<std::string>& scriptPaths = {})
{
    std::optional<std::vector<Child<T>>> rootChildren = listChildren(root);
    if (!rootChildren)
        return std::nullopt;

    WalkResult<T> result;
    // folders waiting to be listed, with their path and depth (1 for the root's subfolders)
    struct Folder
    {
        T Handle;
        std::string Path;
        int Depth;
    };
    std::deque<Folder> folders;
    int listedFolderCount = 0;

    auto addChildren = [&](std::vector<Child<T>>& children, const std::string& folderPath, int depth) {
        // Some providers list two documents with the same name, and only the first one counts
        std::vector<Child<T>*> distinct;
        std::set<std::string> names;
        for (Child<T>& child : children)
        {
            if (names.insert(child.Name).second)
                distinct.push_back(&child);
        }
        std::stable_sort(distinct.begin(), distinct.end(), [](const Child<T>* a, const Child<T>* b) { return NameLess(a->Name, b->Name); });

        for (Child<T>* child : distinct)
        {
            std::string path = folderPath.empty() ? child->Name : folderPath + "/" + child->Name;
            if (child->IsFolder)
            {
                if (depth < MAX_DEPTH && IsNamePartAllowed(child->Name))
                    folders.push_back({child->Handle, path, depth + 1});
                continue;
            }

            if (IsLuaScriptName(child->Name))
            {
                FoundFile<T> scriptFile {path, child->Size, child->Handle};
                if (depth == 0)
                {
                    result.TopLevelScripts.push_back(std::move(scriptFile));
                    continue;
                }
                result.ScriptCandidates.push_back(std::move(scriptFile));
                if (scriptPaths.count(path))
                    continue;
            }

            std::optional<std::string> moduleName = depth > 0 ? GetModuleName(path) : std::nullopt;
            bool isImage = !moduleName && IsImagePath(path);
            if (!moduleName && !isImage)
                continue;
            // The rest of the folder is still gone through for its scripts
            if (result.Modules.size() + result.Images.size() >= (size_t)MAX_FOUND_FILES)
            {
                result.GaveUp = true;
                continue;
            }
            FoundFile<T> file {path, child->Size, child->Handle};
            if (moduleName)
                result.Modules.push_back({std::move(*moduleName), std::move(file)});
            else
                result.Images.push_back(std::move(file));
        }
    };

    addChildren(*rootChildren, "", 0);
    while (!result.GaveUp && !folders.empty())
    {
        if (listedFolderCount >= MAX_FOLDERS)
        {
            result.GaveUp = true;
            break;
        }
        Folder folder = std::move(folders.front());
        folders.pop_front();
        listedFolderCount++;
        std::optional<std::vector<Child<T>>> children = listChildren(folder.Handle);
        if (!children)
            result.SkippedFolder = true;
        else
            addChildren(*children, folder.Path, folder.Depth);
    }

    result.Modules = PickModules(std::move(result.Modules));
    std::stable_sort(result.Modules.begin(), result.Modules.end(),
        [](const Module<T>& a, const Module<T>& b) { return NameLess(a.File.Path, b.File.Path); });
    SortByPath(result.Images);
    SortByPath(result.TopLevelScripts);
    SortByPath(result.ScriptCandidates);
    return result;
}

// The folder's scripts, or none when the root can't be listed: every top-level .lua file, and each subfolder one whose first line is a
// description (see LuaScriptHeaderParser::StartsWithDescription), read with [readStart] (a FoundFile<T> to std::optional<std::string>, none
// when the file can't be read). At most MAX_SUBFOLDER_SCRIPT_CANDIDATES subfolder files are read, in path order. The listing is incomplete
// when the walk gave up or skipped a folder, there were more subfolder files than that, or one of them couldn't be read
template <class T, class ListChildren, class ReadStart>
std::optional<ScriptListing<T>> ListScripts(const T& root, ListChildren&& listChildren, ReadStart&& readStart)
{
    std::optional<WalkResult<T>> walk = Walk(root, listChildren);
    if (!walk)
        return std::nullopt;

    ScriptListing<T> listing;
    listing.Scripts = walk->TopLevelScripts;
    listing.IsComplete = !walk->GaveUp && !walk->SkippedFolder && walk->ScriptCandidates.size() <= (size_t)MAX_SUBFOLDER_SCRIPT_CANDIDATES;
    size_t candidateCount = std::min(walk->ScriptCandidates.size(), (size_t)MAX_SUBFOLDER_SCRIPT_CANDIDATES);
    for (size_t i = 0; i < candidateCount; i++)
    {
        const FoundFile<T>& candidate = walk->ScriptCandidates[i];
        std::optional<std::string> start = readStart(candidate);
        if (!start)
            listing.IsComplete = false;
        else if (LuaScriptHeaderParser::StartsWithDescription(*start))
            listing.Scripts.push_back(candidate);
    }
    SortByPath(listing.Scripts);
    return listing;
}

// Takes modules in path order until a cap is hit; after the first one that doesn't fit, every later one is left out. The reasons are the
// REASON_ constants, nullptr when the module is taken
class ModuleBudget
{
public:
    // why the next module is left out before reading it (given its [listedSize] from the folder listing, if known), or nullptr to read it
    const char* CheckBeforeRead(std::optional<int64_t> listedSize);
    // the most bytes to read of a module: one past what could still be taken, so a module that doesn't fit is never read whole
    int ReadLimit() const;
    // takes a module of [size] bytes (read with ReadLimit), returning nullptr if it fits or why it doesn't
    const char* Take(int64_t size);

private:
    int ConsideredCount = 0;
    int64_t BytesLeft = MAX_MODULES_SIZE;
    bool IsFull = false;
};

// Takes images in path order until a cap is hit, like ModuleBudget, with CheckBeforeDecode before the size is known and Take after
class ImageBudget
{
public:
    const char* CheckBeforeDecode();
    // takes an image of [width] x [height] pixels, returning nullptr if it fits or why it doesn't
    const char* Take(int width, int height);

private:
    int ConsideredCount = 0;
    int64_t PixelsLeft = MAX_IMAGE_PIXELS;
    bool IsFull = false;
};

}
