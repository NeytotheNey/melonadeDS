// The desktop's settings: one JSON object in settings.json in the profile folder, with the Android app's preference key names where the app
// has the setting (video_renderer, video_internal_resolution, ...) and a desktop_ prefix for the ones only the desktop has. Values given on
// the command line are overrides: they win for the run and are never written. Values are plain JSON (strings, numbers, booleans, objects);
// the getters accept a number written as a string and the other way round, since the app stores its list settings as strings ("4").
// Keys the desktop doesn't know are kept as they are. UI thread only
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "picojson/picojson.h"

namespace Frontend
{

// Where the profile (settings and, in later phases, the library, RetroAchievements login and art) lives: [profileArg] (--profile) if
// given, otherwise a fresh temporary folder for a scripted run, --hidden or --frames (made here; [temporary] says so, and the caller removes it at exit), the
// exe's folder when a portable.txt sits there, or %APPDATA%\melonadeDS. The folder is made if it's missing; empty on failure
std::filesystem::path ResolveProfileDir(const std::string& profileArg, bool scripted, const std::filesystem::path& exeDir, bool& temporary);

// a path from a UTF-8 string (SDL's), and back
std::filesystem::path PathFromUtf8(const std::string& text);
std::string PathToUtf8(const std::filesystem::path& path);

class Settings
{
public:
    // reads [dir]\settings.json; a missing file is an empty one. A file that doesn't parse is kept as settings.json.bad on the first save
    bool Load(const std::filesystem::path& dir);
    // writes the stored values (never the overrides) through a temporary file, so a crash can't leave half a file
    bool Save();
    const std::filesystem::path& Dir() const { return ProfileDir; }
    std::filesystem::path File() const { return ProfileDir / "settings.json"; }

    // the run's value: the override if there is one, else the stored value; null if neither
    const picojson::value* Find(const std::string& key) const;
    std::string GetString(const std::string& key, const std::string& fallback) const;
    bool GetBool(const std::string& key, bool fallback) const;
    int GetInt(const std::string& key, int fallback) const;
    // a list of strings (rom_search_dirs); empty when the key is missing or isn't an array
    std::vector<std::string> GetStringList(const std::string& key) const;

    // a change made while running (a hotkey, later the menus): stored and saved, unless the command line overrides the key, in which case
    // only the override changes, so the setting the command line gave stays the run's and isn't written. [save] false leaves the write
    // for a later Save()
    void Set(const std::string& key, const picojson::value& value, bool save = true);
    // a stored value written whatever the overrides say (the window places, which have overrides of their own)
    void SetStored(const std::string& key, const picojson::value& value);
    const picojson::value* FindStored(const std::string& key) const;

    // a value for this run only (the command line)
    void Override(const std::string& key, const picojson::value& value) { Overrides[key] = value; }
    bool IsOverridden(const std::string& key) const { return Overrides.count(key) != 0; }

    // --setting KEY=VALUE's value: true/false, a number, or else a string
    static picojson::value ParseValue(const std::string& text);

private:
    std::filesystem::path ProfileDir;
    picojson::object Stored;
    std::map<std::string, picojson::value> Overrides;
    bool KeepBadFile = false;
};

}
