#include "lua/LuaScriptFolderContent.h"

#include "lua/LuaText.h"

namespace Frontend::LuaScriptFolderContent
{

namespace
{

constexpr std::string_view PNG_EXTENSION = ".png";
constexpr std::string_view INIT_MODULE = "init";

std::vector<std::string_view> SplitPath(std::string_view path)
{
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true)
    {
        size_t slash = path.find('/', start);
        parts.push_back(path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start));
        if (slash == std::string_view::npos)
            return parts;
        start = slash + 1;
    }
}

std::optional<std::string_view> RemoveExtension(std::string_view fileName, std::string_view extension)
{
    if (!LuaText::EndsWithIgnoringCase(fileName, extension))
        return std::nullopt;
    return fileName.substr(0, fileName.size() - extension.size());
}

bool AreFolderPartsAllowed(const std::vector<std::string_view>& parts)
{
    return std::all_of(parts.begin(), parts.end() - 1, [](std::string_view part) { return IsNamePartAllowed(part); });
}

}

bool IsNamePartAllowed(std::string_view part)
{
    return !part.empty() && std::all_of(part.begin(), part.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

bool IsLuaScriptName(std::string_view fileName)
{
    return LuaText::EndsWithIgnoringCase(fileName, LUA_EXTENSION);
}

std::optional<std::string> GetModuleName(std::string_view path)
{
    std::vector<std::string_view> parts = SplitPath(path);
    if (parts.size() < 2)
        return std::nullopt;
    std::optional<std::string_view> baseName = RemoveExtension(parts.back(), LUA_EXTENSION);
    if (!baseName || !AreFolderPartsAllowed(parts) || !IsNamePartAllowed(*baseName))
        return std::nullopt;

    std::string name;
    for (size_t i = 0; i + 1 < parts.size(); i++)
        name += (i ? "." : "") + std::string(parts[i]);
    if (*baseName != INIT_MODULE)
        name += "." + std::string(*baseName);
    return name;
}

bool IsImagePath(std::string_view path)
{
    std::vector<std::string_view> parts = SplitPath(path);
    std::optional<std::string_view> baseName = RemoveExtension(parts.back(), PNG_EXTENSION);
    return baseName && AreFolderPartsAllowed(parts) && IsNamePartAllowed(*baseName);
}

bool IsInitModulePath(std::string_view path)
{
    size_t slash = path.rfind('/');
    std::string_view fileName = slash == std::string_view::npos ? path : path.substr(slash + 1);
    return RemoveExtension(fileName, LUA_EXTENSION) == INIT_MODULE;
}

int CompareNames(std::string_view a, std::string_view b)
{
    std::u16string a16 = LuaText::ToUtf16(a), b16 = LuaText::ToUtf16(b);
    int order = LuaText::CompareIgnoringCase(a16, b16);
    return order != 0 ? order : LuaText::CompareUtf16(a16, b16);
}

const char* ModuleBudget::CheckBeforeRead(std::optional<int64_t> listedSize)
{
    if (ConsideredCount >= MAX_MODULE_COUNT)
        return REASON_TOO_MANY_MODULES;
    ConsideredCount++;
    if (listedSize && *listedSize > MAX_MODULE_SIZE)
        return REASON_MODULE_TOO_LARGE;
    if (IsFull)
        return REASON_MODULES_TOO_LARGE;
    return nullptr;
}

int ModuleBudget::ReadLimit() const
{
    return (int)std::min<int64_t>(MAX_MODULE_SIZE, BytesLeft) + 1;
}

const char* ModuleBudget::Take(int64_t size)
{
    if (size <= MAX_MODULE_SIZE && size <= BytesLeft)
    {
        BytesLeft -= size;
        return nullptr;
    }

    IsFull = true;
    return size > MAX_MODULE_SIZE ? REASON_MODULE_TOO_LARGE : REASON_MODULES_TOO_LARGE;
}

const char* ImageBudget::CheckBeforeDecode()
{
    if (ConsideredCount >= MAX_IMAGE_COUNT)
        return REASON_TOO_MANY_IMAGES;
    ConsideredCount++;
    return IsFull ? REASON_IMAGES_TOO_LARGE : nullptr;
}

const char* ImageBudget::Take(int width, int height)
{
    if (width > MAX_IMAGE_SIDE || height > MAX_IMAGE_SIDE)
        return REASON_IMAGE_TOO_LARGE;
    int64_t pixelCount = (int64_t)width * height;
    if (pixelCount > PixelsLeft)
    {
        IsFull = true;
        return REASON_IMAGES_TOO_LARGE;
    }
    PixelsLeft -= pixelCount;
    return nullptr;
}

}
