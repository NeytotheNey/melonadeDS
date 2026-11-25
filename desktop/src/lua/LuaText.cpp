#include "lua/LuaText.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace Frontend::LuaText
{

namespace
{

constexpr char16_t REPLACEMENT = 0xFFFD;

void AppendCodePoint(std::u16string& out, uint32_t codePoint)
{
    if (codePoint < 0x10000)
    {
        out += (char16_t)codePoint;
        return;
    }
    codePoint -= 0x10000;
    out += (char16_t)(0xD800 + (codePoint >> 10));
    out += (char16_t)(0xDC00 + (codePoint & 0x3FF));
}

void AppendUtf8(std::string& out, uint32_t codePoint)
{
    if (codePoint < 0x80)
    {
        out += (char)codePoint;
    }
    else if (codePoint < 0x800)
    {
        out += (char)(0xC0 | (codePoint >> 6));
        out += (char)(0x80 | (codePoint & 0x3F));
    }
    else if (codePoint < 0x10000)
    {
        out += (char)(0xE0 | (codePoint >> 12));
        out += (char)(0x80 | ((codePoint >> 6) & 0x3F));
        out += (char)(0x80 | (codePoint & 0x3F));
    }
    else
    {
        out += (char)(0xF0 | (codePoint >> 18));
        out += (char)(0x80 | ((codePoint >> 12) & 0x3F));
        out += (char)(0x80 | ((codePoint >> 6) & 0x3F));
        out += (char)(0x80 | (codePoint & 0x3F));
    }
}

bool IsHighSurrogate(char16_t c)
{
    return c >= 0xD800 && c <= 0xDBFF;
}

bool IsLowSurrogate(char16_t c)
{
    return c >= 0xDC00 && c <= 0xDFFF;
}

// Character.toUpperCase / toLowerCase for one UTF-16 unit; Windows' invariant (simple) case mapping past ASCII, surrogates unchanged
char16_t MapCase(char16_t c, bool upper)
{
    if (c < 0x80)
    {
        if (upper && c >= 'a' && c <= 'z')
            return (char16_t)(c - 'a' + 'A');
        if (!upper && c >= 'A' && c <= 'Z')
            return (char16_t)(c - 'A' + 'a');
        return c;
    }
    if (IsHighSurrogate(c) || IsLowSurrogate(c))
        return c;
    wchar_t in = (wchar_t)c;
    wchar_t out = in;
    if (LCMapStringEx(LOCALE_NAME_INVARIANT, upper ? LCMAP_UPPERCASE : LCMAP_LOWERCASE, &in, 1, &out, 1, nullptr, nullptr, 0) != 1)
        return c;
    return (char16_t)out;
}

bool IsDigits(std::string_view text, size_t& position, bool hex)
{
    size_t start = position;
    while (position < text.size())
    {
        char c = text[position];
        bool isDigit = (c >= '0' && c <= '9') || (hex && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
        if (!isDigit)
            break;
        position++;
    }
    return position > start;
}

// digits with an optional point, at least one digit in all
bool SkipMantissa(std::string_view text, size_t& position, bool hex)
{
    bool hasDigits = IsDigits(text, position, hex);
    if (position < text.size() && text[position] == '.')
    {
        position++;
        hasDigits = IsDigits(text, position, hex) || hasDigits;
    }
    return hasDigits;
}

bool SkipExponent(std::string_view text, size_t& position, char marker)
{
    if (position >= text.size() || (text[position] | 0x20) != marker)
        return false;
    position++;
    if (position < text.size() && (text[position] == '+' || text[position] == '-'))
        position++;
    return IsDigits(text, position, false);
}

// [body] (no sign or suffix) if it's a Java decimal or hex float literal: its digits for from_chars and whether it's hex
std::optional<std::pair<std::string_view, bool>> FloatDigits(std::string_view body)
{
    bool hex = body.size() > 2 && body[0] == '0' && (body[1] | 0x20) == 'x';
    std::string_view digits = hex ? body.substr(2) : body;
    size_t position = 0;
    if (!SkipMantissa(digits, position, hex))
        return std::nullopt;
    // a hex float needs its binary exponent; a decimal's exponent is optional
    if (hex && !SkipExponent(digits, position, 'p'))
        return std::nullopt;
    if (!hex && position < digits.size() && !SkipExponent(digits, position, 'e'))
        return std::nullopt;
    if (position != digits.size())
        return std::nullopt;
    return std::make_pair(digits, hex);
}

}

std::u16string ToUtf16(std::string_view utf8)
{
    std::u16string out;
    out.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size())
    {
        uint8_t lead = (uint8_t)utf8[i];
        if (lead < 0x80)
        {
            out += (char16_t)lead;
            i++;
            continue;
        }

        int needed = 0;
        uint32_t codePoint = 0;
        uint8_t low = 0x80, high = 0xBF;
        if (lead >= 0xC2 && lead <= 0xDF)
        {
            needed = 1;
            codePoint = lead & 0x1F;
        }
        else if (lead >= 0xE0 && lead <= 0xEF)
        {
            needed = 2;
            codePoint = lead & 0x0F;
            low = lead == 0xE0 ? 0xA0 : 0x80;
            high = lead == 0xED ? 0x9F : 0xBF;
        }
        else if (lead >= 0xF0 && lead <= 0xF4)
        {
            needed = 3;
            codePoint = lead & 0x07;
            low = lead == 0xF0 ? 0x90 : 0x80;
            high = lead == 0xF4 ? 0x8F : 0xBF;
        }
        else
        {
            out += REPLACEMENT;
            i++;
            continue;
        }

        // the longest valid start of a sequence is one replacement, and decoding goes on at the byte that broke it
        size_t next = i + 1;
        bool isValid = true;
        for (int k = 0; k < needed; k++)
        {
            uint8_t byte = next < utf8.size() ? (uint8_t)utf8[next] : 0;
            if (next >= utf8.size() || byte < low || byte > high)
            {
                isValid = false;
                break;
            }
            codePoint = (codePoint << 6) | (byte & 0x3F);
            next++;
            low = 0x80;
            high = 0xBF;
        }
        if (isValid)
            AppendCodePoint(out, codePoint);
        else
            out += REPLACEMENT;
        i = next;
    }
    return out;
}

std::string ToUtf8(std::u16string_view utf16)
{
    std::string out;
    out.reserve(utf16.size());
    for (size_t i = 0; i < utf16.size(); i++)
    {
        char16_t c = utf16[i];
        if (IsHighSurrogate(c) && i + 1 < utf16.size() && IsLowSurrogate(utf16[i + 1]))
        {
            AppendUtf8(out, 0x10000 + (((uint32_t)c - 0xD800) << 10) + ((uint32_t)utf16[i + 1] - 0xDC00));
            i++;
        }
        else if (IsHighSurrogate(c) || IsLowSurrogate(c))
        {
            AppendUtf8(out, REPLACEMENT);
        }
        else
        {
            AppendUtf8(out, c);
        }
    }
    return out;
}

bool IsWhitespace(char16_t c)
{
    if ((c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20))
        return true;
    // the space, line and paragraph separators (Zs, Zl, Zp)
    return c == 0x00A0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F
        || c == 0x3000;
}

bool IsRegexSpace(char16_t c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == 0x0B || c == '\f' || c == '\r';
}

bool IsLetterOrDigit(char16_t c)
{
    if (c < 0x80)
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    // a surrogate is category Cs: neither
    if (IsHighSurrogate(c) || IsLowSurrogate(c))
        return false;
    wchar_t in = (wchar_t)c;
    WORD type = 0;
    if (!GetStringTypeW(CT_CTYPE1, &in, 1, &type))
        return false;
    return (type & (C1_ALPHA | C1_DIGIT)) != 0;
}

bool IsRegexDot(char16_t c)
{
    return c != '\n' && c != '\r' && c != 0x0085 && c != 0x2028 && c != 0x2029;
}

std::u16string_view Trim(std::u16string_view text)
{
    size_t start = 0;
    while (start < text.size() && IsWhitespace(text[start]))
        start++;
    return TrimEnd(text.substr(start));
}

std::u16string_view TrimEnd(std::u16string_view text)
{
    size_t end = text.size();
    while (end > 0 && IsWhitespace(text[end - 1]))
        end--;
    return text.substr(0, end);
}

bool IsBlank(std::string_view utf8)
{
    return Trim(ToUtf16(utf8)).empty();
}

bool StartsWithIgnoringCase(std::u16string_view text, std::u16string_view prefix)
{
    if (text.size() < prefix.size())
        return false;
    for (size_t i = 0; i < prefix.size(); i++)
    {
        // ASCII only: Kotlin's Unicode case folding also matches a few odd letters (like U+0130 for i), which no script header uses
        if (text[i] >= 0x80 || MapCase(text[i], false) != MapCase(prefix[i], false))
            return false;
    }
    return true;
}

bool EndsWithIgnoringCase(std::string_view name, std::string_view suffix)
{
    if (name.size() < suffix.size())
        return false;
    std::string_view end = name.substr(name.size() - suffix.size());
    for (size_t i = 0; i < suffix.size(); i++)
    {
        char a = end[i], b = suffix[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

int CompareIgnoringCase(std::u16string_view a, std::u16string_view b)
{
    size_t length = std::min(a.size(), b.size());
    for (size_t i = 0; i < length; i++)
    {
        char16_t c1 = a[i], c2 = b[i];
        if (c1 == c2)
            continue;
        c1 = MapCase(c1, true);
        c2 = MapCase(c2, true);
        if (c1 == c2)
            continue;
        c1 = MapCase(c1, false);
        c2 = MapCase(c2, false);
        if (c1 != c2)
            return (int)c1 - (int)c2;
    }
    return (int)a.size() - (int)b.size();
}

int CompareUtf16(std::u16string_view a, std::u16string_view b)
{
    return a.compare(b);
}

std::optional<double> ParseDouble(std::string_view text)
{
    // String.trim(): everything up to the space
    size_t start = 0, end = text.size();
    while (start < end && (uint8_t)text[start] <= ' ')
        start++;
    while (end > start && (uint8_t)text[end - 1] <= ' ')
        end--;
    std::string_view body = text.substr(start, end - start);

    bool isNegative = false;
    if (!body.empty() && (body[0] == '+' || body[0] == '-'))
    {
        isNegative = body[0] == '-';
        body.remove_prefix(1);
    }
    if (body == "NaN")
        return std::numeric_limits<double>::quiet_NaN();
    if (body == "Infinity")
        return isNegative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    if (!body.empty())
    {
        char last = body.back();
        if (last == 'f' || last == 'F' || last == 'd' || last == 'D')
            body.remove_suffix(1);
    }

    auto digits = FloatDigits(body);
    if (!digits)
        return std::nullopt;
    auto [number, isHex] = *digits;
    double value = 0;
    auto result = std::from_chars(number.data(), number.data() + number.size(), value,
        isHex ? std::chars_format::hex : std::chars_format::general);
    if (result.ec == std::errc::result_out_of_range)
    {
        // from_chars leaves overflow and underflow alike; strtod gives the infinity or the zero Java does
        std::string copy(body);
        value = std::strtod(copy.c_str(), nullptr);
    }
    else if (result.ec != std::errc() || result.ptr != number.data() + number.size())
    {
        return std::nullopt;
    }
    return isNegative ? -value : value;
}

std::filesystem::path PathFromUtf8(std::string_view utf8)
{
    // Windows paths are UTF-16 (wchar_t)
    std::u16string utf16 = ToUtf16(utf8);
    return std::filesystem::path(std::wstring(utf16.begin(), utf16.end()));
}

std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::wstring& native = path.native();
    return ToUtf8(std::u16string(native.begin(), native.end()));
}

}
