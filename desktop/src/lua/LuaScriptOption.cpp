#include "lua/LuaScriptOption.h"

#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdio>

#include "lua/LuaText.h"

namespace Frontend
{

namespace
{

// a little room for float noise at both ends of a slider
constexpr double STEP_TOLERANCE = 1e-6;
constexpr double MAX_EXACT_INTEGER = 9007199254740992.0;
// a double's exact decimal expansion has at most 1074 digits after the point
constexpr int EXACT_FRACTION_DIGITS = 1100;
constexpr int MAX_DECIMALS = 64;

// BigDecimal(value).setScale(decimals, HALF_UP).toPlainString(): the double's exact value rounded half away from zero to [decimals] digits
// after the point, with no sign on zero
std::string RoundHalfUp(double value, int decimals)
{
    decimals = std::clamp(decimals, 0, MAX_DECIMALS);
    // BigDecimal can't hold these, and no option makes them
    if (!std::isfinite(value))
        return "0";
    char buffer[EXACT_FRACTION_DIGITS + 400];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, EXACT_FRACTION_DIGITS);
    if (result.ec != std::errc())
        return "0";
    std::string_view exact(buffer, result.ptr - buffer);
    bool isNegative = !exact.empty() && exact.front() == '-';
    if (isNegative)
        exact.remove_prefix(1);
    size_t point = exact.find('.');
    std::string digits(exact.substr(0, point));
    std::string_view fraction = point == std::string_view::npos ? std::string_view() : exact.substr(point + 1);
    std::string kept(fraction.substr(0, decimals));
    kept.resize(decimals, '0');
    digits += kept;

    if ((int)fraction.size() > decimals && fraction[decimals] >= '5')
    {
        int i = (int)digits.size() - 1;
        while (i >= 0 && digits[i] == '9')
            digits[i--] = '0';
        if (i < 0)
            digits.insert(digits.begin(), '1');
        else
            digits[i]++;
    }

    bool isZero = digits.find_first_not_of('0') == std::string::npos;
    std::string text = isNegative && !isZero ? "-" : "";
    text += digits.substr(0, digits.size() - decimals);
    if (decimals > 0)
        text += "." + digits.substr(digits.size() - decimals);
    return text;
}

double ParseRounded(const std::string& text)
{
    double value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

// Long.parseLong(text, 16) for 1 to 16 hex digits without a sign, as UUID.fromString takes each part
std::optional<uint64_t> ParseHexPart(std::string_view text)
{
    if (text.empty() || text.size() > 16)
        return std::nullopt;
    uint64_t value = 0;
    for (char c : text)
    {
        int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0)
            return std::nullopt;
        value = (value << 4) | (uint64_t)digit;
    }
    // a long's range
    if (value > (uint64_t)INT64_MAX)
        return std::nullopt;
    return value;
}

}

LuaScriptOption LuaScriptOption::Toggle(std::string key, std::string label, bool defaultValue, std::optional<std::string> section)
{
    LuaScriptOption option;
    option.OptionKind = Kind::Toggle;
    option.Key = std::move(key);
    option.Label = std::move(label);
    option.Section = std::move(section);
    option.ToggleDefault = defaultValue;
    return option;
}

LuaScriptOption LuaScriptOption::Number(std::string key, std::string label, double min, double max, double step, double defaultValue,
    int decimals, std::optional<std::string> section)
{
    LuaScriptOption option;
    option.OptionKind = Kind::Number;
    option.Key = std::move(key);
    option.Label = std::move(label);
    option.Section = std::move(section);
    option.Min = min;
    option.Max = max;
    option.Step = step;
    option.NumberDefault = defaultValue;
    option.Decimals = decimals;
    option.StepCount = GetStepCount(min, max, step);
    return option;
}

LuaScriptOption LuaScriptOption::Choice(std::string key, std::string label, std::vector<std::string> values, std::string defaultValue,
    std::optional<std::string> section)
{
    LuaScriptOption option;
    option.OptionKind = Kind::Choice;
    option.Key = std::move(key);
    option.Label = std::move(label);
    option.Section = std::move(section);
    option.Values = std::move(values);
    option.ChoiceDefault = std::move(defaultValue);
    return option;
}

LuaScriptOption LuaScriptOption::Layout(std::string key, std::string label, std::optional<std::string> section)
{
    LuaScriptOption option;
    option.OptionKind = Kind::Layout;
    option.Key = std::move(key);
    option.Label = std::move(label);
    option.Section = std::move(section);
    return option;
}

std::string LuaScriptOption::Resolve(const std::optional<std::string>& stored) const
{
    switch (OptionKind)
    {
    case Kind::Toggle:
        if (stored == "true" || stored == "false")
            return *stored;
        return ToggleDefault ? "true" : "false";
    case Kind::Number:
    {
        std::optional<int> step;
        if (stored)
        {
            if (auto number = LuaText::ParseDouble(*stored))
                step = StepOf(*number);
        }
        if (!step)
            step = StepOf(NumberDefault);
        return Format(ValueAt(step.value_or(0)));
    }
    case Kind::Choice:
        if (stored && std::find(Values.begin(), Values.end(), *stored) != Values.end())
            return *stored;
        return ChoiceDefault;
    case Kind::Layout:
        return GetLayoutId(stored).value_or(DONT_CHANGE);
    }
    return "";
}

LuaScriptOptionValue LuaScriptOption::ToNativeValue(const std::string& value) const
{
    LuaScriptOptionValue native;
    native.Key = Key;
    switch (OptionKind)
    {
    case Kind::Toggle:
        native.OptionType = LuaScriptOptionType::Boolean;
        native.Integer = value == "true" ? 1 : 0;
        break;
    case Kind::Number:
    {
        // Resolve's values always parse
        double number = LuaText::ParseDouble(value).value_or(0);
        // Whole numbers are Lua integers, so scripts can use them as indices and in string.format("%d")
        if (number == std::floor(number) && std::fabs(number) < MAX_EXACT_INTEGER)
        {
            native.OptionType = LuaScriptOptionType::Integer;
            native.Integer = (int64_t)number;
        }
        else
        {
            native.OptionType = LuaScriptOptionType::Number;
            native.Number = number;
        }
        break;
    }
    case Kind::Choice:
        native.OptionType = LuaScriptOptionType::String;
        native.Text = value;
        break;
    case Kind::Layout:
        // The script only learns whether a layout was picked: the UUID means nothing to it
        native.OptionType = LuaScriptOptionType::Layout;
        native.Integer = GetLayoutId(value) ? 1 : 0;
        break;
    }
    return native;
}

double LuaScriptOption::ValueAt(int stepIndex) const
{
    return ParseRounded(RoundHalfUp(Min + std::clamp(stepIndex, 0, StepCount) * Step, Decimals));
}

std::optional<int> LuaScriptOption::StepOf(double value) const
{
    if (!std::isfinite(value))
        return std::nullopt;
    double position = (value - Min) / Step;
    if (position < -STEP_TOLERANCE || position > StepCount + STEP_TOLERANCE)
        return std::nullopt;
    // Math.round: half rounds up
    double rounded = std::floor(position);
    if (position - rounded >= 0.5)
        rounded += 1;
    return std::clamp((int)rounded, 0, StepCount);
}

std::string LuaScriptOption::Format(double value) const
{
    // stripTrailingZeros().toPlainString(); RoundHalfUp already gives "0" rather than "-0"
    std::string text = RoundHalfUp(value, Decimals);
    if (text.find('.') != std::string::npos)
    {
        text.erase(text.find_last_not_of('0') + 1);
        if (text.back() == '.')
            text.pop_back();
    }
    return text;
}

int LuaScriptOption::GetStepCount(double min, double max, double step)
{
    if (!(step > 0) || !(max > min))
        return 0;
    double steps = std::floor((max - min) / step + STEP_TOLERANCE);
    return steps > INT_MAX ? INT_MAX : (int)steps;
}

std::optional<std::string> LuaScriptOption::GetLayoutId(const std::optional<std::string>& value)
{
    // UUID.fromString: five parts of hex digits split by dashes, each cut to its field's width, at most 36 characters
    if (!value || value->empty() || value->size() > 36)
        return std::nullopt;
    std::string_view text = *value;
    static constexpr uint64_t MASKS[] = {0xFFFFFFFFull, 0xFFFFull, 0xFFFFull, 0xFFFFull, 0xFFFFFFFFFFFFull};
    uint64_t parts[5];
    size_t start = 0;
    for (int i = 0; i < 5; i++)
    {
        size_t dash = text.find('-', start);
        if ((i < 4) == (dash == std::string_view::npos))
            return std::nullopt;
        auto part = ParseHexPart(text.substr(start, i < 4 ? dash - start : std::string_view::npos));
        if (!part)
            return std::nullopt;
        parts[i] = *part & MASKS[i];
        start = dash + 1;
    }
    char canonical[40];
    snprintf(canonical, sizeof(canonical), "%08llx-%04llx-%04llx-%04llx-%012llx", (unsigned long long)parts[0], (unsigned long long)parts[1],
        (unsigned long long)parts[2], (unsigned long long)parts[3], (unsigned long long)parts[4]);
    return std::string(canonical);
}

std::map<std::string, std::string> ResolveValues(const std::vector<LuaScriptOption>& options, const std::map<std::string, std::string>& stored)
{
    std::map<std::string, std::string> values;
    for (const LuaScriptOption& option : options)
    {
        auto found = stored.find(option.Key);
        values[option.Key] = option.Resolve(found == stored.end() ? std::nullopt : std::optional<std::string>(found->second));
    }
    return values;
}

std::optional<std::string> SectionHeadingBefore(const std::vector<LuaScriptOption>& options, int index)
{
    if (index < 0 || index >= (int)options.size() || !options[index].Section)
        return std::nullopt;
    std::optional<std::string> previousSection = index >= 1 ? options[index - 1].Section : std::nullopt;
    if (options[index].Section == previousSection)
        return std::nullopt;
    return options[index].Section;
}

std::vector<LuaLayoutChoice> ToLuaLayoutChoices(const std::vector<LayoutConfiguration>& layouts)
{
    auto isUnnamed = [](const LayoutConfiguration& layout) { return !layout.Name || LuaText::IsBlank(*layout.Name); };
    int unnamedCount = 0;
    for (const LayoutConfiguration& layout : layouts)
    {
        if (layout.Id && isUnnamed(layout))
            unnamedCount++;
    }

    std::vector<LuaLayoutChoice> choices;
    int unnamedIndex = 0;
    for (const LayoutConfiguration& layout : layouts)
    {
        if (!layout.Id)
            continue;
        bool unnamed = isUnnamed(layout);
        int unnamedNumber = unnamed && unnamedCount > 1 ? ++unnamedIndex : 0;
        choices.push_back({*layout.Id, unnamed ? std::nullopt : layout.Name, unnamedNumber});
    }
    return choices;
}

}
