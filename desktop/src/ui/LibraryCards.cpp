#include "LibraryCards.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

#include "imgui_internal.h"
#include "Icons.h"
#include "RomFile.h"
#include "Ui.h"
#include "Widgets.h"

namespace Ui
{

void GameArt(App& app, ImDrawList* draw, const Frontend::LibraryEntry& entry, ImVec2 min, ImVec2 max, float rounding, float fraction)
{
    const Palette& p = app.Colors();
    float w = max.x - min.x, h = max.y - min.y;
    // the card takes some of the icon's color, more in the dark theme, where the surface is darker
    ImU32 card = entry.Tint ? Blend(p.SurfaceContainerHigh, entry.Tint | 0xFF000000u, p.Dark ? 0.30f : 0.38f) : p.SurfaceContainerHighest;
    draw->AddRectFilled(min, max, card, rounding);
    ImVec2 c(min.x + w / 2, min.y + h / 2);
    if (entry.Icon)
    {
        GLuint texture = app.IconTexture(entry.Id, entry.Icon);
        float scale = std::max(1.f, std::floor(std::min(w, h) * fraction / 32.f));
        // a window too small for one whole multiple still shows it, smaller
        float side = std::min(32.f * scale, std::min(w, h) - 4);
        ImVec2 i0(IM_ROUND(c.x - side / 2), IM_ROUND(c.y - side / 2));
        if (texture)
            draw->AddImage(ImTextureRef((ImTextureID)(intptr_t)texture), i0, ImVec2(i0.x + side, i0.y + side));
    }
    else
    {
        ImWchar icon = entry.State == Frontend::LibraryEntry::kNotRead ? Icon::Cloud : entry.State == Frontend::LibraryEntry::kFailed
            ? Icon::Info : Icon::SportsEsports;
        float size = std::clamp(std::min(w, h) * 0.32f / app.GetScale(), 18.f, 72.f);
        IconAt(app, icon, size, c, p.OnSurfaceVariant);
    }
}

std::string PlayedText(int64_t lastPlayedMs, int64_t now)
{
    if (lastPlayedMs <= 0)
        return "";
    if (now <= 0)
        now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t minutes = std::max<int64_t>(0, (now - lastPlayedMs) / 60000);
    char text[64];
    if (minutes < 1)
        return "Played just now";
    if (minutes < 60)
        snprintf(text, sizeof(text), "Played %lld minute%s ago", (long long)minutes, minutes == 1 ? "" : "s");
    else if (minutes < 24 * 60)
        snprintf(text, sizeof(text), "Played %lld hour%s ago", (long long)(minutes / 60), minutes / 60 == 1 ? "" : "s");
    else if (minutes < 48 * 60)
        return "Played yesterday";
    else if (minutes < 60 * 24 * 60)
        snprintf(text, sizeof(text), "Played %lld days ago", (long long)(minutes / (24 * 60)));
    else
    {
        time_t t = (time_t)(lastPlayedMs / 1000);
        tm local {};
        localtime_s(&local, &t);
        strftime(text, sizeof(text), "Played %b %d, %Y", &local);
    }
    return text;
}

std::string PlayedDate(int64_t lastPlayedMs)
{
    if (lastPlayedMs <= 0)
        return "Never";
    time_t t = (time_t)(lastPlayedMs / 1000);
    tm local {};
    localtime_s(&local, &t);
    char text[64];
    strftime(text, sizeof(text), "%b %d, %Y, %I:%M %p", &local);
    return text;
}

std::string RegionShort(const std::string& gameCode)
{
    if (gameCode.size() < 4)
        return "";
    switch (gameCode[3])
    {
    case 'E': case 'L': return "USA";
    case 'P': case 'W': case 'X': case 'Y': case 'Z': return "EUR";
    case 'J': return "JPN";
    case 'K': return "KOR";
    case 'C': return "CHN";
    case 'U': return "AUS";
    case 'D': return "GER";
    case 'F': return "FRA";
    case 'I': return "ITA";
    case 'S': return "SPA";
    case 'H': return "NED";
    case 'O': return "USA/EUR";
    case 'A': return "World";
    default: return Frontend::RegionName(gameCode);
    }
}

std::string SizeText(uint64_t bytes)
{
    char text[32];
    if (bytes >= 1024ull * 1024 * 1024)
        snprintf(text, sizeof(text), "%.1f GB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024 * 1024)
        snprintf(text, sizeof(text), "%.0f MB", bytes / (1024.0 * 1024));
    else
        snprintf(text, sizeof(text), "%.0f KB", std::max<uint64_t>(bytes, 1) / 1024.0 + 0.5);
    return text;
}

}
