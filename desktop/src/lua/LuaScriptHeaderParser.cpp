#include "lua/LuaScriptHeaderParser.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "lua/LuaText.h"

namespace Frontend::LuaScriptHeaderParser
{

namespace
{

using Text = std::u16string;
using View = std::u16string_view;

constexpr int MAX_DESCRIPTION_LENGTH = 120;
constexpr int MAX_OPTION_NAME_LENGTH = 32;

// an option line split up: its key, its label (the key if it has none), the section it's under, and its other attributes
struct OptionLine
{
    Text Key;
    Text Label;
    std::optional<Text> Section;
    std::map<Text, Text> Attributes;

    std::optional<View> Get(View name) const
    {
        auto found = Attributes.find(Text(name));
        if (found == Attributes.end())
            return std::nullopt;
        return View(found->second);
    }
};

std::optional<std::string> ToUtf8(const std::optional<Text>& text)
{
    if (!text)
        return std::nullopt;
    return LuaText::ToUtf8(*text);
}

// Kotlin's lines(): split at \r\n, \n and \r
std::vector<View> Lines(View text)
{
    std::vector<View> lines;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); i++)
    {
        if (text[i] != '\n' && text[i] != '\r')
            continue;
        lines.push_back(text.substr(start, i - start));
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
            i++;
        start = i + 1;
    }
    lines.push_back(text.substr(start));
    return lines;
}

// `<key>\s*:\s*(.*)` over the whole of [text], ignoring case: what (.*) took
std::optional<View> MatchKey(View text, View key)
{
    if (!LuaText::StartsWithIgnoringCase(text, key))
        return std::nullopt;
    size_t position = key.size();
    while (position < text.size() && LuaText::IsRegexSpace(text[position]))
        position++;
    if (position >= text.size() || text[position] != ':')
        return std::nullopt;
    position++;
    while (position < text.size() && LuaText::IsRegexSpace(text[position]))
        position++;
    View rest = text.substr(position);
    if (!std::all_of(rest.begin(), rest.end(), LuaText::IsRegexDot))
        return std::nullopt;
    return rest;
}

// where `^--\[=*\[` ends in [line], or none
std::optional<size_t> FindBlockCommentOpener(View line)
{
    if (line.size() < 4 || line[0] != '-' || line[1] != '-' || line[2] != '[')
        return std::nullopt;
    size_t position = 3;
    while (position < line.size() && line[position] == '=')
        position++;
    if (position >= line.size() || line[position] != '[')
        return std::nullopt;
    return position + 1;
}

// where the first `]=*]` starts in [text], or none
std::optional<size_t> FindBlockCommentCloser(View text)
{
    for (size_t i = 0; i < text.size(); i++)
    {
        if (text[i] != ']')
            continue;
        size_t position = i + 1;
        while (position < text.size() && text[position] == '=')
            position++;
        if (position < text.size() && text[position] == ']')
            return i;
    }
    return std::nullopt;
}

// [text] cut to [maxLength] characters, ending in an ellipsis when it was longer
Text CapText(View text, int maxLength)
{
    if ((int)text.size() <= maxLength)
        return Text(text);

    size_t cut = maxLength - 1;
    // Don't split a surrogate pair
    if (text[cut - 1] >= 0xD800 && text[cut - 1] <= 0xDBFF && text[cut] >= 0xDC00 && text[cut] <= 0xDFFF)
        cut--;
    return Text(LuaText::TrimEnd(text.substr(0, cut))) + u"…";
}

bool IsOptionName(View key)
{
    if (key.empty() || key.size() > MAX_OPTION_NAME_LENGTH || !((key[0] >= 'a' && key[0] <= 'z') || key[0] == '_'))
        return false;
    return std::all_of(key.begin(), key.end(), [](char16_t c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

bool IsAttributeName(View name)
{
    return !name.empty() && std::all_of(name.begin(), name.end(), [](char16_t c) { return (c >= 'a' && c <= 'z') || c == '_'; });
}

// `-?\d{1,12}(\.\d{1,6})?`: plain decimals only (no exponents, hex, inf or nan), short enough to stay exact
bool IsPlainNumber(View text)
{
    size_t position = !text.empty() && text[0] == '-' ? 1 : 0;
    auto countDigits = [&]() {
        size_t start = position;
        while (position < text.size() && text[position] >= '0' && text[position] <= '9')
            position++;
        return position - start;
    };
    size_t whole = countDigits();
    if (whole < 1 || whole > 12)
        return false;
    if (position == text.size())
        return true;
    if (text[position] != '.')
        return false;
    position++;
    size_t fraction = countDigits();
    return fraction >= 1 && fraction <= 6 && position == text.size();
}

double ToDouble(View plainNumber)
{
    return LuaText::ParseDouble(LuaText::ToUtf8(plainNumber)).value_or(0);
}

int GetDecimals(View number)
{
    size_t point = number.find('.');
    return point == View::npos ? 0 : (int)(number.size() - point - 1);
}

// `name=value` pairs split by spaces, where a value runs to the next space or is quoted ("...", which can hold spaces but not quotes)
std::optional<std::map<Text, Text>> ParseAttributes(View text)
{
    std::map<Text, Text> attributes;
    size_t position = 0;
    while (true)
    {
        while (position < text.size() && LuaText::IsWhitespace(text[position]))
            position++;
        if (position >= text.size())
            return attributes;

        size_t equalsIndex = text.find('=', position);
        if (equalsIndex == View::npos)
            return std::nullopt;
        Text name(text.substr(position, equalsIndex - position));
        if (!IsAttributeName(name) || attributes.count(name))
            return std::nullopt;

        position = equalsIndex + 1;
        if (position < text.size() && text[position] == '"')
        {
            size_t closingIndex = text.find('"', position + 1);
            if (closingIndex == View::npos)
                return std::nullopt;
            attributes[name] = Text(text.substr(position + 1, closingIndex - position - 1));
            position = closingIndex + 1;
            // Something right after the closing quote, like label="a"b
            if (position < text.size() && !LuaText::IsWhitespace(text[position]))
                return std::nullopt;
        }
        else
        {
            size_t end = position;
            while (end < text.size() && !LuaText::IsWhitespace(text[end]))
                end++;
            attributes[name] = Text(text.substr(position, end - position));
            position = end;
        }
    }
}

std::optional<LuaScriptOption> ParseToggle(const OptionLine& line)
{
    std::optional<View> defaultText = line.Get(u"default");
    bool defaultValue = false;
    if (defaultText == u"true")
        defaultValue = true;
    else if (defaultText && *defaultText != u"false")
        return std::nullopt;
    return LuaScriptOption::Toggle(LuaText::ToUtf8(line.Key), LuaText::ToUtf8(line.Label), defaultValue, ToUtf8(line.Section));
}

std::optional<LuaScriptOption> ParseNumber(const OptionLine& line)
{
    std::optional<View> minText = line.Get(u"min");
    std::optional<View> maxText = line.Get(u"max");
    View stepText = line.Get(u"step").value_or(u"1");
    std::optional<View> defaultText = line.Get(u"default");
    if (!minText || !IsPlainNumber(*minText) || !maxText || !IsPlainNumber(*maxText) || !IsPlainNumber(stepText))
        return std::nullopt;
    if (defaultText && !IsPlainNumber(*defaultText))
        return std::nullopt;

    double min = ToDouble(*minText);
    double max = ToDouble(*maxText);
    double step = ToDouble(stepText);
    int stepCount = LuaScriptOption::GetStepCount(min, max, step);
    if (!(min < max) || !(step > 0) || stepCount < 1 || stepCount > MAX_NUMBER_STEPS)
        return std::nullopt;

    int decimals = std::max(GetDecimals(*minText), GetDecimals(stepText));
    LuaScriptOption option = LuaScriptOption::Number(LuaText::ToUtf8(line.Key), LuaText::ToUtf8(line.Label), min, max, step, min, decimals,
        ToUtf8(line.Section));
    if (!defaultText)
        return option;
    // The default has to be on the slider; between two steps it goes to the closest one
    std::optional<int> defaultStep = option.StepOf(ToDouble(*defaultText));
    if (!defaultStep)
        return std::nullopt;
    option.NumberDefault = option.ValueAt(*defaultStep);
    return option;
}

std::optional<LuaScriptOption> ParseChoice(const OptionLine& line)
{
    std::optional<View> valuesText = line.Get(u"values");
    if (!valuesText)
        return std::nullopt;
    std::vector<Text> values;
    size_t start = 0;
    while (true)
    {
        size_t comma = valuesText->find(',', start);
        values.emplace_back(LuaText::Trim(valuesText->substr(start, comma == View::npos ? View::npos : comma - start)));
        if (comma == View::npos)
            break;
        start = comma + 1;
    }
    if (values.empty() || (int)values.size() > MAX_CHOICE_VALUES || std::set<Text>(values.begin(), values.end()).size() != values.size())
        return std::nullopt;
    if (std::any_of(values.begin(), values.end(), [](const Text& value) { return value.empty() || (int)value.size() > MAX_CHOICE_VALUE_LENGTH; }))
        return std::nullopt;
    Text defaultValue(line.Get(u"default").value_or(View(values.front())));
    if (std::find(values.begin(), values.end(), defaultValue) == values.end())
        return std::nullopt;

    std::vector<std::string> utf8Values;
    for (const Text& value : values)
        utf8Values.push_back(LuaText::ToUtf8(value));
    return LuaScriptOption::Choice(LuaText::ToUtf8(line.Key), LuaText::ToUtf8(line.Label), std::move(utf8Values), LuaText::ToUtf8(defaultValue),
        ToUtf8(line.Section));
}

std::optional<LuaScriptOption> ParseLayout(const OptionLine& line)
{
    // The player's layouts aren't known to the script, so there's nothing it could name as the default
    if (line.Get(u"default"))
        return std::nullopt;
    return LuaScriptOption::Layout(LuaText::ToUtf8(line.Key), LuaText::ToUtf8(line.Label), ToUtf8(line.Section));
}

// Parses what follows `option:`, or returns none on any malformed part (a bad key, an unknown type or attribute, an attribute given twice,
// an unclosed quote, or values the type doesn't allow)
std::optional<LuaScriptOption> ParseOption(View text, const std::optional<Text>& section)
{
    size_t keyLength = 0;
    while (keyLength < text.size() && !LuaText::IsWhitespace(text[keyLength]))
        keyLength++;
    View key = text.substr(0, keyLength);
    if (!IsOptionName(key))
        return std::nullopt;
    auto attributes = ParseAttributes(text.substr(keyLength));
    if (!attributes)
        return std::nullopt;

    // each type's attributes besides type, label and default, and its parser
    struct OptionType
    {
        const char16_t* Name;
        std::set<Text> Attributes;
        std::optional<LuaScriptOption> (*Parse)(const OptionLine&);
    };
    static const OptionType OPTION_TYPES[] = {
        {u"toggle", {}, ParseToggle},
        {u"number", {u"min", u"max", u"step"}, ParseNumber},
        {u"choice", {u"values"}, ParseChoice},
        {u"layout", {}, ParseLayout},
    };
    auto typeName = attributes->find(u"type");
    if (typeName == attributes->end())
        return std::nullopt;
    const OptionType* type = nullptr;
    for (const OptionType& candidate : OPTION_TYPES)
    {
        if (typeName->second == candidate.Name)
            type = &candidate;
    }
    if (!type)
        return std::nullopt;
    for (const auto& [name, value] : *attributes)
    {
        if (name != u"type" && name != u"label" && name != u"default" && !type->Attributes.count(name))
            return std::nullopt;
    }

    auto labelText = attributes->find(u"label");
    Text label(labelText == attributes->end() ? View() : LuaText::Trim(labelText->second));
    if ((int)label.size() > MAX_LABEL_LENGTH)
        return std::nullopt;
    return type->Parse(OptionLine {Text(key), label.empty() ? Text(key) : label, section, std::move(*attributes)});
}

// what's left of a line of the leading comment block once the comment markers are gone
struct LineText
{
    enum class Kind
    {
        Text,
        Skip,
        // code: the leading comment block is over
        End,
    };
    Kind LineKind = Kind::Skip;
    View Content;
};

LineText ReadLineText(View line, size_t index, bool& isInBlockComment)
{
    if (isInBlockComment)
    {
        std::optional<size_t> closerIndex = FindBlockCommentCloser(line);
        if (closerIndex)
            isInBlockComment = false;
        return {LineText::Kind::Text, closerIndex ? line.substr(0, *closerIndex) : line};
    }
    if (index == 0 && line.size() >= 2 && line[0] == '#' && line[1] == '!')
        return {LineText::Kind::Skip, {}};
    if (line.empty())
        return {LineText::Kind::Skip, {}};
    if (line.size() < 2 || line[0] != '-' || line[1] != '-')
        return {LineText::Kind::End, {}};

    std::optional<size_t> opener = FindBlockCommentOpener(line);
    if (opener)
    {
        View rest = line.substr(*opener);
        std::optional<size_t> closerIndex = FindBlockCommentCloser(rest);
        isInBlockComment = !closerIndex;
        return {LineText::Kind::Text, closerIndex ? rest.substr(0, *closerIndex) : rest};
    }
    // Also turns separator lines like "------" into nothing
    size_t dashes = line.find_first_not_of(u'-');
    return {LineText::Kind::Text, dashes == View::npos ? View() : line.substr(dashes)};
}

}

LuaScriptHeader Parse(std::string_view source)
{
    std::u16string decoded = LuaText::ToUtf16(source.substr(0, std::min(source.size(), (size_t)READ_LIMIT)));
    View all = decoded;
    if (!all.empty() && all[0] == 0xFEFF)
        all.remove_prefix(1);
    std::vector<View> lines = Lines(all);
    // The last line was cut off, so it could be half an option
    if (source.size() > (size_t)READ_LIMIT)
        lines.pop_back();

    std::optional<Text> description;
    std::optional<View> firstLine;
    LuaScriptHeader header;
    std::optional<Text> section;
    bool isInBlockComment = false;

    for (size_t index = 0; index < lines.size(); index++)
    {
        LineText lineText = ReadLineText(LuaText::Trim(lines[index]), index, isInBlockComment);
        if (lineText.LineKind == LineText::Kind::End)
            break;
        View text = LuaText::Trim(lineText.Content);
        if (lineText.LineKind == LineText::Kind::Skip || text.empty())
            continue;

        if (std::optional<View> sectionText = MatchKey(text, u"section"))
        {
            // An empty section line ends the section
            View title = LuaText::Trim(*sectionText);
            section = title.empty() ? std::nullopt : std::optional<Text>(CapText(title, MAX_LABEL_LENGTH));
            continue;
        }

        if (std::optional<View> optionText = MatchKey(text, u"option"))
        {
            std::optional<LuaScriptOption> option = ParseOption(*optionText, section);
            bool isTaken = option && std::any_of(header.Options.begin(), header.Options.end(),
                [&](const LuaScriptOption& known) { return known.Key == option->Key; });
            if (!option || (int)header.Options.size() >= MAX_OPTIONS || isTaken)
                header.UnreadableOptionCount++;
            else
                header.Options.push_back(std::move(*option));
            continue;
        }

        if (std::optional<View> descriptionText = MatchKey(text, u"description"))
        {
            // The first non-empty description line wins, and an empty one is never used as the fallback either
            View trimmed = LuaText::Trim(*descriptionText);
            if (!description && !trimmed.empty())
                description = CapText(trimmed, MAX_DESCRIPTION_LENGTH);
            continue;
        }
        // Banner lines like "-- ==========" have no letters or digits, so they aren't a description
        if (!firstLine && std::any_of(text.begin(), text.end(), LuaText::IsLetterOrDigit))
            firstLine = text;
    }

    if (!description && firstLine)
        description = CapText(*firstLine, MAX_DESCRIPTION_LENGTH);
    header.Description = ToUtf8(description);
    return header;
}

}
