// The layout editor's canvas, as the app's LayoutEditorView: the screens where the layout puts them (the game's frame drawn there by the
// renderer through the host's preview; with no game the app's blue and red stand-ins), the selected one's resize handles, the snap grid, and
// the input. Mouse: a click selects (and nothing deselects), a drag moves, a corner handle resizes with the opposite corner fixed, the wheel
// resizes. Controller: L / R pick a screen, the D-pad moves it a pixel, the left stick 16 at a time (to the next snap line with snapping on),
// the triggers make it smaller or bigger, A opens its panel, X undoes, Start opens the menu, B goes back. Keyboard: Tab, the arrows (Shift:
// 16 pixels), - and + (Shift: one pixel), Enter, Ctrl+Z, M, Esc. A second window being edited is shown as a small copy in the main window
#include "LayoutEditor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "Icons.h"
#include "Settings.h"
#include "Ui.h"
#include "Widgets.h"
#include "layout/Layouts.h"

namespace Ui
{

using Frontend::Corner;
using Frontend::EditorSpace;
using Frontend::IntRect;
using Frontend::LayoutComponent;
using Frontend::LayoutEditTarget;

namespace
{

// the app's editor colors (background_top_screen.xml, background_bottom_screen.xml, LayoutEditorOverlayDrawer), the same whatever the theme
constexpr ImU32 TOP_FILL = IM_COL32(0x22, 0x99, 0xFF, 0x77);
constexpr ImU32 TOP_STROKE = IM_COL32(0x22, 0x88, 0xFF, 0xFF);
constexpr ImU32 BOTTOM_FILL = IM_COL32(0xFF, 0x44, 0x44, 0x77);
constexpr ImU32 BOTTOM_STROKE = IM_COL32(0xFF, 0x44, 0x44, 0xFF);
constexpr ImU32 HANDLE_FILL = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
constexpr ImU32 HANDLE_STROKE = IM_COL32(0, 0, 0, 0x99);
constexpr ImU32 GRID = IM_COL32(0xFF, 0xFF, 0xFF, 0x50);
constexpr ImU32 LABEL_BACKGROUND = IM_COL32(0xFF, 0xFF, 0xFF, 0xAF);
constexpr float UNSELECTED_ALPHA = 0.5f;
// the handles' touch areas take at most this much of the screen's smaller side; a small screen gets only the bottom-right one, bigger
constexpr float HANDLE_TOUCH_FRACTION = 0.25f;
constexpr float SMALL_VIEW_HANDLE_TOUCH_FRACTION = 0.4f;
// the app's pill steps a size by 16 pixels; a coarse move is as far
constexpr int SIZE_STEP = 16;
constexpr int COARSE_STEP = 16;
// a held stick or trigger: a step at once, then after 24 frames (the app's 400 ms at 60) one every 3 (50 ms)
constexpr int REPEAT_DELAY = 24;
constexpr int REPEAT_EVERY = 3;
const char* const TOP = "TOP_SCREEN";
const char* const BOTTOM = "BOTTOM_SCREEN";

bool Repeats(int frames)
{
    return frames == 1 || (frames > REPEAT_DELAY && (frames - REPEAT_DELAY) % REPEAT_EVERY == 0);
}

bool Inside(ImVec2 p, ImVec2 a, ImVec2 b)
{
    return p.x >= a.x && p.x < b.x && p.y >= a.y && p.y < b.y;
}

// LayoutEditorGridSize: the grid's spacing in dp (here reference pixels)
float GridSpacing(const Frontend::Settings* settings)
{
    std::string size = settings ? settings->GetString("layout_editor_grid_size", "MEDIUM") : "MEDIUM";
    return size == "SMALL" ? 16.f : size == "LARGE" ? 48.f : 32.f;
}

bool Pressed(ImGuiKey key, bool repeat = true)
{
    return ImGui::IsKeyPressed(key, repeat);
}

}

LayoutEditor::~LayoutEditor()
{
    // the windows show the layout in use again, however the editor was closed
    if (HostRef && HostRef->SetLayoutPreview)
        HostRef->SetLayoutPreview(std::nullopt);
}

bool LayoutEditor::Start(App& app)
{
    Started = true;
    Host& host = app.GetHost();
    HostRef = &host;
    if (!host.Layouts || !host.LayoutEditTarget || !host.SetLayoutPreview)
    {
        app.Pop();
        return false;
    }
    LayoutEditTarget target = host.LayoutEditTarget(-1);
    Surface = target.Surface;
    if (target.W <= 0 || target.H <= 0)
    {
        // no window to make a variant for (the second window gone)
        app.Toast("There's no window to edit the layout in");
        app.Pop();
        return false;
    }
    Frontend::LayoutConfiguration config;
    Frontend::LayoutEditSession::SeedSource seed;
    using What = LayoutEditorOpen::What;
    std::string id = Open.Kind == What::Layout ? Open.Id : Open.Kind == What::InUse && host.LayoutInUse ? host.LayoutInUse() : "";
    if (const Frontend::LayoutConfiguration* custom = id.empty() ? nullptr : host.Layouts->FindCustom(id))
        config = *custom;
    else if (Open.Kind == What::Layout)
    {
        app.Toast("There's no layout " + id + " to edit");
        app.Pop();
        return false;
    }
    else if (Open.Kind == What::InUse)
    {
        // a built-in layout can't be changed: a new one from what it shows, which takes its place once it's saved
        Host* h = &host;
        if (host.ShownAsLayout)
            seed = [h](const LayoutEditTarget& t) { return h->ShownAsLayout(t.Surface); };
        PickAfterSave = true;
    }
    Session.Open(config, target, seed);
    if (target.Detached && Session.DetachedUnreadable())
    {
        app.Toast("This layout's detached windows can't be read, so they can't be edited (put the screens together to edit it)");
        app.Pop();
        return false;
    }
    if (!host.Layouts->CanSave())
        app.Toast("layouts.json couldn't be read, so changes here can't be saved");
    printf("layout editor: %s, %s\n", config.Id ? ("layout " + *config.Id).c_str() : "a new layout", StatusText().c_str());
    fflush(stdout);
    return true;
}

std::string LayoutEditor::StatusText() const
{
    const LayoutEditTarget& t = Session.Target();
    char text[256];
    std::string window = !t.Detached ? "window" : t.TopScreen ? "top screen's window" : "bottom screen's window";
    int w = 0, h = 0;
    using From = Frontend::PickedLayout::From;
    if (Session.ShownFrom() == From::Exact)
        snprintf(text, sizeof(text), "the %s, %d x %d: its own variant", window.c_str(), t.W, t.H);
    else if (Session.ScaledFrom(w, h))
        snprintf(text, sizeof(text), "the %s, %d x %d: the %d x %d variant scaled; the first change makes one for this size", window.c_str(), t.W,
            t.H, w, h);
    else
        snprintf(text, sizeof(text), "the %s, %d x %d: %s; the first change makes a variant for this size", window.c_str(), t.W, t.H,
            Session.ShownFromSeed() ? "the screens as the layout in use shows them" : t.Detached ? "the screen centered" : "the default layout");
    return text;
}

void LayoutEditor::SyncTarget(App& app)
{
    // the window edited, at its size now (a resize, or the screens put together or apart meanwhile)
    LayoutEditTarget t = HostRef->LayoutEditTarget(Surface);
    Surface = t.Detached ? t.Surface : 0;
    if (t.W <= 0 || t.H <= 0 || t == Session.Target())
        return;
    if (t.Detached && Session.DetachedUnreadable())
    {
        // (F9 is held off while the editor is open, so the windows can only have been taken apart some other way)
        if (!RefusedDetached)
            app.Toast("This layout's detached windows can't be read, so they can't be edited");
        RefusedDetached = true;
        return;
    }
    Session.Retarget(t);
    DragKind = Drag::None;
    Dragging = false;
    if (Selection())
        Select(Selected);
    Changed();
    printf("layout editor: now %s\n", StatusText().c_str());
    fflush(stdout);
}

std::optional<float> LayoutEditor::WideRatio(const std::string& name) const
{
    const std::optional<std::string>& key = Session.Shown().WideAspect;
    if (name != TOP || !key)
        return std::nullopt;
    return Frontend::WidescreenAspectRatio(*key);
}

void LayoutEditor::Select(const std::string& name)
{
    Selected = name;
    const LayoutComponent* s = Selection();
    if (!s)
    {
        Selected.clear();
        PanelOpen = false;
        return;
    }
    // the shape it has picks the ratio it's resized at, and the corner nearest the window's stays when the controls resize it
    Aspect = Frontend::GuessAspect(s->Rect, WideRatio(name));
    Anchor = Frontend::NearestCorner(s->Rect, Session.Target().W, Session.Target().H);
    Report("select");
}

void LayoutEditor::SelectNext(int direction)
{
    std::vector<std::string> names;
    for (const char* name : {TOP, BOTTOM})
    {
        if (Session.FindScreen(name))
            names.push_back(name);
    }
    if (names.empty())
        return;
    int n = (int)names.size();
    int at = (int)(std::find(names.begin(), names.end(), Selected) - names.begin());
    int next = at >= n ? (direction > 0 ? 0 : n - 1) : ((at + direction) % n + n) % n;
    Select(names[next]);
}

EditorSpace LayoutEditor::Space(App& app, const std::string& name) const
{
    EditorSpace space;
    space.W = Session.Target().W;
    space.H = Session.Target().H;
    const Frontend::Settings* settings = HostRef ? HostRef->Settings : nullptr;
    space.Snap = settings && settings->GetBool("layout_editor_snap_to_grid", false);
    // the app's sizes are dp: reference pixels at the UI's scale, in the window's pixels
    float fb = std::max(ImGui::GetIO().DisplayFramebufferScale.x, 0.01f);
    space.Cell = std::max(4, Frontend::RoundToInt(app.Px(GridSpacing(settings)) * fb));
    space.MinSize = std::max(8, Frontend::RoundToInt(app.Px(30) * fb));
    if (const LayoutComponent* other = name.empty() ? nullptr : Session.FindScreen(name == TOP ? BOTTOM : TOP))
        space.Other = other->Rect;
    return space;
}

bool LayoutEditor::Continues(const char* what)
{
    auto now = std::chrono::steady_clock::now();
    bool same = LastChange == what && now - LastChangeAt < std::chrono::milliseconds(700);
    LastChange = what;
    LastChangeAt = now;
    return same;
}

void LayoutEditor::Apply(const LayoutComponent& screen, bool merge)
{
    Session.SetScreens({screen}, merge);
    Changed();
}

void LayoutEditor::Changed()
{
    PreviewDirty = true;
    if (!Selected.empty() && !Selection())
    {
        Selected.clear();
        PanelOpen = false;
    }
}

void LayoutEditor::Report(const char* what) const
{
    const LayoutComponent* s = Selection();
    if (s)
        printf("layout editor: %s %s at %d,%d %dx%d, alpha %.2f%s\n", what, s->Component.c_str(), s->Rect.X, s->Rect.Y, s->Rect.W, s->Rect.H,
            s->Alpha, s->OnTop ? ", on top" : "");
    else
        printf("layout editor: %s\n", what);
    fflush(stdout);
}

void LayoutEditor::Nudge(App& app, int dx, int dy, bool coarse)
{
    const LayoutComponent* s = Selection();
    if (!s || (dx == 0 && dy == 0))
        return;
    EditorSpace space = Space(app, Selected);
    IntRect r = s->Rect, moved = r;
    if (coarse && space.Snap)
    {
        // with snapping on, a coarse step goes to the next line that way (a desktop addition: the D-pad has no drag to snap with)
        if (dx)
            moved.X = Frontend::NextSnapStart(r.X, r.W, Frontend::SnapAxisX(space), dx);
        if (dy)
            moved.Y = Frontend::NextSnapStart(r.Y, r.H, Frontend::SnapAxisY(space), dy);
    }
    else
    {
        int step = coarse ? COARSE_STEP : 1;
        moved = Frontend::NudgeRect(r, dx * step, dy * step, space);
    }
    if (moved == r)
        return;
    LayoutComponent c = *s;
    c.Rect = moved;
    Apply(c, Continues("move"));
    Anchor = Frontend::NearestCorner(moved, space.W, space.H);
    Report("move");
}

void LayoutEditor::ResizeStep(App& app, int widthOffset)
{
    const LayoutComponent* s = Selection();
    if (!s)
        return;
    EditorSpace space = Space(app, Selected);
    IntRect r = Frontend::ResizeBy(s->Rect, widthOffset, LockedRatio(), Anchor, space);
    if (r == s->Rect)
        return;
    LayoutComponent c = *s;
    c.Rect = r;
    Apply(c, Continues("size"));
    Report("size");
}

void LayoutEditor::OpenPanel()
{
    if (!Selection())
        return;
    PanelOpen = true;
    // its first row gets the focus
    WantFocus = true;
    FocusId = 0;
    printf("layout editor: panel\n");
    fflush(stdout);
}

void LayoutEditor::UpdateCanvas(App& app)
{
    const LayoutEditTarget& t = Session.Target();
    ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    float fb = std::max(ImGui::GetIO().DisplayFramebufferScale.x, 0.01f);
    MiniMap = t.Surface != 0 && t.W > 0 && t.H > 0;
    if (!MiniMap)
    {
        // the main window itself: its drawable pixels are the canvas
        Origin = win;
        UnitsPerPx = 1 / fb;
        return;
    }
    // the second window as a copy in the middle of this one, under the top bar
    float top = app.Px(132), bottom = FooterHeight(app) + app.Px(24);
    float areaW = size.x - app.Px(64), areaH = std::max(app.Px(60), size.y - top - bottom);
    UnitsPerPx = std::max(0.01f, std::min(areaW / t.W, areaH / t.H));
    Origin = ImVec2(win.x + (size.x - t.W * UnitsPerPx) / 2, win.y + top + (areaH - t.H * UnitsPerPx) / 2);
}

std::string LayoutEditor::HitScreen(ImVec2 at) const
{
    const LayoutComponent* bottom = Session.FindScreen(BOTTOM);
    // the bottom screen is over the top one only when it's marked on top (DSRenderer's order)
    const char* order[2] = {TOP, BOTTOM};
    if (bottom && bottom->OnTop)
        std::swap(order[0], order[1]);
    for (const char* name : order)
    {
        const LayoutComponent* s = Session.FindScreen(name);
        if (s && Inside(at, U((float)s->Rect.X, (float)s->Rect.Y), U((float)s->Rect.Right(), (float)s->Rect.Bottom())))
            return name;
    }
    return "";
}

std::optional<Corner> LayoutEditor::HitHandle(App& app, ImVec2 at) const
{
    const LayoutComponent* s = Selection();
    if (!s)
        return std::nullopt;
    ImVec2 a = U((float)s->Rect.X, (float)s->Rect.Y), b = U((float)s->Rect.Right(), (float)s->Rect.Bottom());
    if (at.x < a.x || at.x > b.x || at.y < a.y || at.y > b.y)
        return std::nullopt;
    float smaller = std::min(b.x - a.x, b.y - a.y);
    bool small = smaller * HANDLE_TOUCH_FRACTION < app.Px(20);
    float touch = small ? smaller * SMALL_VIEW_HANDLE_TOUCH_FRACTION : std::min(app.Px(48), smaller * HANDLE_TOUCH_FRACTION);
    for (Corner c : {Corner::TopLeft, Corner::TopRight, Corner::BottomLeft, Corner::BottomRight})
    {
        if (small && c != Corner::BottomRight)
            continue;
        float cx = Frontend::IsLeft(c) ? a.x : b.x, cy = Frontend::IsTop(c) ? a.y : b.y;
        if (std::fabs(at.x - cx) <= touch && std::fabs(at.y - cy) <= touch)
            return c;
    }
    return std::nullopt;
}

void LayoutEditor::MouseDown(App& app, ImVec2 at)
{
    DragFrom = at;
    Dragging = false;
    DragChanged = false;
    if (std::optional<Corner> corner = HitHandle(app, at))
    {
        DragKind = Drag::Resize;
        DragCorner = *corner;
        DragStart = Selection()->Rect;
        return;
    }
    std::string hit = HitScreen(at);
    if (hit.empty())
    {
        // a click on nothing deselects (the app's tap on the editor)
        DragKind = Drag::None;
        if (!Selected.empty())
        {
            Selected.clear();
            PanelOpen = false;
            Report("deselect");
        }
        return;
    }
    if (hit != Selected)
        Select(hit);
    DragKind = Drag::Move;
    DragStart = Selection()->Rect;
}

void LayoutEditor::MouseDrag(App& app, ImVec2 at)
{
    const LayoutComponent* s = Selection();
    if (!s)
    {
        DragKind = Drag::None;
        return;
    }
    ImVec2 d(at.x - DragFrom.x, at.y - DragFrom.y);
    // nothing moves until the pointer is past the drag threshold (the app's touch slop)
    float threshold = ImGui::GetIO().MouseDragThreshold;
    if (!Dragging && d.x * d.x + d.y * d.y < threshold * threshold)
        return;
    Dragging = true;
    float ox = d.x / UnitsPerPx, oy = d.y / UnitsPerPx;
    EditorSpace space = Space(app, Selected);
    IntRect r = DragKind == Drag::Move ? Frontend::DragRect(DragStart, ox, oy, space)
                                        : Frontend::ResizeFromHandle(DragStart, DragCorner, ox, oy, LockedRatio(), space);
    if (r == s->Rect)
        return;
    LayoutComponent c = *s;
    c.Rect = r;
    // one undo point for the whole drag
    Apply(c, DragChanged);
    DragChanged = true;
}

void LayoutEditor::MouseUp()
{
    if (DragChanged && Selection())
    {
        Anchor = Frontend::NearestCorner(Selection()->Rect, Session.Target().W, Session.Target().H);
        Report(DragKind == Drag::Move ? "drag" : "resize");
    }
    DragKind = Drag::None;
    Dragging = false;
    DragChanged = false;
}

void LayoutEditor::HandleMouse(App& app)
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 at = io.MousePos;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (Inside(at, MenuMin, MenuMax))
        {
            OpenMenu(app);
            return;
        }
        // the panel's rows take their own clicks
        if (!(PanelOpen && Inside(at, PanelMin, PanelMax)))
            MouseDown(app, at);
    }
    if (DragKind != Drag::None)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            MouseDrag(app, at);
        else
            MouseUp();
    }
    // the wheel over the selected screen resizes it, as its pill's buttons do
    if (io.MouseWheel != 0 && !Selected.empty() && !(PanelOpen && Inside(at, PanelMin, PanelMax)) && HitScreen(at) == Selected)
        ResizeStep(app, io.MouseWheel > 0 ? SIZE_STEP : -SIZE_STEP);
}

void LayoutEditor::HandleKeys(App& app)
{
    ImGuiIO& io = ImGui::GetIO();
    bool shift = io.KeyShift;
    if (Pressed(ImGuiKey_GamepadFaceDown, false) || Pressed(ImGuiKey_Enter, false) || Pressed(ImGuiKey_KeypadEnter, false)
        || Pressed(ImGuiKey_Space, false))
    {
        // the selected screen's panel (the app's item controls), or a screen to start with
        if (Selection())
            OpenPanel();
        else
            SelectNext(1);
        return;
    }
    if (Pressed(ImGuiKey_Tab, false))
        SelectNext(shift ? -1 : 1);
    if (io.KeyCtrl && Pressed(ImGuiKey_Z, false))
        Clear(app);
    if (!io.KeyCtrl && (Pressed(ImGuiKey_M, false) || Pressed(ImGuiKey_Menu, false)))
    {
        OpenMenu(app);
        return;
    }
    if (!Selection())
    {
        StickFrames = TriggerFrames = 0;
        return;
    }
    // the D-pad and the arrows a pixel (the arrows with Shift a coarse step)
    int dx = (Pressed(ImGuiKey_GamepadDpadRight) || Pressed(ImGuiKey_RightArrow)) - (Pressed(ImGuiKey_GamepadDpadLeft) || Pressed(ImGuiKey_LeftArrow));
    int dy = (Pressed(ImGuiKey_GamepadDpadDown) || Pressed(ImGuiKey_DownArrow)) - (Pressed(ImGuiKey_GamepadDpadUp) || Pressed(ImGuiKey_UpArrow));
    if (dx || dy)
        Nudge(app, dx, dy, shift);
    // the left stick a coarse step, repeating while it's held
    int sx = ImGui::IsKeyDown(ImGuiKey_GamepadLStickRight) - ImGui::IsKeyDown(ImGuiKey_GamepadLStickLeft);
    int sy = ImGui::IsKeyDown(ImGuiKey_GamepadLStickDown) - ImGui::IsKeyDown(ImGuiKey_GamepadLStickUp);
    StickFrames = (sx || sy) ? StickFrames + 1 : 0;
    if ((sx || sy) && Repeats(StickFrames))
        Nudge(app, sx, sy, true);
    // the triggers smaller and bigger, repeating; - and + (Shift: a pixel)
    int grow = ImGui::IsKeyDown(ImGuiKey_GamepadR2) - ImGui::IsKeyDown(ImGuiKey_GamepadL2);
    TriggerFrames = grow ? TriggerFrames + 1 : 0;
    if (grow && Repeats(TriggerFrames))
        ResizeStep(app, grow * SIZE_STEP);
    int key = (Pressed(ImGuiKey_Equal) || Pressed(ImGuiKey_KeypadAdd)) - (Pressed(ImGuiKey_Minus) || Pressed(ImGuiKey_KeypadSubtract));
    if (key)
        ResizeStep(app, key * (shift ? 1 : SIZE_STEP));
}

void LayoutEditor::Keep(App& app)
{
    if (!Started || !HostRef)
        return;
    SyncTarget(app);
    if (PreviewDirty)
    {
        HostRef->SetLayoutPreview(Frontend::LayoutPreview {Session.Target().Surface, Session.Shown(), Session.IsWideTarget()});
        PreviewDirty = false;
    }
}

void LayoutEditor::DeleteKey(App& app)
{
    (void)app;
    const LayoutComponent* s = Selection();
    if (!s)
        return;
    std::string name = s->Component;
    Session.RemoveScreen(name);
    Changed();
    Selected.clear();
    PanelOpen = false;
    Report(("removed " + name).c_str());
}

bool LayoutEditor::Back(App& app)
{
    if (DragKind != Drag::None)
        MouseUp();
    else if (PanelOpen)
        PanelOpen = false;
    else if (!Selected.empty())
    {
        Selected.clear();
        Report("deselect");
    }
    else
        AskExit(app);
    return true;
}

void LayoutEditor::Tab(App& app, int direction)
{
    (void)app;
    SelectNext(direction);
}

void LayoutEditor::Clear(App& app)
{
    if (!Session.Undo())
    {
        app.Toast("Nothing to undo");
        return;
    }
    Changed();
    if (Selection())
        Select(Selected);
    Report("undo");
}

void LayoutEditor::DrawHandles(App& app, ImVec2 a, ImVec2 b)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float handle = app.Px(10), corner = app.Px(18), stroke = std::max(1.f, app.Px(1));
    bool cornerOnly = std::min(b.x - a.x, b.y - a.y) * HANDLE_TOUCH_FRACTION < app.Px(20);
    auto square = [&](ImVec2 min, ImVec2 max)
    {
        draw->AddRectFilled(min, max, HANDLE_FILL);
        draw->AddRect(min, max, HANDLE_STROKE, 0, stroke);
    };
    if (!cornerOnly)
    {
        square(a, ImVec2(a.x + handle, a.y + handle));
        square(ImVec2(b.x - handle, a.y), ImVec2(b.x, a.y + handle));
        square(ImVec2(a.x, b.y - handle), ImVec2(a.x + handle, b.y));
    }
    square(ImVec2(b.x - corner, b.y - corner), b);
    // the bottom-right one with the app's resize arrows, drawn as a line with two heads
    ImVec2 p0(b.x - corner + app.Px(4), b.y - app.Px(4)), p1(b.x - app.Px(4), b.y - corner + app.Px(4));
    draw->AddLine(p0, p1, IM_COL32(0, 0, 0, 0xFF), std::max(1.f, app.Px(1.5f)));
}

void LayoutEditor::DrawScreens(App& app, bool filled)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const LayoutComponent* bottom = Session.FindScreen(BOTTOM);
    // underneath first: the bottom screen is under the top one unless it's marked on top
    const char* order[2] = {BOTTOM, TOP};
    if (bottom && bottom->OnTop)
        std::swap(order[0], order[1]);
    float stroke = std::max(1.f, app.Px(2));
    for (const char* name : order)
    {
        const LayoutComponent* s = Session.FindScreen(name);
        if (!s)
            continue;
        bool isTop = name == TOP, selected = Selected == name;
        ImVec2 a = U((float)s->Rect.X, (float)s->Rect.Y), b = U((float)s->Rect.Right(), (float)s->Rect.Bottom());
        if (filled)
            draw->AddRectFilled(a, b, selected ? (isTop ? TOP_FILL : BOTTOM_FILL) : WithAlpha(isTop ? TOP_FILL : BOTTOM_FILL, UNSELECTED_ALPHA));
        ImU32 color = isTop ? TOP_STROKE : BOTTOM_STROKE;
        if (selected)
            draw->AddRect(ImVec2(a.x - stroke, a.y - stroke), ImVec2(b.x + stroke, b.y + stroke), IM_COL32(0xFF, 0xFF, 0xFF, 0xFF), 0, stroke);
        draw->AddRect(a, b, selected ? color : WithAlpha(color, 0.8f), 0, stroke);
        // which screen it is, in its corner
        const char* label = isTop ? "Top" : "Bottom";
        ImVec2 ts = TextSize(app, app.Medium, 13, label);
        // clear of the top-left handle
        ImVec2 l0(a.x + app.Px(14), a.y + app.Px(4)), l1(l0.x + ts.x + app.Px(12), l0.y + ts.y + app.Px(4));
        if (l1.x < b.x && l1.y < b.y)
        {
            draw->AddRectFilled(l0, l1, color, app.Px(4));
            TextAt(app, app.Medium, 13, ImVec2(l0.x + app.Px(6), l0.y + app.Px(2)), IM_COL32(0xFF, 0xFF, 0xFF, 0xFF), label);
        }
        if (selected)
            DrawHandles(app, a, b);
        if (selected && Dragging && DragKind == Drag::Resize)
        {
            // the size while it's dragged (drawSizeLabel)
            char size[32];
            snprintf(size, sizeof(size), "%d x %d", s->Rect.W, s->Rect.H);
            ImVec2 z = TextSize(app, app.Regular, 14, size);
            ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2), pad(app.Px(8), app.Px(4));
            draw->AddRectFilled(ImVec2(c.x - z.x / 2 - pad.x, c.y - z.y / 2 - pad.y), ImVec2(c.x + z.x / 2 + pad.x, c.y + z.y / 2 + pad.y),
                LABEL_BACKGROUND, app.Px(8));
            TextAt(app, app.Regular, 14, ImVec2(c.x - z.x / 2, c.y - z.y / 2), IM_COL32(0, 0, 0, 0xFF), size);
        }
    }
}

void LayoutEditor::DrawCanvas(App& app)
{
    const Palette& p = app.Colors();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const LayoutEditTarget& t = Session.Target();
    ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    ImVec2 a = U(0, 0), b = U((float)t.W, (float)t.H);
    if (MiniMap)
    {
        // the main window dimmed, and the other window as a frame of its shape (it shows the layout itself as it's edited)
        draw->AddRectFilled(win, ImVec2(win.x + size.x, win.y + size.y), IM_COL32(0, 0, 0, 0xCC));
        draw->AddRectFilled(a, b, IM_COL32(0, 0, 0, 0xFF));
        draw->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), p.Outline, 0, std::max(1.f, app.Px(1)));
        char label[96];
        snprintf(label, sizeof(label), "The %s screen's window, %d x %d", t.TopScreen ? "top" : "bottom", t.W, t.H);
        ImVec2 ls = TextSize(app, app.Regular, 15, label);
        TextAt(app, app.Regular, 15, ImVec2((a.x + b.x - ls.x) / 2, a.y - ls.y - app.Px(6)), IM_COL32(0xFF, 0xFF, 0xFF, 0xDD), label);
    }
    EditorSpace space = Space(app, "");
    if (space.Snap)
    {
        // the lines the screens snap to, from the middle out (gridLines), behind the screens
        float line = std::max(1.f, app.Px(1));
        for (int x : Frontend::SnapGridLines(t.W, space.Cell))
            draw->AddLine(U((float)x, 0), U((float)x, (float)t.H), GRID, line);
        for (int y : Frontend::SnapGridLines(t.H, space.Cell))
            draw->AddLine(U(0, (float)y), U((float)t.W, (float)y), GRID, line);
    }
    bool game = HostRef->GameRunning && HostRef->GameRunning();
    // the game's frame is drawn where the screens are; the stand-ins without one, and always in a copy of the other window
    DrawScreens(app, !game || MiniMap);
}

void LayoutEditor::DrawTopBar(App& app)
{
    const Palette& p = app.Colors();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 win = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    float w = std::min(size.x - app.Px(32), app.Px(760)), h = app.Px(80);
    ImVec2 a(win.x + (size.x - w) / 2, win.y + app.Px(12)), b(a.x + w, a.y + h);
    draw->AddRectFilled(a, b, WithAlpha(p.SurfaceContainerLow, 0.94f), app.Px(20));
    // the menu button on its left (the app's floating menu button), for the mouse; Start or M for the rest
    float button = h - app.Px(16);
    MenuMin = ImVec2(a.x + app.Px(8), a.y + app.Px(8));
    MenuMax = ImVec2(MenuMin.x + button, MenuMin.y + button);
    bool hover = Inside(ImGui::GetIO().MousePos, MenuMin, MenuMax);
    draw->AddRectFilled(MenuMin, MenuMax, hover ? p.PrimaryContainer : p.SecondaryContainer, app.Px(14));
    IconAt(app, Icon::Tune, 26, ImVec2((MenuMin.x + MenuMax.x) / 2, (MenuMin.y + MenuMax.y) / 2 - app.Px(6)), p.OnSecondaryContainer);
    ImVec2 ms = TextSize(app, app.Medium, 12, "Menu");
    TextAt(app, app.Medium, 12, ImVec2((MenuMin.x + MenuMax.x - ms.x) / 2, MenuMax.y - ms.y - app.Px(4)), p.OnSecondaryContainer, "Menu");

    float tx = MenuMax.x + app.Px(14), tw = b.x - app.Px(16) - tx;
    const Frontend::LayoutConfiguration& config = Session.Config();
    bool named = config.Name && !config.Name->empty();
    std::string title = (named ? *config.Name : std::string("New layout")) + (Session.Dirty() ? "  \xc2\xb7 not saved" : "");
    TextAt(app, app.Medium, 18, ImVec2(tx, a.y + app.Px(10)), p.OnSurface, Ellipsize(app, app.Medium, 18, title, tw).c_str());
    std::string status = StatusText();
    status[0] = (char)std::toupper((unsigned char)status[0]);
    TextAt(app, app.Regular, 13, ImVec2(tx, a.y + app.Px(34)), p.OnSurfaceVariant, Ellipsize(app, app.Regular, 13, status, tw).c_str());
    std::string line = "Click a screen, or L / R (Tab) to pick one";
    if (const LayoutComponent* s = Selection())
    {
        char text[160];
        const char* aspect = Aspect == Frontend::ScreenAspect::Ratio43 ? "4:3" : Aspect == Frontend::ScreenAspect::Ratio169 ? "16:9"
            : Aspect == Frontend::ScreenAspect::Wide ? "wide" : "free";
        snprintf(text, sizeof(text), "%s screen: %d x %d at %d, %d, %s, %d%%%s", s->IsTopScreen() ? "Top" : "Bottom", s->Rect.W, s->Rect.H,
            s->Rect.X, s->Rect.Y, aspect, Frontend::RoundToInt(s->Alpha * 100), s->OnTop ? ", on top" : "");
        line = text;
    }
    TextAt(app, app.Regular, 13, ImVec2(tx, a.y + app.Px(54)), p.Primary, Ellipsize(app, app.Regular, 13, line, tw).c_str());
}

void LayoutEditor::Draw(App& app)
{
    if (!Started && !Start(app))
        return;
    FinishNameEntry(app);
    SyncTarget(app);
    UpdateCanvas(app);
    if (!app.KeyboardOpen())
    {
        HandleMouse(app);
        if (!PanelOpen)
            HandleKeys(app);
    }
    Keep(app);
    DrawCanvas(app);
    DrawTopBar(app);
    if (PanelOpen)
        DrawPanel(app);
    else
        PanelMin = PanelMax = ImVec2(0, 0);
    const Palette& p = app.Colors();
    if (PanelOpen)
        FooterHints(app, {{"A|Enter", "Choose"}, {"L/R|Q/W", "Other screen"}, {"B|Esc", "Close"}}, WithAlpha(p.SurfaceContainerLowest, 0.92f));
    else
        FooterHints(app, {{"L/R|Tab", "Pick"}, {"D-pad|Arrows", "Move"}, {"Stick|Shift", "Faster"}, {"LT/RT|- +", "Size"},
                             {"A|Enter", "Options"}, {"X|Ctrl+Z", "Undo"}, {"Start|M", "Menu"}, {"B|Esc", "Back"}},
            WithAlpha(p.SurfaceContainerLowest, 0.92f));
}

std::unique_ptr<Screen> MakeLayoutEditorScreen(const LayoutEditorOpen& open)
{
    return std::make_unique<LayoutEditor>(open);
}

}
