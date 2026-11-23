#include "render/SvgBackground.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>

#include "layout/LayoutGeometry.h"
#include "nanosvg/nanosvg.h"
#include "nanosvg/nanosvgrast.h"

namespace Frontend
{

namespace
{

// what an SVG background may be (rule 2): the built-in ones are under 100 KB with a few hundred elements
constexpr size_t MAX_SVG_BYTES = 16u << 20;
constexpr int MAX_DEPTH = 64;
constexpr size_t MAX_NODES = 200000;
constexpr int MAX_USE_DEPTH = 8;
constexpr long long MAX_TILES = 20000;
// the layers a document may split into (each a full bitmap), and the text parsed for them in all
constexpr int MAX_LAYERS = 24;
constexpr size_t MAX_PARSED_BYTES = 64u << 20;
constexpr int SNIFF_BYTES = 4096;

// An XML element (or a text node, with no name), its attributes as written (entities left as they are, so they're written back the same)
struct Node
{
    std::string Name;
    std::vector<std::pair<std::string, std::string>> Attrs;
    std::vector<Node> Children;
    // a text node's text, or a CDATA section with its markers
    std::string Text;

    const std::string* Attr(const char* name) const
    {
        for (const auto& [k, v] : Attrs)
        {
            if (k == name)
                return &v;
        }
        return nullptr;
    }
    void Set(const char* name, const std::string& value)
    {
        for (auto& [k, v] : Attrs)
        {
            if (k == name)
            {
                v = value;
                return;
            }
        }
        Attrs.emplace_back(name, value);
    }
    void Remove(const char* name)
    {
        Attrs.erase(std::remove_if(Attrs.begin(), Attrs.end(), [&](const auto& a) { return a.first == name; }), Attrs.end());
    }
};

// A small XML reader for SVG files: elements, attributes, text and CDATA; the prolog, comments, processing instructions and a doctype are
// skipped. Bounded in depth and size
class XmlReader
{
public:
    explicit XmlReader(const std::string& text) : Text(text) {}

    bool Read(Node& root, std::string& error)
    {
        SkipProlog();
        if (Pos >= Text.size() || Text[Pos] != '<' || !Element(root, 0))
        {
            error = Error.empty() ? "no root element" : Error;
            return false;
        }
        return true;
    }

private:
    bool Starts(const char* s) const { return Text.compare(Pos, strlen(s), s) == 0; }

    bool SkipTo(const char* end)
    {
        size_t at = Text.find(end, Pos);
        if (at == std::string::npos)
            return false;
        Pos = at + strlen(end);
        return true;
    }

    // what comes before the root and between elements that isn't content: <?...?>, <!--...-->, <!DOCTYPE ...>, white space
    void SkipProlog()
    {
        if (Starts("\xEF\xBB\xBF"))
            Pos += 3;
        for (;;)
        {
            while (Pos < Text.size() && isspace((unsigned char)Text[Pos]))
                Pos++;
            if (Starts("<?"))
            {
                if (!SkipTo("?>"))
                    return;
            }
            else if (Starts("<!--"))
            {
                if (!SkipTo("-->"))
                    return;
            }
            else if (Starts("<!DOCTYPE") || Starts("<!doctype"))
            {
                // an internal subset in [...] can have '>' in it
                size_t bracket = Text.find('[', Pos), close = Text.find('>', Pos);
                if (bracket != std::string::npos && bracket < close && !SkipTo("]>"))
                    return;
                if (bracket == std::string::npos || bracket > close)
                    Pos = close == std::string::npos ? Text.size() : close + 1;
            }
            else
                return;
        }
    }

    bool Fail(const char* what)
    {
        if (Error.empty())
            Error = what;
        return false;
    }

    static bool IsNameChar(char c) { return isalnum((unsigned char)c) || c == ':' || c == '-' || c == '_' || c == '.'; }

    std::string Name()
    {
        size_t start = Pos;
        while (Pos < Text.size() && IsNameChar(Text[Pos]))
            Pos++;
        return Text.substr(start, Pos - start);
    }

    bool Element(Node& node, int depth)
    {
        if (depth > MAX_DEPTH)
            return Fail("nested too deep");
        if (++Nodes > MAX_NODES)
            return Fail("too many elements");
        Pos++; // '<'
        node.Name = Name();
        if (node.Name.empty())
            return Fail("an element without a name");
        for (;;)
        {
            while (Pos < Text.size() && isspace((unsigned char)Text[Pos]))
                Pos++;
            if (Pos >= Text.size())
                return Fail("the file ends inside a tag");
            if (Starts("/>"))
            {
                Pos += 2;
                return true;
            }
            if (Text[Pos] == '>')
            {
                Pos++;
                break;
            }
            std::string attr = Name();
            while (Pos < Text.size() && isspace((unsigned char)Text[Pos]))
                Pos++;
            if (attr.empty() || Pos >= Text.size() || Text[Pos] != '=')
                return Fail("an attribute without a value");
            Pos++;
            while (Pos < Text.size() && isspace((unsigned char)Text[Pos]))
                Pos++;
            char quote = Pos < Text.size() ? Text[Pos] : 0;
            if (quote != '"' && quote != '\'')
                return Fail("an attribute value without quotes");
            size_t end = Text.find(quote, Pos + 1);
            if (end == std::string::npos)
                return Fail("an attribute value that doesn't end");
            node.Attrs.emplace_back(attr, Text.substr(Pos + 1, end - Pos - 1));
            Pos = end + 1;
        }
        // the content, up to </name>
        for (;;)
        {
            size_t lt = Text.find('<', Pos);
            if (lt == std::string::npos)
                return Fail("an element that doesn't end");
            if (lt > Pos)
            {
                Node text;
                text.Text = Text.substr(Pos, lt - Pos);
                node.Children.push_back(std::move(text));
                Pos = lt;
            }
            if (Starts("</"))
            {
                Pos += 2;
                std::string closing = Name();
                if (closing != node.Name)
                    return Fail("tags that don't match");
                size_t gt = Text.find('>', Pos);
                if (gt == std::string::npos)
                    return Fail("a closing tag that doesn't end");
                Pos = gt + 1;
                return true;
            }
            if (Starts("<!--"))
            {
                if (!SkipTo("-->"))
                    return Fail("a comment that doesn't end");
                continue;
            }
            if (Starts("<![CDATA["))
            {
                size_t start = Pos;
                if (!SkipTo("]]>"))
                    return Fail("a CDATA section that doesn't end");
                Node text;
                text.Text = Text.substr(start, Pos - start);
                node.Children.push_back(std::move(text));
                continue;
            }
            if (Starts("<?"))
            {
                if (!SkipTo("?>"))
                    return Fail("a processing instruction that doesn't end");
                continue;
            }
            Node child;
            if (!Element(child, depth + 1))
                return false;
            node.Children.push_back(std::move(child));
        }
    }

    const std::string& Text;
    size_t Pos = 0;
    size_t Nodes = 0;
    std::string Error;
};

void Write(const Node& node, std::string& out)
{
    if (node.Name.empty())
    {
        out += node.Text;
        return;
    }
    out += '<';
    out += node.Name;
    for (const auto& [k, v] : node.Attrs)
    {
        char quote = v.find('"') == std::string::npos ? '"' : '\'';
        out += ' ';
        out += k;
        out += '=';
        out += quote;
        out += v;
        out += quote;
    }
    if (node.Children.empty())
    {
        out += "/>";
        return;
    }
    out += '>';
    for (const Node& child : node.Children)
        Write(child, out);
    out += "</";
    out += node.Name;
    out += '>';
}

std::string Number(double v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

// a length: a number with no unit or px, or a percentage of [base]. False for anything else (em, mm, ...), which is left as it is
bool ParseLength(const std::string& text, double base, double& value)
{
    const char* s = text.c_str();
    char* end = nullptr;
    double v = strtod(s, &end);
    if (end == s)
        return false;
    while (*end == ' ')
        end++;
    if (*end == '%')
    {
        value = v / 100.0 * base;
        return true;
    }
    if (*end == 0 || strcmp(end, "px") == 0)
    {
        value = v;
        return true;
    }
    return false;
}

double LengthOr(const Node& node, const char* attr, double base, double fallback)
{
    double v;
    const std::string* text = node.Attr(attr);
    return text && ParseLength(*text, base, v) ? v : fallback;
}

// "url(#id)" (with spaces or quotes) to "id"; empty for anything else
std::string UrlId(const std::string* value)
{
    if (!value)
        return {};
    size_t open = value->find("url(");
    size_t hash = value->find('#', open == std::string::npos ? 0 : open);
    size_t close = value->find(')', hash == std::string::npos ? 0 : hash);
    if (open == std::string::npos || hash == std::string::npos || close == std::string::npos)
        return {};
    std::string id = value->substr(hash + 1, close - hash - 1);
    while (!id.empty() && (id.back() == ' ' || id.back() == '"' || id.back() == '\''))
        id.pop_back();
    return id;
}

std::string HrefId(const Node& node)
{
    const std::string* href = node.Attr("href");
    if (!href)
        href = node.Attr("xlink:href");
    return href && !href->empty() && (*href)[0] == '#' ? href->substr(1) : std::string();
}

void Index(const Node& node, std::map<std::string, const Node*>& ids)
{
    if (const std::string* id = node.Attr("id"))
        ids.emplace(*id, &node);
    for (const Node& child : node.Children)
        Index(child, ids);
}

// the numbers of a viewBox
bool ParseViewBox(const std::string* text, double box[4])
{
    if (!text)
        return false;
    const char* s = text->c_str();
    for (int i = 0; i < 4; i++)
    {
        while (*s == ' ' || *s == ',' || *s == '\t' || *s == '\n' || *s == '\r')
            s++;
        char* end = nullptr;
        box[i] = strtod(s, &end);
        if (end == s)
            return false;
        s = end;
    }
    return box[2] > 0 && box[3] > 0;
}

// The document's viewport: what percentages are of (the viewBox, or the bitmap without one) and how its user units map to the bitmap
struct Viewport
{
    double BaseW = 0, BaseH = 0;
    double ScaleX = 1, ScaleY = 1, OffsetX = 0, OffsetY = 0;
    // where the viewBox lands in the bitmap, in pixels (an aligned preserveAspectRatio)
    double ShiftX = 0, ShiftY = 0;
};

// percentages of the geometry attributes as user units, so bounding boxes can be worked out here and a pattern's tile document (which is
// smaller) still sees the document's sizes
void ResolvePercentages(Node& node, const Viewport& view)
{
    static const struct { const char* Element; const char* Attrs[4]; char Axis[4]; } kGeometry[] = {
        {"rect", {"x", "y", "width", "height"}, {'x', 'y', 'x', 'y'}},
        {"circle", {"cx", "cy", "r", nullptr}, {'x', 'y', 'd', 0}},
        {"ellipse", {"cx", "cy", "rx", "ry"}, {'x', 'y', 'x', 'y'}},
        {"line", {"x1", "y1", "x2", "y2"}, {'x', 'y', 'x', 'y'}},
        {"use", {"x", "y", "width", "height"}, {'x', 'y', 'x', 'y'}},
        {"image", {"x", "y", "width", "height"}, {'x', 'y', 'x', 'y'}},
        {"pattern", {"x", "y", "width", "height"}, {'x', 'y', 'x', 'y'}},
        {"svg", {"x", "y", "width", "height"}, {'x', 'y', 'x', 'y'}},
    };
    double diagonal = std::sqrt(view.BaseW * view.BaseW + view.BaseH * view.BaseH) / std::sqrt(2.0);
    bool patternInBox = node.Name == "pattern" && !(node.Attr("patternUnits") && *node.Attr("patternUnits") == "userSpaceOnUse");
    for (const auto& g : kGeometry)
    {
        if (node.Name != g.Element || patternInBox)
            continue;
        for (int i = 0; i < 4 && g.Attrs[i]; i++)
        {
            const std::string* text = node.Attr(g.Attrs[i]);
            if (!text || text->find('%') == std::string::npos)
                continue;
            double base = g.Axis[i] == 'x' ? view.BaseW : g.Axis[i] == 'y' ? view.BaseH : diagonal;
            double v;
            if (ParseLength(*text, base, v))
                node.Set(g.Attrs[i], Number(v));
        }
        // rect's rx and ry: of the width and height
        if (node.Name == "rect")
        {
            for (const char* r : {"rx", "ry"})
            {
                const std::string* text = node.Attr(r);
                double v;
                if (text && text->find('%') != std::string::npos && ParseLength(*text, r[1] == 'x' ? view.BaseW : view.BaseH, v))
                    node.Set(r, Number(v));
            }
        }
    }
    // an <svg>'s content has its percentages of its own viewBox (the root's is the document's already)
    Viewport inner = view;
    double box[4];
    if (node.Name == "svg" && ParseViewBox(node.Attr("viewBox"), box))
    {
        inner.BaseW = box[2];
        inner.BaseH = box[3];
    }
    for (Node& child : node.Children)
        ResolvePercentages(child, inner);
}

// a nested <svg> (a new viewport, which nanosvg doesn't know) as a <g> with the transform its viewBox and place mean; its clipping to the
// viewport is left out
void FlattenNestedSvg(Node& node, int depth)
{
    for (Node& child : node.Children)
    {
        if (child.Name == "svg")
        {
            double x = LengthOr(child, "x", 0, 0), y = LengthOr(child, "y", 0, 0);
            double box[4];
            std::string transform = "translate(" + Number(x) + "," + Number(y) + ")";
            const std::string* w = child.Attr("width");
            const std::string* h = child.Attr("height");
            double width, height;
            if (ParseViewBox(child.Attr("viewBox"), box) && w && h && ParseLength(*w, 0, width) && ParseLength(*h, 0, height))
            {
                double sx = width / box[2], sy = height / box[3];
                const std::string* par = child.Attr("preserveAspectRatio");
                if (!(par && par->find("none") != std::string::npos))
                {
                    // xMidYMid meet, the default (and what any other value is taken as here)
                    double scale = par && par->find("slice") != std::string::npos ? std::max(sx, sy) : std::min(sx, sy);
                    transform += " translate(" + Number((width - box[2] * scale) / 2) + "," + Number((height - box[3] * scale) / 2) + ")";
                    sx = sy = scale;
                }
                transform += " scale(" + Number(sx) + "," + Number(sy) + ") translate(" + Number(-box[0]) + "," + Number(-box[1]) + ")";
            }
            const std::string* own = child.Attr("transform");
            child.Name = "g";
            for (const char* attr : {"x", "y", "width", "height", "viewBox", "preserveAspectRatio", "transform"})
                child.Remove(attr);
            child.Set("transform", (own ? *own + " " : std::string()) + transform);
        }
        if (depth < MAX_DEPTH)
            FlattenNestedSvg(child, depth + 1);
    }
}

// <use> expanded: a <g> with the use's attributes and transform (and its x, y as a translate) around a copy of what it refers to
Node Expand(const Node& node, const std::map<std::string, const Node*>& ids, int useDepth, size_t& nodes)
{
    Node out;
    out.Name = node.Name;
    out.Attrs = node.Attrs;
    out.Text = node.Text;
    for (const Node& child : node.Children)
    {
        if (++nodes > MAX_NODES)
            break;
        if (child.Name != "use")
        {
            out.Children.push_back(Expand(child, ids, useDepth, nodes));
            continue;
        }
        auto it = ids.find(HrefId(child));
        if (it == ids.end() || useDepth >= MAX_USE_DEPTH)
            continue;
        Node group;
        group.Name = "g";
        for (const auto& [k, v] : child.Attrs)
        {
            if (k != "x" && k != "y" && k != "width" && k != "height" && k != "href" && k != "xlink:href" && k != "id" && k != "transform")
                group.Attrs.emplace_back(k, v);
        }
        std::string transform = child.Attr("transform") ? *child.Attr("transform") + " " : std::string();
        transform += "translate(" + Number(LengthOr(child, "x", 0, 0)) + "," + Number(LengthOr(child, "y", 0, 0)) + ")";
        group.Set("transform", transform);
        Node referred = *it->second;
        referred.Remove("id");
        if (referred.Name == "symbol")
        {
            referred.Name = "g";
            referred.Remove("viewBox");
            referred.Remove("preserveAspectRatio");
        }
        Node wrapper;
        wrapper.Children.push_back(std::move(referred));
        Node expanded = Expand(wrapper, ids, useDepth + 1, nodes);
        group.Children = std::move(expanded.Children);
        out.Children.push_back(std::move(group));
    }
    return out;
}

bool IsGradient(const Node& node) { return node.Name == "linearGradient" || node.Name == "radialGradient"; }

// a shape's box in its own user units: rects, circles and ellipses; false for any other
bool ShapeBox(const Node& node, double& x, double& y, double& w, double& h)
{
    if (node.Name == "rect")
    {
        x = LengthOr(node, "x", 0, 0);
        y = LengthOr(node, "y", 0, 0);
        w = LengthOr(node, "width", 0, 0);
        h = LengthOr(node, "height", 0, 0);
    }
    else if (node.Name == "circle" || node.Name == "ellipse")
    {
        double cx = LengthOr(node, "cx", 0, 0), cy = LengthOr(node, "cy", 0, 0);
        double rx = LengthOr(node, node.Name == "circle" ? "r" : "rx", 0, 0), ry = LengthOr(node, node.Name == "circle" ? "r" : "ry", 0, 0);
        x = cx - rx;
        y = cy - ry;
        w = 2 * rx;
        h = 2 * ry;
    }
    else
        return false;
    return w > 0 && h > 0;
}

// Gradients as nanosvg needs them, each shape that paints with one getting its own copy where it must:
//  - objectBoundingBox units (the default): a rect, circle or ellipse gets a copy in user space with its box as the gradient's transform,
//    which is what the box units mean (nanosvg takes plain-number box coordinates as pixels, and draws radial ones as circles); another
//    shape gets a copy with its coordinates made percentages, which nanosvg reads against the shape's bounds (close: radial ones stay
//    circles);
//  - a fill-opacity or stroke-opacity, which nanosvg applies to plain colors only: the copy's stops get it in their stop-opacity
class GradientCopies
{
public:
    explicit GradientCopies(const std::map<std::string, const Node*>& ids) : Ids(ids) {}

    void Apply(Node& node)
    {
        for (const char* paint : {"fill", "stroke"})
        {
            std::string id = UrlId(node.Attr(paint));
            auto it = Ids.find(id);
            if (id.empty() || it == Ids.end() || !IsGradient(*it->second))
                continue;
            const Node& gradient = *it->second;
            const std::string* units = gradient.Attr("gradientUnits");
            bool boxUnits = !(units && *units == "userSpaceOnUse");
            double x = 0, y = 0, w = 0, h = 0;
            bool hasBox = boxUnits && ShapeBox(node, x, y, w, h);
            const std::string* opacityText = node.Attr(paint[0] == 'f' ? "fill-opacity" : "stroke-opacity");
            double opacity = opacityText ? std::clamp(atof(opacityText->c_str()), 0.0, 1.0) : 1.0;
            if (!boxUnits && opacity >= 1.0)
                continue;
            std::string key = id + (hasBox ? "@" + Number(x) + "," + Number(y) + "," + Number(w) + "," + Number(h) : boxUnits ? "%" : "") + "|"
                + Number(opacity);
            auto known = Copies.find(key);
            std::string newId = known != Copies.end() ? known->second : Copy(gradient, id, hasBox, boxUnits && !hasBox, x, y, w, h, opacity);
            Copies[key] = newId;
            node.Set(paint, "url(#" + newId + ")");
        }
        for (Node& child : node.Children)
            Apply(child);
    }

    // the copies, for the documents' defs
    void Write(std::string& defs) const
    {
        for (const Node& g : Made)
            Frontend::Write(g, defs);
    }

private:
    std::string Copy(const Node& gradient, const std::string& id, bool hasBox, bool percent, double x, double y, double w, double h, double opacity)
    {
        Node copy = gradient;
        std::string newId = id + "__copy" + std::to_string(Made.size());
        copy.Set("id", newId);
        bool linear = gradient.Name == "linearGradient";
        if (hasBox)
        {
            copy.Set("gradientUnits", "userSpaceOnUse");
            const std::string* own = gradient.Attr("gradientTransform");
            copy.Set("gradientTransform", "matrix(" + Number(w) + " 0 0 " + Number(h) + " " + Number(x) + " " + Number(y) + ")" + (own ? " " + *own : ""));
            // the box units' defaults, which in user space would be of the viewport; percentages as fractions of the box
            const char* const attrs[] = {"x1", "y1", "x2", "y2", "cx", "cy", "r"};
            const char* const defaults[] = {"0", "0", "1", "0", "0.5", "0.5", "0.5"};
            for (int i = linear ? 0 : 4; i < (linear ? 4 : 7); i++)
            {
                const std::string* text = copy.Attr(attrs[i]);
                double v;
                if (!text)
                    copy.Set(attrs[i], defaults[i]);
                else if (text->find('%') != std::string::npos && ParseLength(*text, 1.0, v))
                    copy.Set(attrs[i], Number(v));
            }
            if (!linear)
            {
                for (const char* focal : {"fx", "fy"})
                {
                    const std::string* text = copy.Attr(focal);
                    double v;
                    if (!text)
                        copy.Set(focal, *copy.Attr(focal[1] == 'x' ? "cx" : "cy"));
                    else if (text->find('%') != std::string::npos && ParseLength(*text, 1.0, v))
                        copy.Set(focal, Number(v));
                }
            }
        }
        else if (percent)
        {
            for (const char* attr : {"x1", "y1", "x2", "y2", "cx", "cy", "r", "fx", "fy"})
            {
                const std::string* text = copy.Attr(attr);
                if (text && text->find('%') == std::string::npos)
                    copy.Set(attr, Number(atof(text->c_str()) * 100.0) + "%");
            }
        }
        if (opacity < 1.0)
        {
            // the stops it takes from the gradient it refers to (href), as its own, so they can carry the opacity
            const Node* source = &gradient;
            for (int depth = 0; depth < 8 && !HasStops(*source); depth++)
            {
                auto it = Ids.find(HrefId(*source));
                if (it == Ids.end() || !IsGradient(*it->second))
                    break;
                source = it->second;
            }
            if (source != &gradient)
                copy.Children = source->Children;
            for (Node& stop : copy.Children)
            {
                if (stop.Name != "stop")
                    continue;
                const std::string* own = stop.Attr("stop-opacity");
                stop.Set("stop-opacity", Number((own ? std::clamp(atof(own->c_str()), 0.0, 1.0) : 1.0) * opacity));
            }
        }
        Made.push_back(std::move(copy));
        return newId;
    }

    static bool HasStops(const Node& gradient)
    {
        for (const Node& child : gradient.Children)
        {
            if (child.Name == "stop")
                return true;
        }
        return false;
    }

    const std::map<std::string, const Node*>& Ids;
    std::map<std::string, std::string> Copies;
    std::vector<Node> Made;
};

// nanosvg's parse and rasterizer, freed when they go
struct SvgImage
{
    NSVGimage* Image = nullptr;
    ~SvgImage()
    {
        if (Image)
            nsvgDelete(Image);
    }
};
struct SvgRasterizer
{
    NSVGrasterizer* Rasterizer = nsvgCreateRasterizer();
    ~SvgRasterizer()
    {
        if (Rasterizer)
            nsvgDeleteRasterizer(Rasterizer);
    }
};

bool Parse(std::string text, SvgImage& image)
{
    // nanosvg parses in place
    image.Image = nsvgParse(text.data(), "px", 96.f);
    return image.Image != nullptr;
}

// nanosvg leaves straight alpha; Android's bitmaps (and so the app's textures) are premultiplied
void Premultiply(uint8_t* rgba, size_t pixels)
{
    for (size_t i = 0; i < pixels; i++, rgba += 4)
    {
        int a = rgba[3];
        rgba[0] = (uint8_t)((rgba[0] * a + 127) / 255);
        rgba[1] = (uint8_t)((rgba[1] * a + 127) / 255);
        rgba[2] = (uint8_t)((rgba[2] * a + 127) / 255);
    }
}

// [src] (premultiplied) over [dst], times [opacity]
void Over(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src, float opacity)
{
    int k = (int)std::lround(std::clamp(opacity, 0.f, 1.f) * 255);
    for (size_t i = 0; i + 3 < dst.size(); i += 4)
    {
        int sa = src[i + 3] * k / 255;
        if (sa == 0 && src[i] == 0 && src[i + 1] == 0 && src[i + 2] == 0)
            continue;
        for (int c = 0; c < 4; c++)
        {
            int s = src[i + c] * k / 255;
            dst[i + c] = (uint8_t)std::min(255, s + (dst[i + c] * (255 - sa) + 127) / 255);
        }
    }
}

double OpacityOf(const Node& node)
{
    double opacity = 1;
    for (const char* attr : {"opacity", "fill-opacity"})
    {
        if (const std::string* text = node.Attr(attr))
            opacity *= std::clamp(atof(text->c_str()), 0.0, 1.0);
    }
    return opacity;
}

// The document split for drawing: its defs (gradients, patterns and the rest, which every layer gets), and its top-level content in order,
// each item either shapes nanosvg draws as they are or one shape filled with a pattern. Each layer costs a parse of the defs and a full
// bitmap, so the whole of it has a budget (rule 2): one past it gives up with an error rather than spend seconds on a hostile file. [Cancel]
// is checked between layers and tiles, so a decode given up on (the background changed, the app quits) ends soon
class Composer
{
public:
    Composer(const Node& root, const std::map<std::string, const Node*>& ids, const Viewport& view, int width, int height,
        const std::atomic<bool>* cancel)
        : Root(root), Ids(ids), View(view), Width(width), Height(height), Cancel(cancel)
    {
    }

    bool Draw(const std::string& extraDefs, std::vector<uint8_t>& out, std::string& error)
    {
        // the root's start tag, as Write would write it
        Node root;
        root.Name = Root.Name;
        root.Attrs = Root.Attrs;
        Write(root, Head);
        Head.resize(Head.size() - 2);
        Head += ">";
        Defs = extraDefs;
        for (const Node& child : Root.Children)
        {
            if (child.Name == "defs" || IsGradient(child) || child.Name == "pattern" || child.Name == "style")
                Write(child, Defs);
        }
        if (!Raster.Rasterizer)
        {
            error = "nanosvg's rasterizer couldn't be made";
            return false;
        }
        out.assign((size_t)Width * Height * 4, 0);
        std::string segment;
        for (const Node& child : Root.Children)
        {
            if (child.Name.empty() || child.Name == "defs" || IsGradient(child) || child.Name == "pattern" || child.Name == "style")
                continue;
            const Node* pattern = PatternOf(child);
            if (!pattern)
            {
                Write(child, segment);
                continue;
            }
            if (!DrawSegment(segment, out, error) || !DrawPattern(child, *pattern, out, error))
                return false;
            segment.clear();
            // its stroke, if it has one, over the pattern
            if (child.Attr("stroke"))
            {
                Node outline = child;
                outline.Set("fill", "none");
                Write(outline, segment);
            }
        }
        return DrawSegment(segment, out, error);
    }

private:
    const Node* PatternOf(const Node& node) const
    {
        auto it = Ids.find(UrlId(node.Attr("fill")));
        return it != Ids.end() && it->second->Name == "pattern" ? it->second : nullptr;
    }

    // one more layer, of [text] to parse: false (with the reason) when it's cancelled or over the budget
    bool Spend(size_t text, std::string& error)
    {
        if (Cancel && Cancel->load(std::memory_order_relaxed))
        {
            error = "cancelled";
            return false;
        }
        Layers++;
        ParsedBytes += text;
        if (Layers > MAX_LAYERS || ParsedBytes > MAX_PARSED_BYTES)
        {
            error = "too much for one background (more than " + std::to_string(MAX_LAYERS) + " layers or " + std::to_string(MAX_PARSED_BYTES >> 20)
                + " MB parsed: patterns split it into layers)";
            return false;
        }
        return true;
    }

    // a layer (parsed into [image]) rasterized and put over [out]
    bool DrawSegment(const std::string& content, std::vector<uint8_t>& out, std::string& error)
    {
        if (content.find('<') == std::string::npos)
            return true;
        std::string text = Head + Defs + content + "</svg>";
        if (!Spend(text.size(), error))
            return false;
        SvgImage image;
        if (!Parse(std::move(text), image))
        {
            error = "nanosvg can't read it";
            return false;
        }
        std::vector<uint8_t> layer((size_t)Width * Height * 4);
        nsvgRasterize(Raster.Rasterizer, image.Image, 0, 0, 1, layer.data(), Width, Height, Width * 4);
        Premultiply(layer.data(), (size_t)Width * Height);
        Over(out, layer, 1.f);
        return true;
    }

    // [shape] (a rect) filled with [pattern]'s tile repeated, each tile clipped to its cell, as SVG draws a pattern. Another shape, or a rect
    // with a transform, is left out (true); false only when it's cancelled or over the budget
    bool DrawPattern(const Node& shape, const Node& pattern, std::vector<uint8_t>& out, std::string& error)
    {
        double sx, sy, sw, sh;
        if (shape.Name != "rect" || shape.Attr("transform") || !ShapeBox(shape, sx, sy, sw, sh))
            return true;
        bool userSpace = pattern.Attr("patternUnits") && *pattern.Attr("patternUnits") == "userSpaceOnUse";
        double px = LengthOr(pattern, "x", 0, 0), py = LengthOr(pattern, "y", 0, 0);
        double pw = LengthOr(pattern, "width", 0, 0), ph = LengthOr(pattern, "height", 0, 0);
        if (!userSpace)
        {
            // fractions (or percentages) of the shape's box
            auto fraction = [&](const char* attr, double fallback)
            {
                const std::string* text = pattern.Attr(attr);
                double v;
                return text && ParseLength(*text, 1.0, v) ? v : fallback;
            };
            px = sx + fraction("x", 0) * sw;
            py = sy + fraction("y", 0) * sh;
            pw = fraction("width", 0) * sw;
            ph = fraction("height", 0) * sh;
        }
        // the tile in bitmap pixels
        double tileW = pw * View.ScaleX, tileH = ph * View.ScaleY;
        if (tileW < 1 || tileH < 1)
            return true;
        // pixel x of user x, and back
        auto toX = [&](double ux) { return (ux - View.OffsetX) * View.ScaleX + View.ShiftX; };
        auto toY = [&](double uy) { return (uy - View.OffsetY) * View.ScaleY + View.ShiftY; };
        auto fromX = [&](double x) { return (x - View.ShiftX) / View.ScaleX + View.OffsetX; };
        auto fromY = [&](double y) { return (y - View.ShiftY) / View.ScaleY + View.OffsetY; };
        int clipX0 = std::max(0, RoundToInt((float)toX(sx))), clipY0 = std::max(0, RoundToInt((float)toY(sy)));
        int clipX1 = std::min(Width, RoundToInt((float)toX(sx + sw))), clipY1 = std::min(Height, RoundToInt((float)toY(sy + sh)));
        if (clipX1 <= clipX0 || clipY1 <= clipY0)
            return true;
        long long firstI = (long long)std::floor((fromX(clipX0) - px) / pw), lastI = (long long)std::floor((fromX(clipX1) - px) / pw);
        long long firstJ = (long long)std::floor((fromY(clipY0) - py) / ph), lastJ = (long long)std::floor((fromY(clipY1) - py) / ph);
        Tiles += (lastI - firstI + 1) * (lastJ - firstJ + 1);
        if (Tiles > MAX_TILES)
        {
            error = "its patterns need more than " + std::to_string(MAX_TILES) + " tiles";
            return false;
        }

        // the tile's content once, at the document's user-space origin (patternContentUnits objectBoundingBox scales it by the box)
        std::string content = "<g";
        const std::string* contentUnits = pattern.Attr("patternContentUnits");
        if (contentUnits && *contentUnits == "objectBoundingBox")
            content += " transform=\"matrix(" + Number(sw) + " 0 0 " + Number(sh) + " 0 0)\"";
        content += ">";
        for (const Node& child : pattern.Children)
            Write(child, content);
        content += "</g>";
        std::string text = Head + Defs + content + "</svg>";
        if (!Spend(text.size(), error))
            return false;
        SvgImage image;
        if (!Parse(std::move(text), image))
            return true;

        std::vector<uint8_t> layer((size_t)Width * Height * 4, 0);
        double originX = toX(0), originY = toY(0);
        for (long long j = firstJ; j <= lastJ; j++)
        {
            double cellY = toY(py + j * ph);
            int y0 = std::max(clipY0, RoundToInt((float)cellY)), y1 = std::min(clipY1, RoundToInt((float)(cellY + tileH)));
            for (long long i = firstI; i <= lastI && y1 > y0; i++)
            {
                if (Cancel && Cancel->load(std::memory_order_relaxed))
                {
                    error = "cancelled";
                    return false;
                }
                double cellX = toX(px + i * pw);
                int x0 = std::max(clipX0, RoundToInt((float)cellX)), x1 = std::min(clipX1, RoundToInt((float)(cellX + tileW)));
                if (x1 <= x0)
                    continue;
                // the content's origin lands on the cell's corner; the rasterizer's window is the cell (inside the shape)
                float tx = (float)(cellX - originX - x0), ty = (float)(cellY - originY - y0);
                nsvgRasterize(Raster.Rasterizer, image.Image, tx, ty, 1, &layer[((size_t)y0 * Width + x0) * 4], x1 - x0, y1 - y0, Width * 4);
            }
        }
        Premultiply(layer.data(), (size_t)Width * Height);
        Over(out, layer, (float)OpacityOf(shape));
        return true;
    }

    const Node& Root;
    const std::map<std::string, const Node*>& Ids;
    Viewport View;
    int Width, Height;
    const std::atomic<bool>* Cancel;
    std::string Head, Defs;
    SvgRasterizer Raster;
    // what's been spent: layers (each a parse of the defs, a full bitmap and a composite), the text parsed, the tiles drawn
    int Layers = 0;
    size_t ParsedBytes = 0;
    long long Tiles = 0;
};

}

bool LooksLikeSvg(const uint8_t* data, size_t size)
{
    size_t n = std::min(size, (size_t)SNIFF_BYTES);
    for (size_t i = 0; i + 4 <= n; i++)
    {
        if (data[i] == '<' && tolower(data[i + 1]) == 's' && tolower(data[i + 2]) == 'v' && tolower(data[i + 3]) == 'g')
            return true;
    }
    return false;
}

bool FitInside(std::optional<float> aspectRatio, int maxWidth, int maxHeight, int& width, int& height)
{
    if (maxWidth <= 0 || maxHeight <= 0)
        return false;
    if (!aspectRatio || !std::isfinite(*aspectRatio) || *aspectRatio <= 0)
    {
        width = maxWidth;
        height = maxHeight;
        return true;
    }
    if (maxWidth / (float)maxHeight > *aspectRatio)
    {
        // limited by the height
        width = std::min(std::max(1, RoundToInt(maxHeight * *aspectRatio)), maxWidth);
        height = maxHeight;
    }
    else
    {
        width = maxWidth;
        height = std::min(std::max(1, RoundToInt(maxWidth / *aspectRatio)), maxHeight);
    }
    return true;
}

void ClampToMaxSize(int& width, int& height, int maxSize)
{
    if (maxSize <= 0 || (width <= maxSize && height <= maxSize))
        return;
    float scale = maxSize / (float)std::max(width, height);
    width = std::min(std::max(1, RoundToInt(width * scale)), maxSize);
    height = std::min(std::max(1, RoundToInt(height * scale)), maxSize);
}

// the declared preserveAspectRatio a stretch mode keeps, as nanosvg applies it: the viewBox scaled evenly (meet: to fit, slice: to cover) and
// aligned (xMin, xMid, xMax; YMin, YMid, YMax; the middle by default)
void ApplyPreserveAspectRatio(const std::string& par, int width, int height, Viewport& view)
{
    if (par.find("none") != std::string::npos)
        return;
    double scale = par.find("slice") != std::string::npos ? std::max(view.ScaleX, view.ScaleY) : std::min(view.ScaleX, view.ScaleY);
    auto align = [](double content, double container, const std::string& par, const char* min, const char* max)
    {
        return par.find(min) != std::string::npos ? 0.0 : par.find(max) != std::string::npos ? container - content : (container - content) / 2;
    };
    view.ShiftX = align(view.BaseW * scale, width, par, "xMin", "xMax");
    view.ShiftY = align(view.BaseH * scale, height, par, "YMin", "YMax");
    view.ScaleX = view.ScaleY = scale;
}

bool RasterizeSvg(const std::string& svg, int surfaceWidth, int surfaceHeight, BackgroundMode mode, int maxSize, std::vector<uint8_t>& rgba,
    int& width, int& height, std::string& error, const std::atomic<bool>* cancel)
{
    if (svg.size() > MAX_SVG_BYTES)
    {
        error = "bigger than 16 MB";
        return false;
    }
    // a decode given up on stops between the stages (and inside the drawing, between its layers and tiles)
    auto cancelled = [&]
    {
        if (cancel && cancel->load(std::memory_order_relaxed))
        {
            error = "cancelled";
            return true;
        }
        return false;
    };
    Node document;
    if (!XmlReader(svg).Read(document, error) || cancelled())
        return false;
    if (document.Name != "svg")
    {
        error = "no <svg> root";
        return false;
    }

    // SvgBackground's shape: the viewBox's, else an absolute width and height (which then make the viewBox), else none, and the bitmap's
    // size from it (SvgRasterSize.calculate)
    double box[4];
    bool hasViewBox = ParseViewBox(document.Attr("viewBox"), box);
    std::optional<float> aspect;
    if (hasViewBox)
        aspect = (float)(box[2] / box[3]);
    else
    {
        double w, h;
        const std::string* wt = document.Attr("width");
        const std::string* ht = document.Attr("height");
        if (wt && ht && wt->find('%') == std::string::npos && ht->find('%') == std::string::npos && ParseLength(*wt, 0, w) && ParseLength(*ht, 0, h)
            && w > 0 && h > 0)
        {
            box[0] = box[1] = 0;
            box[2] = w;
            box[3] = h;
            hasViewBox = true;
            aspect = (float)(w / h);
        }
    }
    if (mode == BackgroundMode::Stretch)
    {
        width = surfaceWidth;
        height = surfaceHeight;
        if (width <= 0 || height <= 0)
        {
            error = "an empty surface";
            return false;
        }
    }
    else if (!FitInside(aspect, surfaceWidth, surfaceHeight, width, height))
    {
        error = "an empty surface";
        return false;
    }
    ClampToMaxSize(width, height, maxSize);

    // the root fills the bitmap: its viewBox stretched to it (the fit modes' bitmap has its shape already) unless the stretch mode keeps the
    // document's own preserveAspectRatio; without a viewBox, a user unit is a pixel and percentages are of the bitmap
    Viewport view;
    if (hasViewBox)
    {
        view.BaseW = box[2];
        view.BaseH = box[3];
        view.ScaleX = width / box[2];
        view.ScaleY = height / box[3];
        view.OffsetX = box[0];
        view.OffsetY = box[1];
    }
    else
    {
        view.BaseW = width;
        view.BaseH = height;
        box[0] = box[1] = 0;
        box[2] = width;
        box[3] = height;
    }
    document.Set("width", std::to_string(width));
    document.Set("height", std::to_string(height));
    document.Set("viewBox", Number(box[0]) + " " + Number(box[1]) + " " + Number(box[2]) + " " + Number(box[3]));
    if (!(mode == BackgroundMode::Stretch && document.Attr("preserveAspectRatio")))
        document.Set("preserveAspectRatio", "none");
    else if (hasViewBox)
        ApplyPreserveAspectRatio(*document.Attr("preserveAspectRatio"), width, height, view);

    ResolvePercentages(document, view);
    FlattenNestedSvg(document, 0);
    std::map<std::string, const Node*> sourceIds;
    Index(document, sourceIds);
    size_t nodes = 0;
    Node expanded = Expand(document, sourceIds, 0, nodes);
    if (nodes > MAX_NODES)
    {
        error = "too many elements";
        return false;
    }
    if (cancelled())
        return false;
    std::string extraDefs;
    {
        std::map<std::string, const Node*> ids;
        Index(expanded, ids);
        GradientCopies copies(ids);
        copies.Apply(expanded);
        copies.Write(extraDefs);
    }
    if (cancelled())
        return false;
    std::map<std::string, const Node*> ids;
    Index(expanded, ids);
    Composer composer(expanded, ids, view, width, height, cancel);
    return composer.Draw(extraDefs, rgba, error);
}

}
