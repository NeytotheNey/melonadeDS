#include "Theme.h"

#include <windows.h>

#include <algorithm>

namespace Ui
{

namespace
{

constexpr ImU32 Hex(unsigned rgb)
{
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0xFF);
}

ImVec4 Vec(ImU32 color)
{
    return ImGui::ColorConvertU32ToFloat4(color);
}

Palette MakeLight()
{
    // app/src/main/res/values/colors.xml
    Palette p;
    p.Dark = false;
    p.Primary = Hex(0xB81311);
    p.OnPrimary = Hex(0xFFFFFF);
    p.PrimaryContainer = Hex(0xDC3128);
    p.OnPrimaryContainer = Hex(0xFFFBFF);
    p.InversePrimary = Hex(0xFFB4A9);
    p.Secondary = Hex(0xA23D32);
    p.OnSecondary = Hex(0xFFFFFF);
    p.SecondaryContainer = Hex(0xFE8172);
    p.OnSecondaryContainer = Hex(0x731A13);
    p.Tertiary = Hex(0x006576);
    p.OnTertiary = Hex(0xFFFFFF);
    p.TertiaryContainer = Hex(0x008095);
    p.OnTertiaryContainer = Hex(0xF9FDFF);
    p.Error = Hex(0xBA1A1A);
    p.OnError = Hex(0xFFFFFF);
    p.ErrorContainer = Hex(0xFFDAD6);
    p.OnErrorContainer = Hex(0x93000A);
    p.Background = Hex(0xFFF8F7);
    p.OnBackground = Hex(0x231918);
    p.Surface = Hex(0xFFF8F7);
    p.OnSurface = Hex(0x231918);
    p.SurfaceVariant = Hex(0xF5DDDA);
    p.OnSurfaceVariant = Hex(0x534341);
    p.InverseSurface = Hex(0x392E2C);
    p.InverseOnSurface = Hex(0xFFEDEA);
    p.Outline = Hex(0x857370);
    p.OutlineVariant = Hex(0xD8C2BE);
    p.SurfaceBright = Hex(0xFFF8F7);
    p.SurfaceDim = Hex(0xE8D6D3);
    p.SurfaceContainerLowest = Hex(0xFFFFFF);
    p.SurfaceContainerLow = Hex(0xFFF0EE);
    p.SurfaceContainer = Hex(0xFCEAE7);
    p.SurfaceContainerHigh = Hex(0xF7E4E1);
    p.SurfaceContainerHighest = Hex(0xF1DEDC);
    p.Scrim = Hex(0x000000);
    return p;
}

Palette MakeDark()
{
    // app/src/main/res/values-night/colors.xml
    Palette p;
    p.Dark = true;
    p.Primary = Hex(0xFFB4A9);
    p.OnPrimary = Hex(0x690002);
    p.PrimaryContainer = Hex(0xFF5545);
    p.OnPrimaryContainer = Hex(0x450001);
    p.InversePrimary = Hex(0xBB1614);
    p.Secondary = Hex(0xFFB4A9);
    p.OnSecondary = Hex(0x630E0A);
    p.SecondaryContainer = Hex(0x83251D);
    p.OnSecondaryContainer = Hex(0xFF9A8C);
    p.Tertiary = Hex(0x6AD5EE);
    p.OnTertiary = Hex(0x003640);
    p.TertiaryContainer = Hex(0x239EB6);
    p.OnTertiaryContainer = Hex(0x002129);
    p.Error = Hex(0xFFB4AB);
    p.OnError = Hex(0x690005);
    p.ErrorContainer = Hex(0x93000A);
    p.OnErrorContainer = Hex(0xFFDAD6);
    p.Background = Hex(0x1A1110);
    p.OnBackground = Hex(0xF1DEDC);
    p.Surface = Hex(0x1A1110);
    p.OnSurface = Hex(0xF1DEDC);
    p.SurfaceVariant = Hex(0x534341);
    p.OnSurfaceVariant = Hex(0xD8C2BE);
    p.InverseSurface = Hex(0xF1DEDC);
    p.InverseOnSurface = Hex(0x392E2C);
    p.Outline = Hex(0xA08C89);
    p.OutlineVariant = Hex(0x534341);
    p.SurfaceBright = Hex(0x423735);
    p.SurfaceDim = Hex(0x1A1110);
    p.SurfaceContainerLowest = Hex(0x140C0B);
    p.SurfaceContainerLow = Hex(0x231918);
    p.SurfaceContainer = Hex(0x271D1C);
    p.SurfaceContainerHigh = Hex(0x322826);
    p.SurfaceContainerHighest = Hex(0x3D3231);
    p.Scrim = Hex(0x000000);
    return p;
}

}

const Palette& LightPalette()
{
    static const Palette palette = MakeLight();
    return palette;
}

const Palette& DarkPalette()
{
    static const Palette palette = MakeDark();
    return palette;
}

bool IsDarkTheme(const std::string& setting)
{
    if (setting == "light")
        return false;
    if (setting == "dark")
        return true;
    // Windows' "app mode": AppsUseLightTheme is 0 for dark, and missing (light) before Windows 10 1809
    DWORD light = 1, size = sizeof(light);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
            RRF_RT_REG_DWORD, nullptr, &light, &size) != ERROR_SUCCESS)
        return false;
    return light == 0;
}

ImU32 WithAlpha(ImU32 color, float alpha)
{
    unsigned a = (unsigned)(((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.f, 1.f) + 0.5f);
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

ImU32 Blend(ImU32 base, ImU32 over, float amount)
{
    ImVec4 a = Vec(base), b = Vec(over);
    float t = std::clamp(amount, 0.f, 1.f);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w));
}

void ApplyStyle(const Palette& p, float scale)
{
    ImGuiStyle style;
    style.WindowPadding = ImVec2(0, 0);
    style.WindowRounding = 0;
    style.WindowBorderSize = 0;
    style.ChildBorderSize = 0;
    style.FramePadding = ImVec2(12, 8);
    style.FrameRounding = 12;
    style.ItemSpacing = ImVec2(8, 8);
    style.ScrollbarSize = 10;
    style.ScrollbarRounding = 5;
    style.GrabRounding = 5;
    style.FontSizeBase = 18;
    style.ScaleAllSizes(scale);
    style.FontScaleMain = scale;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = Vec(p.OnSurface);
    c[ImGuiCol_TextDisabled] = Vec(WithAlpha(p.OnSurface, 0.38f));
    c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = Vec(p.SurfaceContainerHigh);
    c[ImGuiCol_Border] = Vec(p.OutlineVariant);
    c[ImGuiCol_FrameBg] = Vec(p.SurfaceContainerHighest);
    c[ImGuiCol_FrameBgHovered] = Vec(Blend(p.SurfaceContainerHighest, p.OnSurface, 0.08f));
    c[ImGuiCol_FrameBgActive] = Vec(Blend(p.SurfaceContainerHighest, p.OnSurface, 0.12f));
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = Vec(WithAlpha(p.Outline, 0.6f));
    c[ImGuiCol_ScrollbarGrabHovered] = Vec(p.Outline);
    c[ImGuiCol_ScrollbarGrabActive] = Vec(p.Primary);
    c[ImGuiCol_Button] = Vec(p.SecondaryContainer);
    c[ImGuiCol_ButtonHovered] = Vec(Blend(p.SecondaryContainer, p.OnSecondaryContainer, 0.08f));
    c[ImGuiCol_ButtonActive] = Vec(Blend(p.SecondaryContainer, p.OnSecondaryContainer, 0.12f));
    c[ImGuiCol_Header] = Vec(p.SecondaryContainer);
    c[ImGuiCol_HeaderHovered] = Vec(Blend(p.SecondaryContainer, p.OnSecondaryContainer, 0.08f));
    c[ImGuiCol_HeaderActive] = Vec(Blend(p.SecondaryContainer, p.OnSecondaryContainer, 0.12f));
    c[ImGuiCol_CheckMark] = Vec(p.Primary);
    c[ImGuiCol_SliderGrab] = Vec(p.Primary);
    c[ImGuiCol_SliderGrabActive] = Vec(p.Primary);
    c[ImGuiCol_TextSelectedBg] = Vec(WithAlpha(p.Primary, 0.35f));
    // the widgets draw their own focus ring (Widgets.cpp), thicker than ImGui's
    c[ImGuiCol_NavCursor] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ModalWindowDimBg] = Vec(WithAlpha(p.Scrim, 0.6f));
    ImGui::GetStyle() = style;
}

}
