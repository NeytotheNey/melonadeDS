// The menus' look: the Android app's Material 3 color scheme (app/src/main/res/values*/colors.xml, the m3* colors that Colors.kt reads),
// light and dark, and the ImGui style built from it
#pragma once

#include <string>

#include "imgui.h"

namespace Ui
{

struct Palette
{
    bool Dark = false;
    ImU32 Primary, OnPrimary, PrimaryContainer, OnPrimaryContainer, InversePrimary;
    ImU32 Secondary, OnSecondary, SecondaryContainer, OnSecondaryContainer;
    ImU32 Tertiary, OnTertiary, TertiaryContainer, OnTertiaryContainer;
    ImU32 Error, OnError, ErrorContainer, OnErrorContainer;
    ImU32 Background, OnBackground, Surface, OnSurface, SurfaceVariant, OnSurfaceVariant;
    ImU32 InverseSurface, InverseOnSurface, Outline, OutlineVariant;
    ImU32 SurfaceBright, SurfaceDim;
    ImU32 SurfaceContainerLowest, SurfaceContainerLow, SurfaceContainer, SurfaceContainerHigh, SurfaceContainerHighest;
    // M3's scrim (black), used at 60% over the game behind the pause menu
    ImU32 Scrim;
};

const Palette& LightPalette();
const Palette& DarkPalette();

// desktop_theme: "light", "dark", or "system" (anything else), which follows Windows' app mode
bool IsDarkTheme(const std::string& setting);

// [color] with its alpha times [alpha]
ImU32 WithAlpha(ImU32 color, float alpha);
// M3's state layers: [over] at [amount] over [base]
ImU32 Blend(ImU32 base, ImU32 over, float amount);

// the ImGui style for [palette] at [scale] (sizes times the UI scale)
void ApplyStyle(const Palette& palette, float scale);

}
