#include "RomConfig.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "picojson/picojson.h"

namespace Frontend
{

namespace
{

constexpr const char* kConsoleKey = "runtimeConsoleType";
constexpr const char* kNameKey = "customName";
constexpr const char* kGbaKey = "gbaSlotConfig.type";
constexpr const char* kGbaRomKey = "gbaSlotConfig.gbaRomPath";
constexpr const char* kLayoutKey = "layoutId";
// RomConfigDto.mods (RomModsConfigDto)
constexpr const char* kWideViewWidthKey = "mods.wideViewWidth";
constexpr const char* kWideCameraKey = "mods.wideCamera";
constexpr const char* kWideCameraMaxWidthKey = "mods.wideCameraMaxWidth";
constexpr const char* kWideCameraReturnToCenterKey = "mods.wideCameraReturnToCenter";
constexpr const char* kWideCameraZoomKey = "mods.wideCameraZoom";
constexpr const char* kWideCameraZoomOnTextKey = "mods.wideCameraZoomOnText";
constexpr const char* kLuaScriptFolderKey = "mods.luaScriptFolder";
// RomModsConfigDto.luaScripts as JSON: [{"fileName": ..., "enabled": ..., "options": {key: value}}]
constexpr const char* kLuaScriptsKey = "mods.luaScripts";
// a stored width or zoom past these is no setting the app could have written, so it reads as none
constexpr long MAX_STORED_WIDTH = 100000;
constexpr float MAX_STORED_ZOOM = 100.f;
// the most scripts and option values per script read back; past them the rest are dropped (a folder walk finds at most 4096 files, and a
// header declares at most 16 options, so the app writes far fewer)
constexpr size_t MAX_STORED_SCRIPTS = 4096;
constexpr size_t MAX_STORED_OPTIONS = 256;

std::string LuaScriptsJson(const std::vector<RomLuaScript>& scripts)
{
    picojson::array list;
    for (const RomLuaScript& script : scripts)
    {
        picojson::object options;
        for (const auto& [key, value] : script.Options)
            options[key] = picojson::value(value);
        picojson::object entry;
        entry["fileName"] = picojson::value(script.FileName);
        entry["enabled"] = picojson::value(script.Enabled);
        entry["options"] = picojson::value(std::move(options));
        list.push_back(picojson::value(std::move(entry)));
    }
    return picojson::value(std::move(list)).serialize();
}

// whether [text]'s arrays and objects nest no deeper than [maxDepth], so picojson's recursive parse can't run out of stack
bool IsNestingWithin(const std::string& text, int maxDepth)
{
    int depth = 0;
    bool inString = false, escaped = false;
    for (char c : text)
    {
        if (inString)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"')
            inString = true;
        else if ((c == '[' || c == '{') && ++depth > maxDepth)
            return false;
        else if (c == ']' || c == '}')
            depth--;
    }
    return true;
}

// As RomModsConfigDto.toModel reads Gson's: entries without a file name are dropped, a missing switch is off, and option values that aren't
// text are dropped (Gson's own leniency reads booleans and numbers as their text, kept here too); JSON that doesn't parse is no scripts
std::vector<RomLuaScript> ParseLuaScripts(const std::string& text)
{
    // the list, its entries and their options: three levels, with room to spare
    constexpr int MAX_NESTING = 8;
    std::vector<RomLuaScript> scripts;
    picojson::value root;
    if (text.empty() || !IsNestingWithin(text, MAX_NESTING) || !picojson::parse(root, text).empty() || !root.is<picojson::array>())
        return scripts;
    for (const picojson::value& item : root.get<picojson::array>())
    {
        if (scripts.size() >= MAX_STORED_SCRIPTS)
            break;
        if (!item.is<picojson::object>())
            continue;
        const picojson::object& entry = item.get<picojson::object>();
        auto fileName = entry.find("fileName");
        if (fileName == entry.end() || !fileName->second.is<std::string>())
            continue;
        RomLuaScript script;
        script.FileName = fileName->second.get<std::string>();
        auto enabled = entry.find("enabled");
        script.Enabled = enabled != entry.end() && enabled->second.is<bool>() && enabled->second.get<bool>();
        auto options = entry.find("options");
        if (options != entry.end() && options->second.is<picojson::object>())
        {
            for (const auto& [key, value] : options->second.get<picojson::object>())
            {
                if (script.Options.size() >= MAX_STORED_OPTIONS)
                    break;
                if (value.is<std::string>())
                    script.Options[key] = value.get<std::string>();
                else if (value.is<bool>() || value.is<double>())
                    script.Options[key] = value.to_str();
            }
        }
        scripts.push_back(std::move(script));
    }
    return scripts;
}

// the whole of [text] as a whole number within +-MAX_STORED_WIDTH, or none
std::optional<int> ParseWidth(const std::string& text)
{
    if (text.empty())
        return std::nullopt;
    errno = 0;
    char* end = nullptr;
    long value = strtol(text.c_str(), &end, 10);
    if (errno != 0 || *end != '\0' || value < -MAX_STORED_WIDTH || value > MAX_STORED_WIDTH)
        return std::nullopt;
    return (int)value;
}

// the whole of [text] as a finite number within +-MAX_STORED_ZOOM, or none
std::optional<float> ParseZoom(const std::string& text)
{
    if (text.empty())
        return std::nullopt;
    char* end = nullptr;
    float value = strtof(text.c_str(), &end);
    if (*end != '\0' || !std::isfinite(value) || std::fabs(value) > MAX_STORED_ZOOM)
        return std::nullopt;
    return value;
}

// --rom-config's on and off (true and false too, as stored)
bool ParseSwitch(const std::string& text, bool& value)
{
    if (text == "on" || text == "true")
        value = true;
    else if (text == "off" || text == "false")
        value = false;
    else
        return false;
    return true;
}

std::string ZoomText(float zoom)
{
    char text[32];
    snprintf(text, sizeof(text), "%g", zoom);
    return text;
}

// the app's enum names, as its RomConfigDto stores them
const char* ConsoleValue(RomConfig::Console c)
{
    switch (c)
    {
    case RomConfig::Console::Ds: return "DS";
    case RomConfig::Console::Dsi: return "DSi";
    default: return "DEFAULT";
    }
}

const char* GbaValue(RomConfig::GbaSlot s)
{
    switch (s)
    {
    case RomConfig::GbaSlot::GbaRom: return "GbaRom";
    case RomConfig::GbaSlot::RumblePak: return "RumblePak";
    case RomConfig::GbaSlot::MemoryExpansion: return "MemoryExpansion";
    default: return "None";
    }
}

}

const std::vector<std::string>& RomConfig::Keys()
{
    static const std::vector<std::string> keys = {kConsoleKey, kNameKey, kGbaKey, kGbaRomKey, kLayoutKey, kWideViewWidthKey, kWideCameraKey,
        kWideCameraMaxWidthKey, kWideCameraReturnToCenterKey, kWideCameraZoomKey, kWideCameraZoomOnTextKey, kLuaScriptFolderKey, kLuaScriptsKey};
    return keys;
}

float RomConfig::ValidWideCameraZoom() const
{
    return WideCameraZoom && std::isfinite(*WideCameraZoom) && *WideCameraZoom >= 1.f ? *WideCameraZoom : 1.f;
}

std::map<std::string, std::string> RomConfig::ToFields() const
{
    std::map<std::string, std::string> fields;
    if (ConsoleType != Console::Default)
        fields[kConsoleKey] = ConsoleValue(ConsoleType);
    if (!CustomName.empty())
        fields[kNameKey] = CustomName;
    if (Gba != GbaSlot::None)
        fields[kGbaKey] = GbaValue(Gba);
    // (also with another slot chosen, so the file is still there when GBA ROM is chosen again)
    if (!GbaRomPath.empty())
        fields[kGbaRomKey] = GbaRomPath;
    if (!LayoutId.empty())
        fields[kLayoutKey] = LayoutId;
    if (WideViewWidth)
        fields[kWideViewWidthKey] = std::to_string(*WideViewWidth);
    if (WideCamera)
        fields[kWideCameraKey] = "true";
    if (WideCameraMaxWidth)
        fields[kWideCameraMaxWidthKey] = std::to_string(*WideCameraMaxWidth);
    if (!WideCameraReturnToCenter)
        fields[kWideCameraReturnToCenterKey] = "false";
    if (WideCameraZoom)
        fields[kWideCameraZoomKey] = ZoomText(*WideCameraZoom);
    if (!WideCameraZoomOnText)
        fields[kWideCameraZoomOnTextKey] = "false";
    if (!LuaScriptFolder.empty())
        fields[kLuaScriptFolderKey] = LuaScriptFolder;
    if (!LuaScripts.empty())
        fields[kLuaScriptsKey] = LuaScriptsJson(LuaScripts);
    return fields;
}

RomConfig RomConfig::FromFields(const std::map<std::string, std::string>& fields)
{
    RomConfig c;
    auto get = [&](const char* key) -> std::string
    {
        auto it = fields.find(key);
        return it != fields.end() ? it->second : std::string();
    };
    std::string console = get(kConsoleKey);
    c.ConsoleType = console == "DS" ? Console::Ds : console == "DSi" ? Console::Dsi : Console::Default;
    c.CustomName = get(kNameKey);
    std::string gba = get(kGbaKey);
    for (GbaSlot s : {GbaSlot::GbaRom, GbaSlot::RumblePak, GbaSlot::MemoryExpansion})
    {
        if (gba == GbaValue(s))
            c.Gba = s;
    }
    c.GbaRomPath = get(kGbaRomKey);
    c.LayoutId = get(kLayoutKey);
    // what doesn't read as its type counts as missing, as Gson's null does in the app: the field's default
    c.WideViewWidth = ParseWidth(get(kWideViewWidthKey));
    c.WideCamera = get(kWideCameraKey) == "true";
    c.WideCameraMaxWidth = ParseWidth(get(kWideCameraMaxWidthKey));
    c.WideCameraReturnToCenter = get(kWideCameraReturnToCenterKey) != "false";
    c.WideCameraZoom = ParseZoom(get(kWideCameraZoomKey));
    c.WideCameraZoomOnText = get(kWideCameraZoomOnTextKey) != "false";
    c.LuaScriptFolder = get(kLuaScriptFolderKey);
    c.LuaScripts = ParseLuaScripts(get(kLuaScriptsKey));
    return c;
}

bool RomConfig::Apply(const std::string& key, const std::string& value, std::string& error)
{
    if (key == "console")
    {
        if (value == "default") ConsoleType = Console::Default;
        else if (value == "ds") ConsoleType = Console::Ds;
        else if (value == "dsi") ConsoleType = Console::Dsi;
        else { error = "console is default, ds or dsi"; return false; }
    }
    else if (key == "gba-slot")
    {
        if (value == "none") Gba = GbaSlot::None;
        else if (value == "gba-rom") Gba = GbaSlot::GbaRom;
        else if (value == "rumble-pak") Gba = GbaSlot::RumblePak;
        else if (value == "memory-expansion") Gba = GbaSlot::MemoryExpansion;
        else { error = "gba-slot is none, gba-rom, rumble-pak or memory-expansion"; return false; }
    }
    else if (key == "gba-rom")
    {
        GbaRomPath = value;
        if (!value.empty())
            Gba = GbaSlot::GbaRom;
    }
    else if (key == "name")
        CustomName = value;
    else if (key == "layout")
        LayoutId = value == "global" ? std::string() : value;
    else if (key == "wideViewWidth" || key == "wideCameraMaxWidth")
    {
        bool isView = key == "wideViewWidth";
        std::optional<int> width = ParseWidth(value);
        if (value == (isView ? "off" : "default"))
            width.reset();
        else if (!width)
        {
            error = key + (isView ? " is a width or off" : " is a width or default");
            return false;
        }
        (isView ? WideViewWidth : WideCameraMaxWidth) = width;
    }
    else if (key == "wideCameraZoom")
    {
        std::optional<float> zoom = ParseZoom(value);
        if (value != "default" && !zoom)
        {
            error = "wideCameraZoom is a zoom (1 = native) or default";
            return false;
        }
        WideCameraZoom = value == "default" ? std::nullopt : zoom;
    }
    else if (key == "wideCamera" || key == "wideCameraReturnToCenter" || key == "wideCameraZoomOnText")
    {
        bool& field = key == "wideCamera" ? WideCamera : key == "wideCameraReturnToCenter" ? WideCameraReturnToCenter : WideCameraZoomOnText;
        if (!ParseSwitch(value, field))
        {
            error = key + " is on or off";
            return false;
        }
    }
    else
    {
        error = "the keys are console, gba-slot, gba-rom, name, layout, wideViewWidth, wideCamera, wideCameraMaxWidth, "
                "wideCameraReturnToCenter, wideCameraZoom and wideCameraZoomOnText";
        return false;
    }
    return true;
}

const char* RomConfig::ConsoleName(Console c)
{
    switch (c)
    {
    case Console::Ds: return "DS";
    case Console::Dsi: return "DSi";
    default: return "Default";
    }
}

const char* RomConfig::GbaSlotName(GbaSlot s)
{
    switch (s)
    {
    case GbaSlot::GbaRom: return "GBA ROM";
    case GbaSlot::RumblePak: return "Rumble Pak";
    case GbaSlot::MemoryExpansion: return "Memory expansion";
    default: return "None";
    }
}

std::string RomConfig::Describe() const
{
    std::string text;
    auto add = [&](const std::string& part) { text += (text.empty() ? "" : ", ") + part; };
    if (ConsoleType != Console::Default)
        add(std::string("console ") + ConsoleName(ConsoleType));
    if (Gba != GbaSlot::None)
        add(std::string("GBA slot ") + GbaSlotName(Gba) + (Gba == GbaSlot::GbaRom ? " (" + (GbaRomPath.empty() ? std::string("not set") : GbaRomPath) + ")" : ""));
    else if (!GbaRomPath.empty())
        add("GBA ROM kept for later (" + GbaRomPath + ")");
    if (!CustomName.empty())
        add("name \"" + CustomName + "\"");
    if (!LayoutId.empty())
        add("layout " + LayoutId);
    if (WideViewWidth)
        add("widescreen " + std::to_string(*WideViewWidth));
    if (WideCamera)
        add("wide camera");
    if (WideCameraMaxWidth)
        add("camera zooms out to " + std::to_string(*WideCameraMaxWidth));
    if (!WideCameraReturnToCenter)
        add("camera stays off center");
    if (WideCameraZoom)
        add("camera zoom " + ZoomText(*WideCameraZoom));
    if (!WideCameraZoomOnText)
        add("camera stays out on text");
    if (!LuaScriptFolder.empty())
        add("Lua script folder " + LuaScriptFolder);
    if (!LuaScripts.empty())
    {
        size_t on = std::count_if(LuaScripts.begin(), LuaScripts.end(), [](const RomLuaScript& s) { return s.Enabled; });
        add(std::to_string(LuaScripts.size()) + " Lua scripts (" + std::to_string(on) + " on)");
    }
    return text.empty() ? "defaults" : text;
}

}
