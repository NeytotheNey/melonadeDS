#include "lua/LuaScriptDataStore.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>

#include "lua/LuaText.h"

namespace fs = std::filesystem;

namespace Frontend
{

namespace
{

constexpr const char* DATA_FOLDER = "lua-data";
constexpr const char* TEMP_SUFFIX = ".tmp";
constexpr std::string_view HOMEBREW_PREFIX = "homebrew-";
constexpr const char* DATA_SUFFIX = ".bin";
// room for the suffix within the 255 bytes a file name can have
constexpr size_t MAX_PLAIN_NAME_BYTES = 200;
constexpr size_t SHORT_HASH_BYTES = 8;

// SHA-256 through Windows' CNG (bcrypt, a system DLL, as the metadata sync's MD5 and SHA1), or none if CNG fails
std::optional<std::array<uint8_t, 32>> Sha256(std::string_view data)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<uint8_t, 32> digest {};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    ok = ok && BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    ok = ok && BCryptHashData(hash, (PUCHAR)data.data(), (ULONG)data.size(), 0) >= 0;
    ok = ok && BCryptFinishHash(hash, digest.data(), (ULONG)digest.size(), 0) >= 0;
    if (hash)
        BCryptDestroyHash(hash);
    if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok)
        return std::nullopt;
    return digest;
}

// the first 8 bytes of the SHA-256 of [data] in lowercase hex, or empty if it couldn't be made
std::string ShortHash(std::string_view data)
{
    std::optional<std::array<uint8_t, 32>> digest = Sha256(data);
    if (!digest)
        return "";
    static const char* HEX = "0123456789abcdef";
    std::string text;
    for (size_t i = 0; i < SHORT_HASH_BYTES; i++)
    {
        text += HEX[(*digest)[i] >> 4];
        text += HEX[(*digest)[i] & 15];
    }
    return text;
}

bool IsCodeCharacter(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// retail codes are 4 uppercase letters or digits, and anything else (NUL bytes, spaces, ndstool's ####) is a placeholder
bool IsGameCode(std::string_view code)
{
    return code.size() == 4 && std::all_of(code.begin(), code.end(), IsCodeCharacter);
}

// what GetGameKey makes: a game code or homebrew-<16 hex digits>
bool IsGameKey(std::string_view key)
{
    if (IsGameCode(key))
        return true;
    if (key.size() != HOMEBREW_PREFIX.size() + 2 * SHORT_HASH_BYTES || key.substr(0, HOMEBREW_PREFIX.size()) != HOMEBREW_PREFIX)
        return false;
    std::string_view hash = key.substr(HOMEBREW_PREFIX.size());
    return std::all_of(hash.begin(), hash.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// a character a name can't hold here: the path separators and NUL as on Android, and the rest Windows refuses (control characters and
// <>:"|?*, where a colon would even name a stream of another file)
bool IsUnsafeNameCharacter(char c)
{
    return (unsigned char)c < 0x20 || c == '/' || c == '\\' || c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*';
}

}

LuaScriptDataStore::LuaScriptDataStore(fs::path profileDir, std::function<void(const std::string&)> warn)
    : DataDir(std::move(profileDir) / DATA_FOLDER), WarnLine(std::move(warn))
{
}

void LuaScriptDataStore::SetSessionGameKey(std::optional<std::string> gameKey)
{
    std::lock_guard<std::mutex> guard(Lock);
    SessionGameKey = std::move(gameKey);
}

std::optional<std::string> LuaScriptDataStore::Read(const std::string& gameKey, const std::string& scriptName)
{
    std::lock_guard<std::mutex> guard(Lock);
    std::optional<fs::path> file = GetFile(gameKey, scriptName);
    std::error_code error;
    if (!file || !fs::is_regular_file(*file, error))
        return std::nullopt;

    // Native never makes a blob this large, so a larger file is left for native to replace
    uintmax_t size = fs::file_size(*file, error);
    if (!error && size > (uintmax_t)MAX_DATA_SIZE)
    {
        Warn("Saved data of " + scriptName + " is too large, so it's not used");
        return std::nullopt;
    }
    std::ifstream in(*file, std::ios::binary);
    std::string data;
    if (!error && in)
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (error || !in.is_open() || in.bad())
    {
        Warn("Could not read the saved data of " + scriptName);
        return std::nullopt;
    }
    // the file grew since its size was read
    if (data.size() > (size_t)MAX_DATA_SIZE)
    {
        Warn("Saved data of " + scriptName + " is too large, so it's not used");
        return std::nullopt;
    }
    return data;
}

void LuaScriptDataStore::Write(const std::string& gameKey, const std::string& scriptName, const std::string& data)
{
    std::lock_guard<std::mutex> guard(Lock);
    std::optional<fs::path> file = GetFile(gameKey, scriptName);
    if (!file)
        return;
    std::error_code error;
    if (data.empty())
    {
        fs::remove(*file, error);
        if (error)
            Warn("Could not delete the saved data of " + scriptName);
        return;
    }

    fs::create_directories(file->parent_path(), error);
    // next to the file, so the rename stays on one volume and replaces it whole; the lock keeps two writes from sharing it
    fs::path tempFile = *file;
    tempFile += TEMP_SUFFIX;
    bool isWritten = false;
    if (!error)
    {
        std::ofstream out(tempFile, std::ios::binary | std::ios::trunc);
        out.write(data.data(), (std::streamsize)data.size());
        out.close();
        isWritten = !out.fail();
    }
    if (isWritten)
        fs::rename(tempFile, *file, error);
    if (!isWritten || error)
    {
        Warn("Could not save the data of " + scriptName);
        std::error_code ignored;
        // a leftover temp file is only wasted space, and the next write replaces it
        fs::remove(tempFile, ignored);
    }
}

int64_t LuaScriptDataStore::GetSize(const std::string& gameKey, const std::string& scriptName)
{
    std::lock_guard<std::mutex> guard(Lock);
    std::optional<fs::path> file = GetFile(gameKey, scriptName);
    std::error_code error;
    if (!file || !fs::is_regular_file(*file, error))
        return 0;
    uintmax_t size = fs::file_size(*file, error);
    return error ? 0 : (int64_t)size;
}

void LuaScriptDataStore::Delete(const std::string& gameKey, const std::string& scriptName)
{
    std::lock_guard<std::mutex> guard(Lock);
    if (gameKey == SessionGameKey)
    {
        Warn("Not deleting the saved data of " + scriptName + " while its game is running");
        return;
    }
    std::optional<fs::path> file = GetFile(gameKey, scriptName);
    std::error_code error;
    if (file)
        fs::remove(*file, error);
    if (error)
        Warn("Could not delete the saved data of " + scriptName);
}

std::optional<std::string> LuaScriptDataStore::GetGameKey(const std::optional<std::string>& gameCode, const std::string& romIdentity)
{
    if (!gameCode)
        return std::nullopt;
    if (IsGameCode(*gameCode) && *gameCode != "0000")
        return *gameCode;

    // with no hash there's no stable key, so the data stays session-only
    std::string hash = ShortHash(romIdentity);
    if (hash.empty())
        return std::nullopt;
    return std::string(HOMEBREW_PREFIX) + hash;
}

std::string LuaScriptDataStore::GetFileName(const std::string& scriptName)
{
    bool isPlainNameSafe = !scriptName.empty() && scriptName != "." && scriptName != ".."
        && std::none_of(scriptName.begin(), scriptName.end(), IsUnsafeNameCharacter) && scriptName.size() <= MAX_PLAIN_NAME_BYTES;
    if (isPlainNameSafe)
        return scriptName + DATA_SUFFIX;

    std::string hash = ShortHash(scriptName);
    if (hash.empty())
        return "";
    return "script-" + hash + DATA_SUFFIX;
}

std::optional<fs::path> LuaScriptDataStore::GetFile(const std::string& gameKey, const std::string& scriptName) const
{
    // The game key comes from GetGameKey(), but native hands it back, so it's checked before it becomes a path
    if (!IsGameKey(gameKey))
        return std::nullopt;
    std::string fileName = GetFileName(scriptName);
    if (fileName.empty())
        return std::nullopt;
    return DataDir / gameKey / LuaText::PathFromUtf8(fileName);
}

void LuaScriptDataStore::Warn(const std::string& message) const
{
    if (WarnLine)
        WarnLine(message);
}

}
