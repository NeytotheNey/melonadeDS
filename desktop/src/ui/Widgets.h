// The menus' controls, drawn in the app's Material 3 look with a focus ring a TV can show from across the room: each is an ImGui item, so
// mouse, keyboard and controller navigation all work on it. Sizes are reference pixels (App::Px)
#pragma once

#include <initializer_list>
#include <string>

#include "imgui.h"

namespace Ui
{

class App;

// text with one of the app's fonts at [size] reference pixels; returns its size in window pixels
ImVec2 TextAt(App& app, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text, float wrapWidth = 0);
ImVec2 TextSize(App& app, ImFont* font, float size, const char* text, float wrapWidth = 0);
// [text] cut with an ellipsis to fit [width] window pixels
std::string Ellipsize(App& app, ImFont* font, float size, const std::string& text, float width);
void IconAt(App& app, ImWchar icon, float size, ImVec2 center, ImU32 color);

// the focus ring around [min, max] (window pixels)
void FocusRing(App& app, ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding);

// A row of a menu list, the width of the region: icon, label, and a hint on the right ([hint] may be null). True when chosen
bool MenuRow(App& app, const char* id, ImWchar icon, const char* label, const char* hint, bool isDefault = false, bool enabled = true);
// A row with an on/off switch; true when chosen (the caller flips the value)
bool ToggleRow(App& app, const char* id, ImWchar icon, const char* label, const char* detail, bool value, bool isDefault = false);
// A row with a value: confirm or Right goes to the next value (1), Left to the previous one (-1); 0 when nothing happened
// [detail], when given, is a second line under the label
int ChoiceRow(App& app, const char* id, ImWchar icon, const char* label, const char* value, bool isDefault = false, const char* detail = nullptr);
// A row with a label over a second line (a folder, a binding), and a chevron; true when chosen
bool DetailRow(App& app, const char* id, ImWchar icon, const char* label, const char* detail, bool isDefault = false, bool enabled = true);
// Tabs along a line at [pos], with the bumper caps that switch them on either side (clickable, not stops for the controller's focus);
// returns the tab clicked, or -1
int TabStrip(App& app, ImVec2 pos, const char* const* labels, int count, int selected);
// A large button, filled (primary container) or tonal (surface container); [width] 0 is the region's width
bool BigButton(App& app, const char* id, ImWchar icon, const char* label, const char* detail, bool filled, float width = 0,
    bool isDefault = false);
// A small heading over a group of rows
void SectionLabel(App& app, const char* text);

// The header of a full-window screen: a back arrow (true when it's clicked) and the title
bool ScreenHeader(App& app, const char* title, bool backArrow);

// The button hints along the bottom: pairs of (button, what it does), the button as the controller's when the last input came from one,
// otherwise as the keyboard's (the second form, after a '|': "A|Enter")
struct Hint { const char* Button; const char* Action; };
// [left]: where the bar starts, from the window's left edge
void FooterHints(App& app, std::initializer_list<Hint> hints, ImU32 background, float left = 0);
float FooterHeight(App& app);

}
