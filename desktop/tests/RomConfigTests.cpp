// RomModsConfigDtoTest.kt's Lua script cases, case for case, over the rows RomConfig stores (RomConfig.cpp's mods.luaScripts JSON in the
// DTO's shape), plus what the desktop's JSON reading drops
#include <string>

#include "LuaScriptTestText.h"
#include "RomConfig.h"
#include "Test.h"

using namespace Frontend;

namespace Tests
{

static RomConfig WithScripts(const std::string& json)
{
    return RomConfig::FromFields({{"mods.luaScripts", json}});
}

TEST(RomConfig, oldConfigsLoadWithNoOptions)
{
    RomConfig config = WithScripts(R"([{"fileName":"aim.lua","enabled":true}])");

    CHECK(config.LuaScripts == std::vector<RomLuaScript>({{"aim.lua", true, {}}}));
}

TEST(RomConfig, optionsSurviveSavingAndLoading)
{
    RomConfig config = WithScripts(R"([{"fileName":"aim.lua","enabled":false,"options":{"speed":"7","bad":null}}])");
    CHECK(config.LuaScripts == std::vector<RomLuaScript>({{"aim.lua", false, {{"speed", "7"}}}}));

    config.LuaScriptFolder = "D:\\Mods\\aim";
    RomConfig reloaded = RomConfig::FromFields(config.ToFields());
    CHECK(reloaded == config);
}

// not in the Kotlin: entries Gson would leave null are dropped, Gson's leniency reads a number or a boolean option as its text, and
// what doesn't parse or nests too deep is no scripts
TEST(RomConfig, unreadableScriptEntriesAreDropped)
{
    RomConfig config = WithScripts(R"([null,{"enabled":true},{"fileName":"a.lua","options":{"n":3,"t":true}}])");
    CHECK(config.LuaScripts == std::vector<RomLuaScript>({{"a.lua", false, {{"n", "3"}, {"t", "true"}}}}));

    CHECK(WithScripts("[{").LuaScripts.empty());
    CHECK(WithScripts(std::string(100, '[') + std::string(100, ']')).LuaScripts.empty());
    // a bracket inside a string doesn't count as nesting
    std::vector<RomLuaScript> bracketed = WithScripts(R"([{"fileName":"[[[[[[[[[.lua"}])").LuaScripts;
    CHECK(bracketed.size() == 1);
    CHECK_SAME(std::string("[[[[[[[[[.lua"), bracketed[0].FileName);
}

TEST(RomConfig, noScriptsStoresNoRows)
{
    CHECK(RomConfig().ToFields().empty());
    CHECK(RomConfig().IsDefault());
}

}
