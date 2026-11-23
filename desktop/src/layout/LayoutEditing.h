// The layout editor's geometry, ported from the app's editor (ui/layouteditor: LayoutEditorSnapping.kt's snapping, LayoutEditorView.kt's
// moving and resizing, LayoutEditorItemControls.kt's sizes and aspect ratios), and what the editor and the windows share about the window
// being edited. Pure functions on editor pixels (a window's drawable pixels), any thread
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "layout/LayoutModel.h"

namespace Frontend
{

// LayoutEditorSnapping.kt's SnapSpan: a segment along one axis, like a screen's left edge and width
struct SnapSpan
{
    int Start = 0, Length = 0;

    int End() const { return Start + Length; }
    int Center() const { return Start + Length / 2; }
};

// SnapAxis: the lines a screen can snap to along one axis of a [Limit]-wide editor, on a grid going out every [Cell] pixels from the middle,
// the editor's edges and the other screen's edges and center (within 5% of its size). A window has no folds, so the app's gaps are left out
struct SnapAxis
{
    int Limit = 0;
    int Cell = 1;
    std::optional<SnapSpan> Other;

    int Origin() const { return Limit / 2; }
    int AlignmentThreshold() const { return Other ? (int)(Other->Length * 0.05f) : 0; }
};

struct SnappedEdge
{
    int Position = 0;
    int Distance = 0;
};

// the grid lines of an axis: the middle one, the ones every [cell] out from it, and both edges (gridLines)
std::vector<int> SnapGridLines(int limit, int cell);
// where a screen [length] long starting at [start] goes so that whichever of its edges is nearer a line sits on it (snapEdges)
int SnapEdges(int start, int length, const SnapAxis& axis);
// an edge dragged to resize, after [fixedEdge] when [growsTowardsEnd], at least [minLength] from it, snapped as snapResizedEdge does
SnappedEdge SnapResizedEdge(int position, int fixedEdge, int minLength, bool growsTowardsEnd, const SnapAxis& axis);
// the start that centers [length] in [limit] (centeredStart)
int CenteredStart(int length, int limit);
// the desktop's controller step with snapping on: the nearest start past [start] in [direction] (1 or -1) where an edge or the center sits on
// one of snapEdges' lines (the other screen's at any distance); [start] itself when there's none inside the editor
int NextSnapStart(int start, int length, const SnapAxis& axis, int direction);

// LayoutEditorView.Anchor: a rect's corner
enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };
bool IsLeft(Corner c);
bool IsTop(Corner c);
Corner Opposite(Corner c);
int CornerX(Corner c, const IntRect& r);
int CornerY(Corner c, const IntRect& r);
// the rect [w] x [h] whose [c] corner is at x,y
IntRect PlaceAt(Corner c, int x, int y, int w, int h);
// the editor corner nearest [r] (getNearestAnchor), which resizing from the controls keeps in place
Corner NearestCorner(const IntRect& r, int editorW, int editorH);

// LayoutEditorView.ScreenAspectRatio: how a screen's size is locked when it's resized: the DS's 4:3, 16:9, a wide variant's own ratio (its
// top screen only) or free
enum class ScreenAspect { Ratio43, Ratio169, Wide, Free };
// the width / height ratio kept, or none for Free; [wideRatio] is the variant's (WidescreenAspectRatio)
std::optional<float> AspectRatioOf(ScreenAspect aspect, std::optional<float> wideRatio);
// guessAspectRatio: the choice the screen's shape already has (within 2 pixels), so resizing it doesn't change its shape; Free otherwise
ScreenAspect GuessAspect(const IntRect& r, std::optional<float> wideRatio);

// WidescreenAspect: the drawn ratio of a wide variant (its even 3D view width over 192) for its key ("16:9"), or none
std::optional<float> WidescreenAspectRatio(const std::string& key);
// WidescreenAspect's keys in the app's order
const std::vector<std::string>& WidescreenAspectKeys();

// The editor around one screen being changed: the editor's size, the snap grid (on or off, its cell), the smallest a side may be (the app's
// 30 dp) and the other screen, which a screen lines up with
struct EditorSpace
{
    int W = 0, H = 0;
    bool Snap = false;
    int Cell = 32;
    int MinSize = 30;
    std::optional<IntRect> Other;
};

// getSnapAxes: the editor's x and y axes for a screen, with the other screen's lines
SnapAxis SnapAxisX(const EditorSpace& space);
SnapAxis SnapAxisY(const EditorSpace& space);
// constrainSize: [w] x [h] at least the minimum and at most [maxW] x [maxH], the height from the width with a locked [ratio]
void ConstrainSize(std::optional<float> ratio, float w, float h, int maxW, int maxH, int minSize, int& outW, int& outH);
// a rect moved back inside the editor (clampInside; one bigger than it sticks out on the left or top)
IntRect ClampInside(const IntRect& r, int editorW, int editorH);
// dragView: [start] moved by the pointer's offset, snapped (snapPosition) when the grid is on
IntRect DragRect(const IntRect& start, float offsetX, float offsetY, const EditorSpace& space);
// moveView: moved by whole pixels, never snapped (the app's nudge arrows and D-pad)
IntRect NudgeRect(const IntRect& r, int dx, int dy, const EditorSpace& space);
// resizeFromHandle: the [corner] handle dragged by the pointer's offset from [start], the opposite corner fixed
IntRect ResizeFromHandle(const IntRect& start, Corner corner, float offsetX, float offsetY, std::optional<float> ratio, const EditorSpace& space);
// resizeView: to [w] x [h] with [anchor] kept in place, inside the editor, snapped when the grid is on
IntRect ResizeTo(const IntRect& r, float w, float h, std::optional<float> ratio, Corner anchor, const EditorSpace& space);
// resizeSelectedViewBy: [widthOffset] wider or narrower, the height following; nothing when there's no room that way
IntRect ResizeBy(const IntRect& r, int widthOffset, std::optional<float> ratio, Corner anchor, const EditorSpace& space);
// the sizes the item controls offer: the DS screen at a whole [scale] (a wide top screen keeps the DS height), and the biggest that fits
void ScaleSize(int scale, ScreenAspect aspect, std::optional<float> ratio, int& w, int& h);
void FitSize(const IntRect& r, std::optional<float> ratio, int editorW, int editorH, int& w, int& h);

// The window the layout editor edits: together, the main window and the layout's variants; detached, the focused window (or the one picked
// in the editor) and the detached pair's variants for the DS screen it shows. W x H is its drawable size, which picks the variant edited
struct LayoutEditTarget
{
    bool Detached = false;
    // 0 the main window, 1 the second one
    int Surface = 0;
    // detached: the window shows the DS top screen
    bool TopScreen = true;
    int W = 0, H = 0;

    bool operator==(const LayoutEditTarget&) const = default;
};

// What the editor shows in that window while it's open: the layout being edited (together, both screens where it puts them, never swapped;
// detached, the window's one screen) with its background, which shows with no game running too
struct LayoutPreview
{
    int Surface = 0;
    UILayout Layout;
    bool IsWide = false;
};

// a variant for a [w] x [h] window, as a new one is saved (no insets or folds, one display the window's size)
LayoutVariant MakeWindowVariant(int w, int h);

}
