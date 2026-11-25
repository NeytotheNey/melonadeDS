// LuaScriptDataStoreTest.kt, case for case, with the ROM URI as the desktop's ROM identity, then the desktop's own cases: the store's file
// handling in a temporary profile folder, and the script reading helpers of LuaScriptStorage.h
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "LuaScriptTestText.h"
#include "Test.h"
#include "lua/LuaScriptDataStore.h"
#include "lua/LuaScriptStorage.h"

using namespace Frontend;
namespace fs = std::filesystem;

namespace
{

bool Matches(const std::string& text, const char* pattern)
{
    return std::regex_match(text, std::regex(pattern));
}

// a new empty folder under the system's temp folder, deleted when the test ends
struct TempFolder
{
    fs::path Path;

    explicit TempFolder(const char* name)
    {
        std::error_code error;
        Path = fs::temp_directory_path(error) / (std::string("melonade-tests-") + name);
        fs::remove_all(Path, error);
        fs::create_directories(Path, error);
    }
    ~TempFolder()
    {
        std::error_code error;
        fs::remove_all(Path, error);
    }
};

void WriteFile(const fs::path& file, const std::string& content)
{
    std::ofstream out(file, std::ios::binary);
    out << content;
}

}

TEST(LuaScriptDataStore, realGameCodesAreTheKey)
{
    CHECK_SAME(std::optional<std::string>("AMHE"), LuaScriptDataStore::GetGameKey("AMHE", "content://roms/hunters.nds"));
}

TEST(LuaScriptDataStore, placeholderCodesAreKeyedByTheRomUri)
{
    std::optional<std::string> key = LuaScriptDataStore::GetGameKey("####", "content://roms/homebrew.nds");
    CHECK(key.has_value());

    CHECK(Matches(*key, "homebrew-[0-9a-f]{16}"));
    CHECK_SAME(key, LuaScriptDataStore::GetGameKey("0000", "content://roms/homebrew.nds"));
    CHECK_SAME(key, LuaScriptDataStore::GetGameKey("amhe", "content://roms/homebrew.nds"));
    CHECK(key != LuaScriptDataStore::GetGameKey("####", "content://roms/other.nds"));
}

TEST(LuaScriptDataStore, unreadableHeadersHaveNoKey)
{
    CHECK_NONE(LuaScriptDataStore::GetGameKey(std::nullopt, "content://roms/hunters.nds"));
}

TEST(LuaScriptDataStore, scriptNamesBecomeFileNames)
{
    CHECK_SAME("aim.lua.bin", LuaScriptDataStore::GetFileName("aim.lua"));
    CHECK_SAME("\xC3\x9Cn\xC3\xAF" "code.lua.bin", LuaScriptDataStore::GetFileName("\xC3\x9Cn\xC3\xAF" "code.lua"));
}

TEST(LuaScriptDataStore, unsafeScriptNamesAreHashed)
{
    std::vector<std::string> unsafeNames = {"../evil.lua", "a/b.lua", "a\\b.lua", "..", ".", "", std::string(201, 'x')};
    for (const std::string& name : unsafeNames)
        CHECK(Matches(LuaScriptDataStore::GetFileName(name), "script-[0-9a-f]{16}\\.bin"));
}

// the desktop's own cases from here on

TEST(LuaScriptDataStore, theHashIsSha256)
{
    // the first 8 bytes of SHA-256("abc"), as MessageDigest gives them
    CHECK_SAME(std::optional<std::string>("homebrew-ba7816bf8f01cfea"), LuaScriptDataStore::GetGameKey("####", "abc"));
}

TEST(LuaScriptDataStore, namesWindowsRefusesAreHashed)
{
    for (const char* name : {"a:b.lua", "a*b.lua", "a?b.lua", "a\"b.lua", "a<b.lua", "a>b.lua", "a|b.lua", "a\tb.lua"})
        CHECK(Matches(LuaScriptDataStore::GetFileName(name), "script-[0-9a-f]{16}\\.bin"));
}

TEST(LuaScriptDataStore, dataIsWrittenReadSizedAndDeleted)
{
    TempFolder profile("data-store");
    std::vector<std::string> warnings;
    LuaScriptDataStore store(profile.Path, [&](const std::string& line) { warnings.push_back(line); });

    CHECK_NONE(store.Read("AMHE", "aim.lua"));
    store.Write("AMHE", "aim.lua", "saved");
    CHECK(fs::is_regular_file(profile.Path / "lua-data" / "AMHE" / "aim.lua.bin"));
    CHECK_SAME(std::optional<std::string>("saved"), store.Read("AMHE", "aim.lua"));
    CHECK_SAME(5LL, (long long)store.GetSize("AMHE", "aim.lua"));
    store.Write("AMHE", "aim.lua", "again");
    CHECK_SAME(std::optional<std::string>("again"), store.Read("AMHE", "aim.lua"));
    CHECK(!fs::exists(profile.Path / "lua-data" / "AMHE" / "aim.lua.bin.tmp"));

    // empty data deletes the file
    store.Write("AMHE", "aim.lua", "");
    CHECK_NONE(store.Read("AMHE", "aim.lua"));
    CHECK_SAME(0LL, (long long)store.GetSize("AMHE", "aim.lua"));
    CHECK(warnings.empty());
}

TEST(LuaScriptDataStore, badGameKeysAndRunningGamesAreLeftAlone)
{
    TempFolder profile("data-store-keys");
    LuaScriptDataStore store(profile.Path);

    store.Write("../x", "aim.lua", "saved");
    CHECK(!fs::exists(profile.Path / "lua-data"));

    store.Write("AMHE", "aim.lua", "saved");
    store.SetSessionGameKey("AMHE");
    store.Delete("AMHE", "aim.lua");
    CHECK_SAME(std::optional<std::string>("saved"), store.Read("AMHE", "aim.lua"));
    store.SetSessionGameKey(std::nullopt);
    store.Delete("AMHE", "aim.lua");
    CHECK_NONE(store.Read("AMHE", "aim.lua"));
}

TEST(LuaScriptDataStore, tooLargeDataIsNotUsed)
{
    TempFolder profile("data-store-large");
    LuaScriptDataStore store(profile.Path);
    fs::create_directories(profile.Path / "lua-data" / "AMHE");
    WriteFile(profile.Path / "lua-data" / "AMHE" / "aim.lua.bin", std::string(LuaScriptDataStore::MAX_DATA_SIZE + 1, 'x'));

    CHECK_NONE(store.Read("AMHE", "aim.lua"));
}

TEST(LuaScriptStorage, scriptsAreListedAndReadWithTheirCap)
{
    TempFolder folder("storage");
    WriteFile(folder.Path / "aim.lua", "-- description: Aims\nprint(1)\n");
    WriteFile(folder.Path / "big.LUA", std::string(LuaScriptStorage::MAX_SCRIPT_SIZE + 1, '-'));
    WriteFile(folder.Path / "notes.txt", "x");
    fs::create_directories(folder.Path / "lib.lua");

    auto listed = LuaScriptStorage::ListScripts(folder.Path);
    CHECK(listed.has_value());
    std::vector<std::string> names;
    for (const LuaScriptStorage::ScriptFile& script : *listed)
        names.push_back(script.Name);
    std::sort(names.begin(), names.end());
    CHECK_SAME((std::vector<std::string> {"aim.lua", "big.LUA"}), names);

    auto results = LuaScriptStorage::ReadScripts(folder.Path, {"aim.lua", "big.LUA", "gone.lua"});
    CHECK_SAME(3ull, results.size());
    CHECK(results[0].second.ResultKind == LuaScriptStorage::ReadResult::Kind::Success);
    CHECK_SAME(std::string("-- description: Aims\nprint(1)\n"), results[0].second.Source);
    CHECK(results[1].second.ResultKind == LuaScriptStorage::ReadResult::Kind::TooLarge);
    CHECK(results[2].second.ResultKind == LuaScriptStorage::ReadResult::Kind::FileNotFound);

    auto header = LuaScriptStorage::ReadHeader(folder.Path / "aim.lua");
    CHECK(header.has_value());
    CHECK_SAME(std::optional<std::string>("Aims"), header->Description);
}

TEST(LuaScriptStorage, aMissingFolderIsNotFound)
{
    TempFolder folder("storage-missing");
    CHECK(!LuaScriptStorage::ListScripts(folder.Path / "gone").has_value());
    auto results = LuaScriptStorage::ReadScripts(folder.Path / "gone", {"aim.lua"});
    CHECK(results[0].second.ResultKind == LuaScriptStorage::ReadResult::Kind::FolderNotFound);
    // an empty listing while scripts are expected counts as a hidden folder
    results = LuaScriptStorage::ReadScripts(folder.Path, {"aim.lua"});
    CHECK(results[0].second.ResultKind == LuaScriptStorage::ReadResult::Kind::FolderNotFound);
}

TEST(LuaScriptStorage, folderContentHasModulesAndDecodedImages)
{
    TempFolder folder("storage-content");
    fs::create_directories(folder.Path / "lib");
    WriteFile(folder.Path / "main.lua", "print(1)");
    WriteFile(folder.Path / "lib" / "util.lua", "return {}");
    WriteFile(folder.Path / "hud.png", "png");
    WriteFile(folder.Path / "bad.png", "not one");

    LuaScriptStorage::ImageDecoder decoder;
    decoder.ReadSize = [](const fs::path& file, int& width, int& height) {
        if (file.filename() != "hud.png")
            return false;
        width = 2;
        height = 1;
        return true;
    };
    decoder.Decode = [](const fs::path& file, int& width, int& height, std::vector<uint32_t>& pixels) {
        if (file.filename() != "hud.png")
            return false;
        width = 2;
        height = 1;
        pixels = {0x80FF0000, 0xFF00FF00};
        return true;
    };
    auto content = LuaScriptStorage::ReadFolderContent(folder.Path, decoder);
    CHECK(content.has_value());
    CHECK(!content->GaveUp);
    CHECK_SAME(1ull, content->Modules.size());
    CHECK_SAME(std::string("lib.util"), content->Modules[0].Name);
    CHECK_SAME(std::optional<std::string>("return {}"), content->Modules[0].Source);
    CHECK_SAME(2ull, content->Images.size());
    CHECK_SAME(std::string("bad.png"), content->Images[0].Path);
    CHECK_SAME(std::string(LuaScriptFolderContent::REASON_NOT_AN_IMAGE), content->Images[0].LeftOutReason);
    CHECK_SAME(std::string("hud.png"), content->Images[1].Path);
    CHECK_SAME(2, content->Images[1].Width);
    CHECK_SAME(2ull, content->Images[1].Pixels.size());
    CHECK(content->Images[1].LeftOutReason.empty());
}

// not in the Kotlin, which has no decoder to swap: the size is read first, so an image over the caps is never decoded, and one that
// decodes to another size than it said is left out
TEST(LuaScriptStorage, anImageOverTheCapsIsNeverDecoded)
{
    TempFolder folder("storage-image-caps");
    WriteFile(folder.Path / "big.png", "png");
    WriteFile(folder.Path / "changed.png", "png");
    WriteFile(folder.Path / "ok.png", "png");

    int decodes = 0;
    LuaScriptStorage::ImageDecoder decoder;
    decoder.ReadSize = [](const fs::path& file, int& width, int& height) {
        width = file.filename() == "big.png" ? LuaScriptFolderContent::MAX_IMAGE_SIDE + 1 : 4;
        height = 4;
        return true;
    };
    decoder.Decode = [&](const fs::path& file, int& width, int& height, std::vector<uint32_t>& pixels) {
        decodes++;
        width = file.filename() == "changed.png" ? 8 : 4;
        height = 4;
        pixels.assign((size_t)width * height, 0xFFFFFFFF);
        return true;
    };
    auto content = LuaScriptStorage::ReadFolderContent(folder.Path, decoder);
    CHECK(content.has_value());
    CHECK_SAME(3ull, content->Images.size());
    CHECK_SAME(std::string(LuaScriptFolderContent::REASON_IMAGE_TOO_LARGE), content->Images[0].LeftOutReason);
    CHECK_SAME(std::string(LuaScriptFolderContent::REASON_NOT_AN_IMAGE), content->Images[1].LeftOutReason);
    CHECK(content->Images[2].LeftOutReason.empty());
    CHECK_SAME(16ull, content->Images[2].Pixels.size());
    CHECK_SAME(2, decodes);
}
