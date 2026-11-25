// The Kotlin/JVM string behavior the Lua script ports (LuaScriptHeaderParser.kt, LuaScriptFolderContent.kt and the rest) rely on, so the
// ports measure, trim, compare and parse text the way the app does: lengths in UTF-16 units, Kotlin's Char.isWhitespace, Java regex's \s,
// String.CASE_INSENSITIVE_ORDER and String.toDoubleOrNull. Strings outside these helpers are UTF-8
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Frontend::LuaText
{

// UTF-8 as Kotlin's decodeToString() reads it: each malformed sequence becomes one U+FFFD
std::u16string ToUtf16(std::string_view utf8);
// a lone surrogate becomes U+FFFD
std::string ToUtf8(std::u16string_view utf16);

// Kotlin's Char.isWhitespace (Character.isWhitespace or isSpaceChar)
bool IsWhitespace(char16_t c);
// Java regex's \s: [ \t\n\x0B\f\r]
bool IsRegexSpace(char16_t c);
// Character.isLetterOrDigit, through Windows' character types for anything past ASCII
bool IsLetterOrDigit(char16_t c);
// Java regex's `.`: anything but a line terminator
bool IsRegexDot(char16_t c);

// Kotlin's trim() and trimEnd()
std::u16string_view Trim(std::u16string_view text);
std::u16string_view TrimEnd(std::u16string_view text);
// Kotlin's isNullOrBlank() for a UTF-8 string
bool IsBlank(std::string_view utf8);

// whether [text] starts with the ASCII [prefix], ignoring ASCII case
bool StartsWithIgnoringCase(std::u16string_view text, std::u16string_view prefix);
// whether the UTF-8 [name] ends with the ASCII [suffix], ignoring case (exact for ASCII suffixes, as no other character folds to an ASCII
// letter in "lua" or "png")
bool EndsWithIgnoringCase(std::string_view name, std::string_view suffix);

// String.CASE_INSENSITIVE_ORDER over UTF-16 units, then String.compareTo for ties: <0, 0 or >0
int CompareIgnoringCase(std::u16string_view a, std::u16string_view b);
int CompareUtf16(std::u16string_view a, std::u16string_view b);

// String.toDoubleOrNull (Java's Double.valueOf syntax: surrounding control characters and spaces, a sign, NaN, Infinity, decimals with an
// exponent, hex floats, an f or d suffix)
std::optional<double> ParseDouble(std::string_view text);

// paths from and to UTF-8
std::filesystem::path PathFromUtf8(std::string_view utf8);
std::string PathToUtf8(const std::filesystem::path& path);

}
