#include "lua/LuaScriptStorage.h"

#include <algorithm>
#include <fstream>
#include <map>

#include "lua/LuaScriptFolderSync.h"
#include "lua/LuaScriptHeaderParser.h"
#include "lua/LuaText.h"

namespace fs = std::filesystem;

namespace Frontend::LuaScriptStorage
{

namespace
{

ReadResult ReadScript(const ScriptFile& scriptFile)
{
    // One byte over the limit is enough to know the file is too large, so a huge file is never read whole
    std::optional<std::string> content = ReadAtMost(scriptFile.Path, (size_t)MAX_SCRIPT_SIZE + 1);
    if (!content)
        return {ReadResult::Kind::Failed, {}};
    if (content->size() > (size_t)MAX_SCRIPT_SIZE)
        return {ReadResult::Kind::TooLarge, {}};
    return {ReadResult::Kind::Success, std::move(*content)};
}

FolderModule ReadModule(const LuaScriptFolderContent::Module<fs::path>& module, LuaScriptFolderContent::ModuleBudget& budget)
{
    const LuaScriptFolderContent::FoundFile<fs::path>& file = module.File;
    if (const char* reason = budget.CheckBeforeRead(file.Size))
        return {module.Name, file.Path, std::nullopt, reason};

    std::optional<std::string> source = ReadAtMost(file.Handle, (size_t)budget.ReadLimit());
    if (!source)
        return {module.Name, file.Path, std::nullopt, LuaScriptFolderContent::REASON_UNREADABLE};
    if (const char* reason = budget.Take((int64_t)source->size()))
        return {module.Name, file.Path, std::nullopt, reason};
    return {module.Name, file.Path, std::move(*source), ""};
}

// Reads the image's size first, so one over the caps is never decoded, then decodes it, as the app's decodeImage does
FolderImage DecodeImage(const LuaScriptFolderContent::FoundFile<fs::path>& file, LuaScriptFolderContent::ImageBudget& budget,
    const ImageDecoder& decoder)
{
    FolderImage image;
    image.Path = file.Path;
    if (const char* reason = budget.CheckBeforeDecode())
    {
        image.LeftOutReason = reason;
        return image;
    }

    int width = 0, height = 0;
    if (!decoder.ReadSize || !decoder.ReadSize(file.Handle, width, height) || width <= 0 || height <= 0)
    {
        image.LeftOutReason = LuaScriptFolderContent::REASON_NOT_AN_IMAGE;
        return image;
    }
    if (const char* reason = budget.Take(width, height))
    {
        image.LeftOutReason = reason;
        return image;
    }

    int decodedWidth = 0, decodedHeight = 0;
    std::vector<uint32_t> pixels;
    bool isDecoded = decoder.Decode && decoder.Decode(file.Handle, decodedWidth, decodedHeight, pixels);
    // the file could have changed since its size was read, and the caps were checked against that size
    if (!isDecoded || decodedWidth != width || decodedHeight != height || (int64_t)pixels.size() != (int64_t)width * height)
    {
        image.LeftOutReason = LuaScriptFolderContent::REASON_NOT_AN_IMAGE;
        return image;
    }
    image.Width = width;
    image.Height = height;
    image.Pixels = std::move(pixels);
    return image;
}

}

const char* ResultName(ReadResult::Kind kind)
{
    switch (kind)
    {
    case ReadResult::Kind::Success: return "Success";
    case ReadResult::Kind::FolderNotFound: return "FolderNotFound";
    case ReadResult::Kind::FileNotFound: return "FileNotFound";
    case ReadResult::Kind::TooLarge: return "TooLarge";
    case ReadResult::Kind::Failed: return "Failed";
    }
    return "Failed";
}

std::optional<std::vector<LuaScriptFolderContent::Child<fs::path>>> ListChildren(const fs::path& folder)
{
    std::vector<LuaScriptFolderContent::Child<fs::path>> children;
    std::error_code error;
    // a listing that fails partway isn't the folder's content, so it counts as unreadable
    for (fs::directory_iterator entry(folder, error), end; !error && entry != end; entry.increment(error))
    {
        std::error_code entryError;
        bool isFolder = entry->is_directory(entryError);
        std::optional<int64_t> size;
        if (!isFolder && entry->is_regular_file(entryError))
        {
            uintmax_t fileSize = entry->file_size(entryError);
            if (!entryError)
                size = (int64_t)fileSize;
        }
        children.push_back({LuaText::PathToUtf8(entry->path().filename()), isFolder, size, entry->path()});
    }
    if (error)
        return std::nullopt;
    return children;
}

std::optional<std::vector<ScriptFile>> ListScripts(const fs::path& folder)
{
    // checked first, because listing a file or a missing folder would fail anyway but says less
    std::error_code error;
    if (!fs::is_directory(folder, error))
        return std::nullopt;
    std::optional<std::vector<LuaScriptFolderContent::Child<fs::path>>> children = ListChildren(folder);
    if (!children)
        return std::nullopt;
    std::vector<ScriptFile> scripts;
    for (LuaScriptFolderContent::Child<fs::path>& child : *children)
    {
        if (!child.IsFolder && LuaScriptFolderContent::IsLuaScriptName(child.Name))
            scripts.push_back({std::move(child.Name), std::move(child.Handle)});
    }
    return scripts;
}

std::vector<std::pair<std::string, ReadResult>> ReadScripts(const std::optional<fs::path>& folder, const std::vector<std::string>& fileNames)
{
    std::optional<std::map<std::string, ScriptFile>> scriptFiles;
    std::optional<std::vector<ScriptFile>> listed = folder ? ListScripts(*folder) : std::nullopt;
    if (listed)
    {
        std::vector<std::string> names;
        for (const ScriptFile& script : *listed)
            names.push_back(script.Name);
        if (!LuaScriptFolderSync::IsSuspiciouslyEmpty(fileNames.size(), names))
        {
            scriptFiles.emplace();
            for (ScriptFile& script : *listed)
                (*scriptFiles)[script.Name] = std::move(script);
        }
    }

    std::vector<std::pair<std::string, ReadResult>> results;
    for (const std::string& fileName : fileNames)
    {
        ReadResult result {ReadResult::Kind::FolderNotFound, {}};
        if (scriptFiles)
        {
            auto found = scriptFiles->find(fileName);
            result = found != scriptFiles->end() ? ReadScript(found->second) : ReadResult {ReadResult::Kind::FileNotFound, {}};
        }
        results.emplace_back(fileName, std::move(result));
    }
    return results;
}

ReadResult ReadScriptFile(const fs::path& file)
{
    std::error_code error;
    if (!fs::is_regular_file(file, error))
        return {ReadResult::Kind::FileNotFound, {}};
    return ReadScript({LuaText::PathToUtf8(file.filename()), file});
}

std::optional<LuaScriptHeader> ReadHeader(const fs::path& scriptFile)
{
    // One byte more than the header limit, so the parser knows the file goes on
    std::optional<std::string> start = ReadAtMost(scriptFile, (size_t)LuaScriptHeaderParser::READ_LIMIT + 1);
    if (!start)
        return std::nullopt;
    return LuaScriptHeaderParser::Parse(*start);
}

std::optional<std::string> ReadAtMost(const fs::path& file, size_t limit)
{
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open())
        return std::nullopt;
    std::string data;
    char buffer[8192];
    while (data.size() < limit)
    {
        in.read(buffer, (std::streamsize)std::min(sizeof(buffer), limit - data.size()));
        data.append(buffer, (size_t)in.gcount());
        if (!in)
            break;
    }
    if (in.bad())
        return std::nullopt;
    return data;
}

std::optional<FolderContent> ReadFolderContent(const fs::path& folder, const ImageDecoder& decoder)
{
    std::error_code error;
    if (!fs::is_directory(folder, error))
        return std::nullopt;
    auto walk = LuaScriptFolderContent::Walk(folder, [](const fs::path& path) { return ListChildren(path); });
    if (!walk)
        return std::nullopt;

    FolderContent content;
    content.GaveUp = walk->GaveUp;
    LuaScriptFolderContent::ModuleBudget moduleBudget;
    for (const LuaScriptFolderContent::Module<fs::path>& module : walk->Modules)
        content.Modules.push_back(ReadModule(module, moduleBudget));
    LuaScriptFolderContent::ImageBudget imageBudget;
    for (const LuaScriptFolderContent::FoundFile<fs::path>& file : walk->Images)
        content.Images.push_back(DecodeImage(file, imageBudget, decoder));
    return content;
}

}
