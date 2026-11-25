// The options a script declares in its header and how their stored values become melon.options, ported from the app's
// domain/model/emulator LuaScriptOption.kt (with resolveValues and sectionHeadingBefore), LuaScriptOptionType.kt, LuaScriptOptionValue.kt,
// LuaScriptHeader.kt and LuaLayoutChoice.kt. Kotlin's sealed class is one struct here, its [Kind] saying which of the four it is and which
// fields count. Strings are UTF-8
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "layout/LayoutModel.h"

namespace Frontend
{

// How a script sees an option's value in melon.options; the values are the kOptionType constants in LuaScriptingJNI.cpp
enum class LuaScriptOptionType
{
    Boolean = 0,
    Integer = 1,
    Number = 2,
    String = 3,
    // a layout option, seen by scripts as a boolean: 1 if the player picked a layout, 0 for "Don't change"
    Layout = 4,
};

// One value of melon.options, typed the way the script sees it; [Text] is the choice's UTF-8 bytes
struct LuaScriptOptionValue
{
    std::string Key;
    LuaScriptOptionType OptionType = LuaScriptOptionType::Boolean;
    int64_t Integer = 0;
    double Number = 0;
    std::string Text;

    // the type as native reads it
    int Type() const { return (int)OptionType; }
};

// An option a script declares in its header (see LuaScriptHeader), stored as text in the canonical form Resolve gives
struct LuaScriptOption
{
    enum class Kind
    {
        Toggle,
        // a slider from Min to the last step that fits before Max, in steps of Step, rounded to the header's Decimals
        Number,
        Choice,
        // a layout UUID, or empty for "Don't change"; whether the layout exists is checked only when the script asks
        Layout,
    };

    static constexpr const char* DONT_CHANGE = "";

    Kind OptionKind = Kind::Toggle;
    std::string Key;
    std::string Label;
    // the heading of the section: line this option comes after, or none
    std::optional<std::string> Section;

    bool ToggleDefault = false;

    double Min = 0;
    double Max = 0;
    double Step = 0;
    double NumberDefault = 0;
    int Decimals = 0;
    // how many steps the slider has
    int StepCount = 0;

    std::vector<std::string> Values;
    std::string ChoiceDefault;

    static LuaScriptOption Toggle(std::string key, std::string label, bool defaultValue, std::optional<std::string> section = std::nullopt);
    static LuaScriptOption Number(std::string key, std::string label, double min, double max, double step, double defaultValue, int decimals,
        std::optional<std::string> section = std::nullopt);
    static LuaScriptOption Choice(std::string key, std::string label, std::vector<std::string> values, std::string defaultValue,
        std::optional<std::string> section = std::nullopt);
    static LuaScriptOption Layout(std::string key, std::string label, std::optional<std::string> section = std::nullopt);

    // [stored] as canonical text if it's valid for this option (a number is moved onto the slider's steps), otherwise the default
    std::string Resolve(const std::optional<std::string>& stored) const;
    // the value as the script sees it, for a value Resolve gave
    LuaScriptOptionValue ToNativeValue(const std::string& value) const;

    // Number only
    double ValueAt(int stepIndex) const;
    // the step closest to [value], or none if it's off the slider
    std::optional<int> StepOf(double value) const;
    std::string Format(double value) const;
    // whole steps from [min] that fit before [max], or 0 if none fits
    static int GetStepCount(double min, double max, double step);

    // Layout only: the layout a value names in canonical (lowercase) form, or none for "Don't change" (or anything that isn't a UUID)
    static std::optional<std::string> GetLayoutId(const std::optional<std::string>& value);

    // as the data classes compare (StepCount follows from the rest)
    bool operator==(const LuaScriptOption& other) const = default;
};

// every declared option's value for the script, from [stored] or the default, dropping undeclared keys
std::map<std::string, std::string> ResolveValues(const std::vector<LuaScriptOption>& options, const std::map<std::string, std::string>& stored);

// the section heading to show before the option at [index], or none when it's in the same section as the one before it (or in none)
std::optional<std::string> SectionHeadingBefore(const std::vector<LuaScriptOption>& options, int index);

// What a script says about itself in its leading comment block, read by LuaScriptHeaderParser: its description and the options that could
// be read, in the order they're declared, with how many option lines broke the rules and were skipped
struct LuaScriptHeader
{
    std::optional<std::string> Description;
    std::vector<LuaScriptOption> Options;
    int UnreadableOptionCount = 0;
};

// A layout offered by a layout option: its name, or none, and a number that tells unnamed layouts apart (1, 2, ...), 0 when named or the
// only unnamed one
struct LuaLayoutChoice
{
    std::string Id;
    std::optional<std::string> Name;
    int UnnamedNumber = 0;

    bool operator==(const LuaLayoutChoice& other) const = default;
};

// layouts in list order (those with an id), numbering unnamed ones when there are several
std::vector<LuaLayoutChoice> ToLuaLayoutChoices(const std::vector<LayoutConfiguration>& layouts);

}
