// The layout editor's panels, as the app's: the selected screen's panel (LayoutEditorItemControls' full panel: size, aspect ratio, the top
// screen's widescreen ratio, the 1x/2x/3x/fit sizes, opacity, above the other screen, centering; plus swap and remove), the menu
// (LayoutEditorActivity.MenuOption: properties, background, the snap grid, add a screen, revert, reset, save, save and exit, exit without
// saving; plus undo and the other detached window), the properties (LayoutPropertiesDialog), the background (LayoutBackgroundDialog and the
// picker), and saving and leaving. Every change shows at once and can be undone; the app's dialogs have OK and Cancel instead
#include "LayoutEditor.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iterator>

#include "Icons.h"
#include "Settings.h"
#include "Ui.h"
#include "Widgets.h"
#include "layout/Layouts.h"
#include "render/Backgrounds.h"

namespace Ui
{

using Frontend::EditorSpace;
using Frontend::IntRect;
using Frontend::LayoutComponent;
using Frontend::ScreenAspect;

namespace
{

const char* const TOP = "TOP_SCREEN";
const char* const BOTTOM = "BOTTOM_SCREEN";
// the app's default name for a new layout (custom_layout_default_name)
const char* const DEFAULT_NAME = "My Layout";
const char* const kModeLabels[] = {"Stretch", "Fit center", "Fit top", "Fit left", "Fit bottom", "Fit right"};
const char* const kOrientations[] = {"FOLLOW_SYSTEM", "PORTRAIT", "LANDSCAPE"};
const char* const kOrientationLabels[] = {"Follow system", "Portrait", "Landscape"};
const char* const kGridSizes[] = {"SMALL", "MEDIUM", "LARGE"};
const char* const kGridLabels[] = {"Small", "Medium", "Large"};

std::string Trimmed(const std::string& text)
{
    size_t start = text.find_first_not_of(" \t");
    if (start == std::string::npos)
        return "";
    return text.substr(start, text.find_last_not_of(" \t") - start + 1);
}

std::string Lower(std::string text)
{
    for (char& c : text)
        c = (char)std::tolower((unsigned char)c);
    return text;
}

int Cycle(int index, int change, int count)
{
    return ((index + change) % count + count) % count;
}

const char* AspectLabel(ScreenAspect aspect)
{
    switch (aspect)
    {
    case ScreenAspect::Ratio43: return "4:3";
    case ScreenAspect::Ratio169: return "16:9";
    case ScreenAspect::Wide: return "Wide";
    default: return "Free";
    }
}

}

void LayoutEditor::DrawPanel(App& app)
{
    const LayoutComponent* s = Selection();
    if (!s)
    {
        PanelOpen = false;
        return;
    }
    // a copy: a change below replaces the session's screens
    LayoutComponent screen = *s;
    const Palette& p = app.Colors();
    ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    float w = std::min(app.Px(420), size.x - app.Px(32));
    // docked on the side away from the screen (dockPanel docks on the half away from it)
    ImVec2 a = U((float)screen.Rect.X, (float)screen.Rect.Y), b = U((float)screen.Rect.Right(), (float)screen.Rect.Bottom());
    bool left = (a.x + b.x) / 2 > win.x + size.x / 2;
    float x0 = left ? win.x + app.Px(16) : win.x + size.x - w - app.Px(16);
    float y0 = win.y + app.Px(104), y1 = win.y + size.y - FooterHeight(app) - app.Px(8);
    PanelMin = ImVec2(x0, y0);
    PanelMax = ImVec2(x0 + w, std::max(y0 + app.Px(80), y1));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(PanelMin, PanelMax, WithAlpha(p.SurfaceContainerLow, 0.96f), app.Px(24));
    TextAt(app, app.Medium, 20, ImVec2(x0 + app.Px(24), y0 + app.Px(16)), p.OnSurface, screen.IsTopScreen() ? "Top screen" : "Bottom screen");

    float ring = app.Px(12);
    ImGui::SetCursorScreenPos(ImVec2(x0 + app.Px(12), y0 + app.Px(56)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ring, ring));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, app.Px(2)));
    ImGui::BeginChild("##panel", ImVec2(w - app.Px(24), PanelMax.y - y0 - app.Px(64)), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
    PanelRows(app, screen);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

void LayoutEditor::PanelRows(App& app, const LayoutComponent& screen)
{
    // the rows change a copy, stored once they're all drawn (a change replaces the session's screens)
    PanelEdit edit;
    edit.Screen = screen;
    EditorSpace space = Space(app, Selected);
    SizeRows(app, edit, space);
    ShapeRows(app, edit, space);
    ScaleRow(app, edit, space);
    PlaceRows(app, edit, space);
    edit.Remove = MenuRow(app, "##remove", Icon::Delete, "Remove this screen", "Menu > Add puts it back");
    edit.Done = MenuRow(app, "##done", Icon::Check, "Done", nullptr);
    ApplyPanel(edit, screen, space);
}

void LayoutEditor::SizeRows(App& app, PanelEdit& edit, const EditorSpace& space)
{
    // one row with the ratio locked, width and height when it's free (a pixel a step)
    IntRect& r = edit.Screen.Rect;
    char value[32];
    if (Aspect != ScreenAspect::Free)
    {
        snprintf(value, sizeof(value), "%d x %d", r.W, r.H);
        if (int ch = ChoiceRow(app, "##size", Icon::Straighten, "Size", value, true))
        {
            r = Frontend::ResizeTo(r, (float)(r.W + ch), (float)r.H, LockedRatio(), Anchor, space);
            edit.Kind = "size";
        }
        return;
    }
    snprintf(value, sizeof(value), "%d", r.W);
    if (int ch = ChoiceRow(app, "##width", Icon::Straighten, "Width", value, true))
    {
        r = Frontend::ResizeTo(r, (float)(r.W + ch), (float)r.H, std::nullopt, Anchor, space);
        edit.Kind = "size";
    }
    snprintf(value, sizeof(value), "%d", r.H);
    if (int ch = ChoiceRow(app, "##height", Icon::Straighten, "Height", value))
    {
        r = Frontend::ResizeTo(r, (float)r.W, (float)(r.H + ch), std::nullopt, Anchor, space);
        edit.Kind = "size";
    }
}

void LayoutEditor::ShapeRows(App& app, PanelEdit& edit, const EditorSpace& space)
{
    // the ratio it keeps: 4:3, 16:9 (in a wide variant the top screen's own ratio instead), free
    std::optional<float> wide = WideRatio(Selected);
    const std::optional<std::string>& wideKey = Session.Shown().WideAspect;
    const ScreenAspect aspects[3] = {ScreenAspect::Ratio43, wide ? ScreenAspect::Wide : ScreenAspect::Ratio169, ScreenAspect::Free};
    int aspectAt = (int)(std::find(aspects, aspects + 3, Aspect) - aspects);
    std::string aspectText = Aspect == ScreenAspect::Wide && wideKey ? "Wide (" + *wideKey + ")" : AspectLabel(Aspect);
    if (int ch = ChoiceRow(app, "##aspect", Icon::AspectRatio, "Aspect ratio", aspectText.c_str()))
    {
        Aspect = aspects[Cycle(aspectAt % 3, ch, 3)];
        // the width kept, the height fixed to the new ratio
        if (std::optional<float> ratio = LockedRatio())
        {
            edit.Screen.Rect = Frontend::ResizeTo(edit.Screen.Rect, (float)edit.Screen.Rect.W, (float)edit.Screen.Rect.H, ratio, Anchor, space);
            edit.Kind = "aspect";
        }
    }
    // the variant's widescreen ratio, from its top screen (the app's Wide chip)
    if (!edit.Screen.IsTopScreen())
        return;
    const std::vector<std::string>& keys = Frontend::WidescreenAspectKeys();
    int at = wideKey ? (int)(std::find(keys.begin(), keys.end(), *wideKey) - keys.begin()) + 1 : 0;
    if (int ch = ChoiceRow(app, "##wide", Icon::Fullscreen, "Widescreen", wideKey ? wideKey->c_str() : "Off", false,
            "The top screen at a wide ratio, for WideMelon"))
    {
        int next = Cycle(std::min(at, (int)keys.size()), ch, (int)keys.size() + 1);
        edit.Wide = next == 0 ? std::optional<std::string>() : std::optional<std::string>(keys[next - 1]);
    }
}

void LayoutEditor::ScaleRow(App& app, PanelEdit& edit, const EditorSpace& space)
{
    // the DS screen at 1x, 2x and 3x where they fit, and as big as it fits (the app's scale shortcuts)
    IntRect& r = edit.Screen.Rect;
    std::optional<float> ratio = LockedRatio();
    std::vector<std::pair<std::string, std::pair<int, int>>> scales;
    for (int scale = 1; scale <= 3; scale++)
    {
        int w, h;
        Frontend::ScaleSize(scale, Aspect, ratio, w, h);
        if (w <= space.W && h <= space.H)
            scales.push_back({std::to_string(scale) + "x", {w, h}});
    }
    int fitW, fitH;
    Frontend::FitSize(r, ratio, space.W, space.H, fitW, fitH);
    scales.push_back({"Fit", {fitW, fitH}});
    int at = -1;
    for (size_t i = 0; i < scales.size() && at < 0; i++)
    {
        if (scales[i].second == std::make_pair(r.W, r.H))
            at = (int)i;
    }
    if (int ch = ChoiceRow(app, "##scale", Icon::FitScreen, "Scale", at >= 0 ? scales[at].first.c_str() : "Custom"))
    {
        int n = (int)scales.size();
        int next = at < 0 ? (ch > 0 ? 0 : n - 1) : Cycle(at, ch, n);
        r = Frontend::ResizeTo(r, (float)scales[next].second.first, (float)scales[next].second.second, ratio, Anchor, space);
        edit.Kind = "scale";
    }
}

void LayoutEditor::PlaceRows(App& app, PanelEdit& edit, const EditorSpace& space)
{
    LayoutComponent& c = edit.Screen;
    const LayoutComponent* other = Session.FindScreen(c.IsTopScreen() ? BOTTOM : TOP);
    int opacity = Frontend::RoundToInt(c.Alpha * 100);
    char value[16];
    snprintf(value, sizeof(value), "%d%%", opacity);
    if (int ch = ChoiceRow(app, "##opacity", Icon::Contrast, "Opacity", value))
    {
        c.Alpha = std::clamp(opacity + 5 * ch, 0, 100) / 100.f;
        edit.Kind = "opacity";
    }
    if (ToggleRow(app, "##ontop", Icon::Layers, "Above the other screen", nullptr, c.OnTop))
    {
        c.OnTop = !c.OnTop;
        edit.Kind = "on top";
        // only one screen can be above the other
        if (c.OnTop && other && other->OnTop)
        {
            edit.Others.push_back(*other);
            edit.Others.back().OnTop = false;
        }
    }
    if (MenuRow(app, "##centerh", Icon::Dashboard, "Center horizontally", nullptr))
    {
        c.Rect.X = Frontend::CenteredStart(c.Rect.W, space.W);
        edit.Kind = "center";
    }
    if (MenuRow(app, "##centerv", Icon::Dashboard, "Center vertically", nullptr))
    {
        c.Rect.Y = Frontend::CenteredStart(c.Rect.H, space.H);
        edit.Kind = "center";
    }
    if (other && MenuRow(app, "##swap", Icon::SwapVert, c.IsTopScreen() ? "Swap places with the bottom screen" : "Swap places with the top screen", nullptr))
    {
        edit.Others.push_back(*other);
        edit.Others.back().Rect = c.Rect;
        c.Rect = other->Rect;
        edit.Kind = "swap";
    }
}

void LayoutEditor::ApplyPanel(const PanelEdit& edit, const LayoutComponent& before, const EditorSpace& space)
{
    if (edit.Wide)
    {
        Session.SetWideAspect(*edit.Wide);
        Changed();
        Select(Selected);
        Report("widescreen");
        return;
    }
    if (edit.Remove)
    {
        Session.RemoveScreen(before.Component);
        Changed();
        Report(("removed " + before.Component).c_str());
        Selected.clear();
        PanelOpen = false;
        return;
    }
    const LayoutComponent& c = edit.Screen;
    if (!(c.Rect == before.Rect) || c.Alpha != before.Alpha || c.OnTop != before.OnTop || !edit.Others.empty())
    {
        std::vector<LayoutComponent> changed = edit.Others;
        changed.push_back(c);
        Session.SetScreens(changed, Continues(edit.Kind.c_str()));
        Changed();
        if (edit.Kind == "center" || edit.Kind == "swap")
            Anchor = Frontend::NearestCorner(c.Rect, space.W, space.H);
        // the other screen's place has its own shape
        if (edit.Kind == "swap")
            Aspect = Frontend::GuessAspect(c.Rect, WideRatio(Selected));
        Report(edit.Kind.c_str());
    }
    if (edit.Done)
    {
        // as the app's Done: the controls close and the screen is let go
        PanelOpen = false;
        Selected.clear();
        Report("deselect");
    }
}

void LayoutEditor::OpenMenu(App& app)
{
    LayoutEditor* self = this;
    std::string body = StatusText();
    body[0] = (char)std::toupper((unsigned char)body[0]);
    app.Push(MakePanelScreen("layout-editor/menu", "Layout editor", body, [self](App& app) { self->MenuRows(app); }));
}

void LayoutEditor::MenuRows(App& app)
{
    Keep(app);
    Host& host = *HostRef;
    Frontend::Settings* settings = host.Settings;
    const Frontend::LayoutConfiguration& config = Session.Config();
    std::string name = config.Name && !config.Name->empty() ? *config.Name : "Not set";
    if (MenuRow(app, "##props", Icon::Tune, "Properties", name.c_str(), true))
    {
        app.Pop();
        OpenProperties(app);
        return;
    }
    const Frontend::ScreenLayout& shown = Session.Shown().Main;
    std::string background = (shown.BackgroundId && host.Backgrounds ? host.Backgrounds->NameOf(*shown.BackgroundId) : "None") + " \xc2\xb7 "
        + kModeLabels[(int)shown.GetMode()];
    if (MenuRow(app, "##background", Icon::Image, "Background", background.c_str()))
    {
        app.Pop();
        OpenBackground(app);
        return;
    }
    bool snap = settings && settings->GetBool("layout_editor_snap_to_grid", false);
    if (settings && ToggleRow(app, "##snap", Icon::GridOn, "Snap to grid", "Dragged screens line up with the grid, the edges, the middle and the other screen", snap))
        settings->Set("layout_editor_snap_to_grid", picojson::value(!snap));
    std::string grid = settings ? settings->GetString("layout_editor_grid_size", "MEDIUM") : "MEDIUM";
    int gridAt = grid == "SMALL" ? 0 : grid == "LARGE" ? 2 : 1;
    if (int ch = settings ? ChoiceRow(app, "##grid", Icon::GridOn, "Grid size", kGridLabels[gridAt]) : 0)
        settings->Set("layout_editor_grid_size", picojson::value(std::string(kGridSizes[Cycle(gridAt, ch, 3)])));
    for (const char* screen : {TOP, BOTTOM})
    {
        bool isTop = screen == TOP;
        if (Session.FindScreen(screen) || (Session.Target().Detached && isTop != Session.Target().TopScreen))
            continue;
        if (MenuRow(app, isTop ? "##addtop" : "##addbottom", Icon::Add, isTop ? "Add the top screen" : "Add the bottom screen", nullptr))
        {
            app.Pop();
            // at 1x in the middle (the app adds a component at the top left at its default size)
            LayoutComponent c;
            c.Component = screen;
            int w, h;
            Frontend::ConstrainSize(Frontend::CONSOLE_ASPECT_RATIO, 256, 192, Session.Target().W, Session.Target().H, 1, w, h);
            c.Rect = {Frontend::CenteredStart(w, Session.Target().W), Frontend::CenteredStart(h, Session.Target().H), w, h};
            Session.SetScreens({c});
            Changed();
            Select(screen);
            Report("added");
            return;
        }
    }
    if (Session.CanUndo() && MenuRow(app, "##undo", Icon::History, "Undo", app.UsingPad() ? "X" : "Ctrl+Z"))
    {
        app.Pop();
        Clear(app);
        return;
    }
    if (Session.Target().Detached && MenuRow(app, "##other", Icon::OpenInNew, "Edit the other window",
            Session.Target().TopScreen ? "The bottom screen's window" : "The top screen's window"))
    {
        app.Pop();
        Surface = 1 - Surface;
        Selected.clear();
        PanelOpen = false;
        return;
    }
    MenuSaveRows(app);
}

void LayoutEditor::MenuSaveRows(App& app)
{
    if (Session.Dirty() && MenuRow(app, "##revert", Icon::SettingsBackupRestore, "Revert changes", "Back to the layout as it was saved"))
    {
        app.Pop();
        Session.Revert();
        Changed();
        Report("reverted");
        return;
    }
    if (MenuRow(app, "##reset", Icon::RestartAlt, "Reset to default",
            Session.Target().Detached ? "No detached layouts: each window shows its screen at its fit" : "No sizes of its own: windows show the Default layout"))
    {
        app.Pop();
        Session.Reset();
        Changed();
        Report("reset");
        return;
    }
    if (MenuRow(app, "##save", Icon::Save, "Save", nullptr))
    {
        app.Pop();
        Save(app, false);
        return;
    }
    if (MenuRow(app, "##saveexit", Icon::Save, "Save and exit", nullptr))
    {
        app.Pop();
        Save(app, true);
        return;
    }
    if (MenuRow(app, "##exit", Icon::Logout, "Exit without saving", nullptr))
    {
        app.Pop();
        if (!Session.Dirty())
        {
            Exit(app);
            return;
        }
        LayoutEditor* self = this;
        app.Push(MakePanelScreen("layout-editor/discard", "Discard changes?", "This layout has unsaved changes. They will be lost if you exit now.",
            [self](App& app)
            {
                self->Keep(app);
                if (MenuRow(app, "##discard", Icon::Delete, "Discard", nullptr, true))
                {
                    app.Pop();
                    self->Exit(app);
                }
                else if (MenuRow(app, "##cancel", Icon::Close, "Cancel", nullptr))
                    app.Pop();
            }));
    }
}

void LayoutEditor::OpenProperties(App& app)
{
    LayoutEditor* self = this;
    app.Push(MakePanelScreen("layout-editor/properties", "Properties",
        "Its name, and what the phone uses: the orientation it turns to, and its on-screen buttons' opacity (a window doesn't turn, and the "
        "desktop draws no buttons)",
        [self](App& app)
        {
            self->Keep(app);
            self->FinishNameEntry(app);
            const Frontend::LayoutConfiguration& config = self->Session.Config();
            if (DetailRow(app, "##name", Icon::Title, "Name", config.Name && !config.Name->empty() ? config.Name->c_str() : "Not set", true))
            {
                self->NameEntry = config.Name && !config.Name->empty() ? *config.Name : DEFAULT_NAME;
                self->PendingName = NameFor::Rename;
                app.OpenKeyboard(&self->NameEntry, "Layout name", 40);
            }
            int at = (int)(std::find_if(std::begin(kOrientations), std::end(kOrientations), [&](const char* o) { return config.Orientation == o; })
                - std::begin(kOrientations));
            if (int ch = ChoiceRow(app, "##orientation", Icon::Monitor, "Orientation", at < 3 ? kOrientationLabels[at] : config.Orientation.c_str()))
                self->Session.SetProperties(kOrientations[Cycle(at % 3, ch, 3)], config.UseCustomOpacity, config.Opacity);
            if (ToggleRow(app, "##defaultopacity", Icon::Tune, "Use the default opacity", "For the phone's on-screen buttons", !config.UseCustomOpacity))
                self->Session.SetProperties(config.Orientation, !config.UseCustomOpacity, config.Opacity);
            char value[16];
            snprintf(value, sizeof(value), config.UseCustomOpacity ? "%d%%" : "Default", config.Opacity);
            int ch = ChoiceRow(app, "##opacity", Icon::Contrast, "Button opacity", value);
            if (ch && config.UseCustomOpacity)
                self->Session.SetProperties(config.Orientation, true, std::clamp(config.Opacity + 5 * ch, 0, 100));
            if (MenuRow(app, "##done", Icon::Check, "Done", nullptr))
                app.Pop();
        }));
}

void LayoutEditor::OpenBackground(App& app)
{
    LayoutEditor* self = this;
    app.Push(MakePanelScreen("layout-editor/background", "Background", "Behind the screens, in this window's variant", [self](App& app)
    {
        self->Keep(app);
        Host& host = *self->HostRef;
        const Frontend::ScreenLayout& shown = self->Session.Shown().Main;
        std::string name = shown.BackgroundId && host.Backgrounds ? host.Backgrounds->NameOf(*shown.BackgroundId) : "None";
        if (DetailRow(app, "##pick", Icon::Image, "Background", name.c_str(), true))
            self->OpenBackgroundList(app);
        int mode = (int)shown.GetMode();
        if (int ch = ChoiceRow(app, "##mode", Icon::FitScreen, "Mode", kModeLabels[mode]))
        {
            self->Session.SetBackground(shown.BackgroundId, Frontend::BackgroundModeName((Frontend::BackgroundMode)Cycle(mode, ch, 6)));
            self->Changed();
            self->Report("background mode");
        }
        if (!host.Hidden && host.PickBackgroundFile && host.Backgrounds
            && MenuRow(app, "##file", Icon::FolderOpen, "Add an image file", "The Windows file dialog: needs a mouse or keyboard"))
        {
            // after the frame (the dialog is modal), as Open ROM; the editor may be gone by then
            std::weak_ptr<bool> alive = self->Alive;
            App* ui = &app;
            host.PickBackgroundFile([self, alive, ui](const std::optional<std::string>& file)
            {
                if (alive.expired() || !file)
                    return;
                self->AddBackgroundFile(*ui, *file);
            });
        }
        if (MenuRow(app, "##done", Icon::Check, "Done", nullptr))
            app.Pop();
    }));
}

void LayoutEditor::AddBackgroundFile(App& app, const std::string& file)
{
    std::string error;
    std::optional<std::string> id = HostRef->Backgrounds->AddUserFile(Frontend::PathFromUtf8(file), error);
    if (!id)
    {
        app.Toast("Couldn't add it: " + error);
        return;
    }
    Session.SetBackground(id, Session.Shown().Main.Mode);
    Changed();
    Report("background");
}

void LayoutEditor::OpenBackgroundList(App& app)
{
    LayoutEditor* self = this;
    app.Push(MakePanelScreen("layout-editor/backgrounds", "Pick a background", "", [self](App& app)
    {
        self->Keep(app);
        Host& host = *self->HostRef;
        const Frontend::ScreenLayout& shown = self->Session.Shown().Main;
        std::string current = shown.BackgroundId ? Lower(*shown.BackgroundId) : "";
        std::optional<std::string> picked;
        bool chosen = false;
        if (MenuRow(app, "##none", current.empty() ? Icon::Check : Icon::HideImage, "None", nullptr, current.empty()))
            chosen = true;
        if (host.Backgrounds)
        {
            for (const Frontend::Backgrounds::Entry& e : host.Backgrounds->All())
            {
                std::string id = "##" + e.Id;
                bool isCurrent = Lower(e.Id) == current;
                if (MenuRow(app, id.c_str(), isCurrent ? Icon::Check : Icon::Image, e.Name.c_str(), e.BuiltIn ? "Built in" : "Yours", isCurrent))
                {
                    picked = e.Id;
                    chosen = true;
                }
            }
        }
        if (chosen)
        {
            app.Pop();
            self->Session.SetBackground(picked, shown.Mode);
            self->Changed();
            self->Report("background");
        }
    }));
}

void LayoutEditor::FinishNameEntry(App& app)
{
    if (PendingName == NameFor::None || app.KeyboardOpen())
        return;
    NameFor what = PendingName;
    PendingName = NameFor::None;
    std::string name = Trimmed(NameEntry);
    if (name.empty())
    {
        if (what != NameFor::Rename)
            app.Toast("A layout needs a name to be saved");
        return;
    }
    if (!Session.Config().Name || *Session.Config().Name != name)
        Session.SetName(name);
    printf("layout editor: named \"%s\"\n", name.c_str());
    fflush(stdout);
    if (what != NameFor::Rename)
        Save(app, what == NameFor::SaveAndExit);
}

void LayoutEditor::Save(App& app, bool exitAfter)
{
    Host& host = *HostRef;
    const Frontend::LayoutConfiguration& config = Session.Config();
    if (!config.Name || Trimmed(*config.Name).empty())
    {
        // as the app asks for one before it saves
        NameEntry = DEFAULT_NAME;
        PendingName = exitAfter ? NameFor::SaveAndExit : NameFor::Save;
        app.OpenKeyboard(&NameEntry, "Layout name", 40);
        return;
    }
    if (!host.Layouts->CanSave())
    {
        app.Toast("layouts.json couldn't be read, so it isn't written over");
        return;
    }
    // a new layout from the one in use saved before any change: the screens it shows go into it, or it would be saved empty
    Session.StoreSeed();
    bool isNew = !config.Id;
    std::string id = isNew ? Frontend::Layouts::NewId() : *config.Id;
    Frontend::LayoutConfiguration toSave = config;
    toSave.Id = id;
    if (!host.Layouts->Put(toSave))
    {
        app.Toast("Couldn't save layouts.json");
        return;
    }
    Session.SetId(id);
    Session.MarkSaved();
    printf("layout editor: saved \"%s\" (%s)\n", toSave.Name->c_str(), id.c_str());
    fflush(stdout);
    app.Toast("Layout saved");
    if (host.LayoutsChanged)
        host.LayoutsChanged();
    if (PickAfterSave && isNew && host.PickLayoutInGame)
        host.PickLayoutInGame(id);
    PickAfterSave = false;
    if (exitAfter)
        Exit(app);
}

void LayoutEditor::AskExit(App& app)
{
    if (!Session.Dirty())
    {
        Exit(app);
        return;
    }
    LayoutEditor* self = this;
    printf("layout editor: unsaved changes, asking\n");
    fflush(stdout);
    app.Push(MakePanelScreen("layout-editor/exit", "Save your changes?", "This layout has unsaved changes. They will be lost if you exit without saving.",
        [self](App& app)
        {
            self->Keep(app);
            if (MenuRow(app, "##save", Icon::Save, "Save and exit", nullptr, true))
            {
                app.Pop();
                self->Save(app, true);
            }
            else if (MenuRow(app, "##discard", Icon::Delete, "Exit without saving", nullptr))
            {
                app.Pop();
                self->Exit(app);
            }
            else if (MenuRow(app, "##keep", Icon::Edit, "Keep editing", nullptr))
                app.Pop();
        }));
}

void LayoutEditor::Exit(App& app)
{
    if (HostRef && HostRef->SetLayoutPreview)
        HostRef->SetLayoutPreview(std::nullopt);
    printf("layout editor: closed%s\n", Session.Dirty() ? ", changes discarded" : "");
    fflush(stdout);
    app.Pop();
}

}
