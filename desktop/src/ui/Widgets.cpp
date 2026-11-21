#include "Widgets.h"

#include <algorithm>
#include <cstring>

#include "imgui_internal.h"
#include "Icons.h"
#include "Ui.h"

namespace Ui
{

namespace
{

constexpr float kRowHeight = 52;
constexpr float kLabelSize = 19;
constexpr float kDetailSize = 15;

// an item's state for drawing
struct ItemState
{
    bool Focused, Hovered, Held;
};

ItemState StateOfLastItem();

ItemState StateOfLastItem()
{
    return {ImGui::IsItemFocused(), ImGui::IsItemHovered(), ImGui::IsItemActive()};
}

ImU32 RowBackground(App& app, const ItemState& state, ImU32 base)
{
    const Palette& p = app.Colors();
    if (state.Focused)
        return state.Held ? Blend(p.SecondaryContainer, p.OnSecondaryContainer, 0.12f) : p.SecondaryContainer;
    if (state.Held)
        return Blend(base, p.OnSurface, 0.12f);
    if (state.Hovered)
        return Blend(base, p.OnSurface, 0.08f);
    return base;
}

// an invisible item the size of [size] at the cursor; true when it's chosen. Disabled items can't be focused or chosen
bool Item(const char* id, ImVec2 size, bool enabled)
{
    if (!enabled)
        ImGui::BeginDisabled();
    bool pressed = ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
    if (!enabled)
        ImGui::EndDisabled();
    return pressed && enabled;
}

bool PressedLeft()
{
    return ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft)
        || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft);
}

bool PressedRight()
{
    return ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight)
        || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight);
}

}

ImVec2 TextSize(App& app, ImFont* font, float size, const char* text, float wrapWidth)
{
    return font->CalcTextSizeA(app.Px(size), FLT_MAX, wrapWidth, text);
}

ImVec2 TextAt(App& app, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text, float wrapWidth)
{
    ImGui::GetWindowDrawList()->AddText(font, app.Px(size), ImVec2(IM_ROUND(pos.x), IM_ROUND(pos.y)), color, text, nullptr, wrapWidth);
    return TextSize(app, font, size, text, wrapWidth);
}

std::string Ellipsize(App& app, ImFont* font, float size, const std::string& text, float width)
{
    if (TextSize(app, font, size, text.c_str()).x <= width)
        return text;
    std::string cut = text;
    while (!cut.empty())
    {
        // drop a whole UTF-8 character: its continuation bytes, then its first one
        while (!cut.empty() && ((unsigned char)cut.back() & 0xC0) == 0x80)
            cut.pop_back();
        if (!cut.empty())
            cut.pop_back();
        if (TextSize(app, font, size, (cut + "\xe2\x80\xa6").c_str()).x <= width)
            break;
    }
    return cut + "\xe2\x80\xa6";
}

void IconAt(App& app, ImWchar icon, float size, ImVec2 center, ImU32 color)
{
    const char* text = IconText(icon);
    ImVec2 extent = TextSize(app, app.Icons, size, text);
    ImGui::GetWindowDrawList()->AddText(app.Icons, app.Px(size), ImVec2(IM_ROUND(center.x - extent.x / 2), IM_ROUND(center.y - extent.y / 2)),
        color, text);
}

ImVec2 ItalicTextAt(App& app, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    int first = draw->VtxBuffer.Size;
    ImVec2 at(IM_ROUND(pos.x), IM_ROUND(pos.y));
    draw->AddText(font, app.Px(size), at, color, text);
    // about 12 degrees, around the baseline
    float baseline = at.y + app.Px(size) * 0.8f;
    for (int i = first; i < draw->VtxBuffer.Size; i++)
        draw->VtxBuffer[i].pos.x += (baseline - draw->VtxBuffer[i].pos.y) * 0.21f;
    return TextSize(app, font, size, text);
}

std::vector<std::string> WrapLines(App& app, ImFont* font, float size, const std::string& text, float width, int lines)
{
    std::vector<std::string> out;
    const char* p = text.c_str();
    const char* end = p + text.size();
    while (p < end && (int)out.size() < lines)
    {
        while (p < end && *p == ' ')
            p++;
        if (p >= end)
            break;
        if ((int)out.size() == lines - 1)
        {
            out.push_back(Ellipsize(app, font, size, std::string(p, end), width));
            break;
        }
        const char* cut = font->CalcWordWrapPosition(app.Px(size), p, end, width);
        if (cut <= p)
            cut = p + 1;
        // (a UTF-8 character whole)
        while (cut < end && ((unsigned char)*cut & 0xC0) == 0x80)
            cut++;
        std::string line(p, cut);
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
        out.push_back(line);
        p = cut;
    }
    return out;
}

void FocusRing(App& app, ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding)
{
    // 3 px of the primary color, 2 px out from the item, as the mockup's focus outline
    float thickness = std::max(2.f, IM_ROUND(app.Px(3))), gap = IM_ROUND(app.Px(2));
    float out = gap + thickness;
    draw->AddRect(ImVec2(min.x - out, min.y - out), ImVec2(max.x + out, max.y + out), app.Colors().Primary, rounding + out, thickness);
}

bool MenuRow(App& app, const char* id, ImWchar icon, const char* label, const char* hint, bool isDefault, bool enabled)
{
    const Palette& p = app.Colors();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, app.Px(kRowHeight));
    bool pressed = Item(id, size, enabled);
    app.TrackFocus(isDefault);
    ItemState state = StateOfLastItem();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 max(pos.x + size.x, pos.y + size.y);
    float radius = size.y / 2;
    draw->AddRectFilled(pos, max, RowBackground(app, state, 0), radius);
    if (state.Focused)
        FocusRing(app, draw, pos, max, radius);
    ImU32 text = !enabled ? WithAlpha(p.OnSurface, 0.38f) : state.Focused ? p.OnSecondaryContainer : p.OnSurface;
    ImU32 sub = !enabled ? WithAlpha(p.OnSurface, 0.38f) : state.Focused ? p.OnSecondaryContainer : p.OnSurfaceVariant;
    float cy = pos.y + size.y / 2;
    IconAt(app, icon, 26, ImVec2(pos.x + app.Px(20 + 13), cy), text);
    float labelX = pos.x + app.Px(20 + 26 + 18);
    float hintW = hint ? TextSize(app, app.Regular, kDetailSize, hint).x : 0;
    std::string fitted = Ellipsize(app, app.Medium, kLabelSize, label, max.x - labelX - hintW - app.Px(36));
    ImVec2 labelSize = TextSize(app, app.Medium, kLabelSize, fitted.c_str());
    TextAt(app, app.Medium, kLabelSize, ImVec2(labelX, cy - labelSize.y / 2), text, fitted.c_str());
    if (hint)
    {
        ImVec2 hintSize = TextSize(app, app.Regular, kDetailSize, hint);
        TextAt(app, app.Regular, kDetailSize, ImVec2(max.x - app.Px(20) - hintSize.x, cy - hintSize.y / 2), sub, hint);
    }
    return pressed;
}

bool ToggleRow(App& app, const char* id, ImWchar icon, const char* label, const char* detail, bool value, bool isDefault)
{
    const Palette& p = app.Colors();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, app.Px(kRowHeight));
    bool pressed = Item(id, size, true);
    app.TrackFocus(isDefault);
    ItemState state = StateOfLastItem();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 max(pos.x + size.x, pos.y + size.y);
    float radius = size.y / 2;
    draw->AddRectFilled(pos, max, RowBackground(app, state, 0), radius);
    if (state.Focused)
        FocusRing(app, draw, pos, max, radius);
    ImU32 text = state.Focused ? p.OnSecondaryContainer : p.OnSurface;
    ImU32 sub = state.Focused ? p.OnSecondaryContainer : p.OnSurfaceVariant;
    float cy = pos.y + size.y / 2;
    IconAt(app, icon, 26, ImVec2(pos.x + app.Px(33), cy), text);

    // the switch: M3's 52 x 32 track, the thumb 24 on and 16 off
    float trackW = app.Px(52), trackH = app.Px(32);
    ImVec2 t0(max.x - app.Px(20) - trackW, cy - trackH / 2), t1(t0.x + trackW, t0.y + trackH);
    if (value)
    {
        draw->AddRectFilled(t0, t1, p.Primary, trackH / 2);
        draw->AddCircleFilled(ImVec2(t1.x - trackH / 2, cy), app.Px(12), p.OnPrimary);
    }
    else
    {
        draw->AddRectFilled(t0, t1, p.SurfaceContainerHighest, trackH / 2);
        draw->AddRect(t0, t1, p.Outline, trackH / 2, std::max(1.f, app.Px(2)));
        draw->AddCircleFilled(ImVec2(t0.x + trackH / 2, cy), app.Px(8), p.Outline);
    }

    float labelX = pos.x + app.Px(64);
    float textW = t0.x - labelX - app.Px(16);
    std::string fitted = Ellipsize(app, app.Medium, kLabelSize, label, textW);
    if (detail && *detail)
    {
        ImVec2 l = TextSize(app, app.Medium, kLabelSize, fitted.c_str());
        ImVec2 d = TextSize(app, app.Regular, 13, detail);
        float top = cy - (l.y + d.y) / 2;
        TextAt(app, app.Medium, kLabelSize, ImVec2(labelX, top), text, fitted.c_str());
        TextAt(app, app.Regular, 13, ImVec2(labelX, top + l.y), sub, Ellipsize(app, app.Regular, 13, detail, textW).c_str());
    }
    else
    {
        ImVec2 l = TextSize(app, app.Medium, kLabelSize, fitted.c_str());
        TextAt(app, app.Medium, kLabelSize, ImVec2(labelX, cy - l.y / 2), text, fitted.c_str());
    }
    return pressed;
}

// the label, and the detail under it when there is one, left at [x] and fitting [width], centered on [cy]
static void LabelAndDetail(App& app, float x, float cy, float width, ImU32 text, ImU32 sub, const char* label, const char* detail)
{
    std::string fitted = Ellipsize(app, app.Medium, kLabelSize, label, width);
    ImVec2 l = TextSize(app, app.Medium, kLabelSize, fitted.c_str());
    if (detail && *detail)
    {
        std::string d = Ellipsize(app, app.Regular, 13, detail, width);
        ImVec2 ds = TextSize(app, app.Regular, 13, d.c_str());
        float top = cy - (l.y + ds.y) / 2;
        TextAt(app, app.Medium, kLabelSize, ImVec2(x, top), text, fitted.c_str());
        TextAt(app, app.Regular, 13, ImVec2(x, top + l.y), sub, d.c_str());
    }
    else
        TextAt(app, app.Medium, kLabelSize, ImVec2(x, cy - l.y / 2), text, fitted.c_str());
}

int ChoiceRow(App& app, const char* id, ImWchar icon, const char* label, const char* value, bool isDefault, const char* detail)
{
    const Palette& p = app.Colors();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, app.Px(kRowHeight));
    bool pressed = Item(id, size, true);
    app.TrackFocus(isDefault);
    ItemState state = StateOfLastItem();
    int change = pressed ? 1 : 0;
    if (state.Focused && !app.Busy)
    {
        if (PressedLeft())
            change = -1;
        else if (PressedRight())
            change = 1;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 max(pos.x + size.x, pos.y + size.y);
    float radius = size.y / 2;
    draw->AddRectFilled(pos, max, RowBackground(app, state, 0), radius);
    if (state.Focused)
        FocusRing(app, draw, pos, max, radius);
    ImU32 text = state.Focused ? p.OnSecondaryContainer : p.OnSurface;
    ImU32 accent = state.Focused ? p.OnSecondaryContainer : p.Primary;
    float cy = pos.y + size.y / 2;
    IconAt(app, icon, 26, ImVec2(pos.x + app.Px(33), cy), text);

    // the value between two arrows that say Left and Right change it
    ImVec2 v = TextSize(app, app.Medium, kLabelSize, value);
    float arrow = app.Px(22);
    float valueRight = max.x - app.Px(16) - arrow;
    IconAt(app, Icon::ChevronRight, 24, ImVec2(valueRight + arrow / 2, cy), accent);
    TextAt(app, app.Medium, kLabelSize, ImVec2(valueRight - v.x - app.Px(4), cy - v.y / 2), accent, value);
    // a left chevron: the right one mirrored, drawn as a path so the icon font needs one glyph
    float lx = valueRight - v.x - app.Px(4) - arrow / 2 - app.Px(2), h = app.Px(6);
    draw->PathLineTo(ImVec2(lx + h / 2, cy - h));
    draw->PathLineTo(ImVec2(lx - h / 2, cy));
    draw->PathLineTo(ImVec2(lx + h / 2, cy + h));
    draw->PathStroke(accent, std::max(1.5f, app.Px(2.2f)));

    float labelX = pos.x + app.Px(64);
    ImU32 sub = state.Focused ? p.OnSecondaryContainer : p.OnSurfaceVariant;
    LabelAndDetail(app, labelX, cy, lx - h - labelX - app.Px(12), text, sub, label, detail);
    return change;
}

bool DetailRow(App& app, const char* id, ImWchar icon, const char* label, const char* detail, bool isDefault, bool enabled)
{
    const Palette& p = app.Colors();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, app.Px(kRowHeight));
    bool pressed = Item(id, size, enabled);
    app.TrackFocus(isDefault);
    ItemState state = StateOfLastItem();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 max(pos.x + size.x, pos.y + size.y);
    float radius = size.y / 2;
    draw->AddRectFilled(pos, max, RowBackground(app, state, 0), radius);
    if (state.Focused)
        FocusRing(app, draw, pos, max, radius);
    ImU32 text = !enabled ? WithAlpha(p.OnSurface, 0.38f) : state.Focused ? p.OnSecondaryContainer : p.OnSurface;
    ImU32 sub = !enabled ? WithAlpha(p.OnSurface, 0.38f) : state.Focused ? p.OnSecondaryContainer : p.OnSurfaceVariant;
    float cy = pos.y + size.y / 2;
    IconAt(app, icon, 26, ImVec2(pos.x + app.Px(33), cy), text);
    IconAt(app, Icon::ChevronRight, 24, ImVec2(max.x - app.Px(16) - app.Px(11), cy), sub);
    float labelX = pos.x + app.Px(64);
    LabelAndDetail(app, labelX, cy, max.x - app.Px(52) - labelX, text, sub, label, detail);
    return pressed;
}

int TabStrip(App& app, ImVec2 pos, const char* const* labels, int count, int selected)
{
    const Palette& p = app.Colors();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float x = pos.x, y = pos.y;
    int clicked = -1;
    auto cap = [&](const char* text)
    {
        ImVec2 s = TextSize(app, app.Medium, 13, text);
        float h = app.Px(24), w = std::max(h, s.x + app.Px(12));
        ImVec2 c0(x, y + (app.Px(40) - h) / 2);
        draw->AddRectFilled(c0, ImVec2(c0.x + w, c0.y + h), p.SurfaceContainerHighest, app.Px(6));
        TextAt(app, app.Medium, 13, ImVec2(c0.x + (w - s.x) / 2, c0.y + (h - s.y) / 2), p.OnSurfaceVariant, text);
        x += w + app.Px(10);
    };
    // the tabs' padding shrinks, then their text, when they don't fit the window (a narrow one): the caps take about 2 x 34
    float room = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - app.Px(16) - pos.x - app.Px(2 * 34 + 4);
    float textSize = 16, pad = 36;
    for (;;)
    {
        float total = 0;
        for (int i = 0; i < count; i++)
            total += TextSize(app, app.Medium, textSize, labels[i]).x + app.Px(pad) + app.Px(6);
        if (total <= room || textSize <= 12)
            break;
        if (pad > 16)
            pad -= 4;
        else
            textSize -= 1;
    }
    cap(app.UsingPad() ? "L" : "Q");
    for (int i = 0; i < count; i++)
    {
        ImVec2 s = TextSize(app, app.Medium, textSize, labels[i]);
        float w = s.x + app.Px(pad), h = app.Px(40);
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::PushID(i);
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
        if (ImGui::InvisibleButton("##tab", ImVec2(w, h)))
            clicked = i;
        ImGui::PopItemFlag();
        ImGui::PopID();
        bool hovered = ImGui::IsItemHovered();
        bool on = i == selected;
        ImU32 bg = on ? p.SecondaryContainer : hovered ? WithAlpha(p.OnSurface, 0.08f) : 0;
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), bg, h / 2);
        TextAt(app, app.Medium, textSize, ImVec2(x + (w - s.x) / 2, y + (h - s.y) / 2), on ? p.OnSecondaryContainer : p.OnSurfaceVariant, labels[i]);
        x += w + app.Px(6);
    }
    x += app.Px(4);
    cap(app.UsingPad() ? "R" : "W");
    return clicked;
}

bool BigButton(App& app, const char* id, ImWchar icon, const char* label, const char* detail, bool filled, float width, bool isDefault)
{
    const Palette& p = app.Colors();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool hasDetail = detail && *detail;
    ImVec2 size(width > 0 ? width : ImGui::GetContentRegionAvail().x, app.Px(hasDetail ? 84 : 68));
    bool pressed = Item(id, size, true);
    app.TrackFocus(isDefault);
    ItemState state = StateOfLastItem();
    ImU32 base = filled ? p.PrimaryContainer : p.SurfaceContainerHigh;
    ImU32 on = filled ? p.OnPrimaryContainer : p.OnSurface;
    ImU32 bg = state.Held ? Blend(base, on, 0.12f) : (state.Focused || state.Hovered) ? Blend(base, on, 0.08f) : base;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 max(pos.x + size.x, pos.y + size.y);
    float radius = app.Px(20);
    draw->AddRectFilled(pos, max, bg, radius);
    if (state.Focused)
        FocusRing(app, draw, pos, max, radius);
    float cy = pos.y + size.y / 2;
    IconAt(app, icon, 30, ImVec2(pos.x + app.Px(24 + 15), cy), on);
    float textX = pos.x + app.Px(24 + 30 + 18), textW = max.x - textX - app.Px(16);
    std::string fitted = Ellipsize(app, app.Medium, 21, label, textW);
    ImVec2 l = TextSize(app, app.Medium, 21, fitted.c_str());
    if (hasDetail)
    {
        std::string d = Ellipsize(app, app.Regular, 14, detail, textW);
        ImVec2 ds = TextSize(app, app.Regular, 14, d.c_str());
        float top = cy - (l.y + ds.y + app.Px(2)) / 2;
        TextAt(app, app.Medium, 21, ImVec2(textX, top), on, fitted.c_str());
        TextAt(app, app.Regular, 14, ImVec2(textX, top + l.y + app.Px(2)), filled ? on : p.OnSurfaceVariant, d.c_str());
    }
    else
        TextAt(app, app.Medium, 21, ImVec2(textX, cy - l.y / 2), on, fitted.c_str());
    return pressed;
}

void SectionLabel(App& app, const char* text)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 s = TextSize(app, app.Medium, 14, text);
    TextAt(app, app.Medium, 14, ImVec2(pos.x + app.Px(20), pos.y + app.Px(10)), app.Colors().Primary, text);
    ImGui::Dummy(ImVec2(1, s.y + app.Px(18)));
}

bool ScreenHeader(App& app, const char* title, bool backArrow)
{
    const Palette& p = app.Colors();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float height = app.Px(88);
    float x = pos.x + app.Px(36);
    bool back = false;
    if (backArrow)
    {
        // clickable, but not a stop for the controller's focus, which B covers
        float d = app.Px(48);
        ImGui::SetCursorScreenPos(ImVec2(x, pos.y + (height - d) / 2));
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
        back = ImGui::InvisibleButton("##back", ImVec2(d, d));
        ImGui::PopItemFlag();
        bool hovered = ImGui::IsItemHovered();
        ImVec2 c(x + d / 2, pos.y + height / 2);
        if (hovered)
            ImGui::GetWindowDrawList()->AddCircleFilled(c, d / 2, WithAlpha(p.OnSurface, 0.08f));
        IconAt(app, Icon::ArrowBack, 28, c, p.OnSurface);
        x += d + app.Px(12);
    }
    ImVec2 t = TextSize(app, app.Medium, 28, title);
    TextAt(app, app.Medium, 28, ImVec2(x, pos.y + (height - t.y) / 2), p.OnSurface, title);
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(1, height));
    return back;
}

float FooterHeight(App& app)
{
    return app.Px(56);
}

void FooterHints(App& app, std::initializer_list<Hint> hints, ImU32 background, float left)
{
    const Palette& p = app.Colors();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 wmin = window->Pos, wmax(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y);
    float h = FooterHeight(app);
    ImVec2 bar(wmin.x + left, wmax.y - h);
    draw->AddRectFilled(bar, wmax, background);
    float x = wmax.x - app.Px(36);
    float cy = bar.y + h / 2;
    // right to left
    std::vector<Hint> list(hints);
    for (auto it = list.rbegin(); it != list.rend(); ++it)
    {
        std::string button = it->Button;
        size_t sep = button.find('|');
        if (sep != std::string::npos)
            button = app.UsingPad() ? button.substr(0, sep) : button.substr(sep + 1);
        ImVec2 a = TextSize(app, app.Regular, 15, it->Action);
        x -= a.x;
        TextAt(app, app.Regular, 15, ImVec2(x, cy - a.y / 2), p.OnSurfaceVariant, it->Action);
        x -= app.Px(10);
        ImVec2 b = TextSize(app, app.Medium, 14, button.c_str());
        float capH = app.Px(28);
        float capW = std::max(capH, b.x + app.Px(14));
        ImVec2 c0(x - capW, cy - capH / 2), c1(x, cy + capH / 2);
        draw->AddRectFilled(c0, c1, p.SurfaceContainerHighest, app.UsingPad() ? capH / 2 : app.Px(6));
        TextAt(app, app.Medium, 14, ImVec2(c0.x + (capW - b.x) / 2, cy - b.y / 2), p.OnSurface, button.c_str());
        x = c0.x - app.Px(28);
    }
}

}
