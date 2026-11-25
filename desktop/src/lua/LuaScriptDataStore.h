// The blobs scripts save with melon.storage, ported from the app's impl/LuaScriptDataStore.kt: kept under
// <profile>/lua-data/<game key>/<script>.bin, written atomically, and outliving the script leaving the folder. Thread-safe: the UI (the
// saved data's size and Clear) and the emulator thread's storage snapshots share it behind its lock
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

namespace Frontend
{

class LuaScriptDataStore
{
public:
    // native's limit for a saved blob
    static constexpr int MAX_DATA_SIZE = 256 * 1024;

    // [profileDir] holds the lua-data folder; [warn] gets the lines the app logs as warnings (no logging when empty)
    explicit LuaScriptDataStore(std::filesystem::path profileDir, std::function<void(const std::string&)> warn = {});

    // the game key of the session that's running scripts, or none when none is; its data can't be deleted meanwhile because native would
    // write it back
    void SetSessionGameKey(std::optional<std::string> gameKey);

    // the saved data of [scriptName], or none if there's none or it can't be read
    std::optional<std::string> Read(const std::string& gameKey, const std::string& scriptName);
    // saves [data] for [scriptName], deleting the file when it's empty
    void Write(const std::string& gameKey, const std::string& scriptName, const std::string& data);
    // how many bytes [scriptName] has saved, 0 if nothing
    int64_t GetSize(const std::string& gameKey, const std::string& scriptName);
    // deletes [scriptName]'s data unless its game is running scripts (see SetSessionGameKey)
    void Delete(const std::string& gameKey, const std::string& scriptName);

    // The folder key for a game: the real game code, or a hash of [romIdentity] for placeholder codes shared by homebrew; none when the
    // header couldn't be read, so data stays session-only. [romIdentity] is what the game's own settings are kept by
    // (Library::ConfigKey: the ROM's RetroAchievements hash, else its path), so the data survives the ROM being moved as they do
    static std::optional<std::string> GetGameKey(const std::optional<std::string>& gameCode, const std::string& romIdentity);
    // the script's file name plus .bin, or a hash when the name can't be a file name here (a path separator, a NUL, a character Windows
    // doesn't take in names, . or .., or too long); empty only if the hash failed
    static std::string GetFileName(const std::string& scriptName);

private:
    std::optional<std::filesystem::path> GetFile(const std::string& gameKey, const std::string& scriptName) const;
    void Warn(const std::string& message) const;

    std::filesystem::path DataDir;
    std::function<void(const std::string&)> WarnLine;
    std::mutex Lock;
    // under Lock
    std::optional<std::string> SessionGameKey;
};

}
