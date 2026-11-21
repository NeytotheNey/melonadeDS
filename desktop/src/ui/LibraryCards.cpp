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

bool GameArt(App& app, ImDrawList* draw, const Frontend::LibraryEntry& entry, ImVec2 min, ImVec2 max, float rounding, float fraction)
{
    return GameArtIn(app, draw, entry, min, max, rounding, min, max, fraction);
}

bool IsTitleArt(const Frontend::ArtRef& file)
{
    const std::string& p = file.Path;
    return p.size() >= 9 && p.compare(p.size() - 9, 9, "title.png") == 0;
}

void Scrim(App& app, ImDrawList* draw, ImVec2 min, ImVec2 max, float top, float rounding)
{
    // a soft edge, then the text's band
    float soft = app.Px(28);
    ImU32 clear = IM_COL32(0, 0, 0, 0), dark = IM_COL32(0, 0, 0, 170);
    draw->AddRectFilledMultiColor(ImVec2(min.x, top - soft), ImVec2(max.x, top), clear, clear, dark, dark);
    draw->AddRectFilled(ImVec2(min.x, top), max, dark, rounding, ImDrawFlags_RoundBottom);
}

bool GameArtIn(App& app, ImDrawList* draw, const Frontend::LibraryEntry& entry, ImVec2 min, ImVec2 max, float rounding, ImVec2 iconMin,
    ImVec2 iconMax, float fraction)
{
    const Palette& p = app.Colors();
    // the card takes some of the icon's color, more in the dark theme, where the surface is darker
    ImU32 card = entry.Tint ? Blend(p.SurfaceContainerHigh, entry.Tint | 0xFF000000u, p.Dark ? 0.30f : 0.38f) : p.SurfaceContainerHighest;
    draw->AddRectFilled(min, max, card, rounding);
    if (!entry.ArtFile.empty() && ArtImage(app, draw, entry.ArtFile, false, min, max, rounding, false))
        return true;
    float w = iconMax.x - iconMin.x, h = iconMax.y - iconMin.y;
    ImVec2 c(iconMin.x + w / 2, iconMin.y + h / 2);
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
    return false;
}

bool ArtImage(App& app, ImDrawList* draw, const Frontend::ArtRef& file, bool topScreen, ImVec2 min, ImVec2 max, float rounding, bool cover)
{
    int tw = 0, th = 0;
    GLuint texture = app.ArtTexture(file.Path, file.Stamp, topScreen, tw, th);
    if (!texture || tw <= 0 || th <= 0)
        return false;
    float bw = max.x - min.x, bh = max.y - min.y;
    if (bw <= 0 || bh <= 0)
        return true;
    float ta = (float)tw / th, ba = bw / bh;
    ImVec2 p0 = min, p1 = max, uv0(0, 0), uv1(1, 1);
    if (cover)
    {
        // the middle of the image, as much as has the box's shape
        if (ta > ba)
        {
            float f = ba / ta;
            uv0.x = (1 - f) / 2;
            uv1.x = 1 - uv0.x;
        }
        else
        {
            float f = ta / ba;
            uv0.y = (1 - f) / 2;
            uv1.y = 1 - uv0.y;
        }
    }
    else
    {
        // all of it, centered, on whole pixels
        float w = ta > ba ? bw : bh * ta, h = ta > ba ? bw / ta : bh;
        p0 = ImVec2(IM_ROUND(min.x + (bw - w) / 2), IM_ROUND(min.y + (bh - h) / 2));
        p1 = ImVec2(IM_ROUND(p0.x + w), IM_ROUND(p0.y + h));
    }
    draw->AddImageRounded(ImTextureRef((ImTextureID)(intptr_t)texture), p0, p1, uv0, uv1, IM_COL32_WHITE, rounding);
    return true;
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

std::string DaysAgo(int64_t ms)
{
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t days = std::max<int64_t>(0, (now - ms) / (24ll * 60 * 60 * 1000));
    return days == 0 ? "today" : days == 1 ? "yesterday" : std::to_string(days) + " days ago";
}

std::string MetadataCredit(const Frontend::LibraryEntry& e, bool hasKey)
{
    using Frontend::RaGameRow;
    if (e.State != Frontend::LibraryEntry::kRead)
        return "";
    const char* dot = "  \xc2\xb7  ";
    if (const RaGameRow* g = e.RaGame.get())
    {
        // an image that failed for good clears the fetch time, so the next manual refresh fetches the game again
        std::string ago = g->FetchedAt > 0 ? DaysAgo(g->FetchedAt) : "";
        switch (g->FetchStatus)
        {
        case RaGameRow::kOk:
            return std::string("Metadata from RetroAchievements") + (ago.empty() ? "" : dot + std::string("cached ") + ago);
        case RaGameRow::kNotFound:
            return std::string("Not found on RetroAchievements") + (ago.empty() ? "" : dot + std::string("checked ") + ago);
        default:
            // both a failed fetch and a refresh asked for leave a row this way
            if (g->HasData())
                return hasKey ? std::string("Metadata from RetroAchievements") + dot + "due for a refresh" : "Metadata from RetroAchievements";
            if (hasKey)
                return std::string("No metadata from RetroAchievements yet") + (ago.empty() ? "" : dot + std::string("last tried ") + ago);
            return "No RetroAchievements Web API key set";
        }
    }
    if (e.RaHashState == Frontend::kRaHashMissing)
        return "Can't be looked up on RetroAchievements";
    if (e.RaHashState == Frontend::kRaHashUnmatched)
        return "Hash not found on RetroAchievements";
    return hasKey ? "RetroAchievements metadata not downloaded yet" : "No RetroAchievements Web API key set";
}

std::string ArtCredit(const Frontend::LibraryEntry& e, bool libretro)
{
    if (e.State != Frontend::LibraryEntry::kRead)
        return "";
    if (e.ArtSource == Frontend::kArtRetroAchievements)
        return "Art from RetroAchievements (retroachievements.org)";
    if (e.ArtSource == Frontend::kArtLibretro)
        return "Art from the libretro thumbnails (No-Intro: " + e.LibretroName + ")";
    if (!libretro)
        return "No art downloaded (libretro thumbnails are off)";
    if (!e.LibretroName.empty())
        return "No art downloaded yet (No-Intro: " + e.LibretroName + ")";
    return "No art downloaded";
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
