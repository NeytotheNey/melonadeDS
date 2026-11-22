#include "layout/LayoutModel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "Settings.h"

namespace Frontend
{

namespace
{

// what a layouts file may hold (rule 2): a phone's has a handful of layouts with a few variants of about twenty components
constexpr size_t MAX_LAYOUTS = 1000;
constexpr size_t MAX_VARIANTS = 1000;
constexpr size_t MAX_COMPONENTS = 256;
constexpr size_t MAX_FOLDS = 16;
constexpr double MAX_COORDINATE = 1e7;

const char* const kModeNames[] = {"STRETCH", "FIT_CENTER", "FIT_TOP", "FIT_LEFT", "FIT_BOTTOM", "FIT_RIGHT"};
const char* const kWideAspects[] = {"16:10", "16:9", "20:9", "21:9"};

using Object = picojson::object;

// Reads one layout's objects as the app's Gson DTOs require them: [Ok] goes false (with the first [Error]) when a field Kotlin declares
// non-null is missing or of the wrong type. Primitive fields (Int, Boolean) Gson leaves at 0 or false when they're missing
struct Reader
{
    bool Ok = true;
    std::string Error;

    void Fail(const std::string& what)
    {
        if (Ok)
            Error = what;
        Ok = false;
    }

    // the field's value, or null when it's missing or JSON null (which Gson reads as missing)
    static const picojson::value* Field(const Object& o, const char* key)
    {
        auto it = o.find(key);
        return it == o.end() || it->second.is<picojson::null>() ? nullptr : &it->second;
    }

    const Object* Obj(const Object& o, const char* key, bool required)
    {
        const picojson::value* v = Field(o, key);
        if (v && v->is<Object>())
            return &v->get<Object>();
        if (v || required)
            Fail(std::string(key) + " isn't an object");
        return nullptr;
    }

    const picojson::array* Array(const Object& o, const char* key, bool required, size_t limit)
    {
        const picojson::value* v = Field(o, key);
        if (v && v->is<picojson::array>())
        {
            if (v->get<picojson::array>().size() > limit)
            {
                Fail(std::string(key) + " has more than " + std::to_string(limit) + " items");
                return nullptr;
            }
            return &v->get<picojson::array>();
        }
        if (v || required)
            Fail(std::string(key) + " isn't a list");
        return nullptr;
    }

    std::optional<std::string> String(const Object& o, const char* key, bool required)
    {
        const picojson::value* v = Field(o, key);
        if (v && v->is<std::string>())
            return v->get<std::string>();
        if (v || required)
            Fail(std::string(key) + " isn't a string");
        return std::nullopt;
    }

    int Int(const Object& o, const char* key)
    {
        const picojson::value* v = Field(o, key);
        if (!v)
            return 0;
        // Gson takes a whole number written as a double (1.0) too
        double d = v->is<double>() ? v->get<double>() : NAN;
        if (!std::isfinite(d) || d != std::floor(d) || std::fabs(d) > MAX_COORDINATE)
        {
            Fail(std::string(key) + " isn't a whole number");
            return 0;
        }
        return (int)d;
    }

    std::optional<float> Float(const Object& o, const char* key)
    {
        const picojson::value* v = Field(o, key);
        if (!v)
            return std::nullopt;
        if (!v->is<double>())
        {
            Fail(std::string(key) + " isn't a number");
            return std::nullopt;
        }
        return (float)v->get<double>();
    }

    std::optional<bool> Bool(const Object& o, const char* key)
    {
        const picojson::value* v = Field(o, key);
        if (!v)
            return std::nullopt;
        if (!v->is<bool>())
        {
            Fail(std::string(key) + " isn't true or false");
            return std::nullopt;
        }
        return v->get<bool>();
    }
};

// the keys of [o] that aren't [known], kept to be written back
Object Unknown(const Object& o, std::initializer_list<const char*> known)
{
    Object extra;
    for (const auto& [key, value] : o)
    {
        bool isKnown = false;
        for (const char* k : known)
            isKnown = isKnown || key == k;
        if (!isKnown)
            extra[key] = value;
    }
    return extra;
}

IntRect ReadRect(Reader& r, const Object& o, Object& extra)
{
    extra = Unknown(o, {"x", "y", "width", "height"});
    return {r.Int(o, "x"), r.Int(o, "y"), r.Int(o, "width"), r.Int(o, "height")};
}

bool ReadDisplay(Reader& r, const Object& o, LayoutDisplay& d)
{
    d.Id = r.Int(o, "id");
    d.Type = r.String(o, "type", true).value_or("");
    d.W = r.Int(o, "width");
    d.H = r.Int(o, "height");
    d.Extra = Unknown(o, {"id", "type", "width", "height"});
    return r.Ok;
}

bool ReadVariant(Reader& r, const Object& o, LayoutVariant& v)
{
    v.Extra = Unknown(o, {"uiSize", "insets", "orientation", "folds", "displays"});
    if (const Object* size = r.Obj(o, "uiSize", true))
    {
        v.W = r.Int(*size, "x");
        v.H = r.Int(*size, "y");
        v.SizeExtra = Unknown(*size, {"x", "y"});
    }
    if (const Object* insets = r.Obj(o, "insets", true))
    {
        v.InsetLeft = r.Int(*insets, "left");
        v.InsetTop = r.Int(*insets, "top");
        v.InsetRight = r.Int(*insets, "right");
        v.InsetBottom = r.Int(*insets, "bottom");
        v.InsetsExtra = Unknown(*insets, {"left", "top", "right", "bottom"});
    }
    v.Orientation = r.String(o, "orientation", true).value_or("");
    if (const picojson::array* folds = r.Array(o, "folds", true, MAX_FOLDS))
    {
        for (const picojson::value& item : *folds)
        {
            if (!item.is<Object>())
            {
                r.Fail("a fold isn't an object");
                break;
            }
            const Object& fo = item.get<Object>();
            ScreenFold fold;
            fold.Orientation = r.String(fo, "orientation", true).value_or("");
            fold.Type = r.String(fo, "type", true).value_or("");
            if (const Object* bounds = r.Obj(fo, "foldBounds", true))
                fold.Bounds = ReadRect(r, *bounds, fold.BoundsExtra);
            fold.Extra = Unknown(fo, {"orientation", "type", "foldBounds"});
            v.Folds.push_back(std::move(fold));
        }
    }
    if (const Object* displays = r.Obj(o, "displays", true))
    {
        v.DisplaysExtra = Unknown(*displays, {"mainScreenDisplay", "secondaryScreenDisplay"});
        if (const Object* main = r.Obj(*displays, "mainScreenDisplay", true))
            ReadDisplay(r, *main, v.MainDisplay);
        if (const Object* second = r.Obj(*displays, "secondaryScreenDisplay", false))
        {
            LayoutDisplay d;
            ReadDisplay(r, *second, d);
            v.SecondDisplay = d;
        }
    }
    return r.Ok;
}

bool ReadScreenLayout(Reader& r, const Object& o, ScreenLayout& s)
{
    s.Extra = Unknown(o, {"backgroundId", "backgroundMode", "components"});
    s.BackgroundId = r.String(o, "backgroundId", false);
    s.Mode = r.String(o, "backgroundMode", true).value_or("FIT_CENTER");
    if (const picojson::array* components = r.Array(o, "components", false, MAX_COMPONENTS))
    {
        s.Components.emplace();
        for (const picojson::value& item : *components)
        {
            if (!item.is<Object>())
            {
                r.Fail("a component isn't an object");
                break;
            }
            const Object& co = item.get<Object>();
            LayoutComponent c;
            if (const Object* rect = r.Obj(co, "rect", true))
                c.Rect = ReadRect(r, *rect, c.RectExtra);
            c.Component = r.String(co, "component", true).value_or("");
            // as PositionedLayoutComponentDto.toModel: no alpha is 1, no onTop false, a spacing that isn't finite 0, else clamped to 0-1
            c.Alpha = r.Float(co, "alpha").value_or(1.f);
            c.OnTop = r.Bool(co, "onTop").value_or(false);
            float spacing = r.Float(co, "spacing").value_or(0.f);
            c.Spacing = std::isfinite(spacing) ? std::clamp(spacing, 0.f, 1.f) : 0.f;
            // the app reads the name in any case and writes the enum's
            for (char& ch : c.Component)
                ch = (char)std::toupper((unsigned char)ch);
            c.Extra = Unknown(co, {"rect", "component", "alpha", "onTop", "spacing"});
            s.Components->push_back(std::move(c));
        }
    }
    return r.Ok;
}

bool ReadUiLayout(Reader& r, const Object& o, UILayout& l)
{
    l.Extra = Unknown(o, {"mainScreenLayoutDto", "secondaryScreenLayoutDto", "wideAspect"});
    if (const Object* main = r.Obj(o, "mainScreenLayoutDto", true))
        ReadScreenLayout(r, *main, l.Main);
    if (const Object* second = r.Obj(o, "secondaryScreenLayoutDto", true))
        ReadScreenLayout(r, *second, l.Secondary);
    l.WideAspect = r.String(o, "wideAspect", false);
    return r.Ok;
}

void ReadEntries(Reader& r, const picojson::array& items, std::vector<LayoutEntry>& out)
{
    for (const picojson::value& item : items)
    {
        if (!item.is<Object>())
        {
            r.Fail("a variant isn't an object");
            return;
        }
        const Object& eo = item.get<Object>();
        LayoutEntry e;
        e.Extra = Unknown(eo, {"variant", "layout"});
        if (const Object* variant = r.Obj(eo, "variant", true))
            ReadVariant(r, *variant, e.Variant);
        if (const Object* layout = r.Obj(eo, "layout", true))
            ReadUiLayout(r, *layout, e.Layout);
        if (!r.Ok)
            return;
        out.push_back(std::move(e));
    }
}

// the detached pair, which the app never reads: one that doesn't parse stays in Extra as it was
std::optional<DetachedLayouts> ReadDetached(const Object& o)
{
    Reader r;
    DetachedLayouts d;
    d.Extra = Unknown(o, {"top", "bottom"});
    if (const picojson::array* top = r.Array(o, "top", false, MAX_VARIANTS))
        ReadEntries(r, *top, d.Top);
    if (const picojson::array* bottom = r.Array(o, "bottom", false, MAX_VARIANTS))
        ReadEntries(r, *bottom, d.Bottom);
    if (!r.Ok)
        return std::nullopt;
    return d;
}

bool ReadConfiguration(Reader& r, const Object& o, LayoutConfiguration& c)
{
    c.Extra = Unknown(o, {"id", "name", "type", "orientation", "useCustomOpacity", "opacity", "layoutVariants", "wideAspect", "detached"});
    c.Id = r.String(o, "id", false);
    c.Name = r.String(o, "name", false);
    c.Type = r.String(o, "type", true).value_or("");
    c.Orientation = r.String(o, "orientation", true).value_or("");
    c.UseCustomOpacity = r.Bool(o, "useCustomOpacity").value_or(false);
    c.Opacity = r.Int(o, "opacity");
    if (const picojson::array* variants = r.Array(o, "layoutVariants", true, MAX_VARIANTS))
        ReadEntries(r, *variants, c.Variants);
    std::optional<std::string> legacyWide = r.String(o, "wideAspect", false);
    if (const picojson::value* detached = Reader::Field(o, "detached"))
    {
        if (detached->is<Object>())
            c.Detached = ReadDetached(detached->get<Object>());
        if (!c.Detached)
            c.Extra["detached"] = *detached;
    }
    if (!r.Ok)
        return false;

    // the single ratio older layouts saved, copied to their variants that have screens placed (background-only ones keep coming from the
    // default layout), unless a variant has its own
    if (legacyWide && IsKnownWideAspect(*legacyWide))
    {
        for (LayoutEntry& e : c.Variants)
        {
            bool hasComponents = e.Layout.Main.Components || e.Layout.Secondary.Components;
            if (!e.Layout.WideAspect && hasComponents)
                e.Layout.WideAspect = legacyWide;
        }
    }
    return true;
}

// Writing: objects with their keys sorted (as the app's settings backup has them), strings escaped as Gson's default writer does (HTML
// characters too), floats as Kotlin prints them (1.0, 0.33)

std::string Quote(const std::string& text)
{
    std::string out = "\"";
    for (unsigned char c : text)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '<': case '>': case '&': case '=': case '\'':
        {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
            break;
        }
        default:
            if (c < 0x20)
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else
                out += (char)c;
        }
    }
    return out + "\"";
}

// the shortest text that reads back as [value], with a ".0" on a whole number, as Java's Float.toString
std::string FloatText(float value)
{
    if (!std::isfinite(value))
        return "0.0";
    char buf[32];
    for (int precision = 1; precision <= 9; precision++)
    {
        snprintf(buf, sizeof(buf), "%.*g", precision, (double)value);
        if (strtof(buf, nullptr) == value)
            break;
    }
    std::string text = buf;
    if (text.find_first_of(".eE") == std::string::npos)
        text += ".0";
    return text;
}

class JsonObject
{
public:
    JsonObject& Str(const char* key, const std::string& value) { Fields[key] = Quote(value); return *this; }
    JsonObject& Str(const char* key, const std::optional<std::string>& value)
    {
        if (value)
            Fields[key] = Quote(*value);
        return *this;
    }
    JsonObject& Int(const char* key, int value) { Fields[key] = std::to_string(value); return *this; }
    JsonObject& Float(const char* key, float value) { Fields[key] = FloatText(value); return *this; }
    JsonObject& Bool(const char* key, bool value) { Fields[key] = value ? "true" : "false"; return *this; }
    JsonObject& Raw(const char* key, std::string json) { Fields[key] = std::move(json); return *this; }
    // the unknown fields, where a known one of the same name isn't written already
    JsonObject& Extra(const Object& extra)
    {
        for (const auto& [key, value] : extra)
        {
            if (!Fields.count(key))
                Fields[key] = value.serialize();
        }
        return *this;
    }
    std::string Text() const
    {
        std::string out = "{";
        for (const auto& [key, value] : Fields)
        {
            if (out.size() > 1)
                out += ",";
            out += Quote(key) + ":" + value;
        }
        return out + "}";
    }

private:
    std::map<std::string, std::string> Fields;
};

std::string RectJson(const IntRect& r, const Object& extra)
{
    return JsonObject().Int("x", r.X).Int("y", r.Y).Int("width", r.W).Int("height", r.H).Extra(extra).Text();
}

std::string DisplayJson(const LayoutDisplay& d)
{
    return JsonObject().Int("id", d.Id).Str("type", d.Type).Int("width", d.W).Int("height", d.H).Extra(d.Extra).Text();
}

std::string VariantJson(const LayoutVariant& v)
{
    std::string folds = "[";
    for (const ScreenFold& f : v.Folds)
    {
        if (folds.size() > 1)
            folds += ",";
        folds += JsonObject().Str("orientation", f.Orientation).Str("type", f.Type).Raw("foldBounds", RectJson(f.Bounds, f.BoundsExtra))
            .Extra(f.Extra).Text();
    }
    folds += "]";
    JsonObject displays;
    displays.Raw("mainScreenDisplay", DisplayJson(v.MainDisplay));
    if (v.SecondDisplay)
        displays.Raw("secondaryScreenDisplay", DisplayJson(*v.SecondDisplay));
    displays.Extra(v.DisplaysExtra);
    return JsonObject()
        .Raw("uiSize", JsonObject().Int("x", v.W).Int("y", v.H).Extra(v.SizeExtra).Text())
        .Raw("insets", JsonObject().Int("left", v.InsetLeft).Int("top", v.InsetTop).Int("right", v.InsetRight).Int("bottom", v.InsetBottom)
            .Extra(v.InsetsExtra).Text())
        .Str("orientation", v.Orientation)
        .Raw("folds", folds)
        .Raw("displays", displays.Text())
        .Extra(v.Extra)
        .Text();
}

std::string ScreenLayoutJson(const ScreenLayout& s)
{
    JsonObject o;
    o.Str("backgroundId", s.BackgroundId).Str("backgroundMode", s.Mode);
    if (s.Components)
    {
        std::string list = "[";
        for (const LayoutComponent& c : *s.Components)
        {
            if (list.size() > 1)
                list += ",";
            list += JsonObject().Raw("rect", RectJson(c.Rect, c.RectExtra)).Str("component", c.Component).Float("alpha", c.Alpha)
                .Bool("onTop", c.OnTop).Float("spacing", c.Spacing).Extra(c.Extra).Text();
        }
        o.Raw("components", list + "]");
    }
    return o.Extra(s.Extra).Text();
}

std::string EntriesJson(const std::vector<LayoutEntry>& entries)
{
    std::string list = "[";
    for (const LayoutEntry& e : entries)
    {
        if (list.size() > 1)
            list += ",";
        std::string layout = JsonObject().Raw("mainScreenLayoutDto", ScreenLayoutJson(e.Layout.Main))
            .Raw("secondaryScreenLayoutDto", ScreenLayoutJson(e.Layout.Secondary)).Str("wideAspect", e.Layout.WideAspect)
            .Extra(e.Layout.Extra).Text();
        list += JsonObject().Raw("variant", VariantJson(e.Variant)).Raw("layout", layout).Extra(e.Extra).Text();
    }
    return list + "]";
}

}

const char* BackgroundModeName(BackgroundMode mode)
{
    return kModeNames[std::clamp((int)mode, 0, 5)];
}

std::optional<BackgroundMode> ParseBackgroundMode(const std::string& name)
{
    std::string upper = name;
    for (char& c : upper)
        c = c == '-' ? '_' : (char)std::toupper((unsigned char)c);
    for (int i = 0; i < 6; i++)
    {
        if (upper == kModeNames[i])
            return (BackgroundMode)i;
    }
    return std::nullopt;
}

const LayoutComponent* ScreenLayout::Find(const char* name) const
{
    if (!Components)
        return nullptr;
    for (const LayoutComponent& c : *Components)
    {
        if (c.Component == name)
            return &c;
    }
    return nullptr;
}

bool LayoutVariant::operator==(const LayoutVariant& o) const
{
    return W == o.W && H == o.H && InsetLeft == o.InsetLeft && InsetTop == o.InsetTop && InsetRight == o.InsetRight && InsetBottom == o.InsetBottom
        && Orientation == o.Orientation && Folds == o.Folds && MainDisplay == o.MainDisplay && SecondDisplay == o.SecondDisplay;
}

bool IsKnownWideAspect(const std::string& key)
{
    for (const char* k : kWideAspects)
    {
        if (key == k)
            return true;
    }
    return false;
}

bool LayoutConfiguration::IsWide() const
{
    for (const LayoutEntry& e : Variants)
    {
        if (e.Layout.WideAspect && IsKnownWideAspect(*e.Layout.WideAspect))
            return true;
    }
    return false;
}

bool ParseLayouts(const std::string& json, std::vector<LayoutConfiguration>& out, std::string& error)
{
    out.clear();
    picojson::value root;
    std::string parseError = ParseJson(json, root);
    if (!parseError.empty())
    {
        error = parseError;
        return false;
    }
    // Gson reads "null" as no list at all, which the app takes as no layouts
    if (root.is<picojson::null>())
        return true;
    if (!root.is<picojson::array>())
    {
        error = "not a list of layouts";
        return false;
    }
    const picojson::array& items = root.get<picojson::array>();
    if (items.size() > MAX_LAYOUTS)
    {
        error = "more than " + std::to_string(MAX_LAYOUTS) + " layouts";
        return false;
    }
    bool ok = true;
    for (const picojson::value& item : items)
    {
        LayoutConfiguration c;
        Reader r;
        if (!item.is<Object>())
            r.Fail("a layout isn't an object");
        else
            ReadConfiguration(r, item.get<Object>(), c);
        if (!r.Ok)
        {
            if (ok)
                error = "layout " + std::to_string(out.size() + 1) + ": " + r.Error;
            ok = false;
            c = LayoutConfiguration();
            c.Unreadable = true;
            c.Raw = item;
            if (item.is<Object>())
            {
                const Object& o = item.get<Object>();
                if (const picojson::value* name = Reader::Field(o, "name"); name && name->is<std::string>())
                    c.Name = name->get<std::string>();
            }
        }
        out.push_back(std::move(c));
    }
    return ok;
}

std::string WriteLayouts(const std::vector<LayoutConfiguration>& layouts)
{
    std::string out = "[";
    for (const LayoutConfiguration& c : layouts)
    {
        if (out.size() > 1)
            out += ",";
        if (c.Unreadable)
        {
            out += c.Raw.serialize();
            continue;
        }
        JsonObject o;
        o.Str("id", c.Id).Str("name", c.Name).Str("type", c.Type).Str("orientation", c.Orientation).Bool("useCustomOpacity", c.UseCustomOpacity)
            .Int("opacity", c.Opacity).Raw("layoutVariants", EntriesJson(c.Variants));
        if (c.Detached)
        {
            o.Raw("detached", JsonObject().Raw("top", EntriesJson(c.Detached->Top)).Raw("bottom", EntriesJson(c.Detached->Bottom))
                .Extra(c.Detached->Extra).Text());
        }
        out += o.Extra(c.Extra).Text();
    }
    return out + "]";
}

}
