// For the Lua script tests (LuaScriptHeaderParserTests.cpp and the rest): CHECK_SAME is Test.h's CHECK_EQ for the strings, maps and
// option types those tests compare, which Test.h's Show doesn't cover, with its own Text overloads so it never clashes with them
#pragma once

#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Test.h"
#include "lua/LuaScriptFolderSync.h"
#include "lua/LuaScriptOption.h"

namespace LuaScriptTests
{

inline std::string Text(std::string_view value)
{
    return "\"" + std::string(value) + "\"";
}

inline std::string Text(const std::string& value)
{
    return Text(std::string_view(value));
}

inline std::string Text(const char* value)
{
    return value ? Text(std::string_view(value)) : std::string("null");
}

inline std::string Text(bool value)
{
    return value ? "true" : "false";
}

inline std::string Text(int value)
{
    return std::to_string(value);
}

inline std::string Text(long long value)
{
    return std::to_string(value);
}

inline std::string Text(unsigned value)
{
    return std::to_string(value);
}

inline std::string Text(unsigned long long value)
{
    return std::to_string(value);
}

inline std::string Text(double value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.17g", value);
    return text;
}

inline std::string Text(Frontend::LuaScriptOptionType type)
{
    return "type " + std::to_string((int)type);
}

template <class T>
std::string Text(const std::optional<T>& value);
template <class T>
std::string Text(const std::vector<T>& values);
template <class A, class B>
std::string Text(const std::pair<A, B>& pair);
template <class K, class V>
std::string Text(const std::map<K, V>& values);

inline std::string Text(const Frontend::LuaScriptOption& o)
{
    static const char* KINDS[] = {"Toggle", "Number", "Choice", "Layout"};
    std::string text = std::string(KINDS[(int)o.OptionKind]) + "(" + Text(o.Key) + ", " + Text(o.Label);
    switch (o.OptionKind)
    {
    case Frontend::LuaScriptOption::Kind::Toggle:
        text += ", default " + Text(o.ToggleDefault);
        break;
    case Frontend::LuaScriptOption::Kind::Number:
        text += ", " + Text(o.Min) + ".." + Text(o.Max) + " step " + Text(o.Step) + ", default " + Text(o.NumberDefault) + ", decimals "
            + Text(o.Decimals);
        break;
    case Frontend::LuaScriptOption::Kind::Choice:
        text += ", " + Text(o.Values) + ", default " + Text(o.ChoiceDefault);
        break;
    case Frontend::LuaScriptOption::Kind::Layout:
        break;
    }
    return text + ", section " + Text(o.Section) + ")";
}

inline std::string Text(const Frontend::RomLuaScript& s)
{
    return "RomLuaScript(" + Text(s.FileName) + ", " + Text(s.Enabled) + ", " + Text(s.Options) + ")";
}

template <class T>
std::string Text(const std::optional<T>& value)
{
    return value ? Text(*value) : std::string("null");
}

template <class T>
std::string Text(const std::vector<T>& values)
{
    std::string text = "[";
    for (size_t i = 0; i < values.size(); i++)
        text += (i ? ", " : "") + Text(values[i]);
    return text + "]";
}

template <class A, class B>
std::string Text(const std::pair<A, B>& pair)
{
    return "(" + Text(pair.first) + ", " + Text(pair.second) + ")";
}

template <class K, class V>
std::string Text(const std::map<K, V>& values)
{
    std::string text = "{";
    for (const auto& [key, value] : values)
        text += (text.size() > 1 ? ", " : "") + Text(key) + "=" + Text(value);
    return text + "}";
}

// a budget's reason (nullptr for none) as text, so it compares by value: the REASON_ pointers can differ between files
inline std::optional<std::string> Reason(const char* reason)
{
    return reason ? std::optional<std::string>(reason) : std::nullopt;
}

}

#define CHECK_SAME(expected, actual) \
    do \
    { \
        const auto& expected_ = (expected); \
        const auto& actual_ = (actual); \
        if (!(expected_ == actual_)) \
        { \
            ::Tests::Fail(__FILE__, __LINE__, "expected " + ::LuaScriptTests::Text(expected_) + ", got " + ::LuaScriptTests::Text(actual_)); \
            return; \
        } \
    } while (0)
