// Input setup: the bindings of one device at a time (L / R go through the keyboard, the controllers plugged in and the ones input.json
// knows), each DS button and hotkey with a primary and an alternate binding, as the app's InputConfig has them. A on a binding waits for
// the next input of that device ("press a button"; several held together make a combo), X clears it. A controller's face layout and a
// reset to its defaults are at the top
#include <algorithm>
#include <cmath>

#include "imgui_internal.h"
#include "Icons.h"
#include "Input.h"
#include "Screens.h"
#include "Ui.h"
#include "Widgets.h"

namespace Ui
{

namespace
{

using Frontend::Action;
using Frontend::Input;

class InputScreen : public Screen
{
public:
    const char* Id() const override { return "input"; }

    void Tab(App& app, int direction) override
    {
        Input* input = app.GetHost().Input;
        if (!input || input->Capturing())
            return;
        std::vector<std::string> ids = input->DeviceIds();
        int index = (int)(std::find(ids.begin(), ids.end(), Device) - ids.begin());
        if (index >= (int)ids.size())
            index = 0;
        index = (index + direction + (int)ids.size()) % (int)ids.size();
        Device = ids[index];
        ConfirmReset = false;
        WantFocus = true;
        FocusId = 0;
        ResetScroll = true;
        printf("ui: input device %s\n", Device.c_str());
        fflush(stdout);
    }

    bool Back(App& app) override
    {
        Input* input = app.GetHost().Input;
        if (input && input->Capturing())
        {
            input->CancelCapture();
            return true;
        }
        return false;
    }

    void Clear(App& app) override
    {
        Input* input = app.GetHost().Input;
        if (!input || input->Capturing() || FocusedAction < 0)
            return;
        input->SetBinding(Device, (Action)FocusedAction, FocusedAlt, Frontend::Binding());
    }

    void Draw(App& app) override
    {
        const Palette& p = app.Colors();
        Host& host = app.GetHost();
        Input* input = host.Input;
        ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        float footer = FooterHeight(app);
        if (ScreenHeader(app, "Buttons and hotkeys", true) && !(input && input->Capturing()))
            app.Pop();
        if (!input)
            return;
        if (Device.empty())
        {
            Device = input->LastDeviceId();
            printf("ui: input device %s\n", Device.c_str());
        }
        Frontend::DeviceProfile* profile = input->Profile(Device);
        if (!profile)
        {
            Device = "keyboard";
            profile = input->Profile(Device);
        }
        switch (input->TakeCaptureResult())
        {
        case Input::CaptureResult::TimedOut: app.Toast("Nothing was pressed, the binding stays as it was"); break;
        default: break;
        }

        DrawDeviceStrip(app, *input, *profile, win, size);

        float pad = app.Px(40);
        float colW = std::min(size.x - 2 * pad, app.Px(900));
        float x = win.x + (size.x - colW) / 2;
        float top = win.y + app.Px(88) + app.Px(56);
        float ring = app.Px(12);
        ImGui::SetCursorScreenPos(ImVec2(x - ring, top - ring));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ring, ring));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(app.Px(8), app.Px(4)));
        if (ResetScroll)
        {
            ImGui::SetNextWindowScroll(ImVec2(0, 0));
            ResetScroll = false;
        }
        bool capturing = input->Capturing();
        if (capturing)
            ImGui::BeginDisabled();
        ImGui::BeginChild("##bindings", ImVec2(colW + 2 * ring, win.y + size.y - footer - top + ring),
            ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
        FocusedAction = -1;

        if (!profile->Keyboard)
        {
            bool nintendo = profile->Layout == Frontend::FaceLayout::Nintendo;
            if (ChoiceRow(app, "##layout", Icon::Gamepad, "Face buttons", nintendo ? "Nintendo: A on the right" : "Positional: A at the bottom",
                    false, "Where the DS's A, B, X and Y are on this controller"))
                input->SetLayout(Device, nintendo ? Frontend::FaceLayout::Positional : Frontend::FaceLayout::Nintendo);
        }
        if (MenuRow(app, "##reset", Icon::SettingsBackupRestore, ConfirmReset ? "Press again to reset every binding" : "Reset to defaults",
                nullptr, false))
        {
            if (ConfirmReset)
            {
                input->ResetDefaults(Device);
                app.Toast(profile->Name + ": back to the defaults");
                ConfirmReset = false;
            }
            else
                ConfirmReset = true;
        }
        if (!ImGui::IsItemFocused())
            ConfirmReset = false;

        float labelW = std::floor((colW - app.Px(16)) * 0.36f);
        float chipW = std::floor((colW - labelW - app.Px(16)) / 2);
        SectionLabel(app, "DS buttons");
        ColumnHeads(app, labelW, chipW);
        static const Action kDs[] = {Action::A, Action::B, Action::X, Action::Y, Action::L, Action::R, Action::Start, Action::Select,
            Action::Up, Action::Down, Action::Left, Action::Right, Action::TouchTap};
        for (Action a : kDs)
            BindingRow(app, *input, *profile, a, labelW, chipW);
        SectionLabel(app, "Hotkeys");
        static const Action kHotkeys[] = {Action::Pause, Action::FastForwardHold, Action::FastForward, Action::QuickSave, Action::QuickLoad,
            Action::SwapScreens, Action::DetachScreens, Action::Screenshot, Action::Fullscreen, Action::Rewind};
        for (Action a : kHotkeys)
            BindingRow(app, *input, *profile, a, labelW, chipW);
        ImGui::Dummy(ImVec2(1, app.Px(6)));
        ImVec2 np = ImGui::GetCursorScreenPos();
        ImVec2 ns = TextAt(app, app.Regular, 14, ImVec2(np.x + app.Px(20), np.y), p.OnSurfaceVariant,
            profile->Keyboard ? "Keyboard combos (Ctrl + S) fire at once. The F-keys F2 to F8 and F10, P and Ctrl + R stay as they are."
                              : "Controller combos fire once held for half a second, so the game's own combos still reach it. The right stick "
                                "moves the touch cursor unless something here uses it.",
            colW - app.Px(40));
        ImGui::Dummy(ImVec2(1, ns.y + app.Px(24)));
        ImGui::EndChild();
        if (capturing)
            ImGui::EndDisabled();
        ImGui::PopStyleVar(2);

        if (FocusedAction >= 0)
            FooterHints(app, {{"A|Enter", "Bind"}, {"X|Del", "Clear"}, {"L/R|Q/W", "Device"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
        else
            FooterHints(app, {{"A|Enter", "Choose"}, {"L/R|Q/W", "Device"}, {"B|Esc", "Back"}}, p.SurfaceContainerLowest);

        if (capturing)
            DrawCapture(app, *input, *profile);
    }

private:
    void DrawDeviceStrip(App& app, Input& input, const Frontend::DeviceProfile& profile, ImVec2 win, ImVec2 size)
    {
        const Palette& p = app.Colors();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float y = win.y + app.Px(88) + app.Px(4), h = app.Px(40);
        float x = win.x + app.Px(40);
        std::vector<std::string> ids = input.DeviceIds();
        int index = (int)(std::find(ids.begin(), ids.end(), Device) - ids.begin());
        auto cap = [&](const char* text)
        {
            ImVec2 s = TextSize(app, app.Medium, 13, text);
            float ch = app.Px(24), w = std::max(ch, s.x + app.Px(12));
            ImVec2 c0(x, y + (h - ch) / 2);
            draw->AddRectFilled(c0, ImVec2(c0.x + w, c0.y + ch), p.SurfaceContainerHighest, app.Px(6));
            TextAt(app, app.Medium, 13, ImVec2(c0.x + (w - s.x) / 2, c0.y + (ch - s.y) / 2), p.OnSurfaceVariant, text);
            x += w + app.Px(10);
        };
        cap(app.UsingPad() ? "L" : "Q");
        // the device, in a pill: its icon, name and whether it's plugged in
        bool connected = input.IsConnected(Device);
        std::string name = profile.Keyboard ? "Keyboard" : profile.Name;
        std::string status = profile.Keyboard ? "" : connected ? "Connected" : "Not connected";
        float maxW = win.x + size.x - app.Px(40) - x - app.Px(120);
        name = Ellipsize(app, app.Medium, 16, name, std::max(maxW - app.Px(160), app.Px(80)));
        ImVec2 ns = TextSize(app, app.Medium, 16, name.c_str());
        ImVec2 ss = TextSize(app, app.Regular, 14, status.c_str());
        float w = app.Px(16 + 26 + 10) + ns.x + (status.empty() ? 0 : app.Px(12) + ss.x) + app.Px(18);
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), p.SecondaryContainer, h / 2);
        IconAt(app, profile.Keyboard ? Icon::Keyboard : Icon::SportsEsports, 22, ImVec2(x + app.Px(16 + 11), y + h / 2), p.OnSecondaryContainer);
        float tx = x + app.Px(16 + 26 + 10);
        TextAt(app, app.Medium, 16, ImVec2(tx, y + (h - ns.y) / 2), p.OnSecondaryContainer, name.c_str());
        if (!status.empty())
            TextAt(app, app.Regular, 14, ImVec2(tx + ns.x + app.Px(12), y + (h - ss.y) / 2),
                connected ? p.OnSecondaryContainer : WithAlpha(p.OnSecondaryContainer, 0.6f), status.c_str());
        x += w + app.Px(10);
        cap(app.UsingPad() ? "R" : "W");
        char count[32];
        snprintf(count, sizeof(count), "%d of %d", std::max(index, 0) + 1, (int)ids.size());
        ImVec2 cs = TextSize(app, app.Regular, 14, count);
        TextAt(app, app.Regular, 14, ImVec2(win.x + size.x - app.Px(40) - cs.x, y + (h - cs.y) / 2), p.OnSurfaceVariant, count);
    }

    void ColumnHeads(App& app, float labelW, float chipW)
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        const Palette& p = app.Colors();
        float x0 = pos.x + labelW + app.Px(8);
        TextAt(app, app.Regular, 13, ImVec2(x0 + app.Px(16), pos.y), p.OnSurfaceVariant, "Primary");
        TextAt(app, app.Regular, 13, ImVec2(x0 + chipW + app.Px(8) + app.Px(16), pos.y), p.OnSurfaceVariant, "Alternate");
        ImGui::Dummy(ImVec2(1, TextSize(app, app.Regular, 13, "Ag").y + app.Px(2)));
    }

    void BindingRow(App& app, Input& input, const Frontend::DeviceProfile& profile, Action action, float labelW, float chipW)
    {
        const Palette& p = app.Colors();
        float h = app.Px(48);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        std::string label = Frontend::ActionLabel(action);
        if (action == Action::Rewind)
            label += " (coming later)";
        ImVec2 ls = TextSize(app, app.Medium, 17, "Ag");
        TextAt(app, app.Medium, 17, ImVec2(pos.x + app.Px(20), pos.y + (h - ls.y) / 2), p.OnSurface,
            Ellipsize(app, app.Medium, 17, label, labelW - app.Px(28)).c_str());
        for (int alt = 0; alt < 2; alt++)
        {
            ImGui::SetCursorScreenPos(ImVec2(pos.x + labelW + app.Px(8) + alt * (chipW + app.Px(8)), pos.y));
            const Frontend::Binding& b = alt ? profile.Map[(int)action].Alt : profile.Map[(int)action].Primary;
            char id[48];
            snprintf(id, sizeof(id), "##%s_%d", Frontend::ActionKey(action), alt);
            ImVec2 c0 = ImGui::GetCursorScreenPos();
            bool pressed = ImGui::InvisibleButton(id, ImVec2(chipW, h), ImGuiButtonFlags_EnableNav);
            // A's primary binding has the focus when the screen opens and when the device changes
            app.TrackFocus(action == Action::A && alt == 0);
            bool focused = ImGui::IsItemFocused(), hovered = ImGui::IsItemHovered();
            if (focused)
            {
                FocusedAction = (int)action;
                FocusedAlt = alt != 0;
            }
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 c1(c0.x + chipW, c0.y + h);
            ImU32 base = focused ? p.SecondaryContainer : p.SurfaceContainerHigh;
            ImU32 on = focused ? p.OnSecondaryContainer : p.OnSurface;
            draw->AddRectFilled(c0, c1, hovered && !focused ? Blend(base, p.OnSurface, 0.08f) : base, app.Px(14));
            if (focused)
                FocusRing(app, draw, c0, c1, app.Px(14));
            bool capturingThis = input.Capturing() && CaptureAction == (int)action && CaptureAlt == (alt != 0);
            std::string text = capturingThis ? "Waiting\xe2\x80\xa6" : b.Empty() ? "\xe2\x80\x94" : Frontend::BindingLabel(b, profile);
            text = Ellipsize(app, app.Regular, 16, text, chipW - app.Px(28));
            ImVec2 ts = TextSize(app, app.Regular, 16, text.c_str());
            TextAt(app, app.Regular, 16, ImVec2(c0.x + app.Px(14), c0.y + (h - ts.y) / 2),
                b.Empty() && !capturingThis ? WithAlpha(on, 0.5f) : on, text.c_str());
            if (pressed && !input.Capturing())
            {
                CaptureAction = (int)action;
                CaptureAlt = alt != 0;
                input.BeginCapture(Device, action, alt != 0);
            }
        }
        ImGui::SetCursorScreenPos(pos);
        ImGui::Dummy(ImVec2(1, h));
    }

    void DrawCapture(App& app, Input& input, const Frontend::DeviceProfile& profile)
    {
        const Palette& p = app.Colors();
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 v0 = viewport->Pos, vs = viewport->Size;
        draw->AddRectFilled(v0, ImVec2(v0.x + vs.x, v0.y + vs.y), WithAlpha(p.Scrim, 0.5f));
        float w = std::min(vs.x - app.Px(32), app.Px(560)), h = app.Px(240);
        ImVec2 c0(v0.x + (vs.x - w) / 2, v0.y + (vs.y - h) / 2), c1(c0.x + w, c0.y + h);
        draw->AddRectFilled(c0, c1, p.SurfaceContainerHigh, app.Px(28));
        auto text = [&](ImFont* font, float size, float y, ImU32 color, const std::string& s)
        {
            std::string fitted = Ellipsize(app, font, size, s, w - app.Px(48));
            ImVec2 ts = font->CalcTextSizeA(app.Px(size), FLT_MAX, 0, fitted.c_str());
            draw->AddText(font, app.Px(size), ImVec2(IM_ROUND(c0.x + (w - ts.x) / 2), IM_ROUND(y)), color, fitted.c_str());
            return ts.y;
        };
        float y = c0.y + app.Px(24);
        ImVec2 ic(c0.x + w / 2, y + app.Px(18));
        const char* icon = IconText(profile.Keyboard ? Icon::Keyboard : Icon::SportsEsports);
        ImVec2 is = app.Icons->CalcTextSizeA(app.Px(36), FLT_MAX, 0, icon);
        draw->AddText(app.Icons, app.Px(36), ImVec2(IM_ROUND(ic.x - is.x / 2), IM_ROUND(ic.y - is.y / 2)), p.Primary, icon);
        y += app.Px(48);
        std::string what = std::string(profile.Keyboard ? "Press a key for " : "Press a button for ") + Frontend::ActionLabel((Action)CaptureAction)
            + (CaptureAlt ? " (alternate)" : "");
        y += text(app.Medium, 21, y, p.OnSurface, what) + app.Px(10);
        std::string held = input.CaptureText();
        y += text(app.Medium, 18, y, p.Primary, held.empty() ? " " : held) + app.Px(12);
        text(app.Regular, 14, y, p.OnSurfaceVariant, "Hold several together for a combo, then let go.");
        char left[96];
        snprintf(left, sizeof(left), "Cancels in %d s, or with %s", (int)std::ceil(input.CaptureSecondsLeft()),
            profile.Keyboard ? "a controller's B" : "Esc");
        text(app.Regular, 14, c1.y - app.Px(24) - app.Px(16), p.OnSurfaceVariant, left);
    }

    // the device whose page is shown: on opening, the one used last (the pad that chose the row, say)
    std::string Device;
    int FocusedAction = -1;
    bool FocusedAlt = false;
    int CaptureAction = 0;
    bool CaptureAlt = false;
    bool ConfirmReset = false;
    bool ResetScroll = true;
};

}

std::unique_ptr<Screen> MakeInputScreen()
{
    return std::make_unique<InputScreen>();
}

}
