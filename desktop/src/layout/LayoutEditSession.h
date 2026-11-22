// The layout the editor works on, as the app's LayoutEditorViewModel keeps it: a copy of the layout with every change stored in it at once,
// the layout as saved last (for Revert and for asking before an exit), and the variant shown in the window being edited. That variant is the
// one made for exactly the window's size; without one, the window's pick (another size's scaled, or the default layout) is shown, and the
// first change makes a variant for this size from it, as the app makes one for the current display. Only what was changed is written into
// a variant (its screens, its background, its wide ratio): everything else in the layout, the on-screen buttons the desktop never draws and
// the fields this build doesn't know included, stays as it was. Changes can be undone (the app has none; the desktop keeps 50). UI thread
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "layout/LayoutEditing.h"
#include "layout/LayoutGeometry.h"
#include "layout/LayoutModel.h"

namespace Frontend
{

class LayoutEditSession
{
public:
    // a new layout's screens for a window in place of the default layout's: what the layout in use shows there now (the pause menu's edit
    // of a built-in arrangement starts from it), asked again for each window and size; none when it has nothing
    using SeedSource = std::function<std::optional<UILayout>(const LayoutEditTarget& target)>;
    // [config]: the layout as saved (a new one has no id)
    void Open(const LayoutConfiguration& config, const LayoutEditTarget& target, SeedSource seed = nullptr);
    // the layout's detached pair is there but didn't parse (kept as it was in Extra): its windows can't be edited
    bool DetachedUnreadable() const;
    // the seed's screens stored as the target's variant while they're still only shown (a save with no change since it opened)
    void StoreSeed();
    // another window, or the same one at a new size: the variant shown picked again (the changes made stay in the layout)
    void Retarget(const LayoutEditTarget& target);
    const LayoutEditTarget& Target() const { return EditTarget; }

    const LayoutConfiguration& Config() const { return Working; }
    // the layout the target window shows, its screens always placed
    const UILayout& Shown() const { return Edit; }
    // how the shown variant was picked (made for this size, another size's scaled, the default layout)
    PickedLayout::From ShownFrom() const { return Source; }
    // the shown screens are the seed's (what the layout in use shows), not yet stored
    bool ShownFromSeed() const { return FromSeed; }
    // the size of the variant the shown one is scaled from; false when it isn't scaled
    bool ScaledFrom(int& w, int& h) const;
    // the layout shows wide in the target window (a variant of its list has a widescreen ratio)
    bool IsWideTarget() const;

    // the shown screen called [name] (TOP_SCREEN or BOTTOM_SCREEN), or null
    const LayoutComponent* FindScreen(const std::string& name) const;

    // Changes, each with an undo point first unless [merge] (the next step of the same drag or held button). Screens are replaced by their
    // component name (or added); the rest of the shown components keep their order
    void SetScreens(const std::vector<LayoutComponent>& screens, bool merge = false);
    void RemoveScreen(const std::string& name);
    void SetBackground(const std::optional<std::string>& id, const std::string& mode);
    // the shown variant's widescreen ratio (none: a normal 4:3 top screen), its top screen widened the way it's drawn in game (setWideAspect)
    void SetWideAspect(const std::optional<std::string>& key);
    void SetName(const std::optional<std::string>& name);
    // the layout's properties the phone uses: the orientation it turns to and the on-screen buttons' opacity
    void SetProperties(const std::string& orientation, bool useCustomOpacity, int opacity);
    // the app's Reset to default: no variants (together; detached, no detached pair), the properties back to the defaults
    void Reset();
    // back to the layout as saved last (undoable)
    void Revert();
    bool Undo();
    bool CanUndo() const { return !UndoList.empty(); }

    // the layout differs from the one saved last
    bool Dirty() const { return IsDirty; }
    // after a save: what's saved now. A new layout gets its id first
    void SetId(const std::string& id);
    void MarkSaved();

private:
    void Pick();
    void Store(bool screens, bool background, bool wide);
    void Checkpoint(bool merge);
    void UpdateDirty();
    std::vector<LayoutEntry>* List(bool create);

    LayoutConfiguration Working, Saved;
    std::string SavedText;
    bool IsDirty = false;
    LayoutEditTarget EditTarget;
    SeedSource Seed;
    // the seed shows where there's no variant (Reset turns it off, an undo of the Reset back on)
    bool SeedActive = false;
    // the shown layout, the variant it's stored in (-1 while it has none for this size), how it was picked, the variant it was scaled from
    UILayout Edit;
    int Index = -1;
    PickedLayout::From Source = PickedLayout::From::Default;
    int ScaledIndex = -1;
    bool FromSeed = false;
    struct UndoPoint
    {
        LayoutConfiguration Config;
        bool SeedActive = false;
    };
    std::vector<UndoPoint> UndoList;
};

}
