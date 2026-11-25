// LuaScriptFolderSyncTest.kt, case for case (assertSame is an equality check: the C++ returns a copy of the kept list)
#include <string>
#include <vector>

#include "LuaScriptTestText.h"
#include "Test.h"
#include "lua/LuaScriptFolderSync.h"

using namespace Frontend;

namespace
{

using Scripts = std::vector<RomLuaScript>;
using Names = std::vector<std::string>;

RomLuaScript Script(const std::string& fileName, bool enabled, std::map<std::string, std::string> options = {})
{
    return {fileName, enabled, std::move(options)};
}

Scripts Sync(const Scripts& scripts, bool hasFolder, const std::optional<Names>& folderScriptNames)
{
    return LuaScriptFolderSync::Sync(scripts, hasFolder, folderScriptNames);
}

}

TEST(LuaScriptFolderSync, newFileIsAddedSwitchedOff)
{
    Scripts scripts = {Script("aim.lua", true)};

    Scripts synced = Sync(scripts, true, Names {"aim.lua", "turbo.lua"});

    CHECK_SAME((Scripts {Script("aim.lua", true), Script("turbo.lua", false)}), synced);
}

TEST(LuaScriptFolderSync, goneFileIsDroppedWhenTheFolderCanBeRead)
{
    Scripts scripts = {Script("aim.lua", true), Script("turbo.lua", true)};

    Scripts synced = Sync(scripts, true, Names {"turbo.lua"});

    CHECK_SAME(Scripts {Script("turbo.lua", true)}, synced);
}

TEST(LuaScriptFolderSync, listIsKeptWhenTheFolderCantBeRead)
{
    Scripts scripts = {Script("turbo.lua", false), Script("aim.lua", true)};

    Scripts synced = Sync(scripts, true, std::nullopt);

    CHECK_SAME(scripts, synced);
}

TEST(LuaScriptFolderSync, emptyListingKeepsKnownScriptsWithTheirSwitchesAndOptions)
{
    // What a folder hidden by Android looks like: an empty listing with no error flag
    Scripts scripts = {Script("aim.lua", true, {{"speed", "7"}}), Script("turbo.lua", false)};

    Scripts synced = Sync(scripts, true, Names {});

    CHECK_SAME(scripts, synced);
}

TEST(LuaScriptFolderSync, emptyListingWithNoKnownScriptsStaysEmpty)
{
    CHECK_SAME(Scripts {}, Sync({}, true, Names {}));
}

TEST(LuaScriptFolderSync, scriptsComeBackUnchangedWhenTheHiddenFolderReturns)
{
    Scripts scripts = {Script("aim.lua", true, {{"speed", "7"}})};

    Scripts whileHidden = Sync(scripts, true, Names {});
    Scripts afterReturn = Sync(whileHidden, true, Names {"aim.lua"});

    CHECK_SAME(scripts, afterReturn);
}

TEST(LuaScriptFolderSync, goneFileIsStillDroppedWhenOthersAreListed)
{
    Scripts scripts = {Script("aim.lua", true), Script("turbo.lua", true)};

    Scripts synced = Sync(scripts, true, Names {"aim.lua"});

    CHECK_SAME(Scripts {Script("aim.lua", true)}, synced);
}

TEST(LuaScriptFolderSync, emptyListingIsSuspiciousOnlyWhenScriptsAreKnown)
{
    CHECK(LuaScriptFolderSync::IsSuspiciouslyEmpty(1, {}));
    CHECK(!LuaScriptFolderSync::IsSuspiciouslyEmpty(0, {}));
    CHECK(!LuaScriptFolderSync::IsSuspiciouslyEmpty(2, {"aim.lua"}));
}

TEST(LuaScriptFolderSync, listWithNoFolderIsCleared)
{
    Scripts scripts = {Script("aim.lua", true)};

    CHECK_SAME(Scripts {}, Sync(scripts, false, std::nullopt));
}

TEST(LuaScriptFolderSync, scriptsAreSortedByNameIgnoringCase)
{
    Scripts synced = Sync({}, true, Names {"b.lua", "C.lua", "a.lua", "A.lua"});

    Names names;
    for (const RomLuaScript& script : synced)
        names.push_back(script.FileName);
    CHECK_SAME((Names {"A.lua", "a.lua", "b.lua", "C.lua"}), names);
}

TEST(LuaScriptFolderSync, sameNameListedTwiceIsOneEntry)
{
    Scripts synced = Sync({}, true, Names {"aim.lua", "aim.lua"});

    CHECK_SAME(Scripts {Script("aim.lua", false)}, synced);
}

TEST(LuaScriptFolderSync, optionsAreKeptWhenSyncing)
{
    Scripts scripts = {Script("aim.lua", true, {{"speed", "7"}})};

    Scripts synced = Sync(scripts, true, Names {"aim.lua", "turbo.lua"});

    CHECK_SAME((Scripts {Script("aim.lua", true, {{"speed", "7"}}), Script("turbo.lua", false)}), synced);
}
