#include "RomConfig.h"

namespace Frontend
{

namespace
{

constexpr const char* kConsoleKey = "runtimeConsoleType";
constexpr const char* kNameKey = "customName";
constexpr const char* kGbaKey = "gbaSlotConfig.type";
constexpr const char* kGbaRomKey = "gbaSlotConfig.gbaRomPath";
constexpr const char* kLayoutKey = "layoutId";

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
    static const std::vector<std::string> keys = {kConsoleKey, kNameKey, kGbaKey, kGbaRomKey, kLayoutKey};
    return keys;
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
    else
    {
        error = "the keys are console, gba-slot, gba-rom, name and layout";
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
    return text.empty() ? "defaults" : text;
}

}
