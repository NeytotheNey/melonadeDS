// The savestate slots, with a Save tab and a Load tab (L / R switch): the app's nine slots, each with its screenshot, the time it was saved,
// or that it's empty. Saving or loading closes the menus and goes back to the game, as the app does
#include <algorithm>
#include <cmath>

#include "Icons.h"
#include "Screens.h"
#include "Ui.h"
#include "Widgets.h"

namespace Ui
{

namespace
{

using Frontend::SaveSlots;

class StatesScreen : public Screen
{
public:
    explicit StatesScreen(bool save) : SaveTab(save) {}

    const char* Id() const override { return SaveTab ? "pause/save" : "pause/load"; }

    bool Back(App& app) override
    {
        // B on the question: no
        if (Ask != Question::None)
        {
            CloseQuestion();
            return true;
        }
        return false;
    }

    void Clear(App& app) override
    {
        // X on a used slot: delete it, once asked
        if (app.Busy || Ask != Question::None || FocusedSlot < 0 || !FocusedExists)
            return;
        OpenQuestion(Question::Delete, FocusedSlot);
    }

    void Tab(App& app, int) override
    {
        if (app.Busy || Ask != Question::None)
            return;
        SaveTab = !SaveTab;
        // the focus stays on the same slot when it can; an empty one can't be loaded
        WantFocus = true;
        printf("ui: %s\n", Id());
    }

    void Draw(App& app) override
    {
        const Palette& p = app.Colors();
        Host& host = app.GetHost();
        ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float pad = app.Px(40), footer = FooterHeight(app);
        std::vector<SaveSlots::Slot> slots = host.ListSlots ? host.ListSlots() : std::vector<SaveSlots::Slot>();

        if (ScreenHeader(app, SaveTab ? "Save state" : "Load state", true) && !app.Busy)
            app.Pop();
        DrawTabs(app, win, size);

        // the grid: three columns where they fit, fewer on a narrow window; the child's padding leaves room for the focus ring
        float ring = app.Px(12);
        float top = win.y + app.Px(88) + app.Px(52);
        float gridW = size.x - 2 * pad;
        float gap = app.Px(16);
        int columns = std::clamp((int)((gridW + gap) / (app.Px(330) + gap)), 1, 3);
        float cardW = std::floor((gridW - gap * (columns - 1)) / columns);
        // three rows in view where they fit; one column (a narrow window) scrolls through short cards
        float gridH = win.y + size.y - footer - top;
        // (the child's content is its height less its padding: ring below, as the ring above is the grid's top)
        float cardH = columns == 1 ? app.Px(128) : std::clamp(std::floor((gridH - ring - 2 * gap) / 3) - 1, app.Px(128), app.Px(172));
        ImGui::SetCursorScreenPos(ImVec2(win.x + pad - ring, top - ring));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ring, ring));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
        ImGui::BeginChild("##slots", ImVec2(gridW + 2 * ring, win.y + size.y - footer - top + ring),
            ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);

        // the focus when the screen opens: the quick slot, or on the Load tab the newest state when the quick slot is empty
        int defaultSlot = 0;
        if (!SaveTab && !(slots.size() > 0 && slots[0].Exists))
        {
            long long newest = -1;
            for (const SaveSlots::Slot& slot : slots)
            {
                if (slot.Exists && slot.Stamp > newest)
                {
                    newest = slot.Stamp;
                    defaultSlot = slot.Number;
                }
            }
        }
        bool asking = Ask != Question::None;
        if (asking)
            ImGui::BeginDisabled();
        FocusedSlot = -1;
        FocusedExists = false;
        for (size_t i = 0; i < slots.size(); i++)
        {
            if (i % columns)
                ImGui::SameLine();
            if (Card(app, slots[i], ImVec2(cardW, cardH), slots[i].Number == defaultSlot) && !app.Busy && !asking)
            {
                // saving over a used slot asks first
                if (SaveTab && slots[i].Exists)
                    OpenQuestion(Question::Overwrite, slots[i].Number);
                else
                    Choose(app, slots[i]);
            }
            if (ImGui::IsItemFocused())
            {
                FocusedSlot = slots[i].Number;
                FocusedExists = slots[i].Exists;
            }
        }
        // (within the child, where it began)
        if (asking)
            ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        if (asking)
            FooterHints(app, {{"A|Enter", "Choose"}, {"B|Esc", "Cancel"}}, p.SurfaceContainerLowest);
        else if (FocusedExists)
            FooterHints(app, {{"A|Enter", SaveTab ? "Save over it" : "Load"}, {"X|Del", "Delete"}, {"L/R|Q/W", SaveTab ? "Load tab" : "Save tab"},
                {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
        else
            FooterHints(app, {{"A|Enter", SaveTab ? "Save here" : "Load"}, {"L/R|Q/W", SaveTab ? "Load tab" : "Save tab"},
                {"B|Esc", "Back"}}, p.SurfaceContainerLowest);
        if (asking)
            DrawQuestion(app, slots);
        (void)draw;
    }

private:
    void DrawTabs(App& app, ImVec2 win, ImVec2 size)
    {
        const Palette& p = app.Colors();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float y = win.y + app.Px(88) + app.Px(4);
        float x = win.x + app.Px(40);
        // the bumpers that switch them, then the two tabs (clickable, but not stops for the controller's focus, which L / R cover)
        auto cap = [&](const char* text)
        {
            ImVec2 s = TextSize(app, app.Medium, 13, text);
            float h = app.Px(24), w = std::max(h, s.x + app.Px(12));
            ImVec2 c0(x, y + (app.Px(40) - h) / 2);
            draw->AddRectFilled(c0, ImVec2(c0.x + w, c0.y + h), p.SurfaceContainerHighest, app.Px(6));
            TextAt(app, app.Medium, 13, ImVec2(c0.x + (w - s.x) / 2, c0.y + (h - s.y) / 2), p.OnSurfaceVariant, text);
            x += w + app.Px(10);
        };
        auto tab = [&](const char* id, const char* label, bool selected) -> bool
        {
            ImVec2 s = TextSize(app, app.Medium, 16, label);
            float w = s.x + app.Px(40), h = app.Px(40);
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
            ImGui::PopItemFlag();
            bool hovered = ImGui::IsItemHovered();
            ImU32 bg = selected ? p.SecondaryContainer : hovered ? WithAlpha(p.OnSurface, 0.08f) : 0;
            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), bg, h / 2);
            TextAt(app, app.Medium, 16, ImVec2(x + (w - s.x) / 2, y + (h - s.y) / 2), selected ? p.OnSecondaryContainer : p.OnSurfaceVariant, label);
            x += w + app.Px(8);
            return pressed;
        };
        cap(app.UsingPad() ? "L" : "Q");
        if (tab("##tabsave", "Save", SaveTab) && !SaveTab)
            Tab(app, 1);
        if (tab("##tabload", "Load", !SaveTab) && SaveTab)
            Tab(app, 1);
        x += app.Px(2);
        cap(app.UsingPad() ? "R" : "W");

        // which files they are, on the right when there's room
        Host& host = app.GetHost();
        if (host.SlotFileNames)
        {
            std::string names = host.SlotFileNames();
            float room = win.x + size.x - app.Px(40) - (x + app.Px(24));
            if (room > app.Px(120))
            {
                std::string fitted = Ellipsize(app, app.Regular, 14, names, room);
                ImVec2 s = TextSize(app, app.Regular, 14, fitted.c_str());
                TextAt(app, app.Regular, 14, ImVec2(win.x + size.x - app.Px(40) - s.x, y + (app.Px(40) - s.y) / 2), p.OnSurfaceVariant,
                    fitted.c_str());
            }
        }
    }

    bool Card(App& app, const SaveSlots::Slot& slot, ImVec2 size, bool isDefault)
    {
        const Palette& p = app.Colors();
        bool enabled = SaveTab || slot.Exists;
        char id[16];
        snprintf(id, sizeof(id), "##slot%d", slot.Number);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        if (!enabled)
            ImGui::BeginDisabled();
        bool pressed = ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
        if (!enabled)
            ImGui::EndDisabled();
        app.TrackFocus(isDefault);
        bool focused = ImGui::IsItemFocused(), hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 max(pos.x + size.x, pos.y + size.y);
        float radius = app.Px(18);
        ImU32 base = focused ? p.SurfaceContainerHigh : p.SurfaceContainer;
        ImU32 bg = held ? Blend(base, p.OnSurface, 0.12f) : hovered && enabled ? Blend(base, p.OnSurface, 0.08f) : base;
        draw->AddRectFilled(pos, max, bg, radius);
        if (focused)
            FocusRing(app, draw, pos, max, radius);

        // the screenshot, both screens (2:3), or the empty slot's outline
        float inset = app.Px(12);
        float shotH = size.y - 2 * inset, shotW = std::floor(shotH * 2 / 3);
        ImVec2 s0(pos.x + inset, pos.y + inset), s1(s0.x + shotW, s0.y + shotH);
        if (slot.Exists)
        {
            int w = 0, h = 0;
            GLuint texture = slot.Screenshot.empty() ? 0 : app.Thumbnail(slot.Screenshot, slot.Stamp, w, h);
            if (texture)
            {
                draw->AddRectFilled(s0, s1, IM_COL32(0, 0, 0, 255), app.Px(8));
                draw->AddImageRounded(ImTextureRef((ImTextureID)(intptr_t)texture), s0, s1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE,
                    app.Px(8));
            }
            else
            {
                draw->AddRectFilled(s0, s1, p.SurfaceContainerHighest, app.Px(8));
                IconAt(app, Icon::HideImage, 28, ImVec2((s0.x + s1.x) / 2, (s0.y + s1.y) / 2), p.OnSurfaceVariant);
            }
        }
        else
        {
            DashedRect(app, draw, s0, s1, enabled ? p.Outline : WithAlpha(p.Outline, 0.5f));
            if (SaveTab)
                IconAt(app, Icon::Add, 30, ImVec2((s0.x + s1.x) / 2, (s0.y + s1.y) / 2), p.Outline);
        }

        // its name, when it was saved, and what A does
        float tx = s1.x + app.Px(16), tw = max.x - tx - app.Px(12);
        float ty = pos.y + app.Px(22);
        ImU32 on = enabled ? p.OnSurface : WithAlpha(p.OnSurface, 0.38f);
        ImU32 sub = enabled ? p.OnSurfaceVariant : WithAlpha(p.OnSurface, 0.38f);
        std::string name = Frontend::SlotName(slot.Number);
        ImVec2 ns = TextAt(app, app.Medium, 19, ImVec2(tx, ty), on, Ellipsize(app, app.Medium, 19, name, tw).c_str());
        ty += ns.y + app.Px(6);
        std::string when = slot.Exists ? slot.When : "Empty";
        ImVec2 ws = TextAt(app, app.Regular, 15, ImVec2(tx, ty), sub, Ellipsize(app, app.Regular, 15, when, tw).c_str());
        ty += ws.y + app.Px(4);
        const char* action = nullptr;
        if (SaveTab)
            action = slot.Exists ? "Press A to save over it" : "Press A to save here";
        else if (slot.Exists)
            action = "Press A to load";
        if (action && focused)
        {
            std::string text = action;
            if (!app.UsingPad())
                text.replace(text.find("A"), 1, "Enter");
            TextAt(app, app.Regular, 14, ImVec2(tx, ty), sub, Ellipsize(app, app.Regular, 14, text, tw).c_str());
        }
        if (app.Busy && slot.Number == BusySlot)
        {
            ImVec2 bs = TextSize(app, app.Medium, 15, SaveTab ? "Saving\xe2\x80\xa6" : "Loading\xe2\x80\xa6");
            TextAt(app, app.Medium, 15, ImVec2(tx, max.y - app.Px(16) - bs.y), p.Primary, SaveTab ? "Saving\xe2\x80\xa6" : "Loading\xe2\x80\xa6");
        }
        return pressed && enabled;
    }

    static void DashedRect(App& app, ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 color)
    {
        float t = std::max(1.f, app.Px(2)), dash = app.Px(7), space = app.Px(5);
        auto line = [&](ImVec2 from, ImVec2 to)
        {
            float len = std::hypot(to.x - from.x, to.y - from.y);
            ImVec2 d((to.x - from.x) / len, (to.y - from.y) / len);
            for (float s = 0; s < len; s += dash + space)
            {
                float e = std::min(len, s + dash);
                draw->AddLine(ImVec2(from.x + d.x * s, from.y + d.y * s), ImVec2(from.x + d.x * e, from.y + d.y * e), color, t);
            }
        };
        float r = t / 2;
        line(ImVec2(a.x + r, a.y + r), ImVec2(b.x - r, a.y + r));
        line(ImVec2(b.x - r, a.y + r), ImVec2(b.x - r, b.y - r));
        line(ImVec2(b.x - r, b.y - r), ImVec2(a.x + r, b.y - r));
        line(ImVec2(a.x + r, b.y - r), ImVec2(a.x + r, a.y + r));
    }

    void Choose(App& app, const SaveSlots::Slot& slot)
    {
        Host& host = app.GetHost();
        int number = slot.Number;
        app.Busy = true;
        BusySlot = number;
        App* ui = &app;
        if (SaveTab)
        {
            host.SaveSlot(number, [ui, number](bool ok)
            {
                ui->Busy = false;
                if (ok)
                {
                    ui->CloseAll();
                    ui->Toast("Saved to " + Frontend::SlotName(number));
                }
                else
                    ui->Toast("Couldn't save to " + Frontend::SlotName(number));
            });
        }
        else
        {
            host.LoadSlot(number, [ui, number](bool ok)
            {
                ui->Busy = false;
                if (ok)
                {
                    ui->CloseAll();
                    ui->Toast("Loaded " + Frontend::SlotName(number));
                }
                else
                    ui->Toast("Couldn't load " + Frontend::SlotName(number));
            });
        }
    }

    enum class Question { None, Overwrite, Delete };

    void OpenQuestion(Question question, int slot)
    {
        Ask = question;
        AskSlot = slot;
        AskFocus = true;
        SavedFocus = FocusId;
        FocusId = 0;
        WantFocus = true;
        printf("ui: ask %s slot %d\n", question == Question::Delete ? "delete" : "overwrite", slot);
        fflush(stdout);
    }

    void CloseQuestion()
    {
        Ask = Question::None;
        FocusId = SavedFocus;
        WantFocus = true;
        printf("ui: %s\n", Id());
        fflush(stdout);
    }

    // the question over the grid, in a window of its own: the answer that does it, and Cancel (the focus starts on Cancel for a delete)
    void DrawQuestion(App& app, const std::vector<SaveSlots::Slot>& slots)
    {
        const Palette& p = app.Colors();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::GetForegroundDrawList(); // (the screen behind is drawn)
        float w = std::min(viewport->Size.x - app.Px(32), app.Px(520)), h = app.Px(224);
        ImVec2 pos(viewport->Pos.x + (viewport->Size.x - w) / 2, viewport->Pos.y + (viewport->Size.y - h) / 2);
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(ImVec2(w, h));
        if (AskFocus)
            ImGui::SetNextWindowFocus();
        AskFocus = false;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(app.Px(24), app.Px(24)));
        ImGui::Begin("##question", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        // the dim over the whole window, then the card
        ImGui::GetBackgroundDrawList();
        draw->PushClipRectFullScreen();
        draw->AddRectFilled(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), WithAlpha(p.Scrim, 0.45f));
        draw->PopClipRect();
        draw->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), p.SurfaceContainerLow, app.Px(28));
        draw->AddRect(pos, ImVec2(pos.x + w, pos.y + h), p.OutlineVariant, app.Px(28), std::max(1.f, app.Px(1)));
        bool del = Ask == Question::Delete;
        std::string when;
        for (const SaveSlots::Slot& slot : slots)
        {
            if (slot.Number == AskSlot)
                when = slot.When;
        }
        float tx = pos.x + app.Px(24), tw = w - app.Px(48);
        std::string title = (del ? "Delete " : "Save over ") + Frontend::SlotName(AskSlot) + "?";
        ImVec2 ts = TextAt(app, app.Medium, 22, ImVec2(tx, pos.y + app.Px(24)), p.OnSurface, Ellipsize(app, app.Medium, 22, title, tw).c_str());
        std::string body = del ? "The state from " + when + " goes to the recycle bin, with its screenshot (Windows asks first where there's no bin)."
                               : "The state from " + when + " is replaced by the game as it is now.";
        TextAt(app, app.Regular, 15, ImVec2(tx, pos.y + app.Px(24) + ts.y + app.Px(10)), p.OnSurfaceVariant, body.c_str(), tw);

        float bw = (tw - app.Px(12)) / 2;
        ImGui::SetCursorScreenPos(ImVec2(tx, pos.y + h - app.Px(24) - app.Px(68)));
        bool yes = BigButton(app, "##yes", del ? Icon::Delete : Icon::Save, del ? "Delete" : "Save over it", nullptr, true, bw, !del);
        ImGui::SameLine(0, app.Px(12));
        bool no = BigButton(app, "##no", Icon::Close, "Cancel", nullptr, false, bw, del);
        ImGui::End();
        ImGui::PopStyleVar();

        if (no)
            CloseQuestion();
        else if (yes)
        {
            Host& host = app.GetHost();
            int slot = AskSlot;
            Question question = Ask;
            CloseQuestion();
            if (question == Question::Delete)
            {
                if (host.DeleteSlot && host.DeleteSlot(slot))
                    app.Toast("Deleted " + Frontend::SlotName(slot));
                else
                    app.Toast("Couldn't delete " + Frontend::SlotName(slot));
            }
            else
            {
                for (const SaveSlots::Slot& s : slots)
                {
                    if (s.Number == slot)
                        Choose(app, s);
                }
            }
        }
    }

    bool SaveTab;
    int BusySlot = -1;
    int FocusedSlot = -1;
    bool FocusedExists = false;
    Question Ask = Question::None;
    int AskSlot = -1;
    bool AskFocus = false;
    ImGuiID SavedFocus = 0;
};

}

std::unique_ptr<Screen> MakeStatesScreen(bool save)
{
    return std::make_unique<StatesScreen>(save);
}

}
