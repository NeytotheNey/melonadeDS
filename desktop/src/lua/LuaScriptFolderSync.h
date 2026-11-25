// The rules that keep a game's script list in step with its linked folder, ported from the app's impl/LuaScriptFolderSync.kt, with the
// list's entry (domain/model/rom/config/RomLuaScript.kt): a script's file name, its switch and its stored option values
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Frontend
{

struct RomLuaScript
{
    std::string FileName;
    bool Enabled = false;
    std::map<std::string, std::string> Options;

    bool operator==(const RomLuaScript& other) const = default;
};

namespace LuaScriptFolderSync
{

// The folder's scripts sorted by name, keeping the switch and options of known ones; with no folder the list is empty, and with an
// unreadable folder ([folderScriptNames] none) it's kept as is so a lost folder doesn't lose the switches.
//
// An empty listing while scripts are known also keeps the list: Android can hide a folder for a few minutes and list it as empty with no
// error flag, and taking that as "every script was deleted" would lose the switches and options for good. The cost is that deleting every
// script by hand leaves their entries until a script is added back or the folder is unlinked
std::vector<RomLuaScript> Sync(const std::vector<RomLuaScript>& scripts, bool hasFolder,
    const std::optional<std::vector<std::string>>& folderScriptNames);

// whether a listing of [folderScriptNames] should count as unreadable because it's empty while [knownScriptCount] scripts are expected in it
bool IsSuspiciouslyEmpty(size_t knownScriptCount, const std::vector<std::string>& folderScriptNames);

}

}
