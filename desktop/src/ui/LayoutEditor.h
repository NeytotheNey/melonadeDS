// The layout editor screen (the app's LayoutEditorActivity and its views), split over two files: LayoutEditorScreen.cpp has the canvas
// (the screens drawn where the layout puts them, the mouse, the controller and the keyboard), LayoutEditorMenus.cpp the item panel, the menu,
// the properties, the background, saving and leaving. The layout itself is a LayoutEditSession; the window being edited shows it through the
// host's preview, so the real renderer draws the game (or, with none running, the background under the editor's stand-ins for the screens)
#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "Host.h"
#include "Screens.h"
#include "layout/LayoutEditSession.h"
#include "layout/LayoutEditing.h"

namespace Ui
{

class LayoutEditor : public Screen
{
public:
    explicit LayoutEditor(LayoutEditorOpen open) : Open(std::move(open)) {}
    ~LayoutEditor() override;

    const char* Id() const override { return "layout-editor"; }
    bool Overlay() const override { return true; }
    bool Dims() const override { return false; }
    bool OwnsMenuButton() const override { return true; }
    void Draw(App& app) override;
    bool Back(App& app) override;
    void Tab(App& app, int direction) override;
    void Clear(App& app) override;
    void MenuButton(App& app) override { OpenMenu(app); }

    // LayoutEditorMenus.cpp
    void OpenMenu(App& app);
    void OpenProperties(App& app);
    void OpenBackground(App& app);
    void OpenBackgroundList(App& app);
    // the file the dialog picked added to backgrounds.json and shown
    void AddBackgroundFile(App& app, const std::string& file);
    void AskExit(App& app);
    void Save(App& app, bool exitAfter);
    void Exit(App& app);
    void DrawPanel(App& app);
    // the name typed on the on-screen keyboard, applied once it closes (from whichever screen is on top)
    void FinishNameEntry(App& app);

    // LayoutEditorScreen.cpp
    bool Start(App& app);
    void SyncTarget(App& app);
    const Frontend::LayoutComponent* Selection() const { return Selected.empty() ? nullptr : Session.FindScreen(Selected); }
    void Select(const std::string& name);
    void SelectNext(int direction);
    std::optional<float> WideRatio(const std::string& name) const;
    std::optional<float> LockedRatio() const { return Frontend::AspectRatioOf(Aspect, WideRatio(Selected)); }
    Frontend::EditorSpace Space(App& app, const std::string& name) const;
    // a screen changed: stored (an undo point first unless it continues the same change), shown, and said in the log
    void Apply(const Frontend::LayoutComponent& screen, bool merge);
    // the next step of a held button or key merges with the last one of the same [what]
    bool Continues(const char* what);
    void Changed();
    void Report(const char* what) const;
    void ResizeStep(App& app, int widthOffset);
    void Nudge(App& app, int dx, int dy, bool coarse);
    void OpenPanel();
    void UpdateCanvas(App& app);
    // a point of the edited window (its drawable pixels) in the main window's ImGui units
    ImVec2 U(float x, float y) const { return ImVec2(Origin.x + x * UnitsPerPx, Origin.y + y * UnitsPerPx); }
    void HandleMouse(App& app);
    void MouseDown(App& app, ImVec2 at);
    void MouseDrag(App& app, ImVec2 at);
    void MouseUp();
    void HandleKeys(App& app);
    // the screen under [at] (the one drawn on top first), or empty; the selected screen's resize handle under it
    std::string HitScreen(ImVec2 at) const;
    std::optional<Frontend::Corner> HitHandle(App& app, ImVec2 at) const;
    void DrawCanvas(App& app);
    void DrawScreens(App& app, bool filled);
    void DrawHandles(App& app, ImVec2 a, ImVec2 b);
    void DrawTopBar(App& app);
    std::string StatusText() const;
    // LayoutEditorMenus.cpp's rows: the menu's, and the panel's, which change a copy of the screen that's stored once they're drawn
    void MenuRows(App& app);
    void MenuSaveRows(App& app);
    struct PanelEdit
    {
        Frontend::LayoutComponent Screen;
        // the other screen, when a row changes it too (on top, swap)
        std::vector<Frontend::LayoutComponent> Others;
        std::string Kind = "panel";
        // a new widescreen ratio for the variant (an empty one: off)
        std::optional<std::optional<std::string>> Wide;
        bool Remove = false, Done = false;
    };
    void PanelRows(App& app, const Frontend::LayoutComponent& screen);
    void SizeRows(App& app, PanelEdit& edit, const Frontend::EditorSpace& space);
    void ShapeRows(App& app, PanelEdit& edit, const Frontend::EditorSpace& space);
    void ScaleRow(App& app, PanelEdit& edit, const Frontend::EditorSpace& space);
    void PlaceRows(App& app, PanelEdit& edit, const Frontend::EditorSpace& space);
    void ApplyPanel(const PanelEdit& edit, const Frontend::LayoutComponent& before, const Frontend::EditorSpace& space);

    LayoutEditorOpen Open;
    Host* HostRef = nullptr;
    bool Started = false;
    Frontend::LayoutEditSession Session;
    // the window edited when detached (0 the main window, 1 the second one)
    int Surface = 0;
    std::string Selected;
    Frontend::ScreenAspect Aspect = Frontend::ScreenAspect::Ratio43;
    Frontend::Corner Anchor = Frontend::Corner::TopLeft;
    bool PanelOpen = false;
    // the new layout replaces a built-in one in use once it's saved (the pause menu's edit of a built-in arrangement)
    bool PickAfterSave = false;
    bool PreviewDirty = true;
    // said once: the layout's detached pair can't be read, so a detached window isn't edited
    bool RefusedDetached = false;
    // the cards the editor opens keep the windows' preview in step (the window resized under a card); they call it from their rows
    void Keep(App& app);
    // the Delete key: the selected screen removed (the app's delete on its pill), undoable
    void DeleteKey(App& app) override;
    // lives as long as the editor, for what comes back after the frame (the file dialog's answer)
    std::shared_ptr<bool> Alive = std::make_shared<bool>(true);

    // the canvas: where the edited window's pixels are in the main window's ImGui units, and whether that's a small copy of the second window
    ImVec2 Origin {0, 0};
    float UnitsPerPx = 1;
    bool MiniMap = false;
    // the top bar's menu button and the panel, which the canvas leaves to their own clicks
    ImVec2 MenuMin, MenuMax, PanelMin, PanelMax;

    enum class Drag { None, Move, Resize };
    Drag DragKind = Drag::None;
    bool Dragging = false, DragChanged = false;
    Frontend::IntRect DragStart;
    Frontend::Corner DragCorner = Frontend::Corner::BottomRight;
    ImVec2 DragFrom {0, 0};

    // held buttons step after a delay and then every few frames (the app's 400 ms, then every 50 ms)
    int StickFrames = 0, TriggerFrames = 0;
    std::string LastChange;
    std::chrono::steady_clock::time_point LastChangeAt {};

    // a name being typed on the on-screen keyboard and what it's for
    enum class NameFor { None, Save, SaveAndExit, Rename };
    NameFor PendingName = NameFor::None;
    std::string NameEntry;
};

}
