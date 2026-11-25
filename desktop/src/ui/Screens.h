// The screens, one file each
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "Library.h"
#include "Screen.h"

namespace Frontend { struct LuaScriptStatus; }

namespace Ui
{

class App;
struct Host;

// what the library opens by itself once its scan is done (--open library/details, library/settings, library/settings/mods): the first
// game's details or settings, or its settings at the Lua scripts
enum class LibraryOpen { None, Details, Settings, SettingsMods };
// the game library, the home screen when no game runs
std::unique_ptr<Screen> MakeLibraryScreen(LibraryOpen open = LibraryOpen::None);
// a game's details, with Play and its settings
std::unique_ptr<Screen> MakeGameDetailsScreen(const Frontend::LibraryEntry& entry);
// a game's own settings (RomConfig: its display name, console, GBA slot, layout, widescreen and Lua scripts), kept in the library by its
// RetroAchievements hash; [atMods]: scrolled to the Lua scripts, the focus on the script folder
std::unique_ptr<Screen> MakeGameSettingsScreen(const Frontend::LibraryEntry& entry, bool atMods = false);
std::unique_ptr<Screen> MakePauseScreen();
// the question before the player stops the game or quits while an unlock hasn't reached RetroAchievements: Keep playing, or Stop anyway
// ([stop] runs then)
std::unique_ptr<Screen> MakePendingUnlocksQuestion(std::function<void()> stop);
// the pause menu's Mods: the session's scripts, each with a switch for this session only and its Options (ui/ModsScreen.cpp)
std::unique_ptr<Screen> MakeModsScreen();
// the pause menu's Achievements (ui/AchievementsScreen.cpp): the session's achievements by set and by progress, each opening its page on
// retroachievements.org; shown while HasAchievementList
std::unique_ptr<Screen> MakeAchievementsScreen();
// the running game's RetroAchievements session has its game loaded, with achievements
bool HasAchievementList(Host& host);
// a script's options over the pause menu: saved to the game's settings and the script restarted with them, or cancelled
std::unique_ptr<Screen> MakeLuaOptionsScreen(const Frontend::LuaScriptStatus& script);
// the savestate slots, on the Save tab or the Load tab
std::unique_ptr<Screen> MakeStatesScreen(bool save);
// the settings, on one of its tabs: video, display, emulation, audio, controls, library, achievements
std::unique_ptr<Screen> MakeSettingsScreen(const std::string& tab = "");
// Settings > Achievements' login (--open settings/achievements/login): the user name and password, and Log in
std::unique_ptr<Screen> MakeRaLoginScreen();
// the bindings of each device (input setup)
std::unique_ptr<Screen> MakeInputScreen();
// a text field and the on-screen keyboard, for tests (--open keyboard)
std::unique_ptr<Screen> MakeKeyboardTestScreen();
// the layouts: the global one (Settings > Display, --open layouts), a game's own ([entry]'s), or one picked while the game runs (the
// pause menu, --open pause/layout)
enum class LayoutsFor { Global, Game, InGame };
std::unique_ptr<Screen> MakeLayoutsScreen(LayoutsFor mode, const Frontend::LibraryEntry* entry = nullptr);
// the layout editor (ui/LayoutEditorScreen.cpp): a custom layout by its id, a new one (starting from the Default, as the app's), or the
// layout the windows show (InUse; a built-in one can't be edited, so a new layout starts from what it shows, and once saved it's picked in
// its place, stored where that one came from)
struct LayoutEditorOpen
{
    enum class What { Layout, New, InUse } Kind = What::New;
    std::string Id;
};
std::unique_ptr<Screen> MakeLayoutEditorScreen(const LayoutEditorOpen& open);
// a card over the screen below it with a title, a line or two of [body] and the rows [rows] draws (the questions and the editor's menus):
// a row that's chosen pops the card first (App::Pop) and then does its thing. Back closes it. [id] is its name in the log
std::unique_ptr<Screen> MakePanelScreen(const std::string& id, const std::string& title, const std::string& body,
    std::function<void(App&)> rows);

}
