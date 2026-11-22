// The app's layouts (domain/model/layout and impl/dtos/layout): what a layout is, and its layouts.json format, which is the app's
// InternalLayoutsRepository file (SettingsBackupFormat.LAYOUTS_FILE, a JSON list of LayoutConfigurationDto), so a file copied from the
// phone loads here and one written here loads on the phone. Everything is kept as the file has it, the on-screen buttons the desktop never
// draws included, and so are fields this build doesn't know: each object keeps them in Extra and they're written back. The one addition is
// a layout's "detached" pair, the one-screen layouts of the detached windows, which the app's Gson parsing skips as an unknown field
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "picojson/picojson.h"

namespace Frontend
{

// Rect.kt, in whole pixels
struct IntRect
{
    int X = 0, Y = 0, W = 0, H = 0;

    int Right() const { return X + W; }
    int Bottom() const { return Y + H; }
    bool Overlaps(const IntRect& o) const { return X < o.Right() && o.X < Right() && Y < o.Bottom() && o.Y < Bottom(); }
    bool operator==(const IntRect&) const = default;
};

// BackgroundMode.kt
enum class BackgroundMode { Stretch, FitCenter, FitTop, FitLeft, FitBottom, FitRight };
// the app's names (STRETCH, FIT_CENTER, ...)
const char* BackgroundModeName(BackgroundMode mode);
// one of those in any case, or with dashes (fit-center); nothing for one it doesn't know
std::optional<BackgroundMode> ParseBackgroundMode(const std::string& name);

// PositionedLayoutComponent: a screen or an on-screen control and its rect. [Component] is the app's LayoutComponent name (TOP_SCREEN,
// BOTTOM_SCREEN, DPAD, BUTTON_L, ...), kept as it is when this build doesn't know it
struct LayoutComponent
{
    IntRect Rect;
    std::string Component;
    float Alpha = 1;
    bool OnTop = false;
    float Spacing = 0;
    picojson::object Extra, RectExtra;

    bool IsTopScreen() const { return Component == "TOP_SCREEN"; }
    bool IsBottomScreen() const { return Component == "BOTTOM_SCREEN"; }
    bool IsScreen() const { return IsTopScreen() || IsBottomScreen(); }
};

// ScreenLayout: one display's background and components; no components (null) means the default layout's
struct ScreenLayout
{
    std::optional<std::string> BackgroundId;
    // as stored (the app's BackgroundMode names); GetMode reads it
    std::string Mode = "FIT_CENTER";
    std::optional<std::vector<LayoutComponent>> Components;
    picojson::object Extra;

    BackgroundMode GetMode() const { return ParseBackgroundMode(Mode).value_or(BackgroundMode::FitCenter); }
    // the first component called [name], or null
    const LayoutComponent* Find(const char* name) const;
};

// UILayout: the main display's layout, the second display's (the phone's external display or second screen) and the variant's widescreen
// ratio (WidescreenAspect.key, "16:9"; none for a normal 4:3 top screen)
struct UILayout
{
    ScreenLayout Main, Secondary;
    std::optional<std::string> WideAspect;
    picojson::object Extra;
};

// LayoutDisplay
struct LayoutDisplay
{
    int Id = 0;
    std::string Type = "BUILT_IN";
    int W = 0, H = 0;
    picojson::object Extra;

    bool operator==(const LayoutDisplay& o) const { return Id == o.Id && Type == o.Type && W == o.W && H == o.H; }
};

// ScreenFold
struct ScreenFold
{
    std::string Orientation, Type;
    IntRect Bounds;
    picojson::object Extra, BoundsExtra;

    bool operator==(const ScreenFold& o) const { return Orientation == o.Orientation && Type == o.Type && Bounds == o.Bounds; }
};

// UILayoutVariant: the surface a layout was made for (its size, insets, orientation, folds and displays)
struct LayoutVariant
{
    int W = 0, H = 0;
    int InsetLeft = 0, InsetTop = 0, InsetRight = 0, InsetBottom = 0;
    std::string Orientation = "PORTRAIT";
    std::vector<ScreenFold> Folds;
    LayoutDisplay MainDisplay;
    std::optional<LayoutDisplay> SecondDisplay;
    picojson::object Extra, SizeExtra, InsetsExtra, DisplaysExtra;

    bool IsLandscape() const { return Orientation == "LANDSCAPE"; }
    // as the app's data class compares them (the unknown fields left out)
    bool operator==(const LayoutVariant& o) const;
};

// LayoutEntryDto: a variant and its layout
struct LayoutEntry
{
    LayoutVariant Variant;
    UILayout Layout;
    picojson::object Extra;
};

// the desktop's own: each detached window's one-screen layouts, by the DS screen the window shows (the layout goes with the screen when
// they're swapped), with variants for window sizes as the together layouts have. Each entry's Main has the window's one screen
struct DetachedLayouts
{
    std::vector<LayoutEntry> Top, Bottom;
    picojson::object Extra;
};

// LayoutConfiguration
struct LayoutConfiguration
{
    std::optional<std::string> Id, Name;
    // DEFAULT or CUSTOM; FOLLOW_SYSTEM, PORTRAIT or LANDSCAPE (the phone turns to it; a window can't)
    std::string Type = "CUSTOM", Orientation = "FOLLOW_SYSTEM";
    // the on-screen buttons' opacity, which the desktop doesn't draw
    bool UseCustomOpacity = false;
    int Opacity = 50;
    std::vector<LayoutEntry> Variants;
    std::optional<DetachedLayouts> Detached;
    picojson::object Extra;
    // a layout the app couldn't read either (a required field missing or of the wrong type): never shown, written back as it was
    bool Unreadable = false;
    picojson::value Raw;

    // LayoutConfiguration.isWide: any variant has a widescreen ratio this build knows
    bool IsWide() const;
};

// WidescreenAspect: whether [key] is one this build knows ("16:10", "16:9", "20:9", "21:9")
bool IsKnownWideAspect(const std::string& key);

// layouts.json's text into [out], as the app's LayoutConfigurationDto.toModel reads it (a top-level wideAspect, from before wide variants,
// goes to each variant that has screens placed and no ratio of its own). False with [error] when it isn't a list of layouts (then [out] is
// empty), or when a layout misses what the app requires: that one is in [out] as Unreadable. Sizes are capped (rule 2 of the coding
// standards)
bool ParseLayouts(const std::string& json, std::vector<LayoutConfiguration>& out, std::string& error);
// the layouts as layouts.json, the app's fields (without the old top-level wideAspect, as the app writes it) and the unknown ones, keys
// sorted as the app's settings backup has them
std::string WriteLayouts(const std::vector<LayoutConfiguration>& layouts);

}
