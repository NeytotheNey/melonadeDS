// LuaScriptFolderContentTest.kt, case for case
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "LuaScriptTestText.h"
#include "Test.h"
#include "lua/LuaScriptFolderContent.h"

using namespace Frontend;
using namespace Frontend::LuaScriptFolderContent;
using LuaScriptTests::Reason;

namespace
{

using Names = std::vector<std::string>;
using Reasons = std::optional<std::string>;

// A folder tree for Walk: each folder path ("" is the root) maps to its children's names, folders ending in "/"
struct FakeTree
{
    std::map<std::string, Names> Folders;
    std::set<std::string> Unreadable;
    Names Listed;

    std::optional<std::vector<Child<std::string>>> List(const std::string& path)
    {
        Listed.push_back(path);
        if (Unreadable.count(path))
            return std::nullopt;
        auto folder = Folders.find(path);
        if (folder == Folders.end())
            return std::nullopt;
        std::vector<Child<std::string>> children;
        for (const std::string& name : folder->second)
        {
            bool isFolder = !name.empty() && name.back() == '/';
            std::string childName = isFolder ? name.substr(0, name.size() - 1) : name;
            children.push_back({childName, isFolder, 10, path.empty() ? childName : path + "/" + childName});
        }
        return children;
    }
};

WalkResult<std::string> WalkTree(FakeTree& tree)
{
    auto result = Walk(std::string(), [&](const std::string& path) { return tree.List(path); });
    return result ? *result : WalkResult<std::string> {};
}

std::vector<std::pair<std::string, std::string>> NamesAndPaths(const WalkResult<std::string>& result)
{
    std::vector<std::pair<std::string, std::string>> namesAndPaths;
    for (const Module<std::string>& module : result.Modules)
        namesAndPaths.emplace_back(module.Name, module.File.Path);
    return namesAndPaths;
}

Names ModuleNames(const WalkResult<std::string>& result)
{
    Names names;
    for (const Module<std::string>& module : result.Modules)
        names.push_back(module.Name);
    return names;
}

Names ImagePaths(const WalkResult<std::string>& result)
{
    Names paths;
    for (const FoundFile<std::string>& image : result.Images)
        paths.push_back(image.Path);
    return paths;
}

bool WasListed(const FakeTree& tree, const std::string& path)
{
    return std::find(tree.Listed.begin(), tree.Listed.end(), path) != tree.Listed.end();
}

}

TEST(LuaScriptFolderContent, moduleNamesComeFromThePath)
{
    CHECK_SAME(std::optional<std::string>("lib.util"), GetModuleName("lib/util.lua"));
    CHECK_SAME(std::optional<std::string>("lib"), GetModuleName("lib/init.lua"));
    CHECK_SAME(std::optional<std::string>("a.b.c"), GetModuleName("a/b/c.LUA"));
    CHECK_SAME(std::optional<std::string>("a.b"), GetModuleName("a/b/init.lua"));
    CHECK_SAME(std::optional<std::string>("my-lib.x_2"), GetModuleName("my-lib/x_2.lua"));
}

TEST(LuaScriptFolderContent, topLevelFilesAndOddNamesAreNotModules)
{
    CHECK_NONE(GetModuleName("util.lua"));
    CHECK_NONE(GetModuleName("lib/my util.lua"));
    CHECK_NONE(GetModuleName("my lib/util.lua"));
    CHECK_NONE(GetModuleName("lib/util.v2.lua"));
    CHECK_NONE(GetModuleName("lib/.lua"));
    CHECK_NONE(GetModuleName("lib/util.txt"));
    CHECK_NONE(GetModuleName("lib/\xC3\xBCtil.lua"));
}

TEST(LuaScriptFolderContent, imagePaths)
{
    CHECK(IsImagePath("hud.png"));
    CHECK(IsImagePath("img/hud.PNG"));
    CHECK(!IsImagePath("img/hud icon.png"));
    CHECK(!IsImagePath("my img/hud.png"));
    CHECK(!IsImagePath("img/hud.jpg"));
}

TEST(LuaScriptFolderContent, walkFindsModulesInSubfoldersAndImagesEverywhere)
{
    FakeTree tree {{
        {"", {"main.lua", "hud.png", "lib/", "img/", "notes.txt"}},
        {"lib", {"util.lua", "init.lua", "deep/"}},
        {"lib/deep", {"thing.lua"}},
        {"img", {"a.png", "b.png"}},
    }};

    WalkResult<std::string> result = WalkTree(tree);

    CHECK_SAME((std::vector<std::pair<std::string, std::string>> {
                   {"lib.deep.thing", "lib/deep/thing.lua"}, {"lib", "lib/init.lua"}, {"lib.util", "lib/util.lua"}}),
        NamesAndPaths(result));
    CHECK_SAME((Names {"hud.png", "img/a.png", "img/b.png"}), ImagePaths(result));
    CHECK(!result.GaveUp);
}

TEST(LuaScriptFolderContent, walkSkipsFoldersWithOddNames)
{
    FakeTree tree {{
        {"", {"my lib/", ".git/", "lib/"}},
        {"my lib", {"util.lua"}},
        {".git", {"config.lua"}},
        {"lib", {"util.lua"}},
    }};

    WalkResult<std::string> result = WalkTree(tree);

    CHECK_SAME(Names {"lib.util"}, ModuleNames(result));
    CHECK_SAME((Names {"", "lib"}), tree.Listed);
}

TEST(LuaScriptFolderContent, walkStopsAtFourLevels)
{
    FakeTree tree {{
        {"", {"a/"}},
        {"a", {"b/", "one.lua"}},
        {"a/b", {"c/"}},
        {"a/b/c", {"d/"}},
        {"a/b/c/d", {"e/", "four.lua"}},
        {"a/b/c/d/e", {"five.lua"}},
    }};

    WalkResult<std::string> result = WalkTree(tree);

    CHECK_SAME((Names {"a.b.c.d.four", "a.one"}), ModuleNames(result));
    CHECK(!WasListed(tree, "a/b/c/d/e"));
}

TEST(LuaScriptFolderContent, walkGivesUpAfter64Folders)
{
    FakeTree tree;
    for (int i = 1; i <= 100; i++)
    {
        tree.Folders[""].push_back("f" + std::to_string(i) + "/");
        tree.Folders["f" + std::to_string(i)] = {"m.lua"};
    }

    WalkResult<std::string> result = WalkTree(tree);

    CHECK(result.GaveUp);
    CHECK_SAME((size_t)MAX_FOLDERS + 1, tree.Listed.size());
    CHECK_SAME((size_t)MAX_FOLDERS, result.Modules.size());
}

TEST(LuaScriptFolderContent, walkGivesUpAfterTooManyFiles)
{
    FakeTree tree;
    for (int i = 1; i <= 5000; i++)
        tree.Folders[""].push_back("i" + std::to_string(i) + ".png");

    WalkResult<std::string> result = WalkTree(tree);

    CHECK(result.GaveUp);
    CHECK_SAME((size_t)MAX_FOUND_FILES, result.Images.size());
}

TEST(LuaScriptFolderContent, unreadableRootIsNull)
{
    FakeTree tree;
    tree.Unreadable = {""};
    CHECK(!Walk(std::string(), [&](const std::string& path) { return tree.List(path); }).has_value());
}

TEST(LuaScriptFolderContent, unreadableSubfolderIsSkipped)
{
    FakeTree tree {{{"", {"a/", "b/"}}, {"b", {"x.lua"}}}, {"a"}};

    CHECK_SAME(Names {"b.x"}, ModuleNames(WalkTree(tree)));
}

TEST(LuaScriptFolderContent, plainModuleWinsOverInitModule)
{
    FakeTree tree {{{"", {"a/"}}, {"a", {"x/", "x.lua"}}, {"a/x", {"init.lua"}}}};

    CHECK_SAME((std::vector<std::pair<std::string, std::string>> {{"a.x", "a/x.lua"}}), NamesAndPaths(WalkTree(tree)));
}

TEST(LuaScriptFolderContent, sameNameListedTwiceIsOneFile)
{
    FakeTree tree {{{"", {"a/", "a/"}}, {"a", {"x.lua", "x.lua"}}}};

    CHECK_SAME(Names {"a.x"}, ModuleNames(WalkTree(tree)));
    CHECK_SAME((Names {"", "a"}), tree.Listed);
}

TEST(LuaScriptFolderContent, moduleBudgetLeavesOutLargeFilesAndStopsAtTheTotal)
{
    ModuleBudget budget;
    const int mb = 1024 * 1024;

    // Too large by its listed size: not read, and the others still fit
    CHECK_SAME(Reasons(REASON_MODULE_TOO_LARGE), Reason(budget.CheckBeforeRead(mb + 1LL)));
    CHECK_NONE(Reason(budget.CheckBeforeRead((int64_t)mb)));
    CHECK_SAME(mb + 1, budget.ReadLimit());
    CHECK_NONE(Reason(budget.Take(mb)));
    CHECK_NONE(Reason(budget.CheckBeforeRead(std::nullopt)));
    CHECK_NONE(Reason(budget.Take(mb - 10)));
    // 10 bytes left: a read of 11 means it doesn't fit, and every later module is left out
    CHECK_SAME(11, budget.ReadLimit());
    CHECK_NONE(Reason(budget.CheckBeforeRead(20)));
    CHECK_SAME(Reasons(REASON_MODULES_TOO_LARGE), Reason(budget.Take(11)));
    CHECK_SAME(Reasons(REASON_MODULES_TOO_LARGE), Reason(budget.CheckBeforeRead(1)));
}

TEST(LuaScriptFolderContent, moduleBudgetTakesAt256Files)
{
    ModuleBudget budget;
    for (int i = 0; i < MAX_MODULE_COUNT; i++)
    {
        CHECK_NONE(Reason(budget.CheckBeforeRead(1)));
        CHECK_NONE(Reason(budget.Take(1)));
    }
    CHECK_SAME(Reasons(REASON_TOO_MANY_MODULES), Reason(budget.CheckBeforeRead(1)));
}

TEST(LuaScriptFolderContent, moduleOverOneMegabyteFoundByReadingIsTooLarge)
{
    ModuleBudget budget;
    CHECK_NONE(Reason(budget.CheckBeforeRead(std::nullopt)));
    CHECK_SAME(Reasons(REASON_MODULE_TOO_LARGE), Reason(budget.Take(budget.ReadLimit())));
}

TEST(LuaScriptFolderContent, imageBudgetCaps)
{
    ImageBudget budget;

    CHECK_NONE(Reason(budget.CheckBeforeDecode()));
    CHECK_SAME(Reasons(REASON_IMAGE_TOO_LARGE), Reason(budget.Take(1025, 1)));
    for (int i = 0; i < 4; i++)
    {
        CHECK_NONE(Reason(budget.CheckBeforeDecode()));
        CHECK_NONE(Reason(budget.Take(1024, 1024)));
    }
    CHECK_NONE(Reason(budget.CheckBeforeDecode()));
    CHECK_SAME(Reasons(REASON_IMAGES_TOO_LARGE), Reason(budget.Take(1, 1)));
    CHECK_SAME(Reasons(REASON_IMAGES_TOO_LARGE), Reason(budget.CheckBeforeDecode()));
}

TEST(LuaScriptFolderContent, imageBudgetTakesAt128Files)
{
    ImageBudget budget;
    for (int i = 0; i < MAX_IMAGE_COUNT; i++)
    {
        CHECK_NONE(Reason(budget.CheckBeforeDecode()));
        CHECK_NONE(Reason(budget.Take(1, 1)));
    }
    CHECK_SAME(Reasons(REASON_TOO_MANY_IMAGES), Reason(budget.CheckBeforeDecode()));
}
