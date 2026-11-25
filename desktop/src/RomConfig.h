// A game's own settings, the app's RomConfig (phase 2 step B): kept in library.db's rom_config table by the ROM's RetroAchievements hash
// (by its path when rc_hash refused it), so they survive the file being moved or renamed, two copies of a game share them, and a game
// started from the command line has them too. Only what differs from the default is stored, one row per field under the app's
// RomConfigDto names (runtimeConsoleType, customName, gbaSlotConfig.type, gbaSlotConfig.gbaRomPath, layoutId, and the widescreen ones under
// mods.) with its enum names as values (DS, DSi, GbaRom, RumblePak, MemoryExpansion), so a later import from the app maps one to one. Rows
// with other keys (a newer build's fields) are left alone.
//
// What the desktop applies when a game boots: the console, the GBA slot (None, an empty slot, by default, as in the app) and widescreen
// with the WideMelon camera, and the layout while it runs. A DSiWare title always boots as a DSi, as the app's RomConfig.forDsiWareTitle()
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Frontend
{

struct RomConfig
{
    enum class Console { Default, Ds, Dsi };
    enum class GbaSlot { None, GbaRom, RumblePak, MemoryExpansion };

    Console ConsoleType = Console::Default;
    // the name the library shows instead of the banner's title ("ROM display name" in the app); empty: the banner's
    std::string CustomName;
    GbaSlot Gba = GbaSlot::None;
    // the GBA ROM for GbaSlot::GbaRom (UTF-8), kept while another slot is chosen so going back finds it; its save is next to it with
    // .sav, as --gba does
    std::string GbaRomPath;
    // the game's own layout (RomConfig.layoutId, a layout's id); empty: the global layout ("Use global layout" in the app). One that's gone
    // falls back to the global one, as the app's observeLayout does
    std::string LayoutId;
    // the app's RomModsConfig widescreen fields: the top screen's 3D width (none: widescreen off; one the core doesn't take counts as off at
    // launch), the WideMelon camera, its widest view (none: DEFAULT_WIDE_CAMERA_MAX_WIDTH), whether it eases back to the middle while the
    // player moves, the zoom last picked with the Wide camera zoom hotkey (none: 1), and whether a text box pulls it in to 1x
    std::optional<int> WideViewWidth;
    bool WideCamera = false;
    std::optional<int> WideCameraMaxWidth;
    bool WideCameraReturnToCenter = true;
    std::optional<float> WideCameraZoom;
    bool WideCameraZoomOnText = true;

    // RomModsConfig.validWideCameraZoom: the remembered zoom if it's a usable one (the camera clamps it to the game's range), otherwise 1
    float ValidWideCameraZoom() const;

    bool IsDefault() const { return *this == RomConfig(); }
    bool operator==(const RomConfig& o) const
    {
        return ConsoleType == o.ConsoleType && CustomName == o.CustomName && Gba == o.Gba && GbaRomPath == o.GbaRomPath && LayoutId == o.LayoutId
            && WideViewWidth == o.WideViewWidth && WideCamera == o.WideCamera && WideCameraMaxWidth == o.WideCameraMaxWidth
            && WideCameraReturnToCenter == o.WideCameraReturnToCenter && WideCameraZoom == o.WideCameraZoom
            && WideCameraZoomOnText == o.WideCameraZoomOnText;
    }
    bool operator!=(const RomConfig& o) const { return !(*this == o); }

    // the rows to store (key, value), the defaults left out
    std::map<std::string, std::string> ToFields() const;
    // from the stored rows; keys it doesn't know are skipped
    static RomConfig FromFields(const std::map<std::string, std::string>& fields);
    // the keys ToFields can write, which a store replaces (other keys stay)
    static const std::vector<std::string>& Keys();

    // --rom-config KEY=VALUE: console default|ds|dsi, gba-slot none|gba-rom|rumble-pak|memory-expansion, gba-rom FILE, name TEXT,
    // layout ID|global, and the app's names for the widescreen fields: wideViewWidth W|off, wideCamera on|off, wideCameraMaxWidth W|default,
    // wideCameraReturnToCenter on|off, wideCameraZoom Z|default, wideCameraZoomOnText on|off. False with [error] for a key or value it
    // doesn't know
    bool Apply(const std::string& key, const std::string& value, std::string& error);

    // for the menus and the log
    static const char* ConsoleName(Console c);
    static const char* GbaSlotName(GbaSlot s);
    // the fields that differ from the default, for the log: "console DSi, GBA slot Rumble Pak" ("defaults" when none)
    std::string Describe() const;
};

}
