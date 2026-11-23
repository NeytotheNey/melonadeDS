#include "layout/LayoutEditSession.h"

#include <cstdio>

namespace Frontend
{

namespace
{

// how many changes can be undone
constexpr size_t MAX_UNDO = 50;

std::string Serialize(const LayoutConfiguration& config)
{
    return WriteLayouts({config});
}

bool HasWide(const std::vector<LayoutEntry>& entries)
{
    for (const LayoutEntry& e : entries)
    {
        if (e.Layout.WideAspect && IsKnownWideAspect(*e.Layout.WideAspect))
            return true;
    }
    return false;
}

}

void LayoutEditSession::Open(const LayoutConfiguration& config, const LayoutEditTarget& target, SeedSource seed)
{
    Working = config;
    Saved = config;
    SavedText = Serialize(config);
    IsDirty = false;
    UndoList.clear();
    EditTarget = target;
    Seed = std::move(seed);
    SeedActive = (bool)Seed;
    Pick();
}

bool LayoutEditSession::DetachedUnreadable() const
{
    // a detached pair that didn't parse stays in Extra as it was, and nothing here may write over it
    return !Working.Detached && Working.Extra.count("detached") != 0;
}

void LayoutEditSession::StoreSeed()
{
    if (FromSeed)
        Store(true, false, false);
}

void LayoutEditSession::Retarget(const LayoutEditTarget& target)
{
    if (target == EditTarget)
        return;
    EditTarget = target;
    Pick();
}

std::vector<LayoutEntry>* LayoutEditSession::List(bool create)
{
    if (!EditTarget.Detached)
        return &Working.Variants;
    if (DetachedUnreadable())
        return nullptr;
    if (!Working.Detached && create)
        Working.Detached.emplace();
    if (!Working.Detached)
        return nullptr;
    return EditTarget.TopScreen ? &Working.Detached->Top : &Working.Detached->Bottom;
}

void LayoutEditSession::Pick()
{
    static const std::vector<LayoutEntry> none;
    const std::vector<LayoutEntry>* list = List(false);
    const char* single = EditTarget.TopScreen ? "TOP_SCREEN" : "BOTTOM_SCREEN";
    bool detached = EditTarget.Detached;
    // the window's own default: the app's default layout together, the one screen centered (buildSingleScreenLayout) detached
    auto defaults = [detached, single](int w, int h)
    {
        if (!detached)
            return BuildDefaultLayout(w, h);
        UILayout layout;
        layout.Main.Components = std::vector<LayoutComponent> {BuildSingleScreen(w, h, single)};
        return layout;
    };
    PickedLayout picked = PickVariant(list ? *list : none, EditTarget.W, EditTarget.H, defaults);
    Edit = picked.Layout;
    Source = picked.Source;
    Index = Source == PickedLayout::From::Exact ? picked.Variant : -1;
    ScaledIndex = Source == PickedLayout::From::Scaled ? picked.Variant : -1;
    // a new layout from the one in use: its screens as that one shows them in this window now (after a swap or a resize too)
    FromSeed = false;
    if (Source == PickedLayout::From::Default && SeedActive && Seed)
    {
        if (std::optional<UILayout> seed = Seed(EditTarget))
        {
            Edit = *seed;
            FromSeed = true;
        }
    }
    if (!Edit.Main.Components)
        Edit.Main.Components.emplace();
}

bool LayoutEditSession::ScaledFrom(int& w, int& h) const
{
    const std::vector<LayoutEntry>* list = const_cast<LayoutEditSession*>(this)->List(false);
    if (Source != PickedLayout::From::Scaled || !list || ScaledIndex < 0 || ScaledIndex >= (int)list->size())
        return false;
    w = (*list)[ScaledIndex].Variant.W;
    h = (*list)[ScaledIndex].Variant.H;
    return true;
}

bool LayoutEditSession::IsWideTarget() const
{
    if (!EditTarget.Detached)
        return Working.IsWide();
    if (!Working.Detached)
        return false;
    return HasWide(EditTarget.TopScreen ? Working.Detached->Top : Working.Detached->Bottom);
}

const LayoutComponent* LayoutEditSession::FindScreen(const std::string& name) const
{
    return Edit.Main.Find(name.c_str());
}

void LayoutEditSession::Store(bool screens, bool background, bool wide)
{
    std::vector<LayoutEntry>* list = List(true);
    // nothing to store into (a detached pair that didn't parse), or no window to make a variant for
    if (!list || EditTarget.W <= 0 || EditTarget.H <= 0)
        return;
    if (Index < 0 || Index >= (int)list->size())
    {
        // the first change in a window of this size: a variant made for it from what it showed (only its background when nothing else
        // changed and it showed the default layout, which then still places its screens, as a variant the app makes for a background)
        LayoutEntry entry;
        entry.Variant = MakeWindowVariant(EditTarget.W, EditTarget.H);
        entry.Layout.Main.BackgroundId = Edit.Main.BackgroundId;
        entry.Layout.Main.Mode = Edit.Main.Mode;
        if (screens || FromSeed || Source != PickedLayout::From::Default)
            entry.Layout.Main.Components = Edit.Main.Components;
        entry.Layout.WideAspect = Edit.WideAspect;
        list->push_back(std::move(entry));
        Index = (int)list->size() - 1;
        Source = PickedLayout::From::Exact;
        ScaledIndex = -1;
        FromSeed = false;
    }
    else
    {
        UILayout& stored = (*list)[Index].Layout;
        if (screens)
            stored.Main.Components = Edit.Main.Components;
        if (background)
        {
            stored.Main.BackgroundId = Edit.Main.BackgroundId;
            stored.Main.Mode = Edit.Main.Mode;
        }
        if (wide)
            stored.WideAspect = Edit.WideAspect;
    }
    UpdateDirty();
}

void LayoutEditSession::Checkpoint(bool merge)
{
    if (merge && !UndoList.empty())
        return;
    UndoList.push_back({Working, SeedActive});
    if (UndoList.size() > MAX_UNDO)
        UndoList.erase(UndoList.begin());
}

void LayoutEditSession::UpdateDirty()
{
    IsDirty = Serialize(Working) != SavedText;
}

void LayoutEditSession::SetScreens(const std::vector<LayoutComponent>& screens, bool merge)
{
    Checkpoint(merge);
    std::vector<LayoutComponent>& components = *Edit.Main.Components;
    for (const LayoutComponent& screen : screens)
    {
        bool found = false;
        for (LayoutComponent& c : components)
        {
            if (c.Component == screen.Component)
            {
                c = screen;
                found = true;
                break;
            }
        }
        if (!found)
            components.push_back(screen);
    }
    Store(true, false, false);
}

void LayoutEditSession::RemoveScreen(const std::string& name)
{
    Checkpoint(false);
    std::vector<LayoutComponent>& components = *Edit.Main.Components;
    for (size_t i = 0; i < components.size(); i++)
    {
        if (components[i].Component == name)
        {
            components.erase(components.begin() + i);
            break;
        }
    }
    Store(true, false, false);
}

void LayoutEditSession::SetBackground(const std::optional<std::string>& id, const std::string& mode)
{
    Checkpoint(false);
    Edit.Main.BackgroundId = id;
    Edit.Main.Mode = mode;
    Store(false, true, false);
}

void LayoutEditSession::SetWideAspect(const std::optional<std::string>& key)
{
    Checkpoint(false);
    Edit.WideAspect = key;
    std::optional<float> ratio = key ? WidescreenAspectRatio(*key) : std::nullopt;
    std::vector<LayoutComponent>& components = *Edit.Main.Components;
    const LayoutComponent* bottom = Edit.Main.Find("BOTTOM_SCREEN");
    std::optional<IntRect> bottomRect = bottom ? std::optional<IntRect>(bottom->Rect) : std::nullopt;
    if (ratio)
    {
        for (LayoutComponent& c : components)
        {
            if (c.IsTopScreen())
                c.Rect = GetWideTopScreenRect(c.Rect, bottomRect ? &*bottomRect : nullptr, *ratio, (float)EditTarget.W);
        }
    }
    // as the app's setWideAspect, the variant is stored whole
    Store(true, false, true);
}

void LayoutEditSession::SetName(const std::optional<std::string>& name)
{
    Checkpoint(false);
    Working.Name = name;
    UpdateDirty();
}

void LayoutEditSession::SetProperties(const std::string& orientation, bool useCustomOpacity, int opacity)
{
    Checkpoint(false);
    Working.Orientation = orientation;
    Working.UseCustomOpacity = useCustomOpacity;
    Working.Opacity = opacity;
    UpdateDirty();
    printf("layout editor: properties %s, %s opacity %d\n", orientation.c_str(), useCustomOpacity ? "custom" : "default", opacity);
    fflush(stdout);
}

void LayoutEditSession::Reset()
{
    Checkpoint(false);
    Working.Orientation = "FOLLOW_SYSTEM";
    Working.UseCustomOpacity = false;
    Working.Opacity = 50;
    if (EditTarget.Detached)
        Working.Detached.reset();
    else
        Working.Variants.clear();
    // the default layout, not the one in use's screens, until it's undone
    SeedActive = false;
    Pick();
    UpdateDirty();
}

void LayoutEditSession::Revert()
{
    Checkpoint(false);
    Working = Saved;
    Pick();
    UpdateDirty();
}

bool LayoutEditSession::Undo()
{
    if (UndoList.empty())
        return false;
    Working = std::move(UndoList.back().Config);
    SeedActive = UndoList.back().SeedActive;
    UndoList.pop_back();
    Pick();
    UpdateDirty();
    return true;
}

void LayoutEditSession::SetId(const std::string& id)
{
    Working.Id = id;
}

void LayoutEditSession::MarkSaved()
{
    Saved = Working;
    SavedText = Serialize(Working);
    IsDirty = false;
    // the undo points keep the old id (a new layout's none), so they'd bring it back unsaved: they go with the save
    for (UndoPoint& point : UndoList)
        point.Config.Id = Working.Id;
}

}
