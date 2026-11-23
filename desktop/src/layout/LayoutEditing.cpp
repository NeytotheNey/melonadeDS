#include "layout/LayoutEditing.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>

#include "layout/LayoutGeometry.h"

namespace Frontend
{

namespace
{

// how close to a line counts as on it when guessing a screen's aspect (guessAspectRatio's 2 pixels)
constexpr float ASPECT_TOLERANCE = 2.f;

// Math.floorDiv
int FloorDiv(int a, int b)
{
    int q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

int ClampStart(int start, int length, int limit)
{
    return std::max(std::min(start, limit - length), 0);
}

// LayoutEditorSnapping.Nearest: the candidate closest to [From] among the ones in [Min]..[Max], or [From] if none is offered
struct Nearest
{
    int From, Min, Max;
    int Value;
    int Distance = INT_MAX;

    Nearest(int from, int min, int max) : From(from), Min(min), Max(max), Value(from) {}

    void Offer(int candidate, int maxDistance = INT_MAX)
    {
        if (candidate < Min || candidate > Max)
            return;
        int distance = std::abs(candidate - From);
        if (distance <= maxDistance && distance < Distance)
        {
            Value = candidate;
            Distance = distance;
        }
    }
};

// offerGridLines: the grid lines on both sides of [position], as the value being snapped ([shift] added: an end edge's start)
void OfferGridLines(Nearest& nearest, int position, const SnapAxis& axis, int shift)
{
    int lineBefore = axis.Origin() + FloorDiv(position - axis.Origin(), axis.Cell) * axis.Cell;
    nearest.Offer(lineBefore + shift);
    nearest.Offer(lineBefore + axis.Cell + shift);
}

// snapPosition for a screen: its nearest edges on lines
IntRect SnapPosition(const IntRect& r, const EditorSpace& space)
{
    IntRect out = r;
    out.X = SnapEdges(r.X, r.W, SnapAxisX(space));
    out.Y = SnapEdges(r.Y, r.H, SnapAxisY(space));
    return out;
}

// getMinWidth
float MinWidth(std::optional<float> ratio, int minSize)
{
    return ratio ? std::max((float)minSize, minSize * *ratio) : (float)minSize;
}

// snapResize: the dragged corner of a resize on the nearest lines, the opposite corner at fixedX, fixedY
void SnapResize(Corner corner, int fixedX, int fixedY, float& width, float& height, std::optional<float> ratio, const EditorSpace& space)
{
    // the desired size can be negative when the pointer goes past the fixed corner, so the side comes from the corner being dragged
    float edgeX = IsLeft(corner) ? fixedX - width : fixedX + width;
    float edgeY = IsTop(corner) ? fixedY - height : fixedY + height;
    float minWidth = MinWidth(ratio, space.MinSize);
    float minHeight = ratio ? minWidth / *ratio : (float)space.MinSize;
    SnappedEdge x = SnapResizedEdge(RoundToInt(edgeX), fixedX, (int)std::ceil(minWidth), !IsLeft(corner), SnapAxisX(space));
    SnappedEdge y = SnapResizedEdge(RoundToInt(edgeY), fixedY, (int)std::ceil(minHeight), !IsTop(corner), SnapAxisY(space));
    float snappedWidth = (float)(IsLeft(corner) ? fixedX - x.Position : x.Position - fixedX);
    float snappedHeight = (float)(IsTop(corner) ? fixedY - y.Position : y.Position - fixedY);
    if (!ratio)
    {
        width = snappedWidth;
        height = snappedHeight;
    }
    else if (x.Distance <= y.Distance)
    {
        width = snappedWidth;
        height = snappedWidth / *ratio;
    }
    else
    {
        width = snappedHeight * *ratio;
        height = snappedHeight;
    }
}

struct WideAspectInfo
{
    const char* Key;
    int WidthRatio, HeightRatio;
};

const WideAspectInfo kWideAspects[] = {{"16:10", 16, 10}, {"16:9", 16, 9}, {"20:9", 20, 9}, {"21:9", 21, 9}};

}

SnapAxis SnapAxisX(const EditorSpace& space)
{
    SnapAxis axis {space.W, std::max(1, space.Cell), std::nullopt};
    if (space.Other)
        axis.Other = SnapSpan {space.Other->X, space.Other->W};
    return axis;
}

SnapAxis SnapAxisY(const EditorSpace& space)
{
    SnapAxis axis {space.H, std::max(1, space.Cell), std::nullopt};
    if (space.Other)
        axis.Other = SnapSpan {space.Other->Y, space.Other->H};
    return axis;
}

std::vector<int> SnapGridLines(int limit, int cell)
{
    cell = std::max(1, cell);
    int origin = limit / 2;
    int firstLine = origin - origin / cell * cell;
    std::vector<int> lines {0};
    for (int line = firstLine; line <= limit; line += cell)
    {
        if (line != lines.back())
            lines.push_back(line);
    }
    if (lines.back() != limit)
        lines.push_back(limit);
    return lines;
}

int CenteredStart(int length, int limit)
{
    return (limit - length) / 2;
}

int SnapEdges(int start, int length, const SnapAxis& axis)
{
    int maxStart = axis.Limit - length;
    if (maxStart < 0)
        return ClampStart(start, length, axis.Limit);
    // every candidate is a new start, ones outside the editor skipped, so the result is always on a line; the start edge goes first and
    // wins ties
    Nearest nearest(start, 0, maxStart);
    OfferGridLines(nearest, start, axis, 0);
    OfferGridLines(nearest, start + length, axis, -length);
    nearest.Offer(0);
    nearest.Offer(maxStart);
    // where the center buttons put the screen, so a drag can land back on it
    nearest.Offer(CenteredStart(length, axis.Limit));
    if (axis.Other)
    {
        int threshold = axis.AlignmentThreshold();
        for (int edge : {axis.Other->Start, axis.Other->End()})
        {
            nearest.Offer(edge, threshold);
            nearest.Offer(edge - length, threshold);
        }
        nearest.Offer(axis.Other->Center() - length / 2, threshold);
    }
    return nearest.Value;
}

SnappedEdge SnapResizedEdge(int position, int fixedEdge, int minLength, bool growsTowardsEnd, const SnapAxis& axis)
{
    // the dragged edge stays on its own side of the fixed one, at least the minimum size away
    int min = growsTowardsEnd ? fixedEdge + minLength : 0;
    int max = growsTowardsEnd ? axis.Limit : fixedEdge - minLength;
    if (min > max)
        return {position, INT_MAX};
    // a pointer dragged past the fixed edge snaps from the minimum size, not from the other side
    int from = std::clamp(position, min, max);
    Nearest nearest(from, min, max);
    OfferGridLines(nearest, from, axis, 0);
    nearest.Offer(0);
    nearest.Offer(axis.Limit);
    if (axis.Other)
    {
        int threshold = axis.AlignmentThreshold();
        nearest.Offer(axis.Other->Start, threshold);
        nearest.Offer(axis.Other->End(), threshold);
        nearest.Offer(growsTowardsEnd ? fixedEdge + axis.Other->Length : fixedEdge - axis.Other->Length, threshold);
    }
    return {nearest.Value, std::abs(nearest.Value - position)};
}

int NextSnapStart(int start, int length, const SnapAxis& axis, int direction)
{
    int maxStart = axis.Limit - length;
    if (maxStart < 0 || direction == 0)
        return start;
    std::vector<int> candidates {0, maxStart, CenteredStart(length, axis.Limit)};
    for (int line : SnapGridLines(axis.Limit, axis.Cell))
    {
        candidates.push_back(line);
        candidates.push_back(line - length);
    }
    if (axis.Other)
    {
        for (int edge : {axis.Other->Start, axis.Other->End()})
        {
            candidates.push_back(edge);
            candidates.push_back(edge - length);
        }
        candidates.push_back(axis.Other->Center() - length / 2);
    }
    int best = start;
    for (int c : candidates)
    {
        if (c < 0 || c > maxStart)
            continue;
        if (direction > 0 && c > start && (best == start || c < best))
            best = c;
        else if (direction < 0 && c < start && (best == start || c > best))
            best = c;
    }
    return best;
}

bool IsLeft(Corner c)
{
    return c == Corner::TopLeft || c == Corner::BottomLeft;
}

bool IsTop(Corner c)
{
    return c == Corner::TopLeft || c == Corner::TopRight;
}

Corner Opposite(Corner c)
{
    switch (c)
    {
    case Corner::TopLeft: return Corner::BottomRight;
    case Corner::TopRight: return Corner::BottomLeft;
    case Corner::BottomLeft: return Corner::TopRight;
    default: return Corner::TopLeft;
    }
}

int CornerX(Corner c, const IntRect& r)
{
    return IsLeft(c) ? r.X : r.Right();
}

int CornerY(Corner c, const IntRect& r)
{
    return IsTop(c) ? r.Y : r.Bottom();
}

IntRect PlaceAt(Corner c, int x, int y, int w, int h)
{
    return {IsLeft(c) ? x : x - w, IsTop(c) ? y : y - h, w, h};
}

Corner NearestCorner(const IntRect& r, int editorW, int editorH)
{
    double left = r.X, right = editorW - r.Right(), top = r.Y, bottom = editorH - r.Bottom();
    const Corner corners[] = {Corner::TopLeft, Corner::TopRight, Corner::BottomLeft, Corner::BottomRight};
    const double distances[] = {left * left + top * top, right * right + top * top, left * left + bottom * bottom, right * right + bottom * bottom};
    int best = 0;
    for (int i = 1; i < 4; i++)
    {
        if (distances[i] < distances[best])
            best = i;
    }
    return corners[best];
}

std::optional<float> AspectRatioOf(ScreenAspect aspect, std::optional<float> wideRatio)
{
    switch (aspect)
    {
    case ScreenAspect::Ratio43: return CONSOLE_ASPECT_RATIO;
    case ScreenAspect::Ratio169: return 16 / 9.f;
    case ScreenAspect::Wide: return wideRatio;
    default: return std::nullopt;
    }
}

ScreenAspect GuessAspect(const IntRect& r, std::optional<float> wideRatio)
{
    // a wide variant's top screen has no plain 16:9, so a 16:9 shape that isn't the variant's ratio counts as Free
    const ScreenAspect candidates[2] = {wideRatio ? ScreenAspect::Wide : ScreenAspect::Ratio43,
        wideRatio ? ScreenAspect::Ratio43 : ScreenAspect::Ratio169};
    for (ScreenAspect a : candidates)
    {
        std::optional<float> ratio = AspectRatioOf(a, wideRatio);
        if (ratio && std::fabs(r.W - r.H * *ratio) <= ASPECT_TOLERANCE)
            return a;
    }
    return ScreenAspect::Free;
}

std::optional<float> WidescreenAspectRatio(const std::string& key)
{
    for (const WideAspectInfo& a : kWideAspects)
    {
        if (key == a.Key)
        {
            // the even 3D view width that gives the ratio (WidescreenAspect.viewWidth), the drawn shape from that rounded width
            int viewWidth = std::clamp(RoundToInt(DS_SCREEN_HEIGHT * a.WidthRatio / (a.HeightRatio * 2.f)) * 2, 256, 768);
            return viewWidth / (float)DS_SCREEN_HEIGHT;
        }
    }
    return std::nullopt;
}

const std::vector<std::string>& WidescreenAspectKeys()
{
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> out;
        for (const WideAspectInfo& a : kWideAspects)
            out.push_back(a.Key);
        return out;
    }();
    return keys;
}

void ConstrainSize(std::optional<float> ratio, float w, float h, int maxW, int maxH, int minSize, int& outW, int& outH)
{
    if (!ratio)
    {
        // at most the maximum, then at least the minimum (the minimum wins, as coerceAtMost then coerceAtLeast)
        outW = std::max(std::min(RoundToInt(w), maxW), minSize);
        outH = std::max(std::min(RoundToInt(h), maxH), minSize);
        return;
    }
    float minWidth = MinWidth(ratio, minSize);
    float maxRatioWidth = std::min((float)maxW, maxH * *ratio);
    float width = std::max(std::min(w, maxRatioWidth), minWidth);
    outW = RoundToInt(width);
    outH = RoundToInt(width / *ratio);
}

IntRect ClampInside(const IntRect& r, int editorW, int editorH)
{
    IntRect out = r;
    out.X = std::min(std::max(r.X, 0), editorW - r.W);
    out.Y = std::min(std::max(r.Y, 0), editorH - r.H);
    return out;
}

IntRect DragRect(const IntRect& start, float offsetX, float offsetY, const EditorSpace& space)
{
    // the unsnapped position snapped, so the screen doesn't stick to a line
    IntRect moved = start;
    moved.X = (int)(start.X + offsetX);
    moved.Y = (int)(start.Y + offsetY);
    IntRect rect = ClampInside(moved, space.W, space.H);
    return space.Snap ? SnapPosition(rect, space) : rect;
}

IntRect NudgeRect(const IntRect& r, int dx, int dy, const EditorSpace& space)
{
    IntRect moved = r;
    moved.X += dx;
    moved.Y += dy;
    return ClampInside(moved, space.W, space.H);
}

IntRect ResizeFromHandle(const IntRect& start, Corner corner, float offsetX, float offsetY, std::optional<float> ratio, const EditorSpace& space)
{
    // the opposite corner stays, so the screen can grow until it reaches the editor's edges on the dragged corner's side
    Corner fixed = Opposite(corner);
    int fixedX = CornerX(fixed, start), fixedY = CornerY(fixed, start);
    float width = start.W + (IsLeft(corner) ? -offsetX : offsetX);
    float height = start.H + (IsTop(corner) ? -offsetY : offsetY);
    int maxW = IsLeft(corner) ? fixedX : space.W - fixedX;
    int maxH = IsTop(corner) ? fixedY : space.H - fixedY;
    // with a locked aspect ratio, whichever side the pointer wants bigger
    if (ratio)
    {
        width = std::max(width, height * *ratio);
        height = width / *ratio;
    }
    if (space.Snap)
        SnapResize(corner, fixedX, fixedY, width, height, ratio, space);
    int w, h;
    ConstrainSize(ratio, width, height, maxW, maxH, space.MinSize, w, h);
    return PlaceAt(fixed, fixedX, fixedY, w, h);
}

IntRect ResizeTo(const IntRect& r, float w, float h, std::optional<float> ratio, Corner anchor, const EditorSpace& space)
{
    int cw, ch;
    ConstrainSize(ratio, w, h, space.W, space.H, space.MinSize, cw, ch);
    IntRect rect = ClampInside(PlaceAt(anchor, CornerX(anchor, r), CornerY(anchor, r), cw, ch), space.W, space.H);
    return space.Snap ? SnapPosition(rect, space) : rect;
}

IntRect ResizeBy(const IntRect& r, int widthOffset, std::optional<float> ratio, Corner anchor, const EditorSpace& space)
{
    if (r.W <= 0 || r.H <= 0 || widthOffset == 0)
        return r;
    if (ratio)
        return ResizeTo(r, (float)(r.W + widthOffset), 0, ratio, anchor, space);
    float desiredScale = (r.W + widthOffset) / (float)r.W;
    float maxScale = std::min(space.W / (float)r.W, space.H / (float)r.H);
    float minScale = std::max(space.MinSize / (float)r.W, space.MinSize / (float)r.H);
    float scale = std::max(std::min(desiredScale, maxScale), minScale);
    // no room left that way
    if ((widthOffset > 0 && scale <= 1.f) || (widthOffset < 0 && scale >= 1.f))
        return r;
    return ResizeTo(r, r.W * scale, r.H * scale, std::nullopt, anchor, space);
}

void ScaleSize(int scale, ScreenAspect aspect, std::optional<float> ratio, int& w, int& h)
{
    if (aspect == ScreenAspect::Wide && ratio)
    {
        h = DS_SCREEN_HEIGHT * scale;
        w = RoundToInt(h * *ratio);
        return;
    }
    w = DS_SCREEN_WIDTH * scale;
    h = ratio ? RoundToInt(w / *ratio) : DS_SCREEN_HEIGHT * scale;
}

void FitSize(const IntRect& r, std::optional<float> ratio, int editorW, int editorH, int& w, int& h)
{
    float shape = ratio ? *ratio : r.W / (float)std::max(r.H, 1);
    float width = std::min((float)editorW, editorH * shape);
    w = RoundToInt(width);
    h = RoundToInt(width / shape);
}

LayoutVariant MakeWindowVariant(int w, int h)
{
    LayoutVariant v;
    v.W = w;
    v.H = h;
    v.Orientation = w > h ? "LANDSCAPE" : "PORTRAIT";
    v.MainDisplay.Id = 0;
    v.MainDisplay.Type = "BUILT_IN";
    v.MainDisplay.W = w;
    v.MainDisplay.H = h;
    return v;
}

}
