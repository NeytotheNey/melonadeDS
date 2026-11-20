#include "Settings.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace Frontend
{

fs::path PathFromUtf8(const std::string& text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string PathToUtf8(const fs::path& path)
{
    std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

fs::path ResolveProfileDir(const std::string& profileArg, bool hidden, const fs::path& exeDir, bool& temporary)
{
    temporary = false;
    std::error_code ec;
    fs::path dir;
    if (!profileArg.empty())
        dir = fs::path(profileArg);
    else if (hidden)
    {
        // a test run: a folder of its own, so it never reads or writes the real profile
        fs::path base = fs::temp_directory_path(ec);
        if (ec)
            return {};
        char name[64];
        snprintf(name, sizeof(name), "melonadeDS-profile-%lu-%llu", GetCurrentProcessId(), (unsigned long long)GetTickCount64());
        dir = base / name;
        temporary = true;
    }
    else if (fs::exists(exeDir / "portable.txt", ec))
        dir = exeDir;
    else
    {
        const wchar_t* appData = _wgetenv(L"APPDATA");
        if (!appData || !*appData)
            return {};
        dir = fs::path(appData) / "melonadeDS";
    }
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir, ec))
        return {};
    return dir;
}

bool Settings::Load(const fs::path& dir)
{
    ProfileDir = dir;
    Stored.clear();
    std::ifstream in(File(), std::ios::binary);
    if (!in)
        return true;
    std::stringstream text;
    text << in.rdbuf();
    std::string content = text.str();
    picojson::value root;
    std::string error = picojson::parse(root, content);
    if (!error.empty() || !root.is<picojson::object>())
    {
        fprintf(stderr, "settings: %s doesn't parse (%s), starting from the defaults; it's kept as settings.json.bad\n",
            PathToUtf8(File()).c_str(), error.empty() ? "not a JSON object" : error.c_str());
        KeepBadFile = true;
        return false;
    }
    Stored = root.get<picojson::object>();
    return true;
}

bool Settings::Save()
{
    if (ProfileDir.empty())
        return false;
    std::error_code ec;
    if (KeepBadFile)
    {
        fs::rename(File(), ProfileDir / "settings.json.bad", ec);
        KeepBadFile = false;
    }
    fs::path temp = ProfileDir / "settings.json.tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            fprintf(stderr, "settings: can't write %s\n", PathToUtf8(temp).c_str());
            return false;
        }
        out << picojson::value(Stored).serialize(true);
        if (!out.good())
            return false;
    }
    // MoveFileEx replaces the old file in one step
    if (!MoveFileExW(temp.c_str(), File().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        fprintf(stderr, "settings: can't replace %s (error %lu)\n", PathToUtf8(File()).c_str(), GetLastError());
        return false;
    }
    return true;
}

const picojson::value* Settings::Find(const std::string& key) const
{
    auto over = Overrides.find(key);
    if (over != Overrides.end())
        return &over->second;
    return FindStored(key);
}

const picojson::value* Settings::FindStored(const std::string& key) const
{
    auto stored = Stored.find(key);
    return stored != Stored.end() ? &stored->second : nullptr;
}

std::string Settings::GetString(const std::string& key, const std::string& fallback) const
{
    const picojson::value* value = Find(key);
    if (!value || value->is<picojson::null>() || value->is<picojson::object>() || value->is<picojson::array>())
        return fallback;
    // a number or a boolean as its JSON text ("4", "true")
    return value->to_str();
}

bool Settings::GetBool(const std::string& key, bool fallback) const
{
    const picojson::value* value = Find(key);
    if (!value)
        return fallback;
    if (value->is<bool>())
        return value->get<bool>();
    if (value->is<double>())
        return value->get<double>() != 0;
    if (value->is<std::string>())
    {
        const std::string& text = value->get<std::string>();
        if (text == "true" || text == "1") return true;
        if (text == "false" || text == "0") return false;
    }
    return fallback;
}

int Settings::GetInt(const std::string& key, int fallback) const
{
    const picojson::value* value = Find(key);
    if (!value)
        return fallback;
    if (value->is<double>())
        return (int)value->get<double>();
    if (value->is<std::string>())
    {
        const std::string& text = value->get<std::string>();
        char* end = nullptr;
        long number = strtol(text.c_str(), &end, 10);
        if (end != text.c_str() && *end == 0)
            return (int)number;
    }
    return fallback;
}

void Settings::Set(const std::string& key, const picojson::value& value, bool save)
{
    auto over = Overrides.find(key);
    if (over != Overrides.end())
    {
        over->second = value;
        return;
    }
    const picojson::value* old = FindStored(key);
    if (old && *old == value)
        return;
    Stored[key] = value;
    if (save)
        Save();
}

void Settings::SetStored(const std::string& key, const picojson::value& value)
{
    Stored[key] = value;
}

picojson::value Settings::ParseValue(const std::string& text)
{
    if (text == "true")
        return picojson::value(true);
    if (text == "false")
        return picojson::value(false);
    char* end = nullptr;
    double number = strtod(text.c_str(), &end);
    if (!text.empty() && end && *end == 0)
        return picojson::value(number);
    return picojson::value(text);
}

}
