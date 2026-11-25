// Reads a script's LuaScriptHeader from the leading comment block in its first READ_LIMIT bytes, without running any Lua; ported from the
// app's impl/LuaScriptHeaderParser.kt (the rules are in MODS.md, "Script folder and the Mods tab"). Lengths count UTF-16 units, as in Kotlin
#pragma once

#include <string_view>

#include "lua/LuaScriptOption.h"

namespace Frontend::LuaScriptHeaderParser
{

constexpr int READ_LIMIT = 8 * 1024;
// what StartsWithDescription needs of a file
constexpr int FIRST_LINE_READ_LIMIT = 512;
constexpr int MAX_OPTIONS = 16;
constexpr int MAX_LABEL_LENGTH = 60;
constexpr int MAX_CHOICE_VALUES = 8;
constexpr int MAX_CHOICE_VALUE_LENGTH = 24;
constexpr int MAX_NUMBER_STEPS = 1000;

// [source] is the start of the file, READ_LIMIT + 1 bytes being enough to know it goes on
LuaScriptHeader Parse(std::string_view source);

// Whether [bytes], the start of a file (the first FIRST_LINE_READ_LIMIT are looked at), open with a description line: after a UTF-8 BOM,
// the first line is a `--` line comment (not a block comment opener) reading `description: ...` with a non-empty value. This is what makes
// a .lua file in a subfolder a script rather than a module
bool StartsWithDescription(std::string_view bytes);

}
