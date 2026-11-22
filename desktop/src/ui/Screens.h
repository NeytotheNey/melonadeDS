// The screens, one file each
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "Library.h"
#include "Screen.h"

namespace Ui
{

class App;

// what the library opens by itself once its scan is done (--open library/details, library/settings): the first game's details or settings
enum class LibraryOpen { None, Details, Settings };
// the game library, the home screen when no game runs
std::unique_ptr<Screen> MakeLibraryScreen(LibraryOpen open = LibraryOpen::None);
// a game's details, with Play and its settings
std::unique_ptr<Screen> MakeGameDetailsScreen(const Frontend::LibraryEntry& entry);
// a game's own settings (RomConfig: its display name, console, GBA slot and layout), kept in the library by its RetroAchievements hash
std::unique_ptr<Screen> MakeGameSettingsScreen(const Frontend::LibraryEntry& entry);
std::unique_ptr<Screen> MakePauseScreen();
// the savestate slots, on the Save tab or the Load tab
std::unique_ptr<Screen> MakeStatesScreen(bool save);
// the settings, on one of its tabs: video, display, emulation, audio, controls, library
std::unique_ptr<Screen> MakeSettingsScreen(const std::string& tab = "");
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
