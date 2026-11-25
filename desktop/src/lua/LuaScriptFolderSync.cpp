#include "lua/LuaScriptFolderSync.h"

#include <algorithm>
#include <set>

#include "lua/LuaScriptFolderContent.h"

namespace Frontend::LuaScriptFolderSync
{

std::vector<RomLuaScript> Sync(const std::vector<RomLuaScript>& scripts, bool hasFolder,
    const std::optional<std::vector<std::string>>& folderScriptNames)
{
    if (!hasFolder)
        return {};
    if (!folderScriptNames || IsSuspiciouslyEmpty(scripts.size(), *folderScriptNames))
        return scripts;

    // the last entry with a name wins, as Kotlin's associateBy
    std::map<std::string, const RomLuaScript*> knownScripts;
    for (const RomLuaScript& script : scripts)
        knownScripts[script.FileName] = &script;

    // Some providers can list two documents with the same name, but scripts are known by name, so they're one entry
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (const std::string& name : *folderScriptNames)
    {
        if (seen.insert(name).second)
            names.push_back(name);
    }
    std::stable_sort(names.begin(), names.end(),
        [](const std::string& a, const std::string& b) { return LuaScriptFolderContent::NameLess(a, b); });

    std::vector<RomLuaScript> synced;
    for (const std::string& name : names)
    {
        auto known = knownScripts.find(name);
        synced.push_back(known != knownScripts.end() ? *known->second : RomLuaScript {name, false, {}});
    }
    return synced;
}

bool IsSuspiciouslyEmpty(size_t knownScriptCount, const std::vector<std::string>& folderScriptNames)
{
    return folderScriptNames.empty() && knownScriptCount > 0;
}

}
