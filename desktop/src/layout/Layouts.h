// The layouts to choose from, as the app's InternalLayoutsRepository has them: its Default layout (built for each window size, never saved)
// first, then the desktop's own four arrangements (the screens as the test frontend placed them, also built for each size and never saved),
// then the custom ones from layouts.json in the profile folder, the app's file (SettingsBackupFormat.LAYOUTS_FILE), in the file's order.
// A file that doesn't parse, or a layout in it the app couldn't read either, is never written over: the layout is left out of the list and
// written back as it was. UI thread
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "layout/LayoutModel.h"

namespace Frontend
{

// a layout whose variants aren't stored but built for each window size
enum class BuiltInLayout
{
    None,        // a layout from layouts.json
    AppDefault,  // the app's Default (LayoutConfiguration.DEFAULT_ID): DefaultLayoutBuilder's screens
    Vertical,    // the desktop's: top over bottom, as big as they fit
    Horizontal,  // side by side
    TopOnly,
    BottomOnly,
};

struct LayoutChoice
{
    LayoutConfiguration Config;
    BuiltInLayout BuiltIn = BuiltInLayout::None;
    // the name shown: the layout's, "Unnamed layout" (numbered when there are several, as the app's LuaLayoutChoice) for one without
    std::string DisplayName;
    // what --layout also takes for it (vertical, horizontal, top, bottom, default)
    std::string Alias;

    const std::string& Id() const { return *Config.Id; }
};

class Layouts
{
public:
    // the app's LayoutConfiguration.DEFAULT_ID
    static constexpr const char* DEFAULT_ID = "00000000-0000-0000-0000-000000000000";
    static constexpr const char* FILE_NAME = "layouts.json";

    // the built-ins, and [profileDir]'s layouts.json; false (with the reason logged) when the file is there but can't be read in full
    bool Load(const std::filesystem::path& profileDir);

    const std::vector<LayoutChoice>& All() const { return Choices; }
    const LayoutChoice* Find(const std::string& id) const;
    // --layout: an id, a name (any case) or an alias
    const LayoutChoice* FindByName(const std::string& text) const;
    const LayoutChoice& Default() const { return Choices.front(); }
    // how many custom layouts the file has
    int CustomCount() const { return (int)Custom.size(); }

    // the custom layouts written as layouts.json to [file] (--export-layouts; the layout editor saves this way later). False when it can't
    // be written
    bool Write(const std::filesystem::path& file) const;
    // to the profile's layouts.json, unless that file didn't parse (then it's kept as it was and this is false)
    bool Save() const;

    // the desktop built-ins' ids (nameUUIDFromBytes of "melonade-desktop-layout:<alias>", like the app's built-in backgrounds)
    static const char* DesktopLayoutId(BuiltInLayout layout);

private:
    void Rebuild();

    std::filesystem::path Dir;
    // the file's layouts as read, the unreadable ones too, which Write keeps
    std::vector<LayoutConfiguration> Custom;
    std::vector<LayoutChoice> Choices;
    // the file didn't parse at all: Save never writes over it
    bool FileBroken = false;
};

}
