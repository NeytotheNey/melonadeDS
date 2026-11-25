// LuaScriptOptionTest.kt, case for case
#include <map>
#include <string>
#include <vector>

#include "LuaScriptTestText.h"
#include "Test.h"
#include "lua/LuaScriptOption.h"

using namespace Frontend;

namespace
{

using Option = LuaScriptOption;
using Values = std::map<std::string, std::string>;

const Option TOGGLE = Option::Toggle("hud", "Show HUD", true);
const Option INTEGER_NUMBER = Option::Number("speed", "Speed", 1.0, 10.0, 1.0, 5.0, 0);
const Option FRACTION_NUMBER = Option::Number("zoom", "Zoom", 0.1, 1.0, 0.1, 0.5, 1);
const Option CHOICE = Option::Choice("mode", "Mode", {"easy", "normal", "hard"}, "normal");

const std::optional<std::string> NONE;

std::string Upper(std::string text)
{
    for (char& c : text)
    {
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
    }
    return text;
}

}

TEST(LuaScriptOption, badOrMissingValuesFallBackToTheDefault)
{
    CHECK_SAME("true", TOGGLE.Resolve(NONE));
    CHECK_SAME("true", TOGGLE.Resolve("yes"));
    CHECK_SAME("5", INTEGER_NUMBER.Resolve("eleven"));
    CHECK_SAME("5", INTEGER_NUMBER.Resolve("11"));
    CHECK_SAME("5", INTEGER_NUMBER.Resolve("NaN"));
    CHECK_SAME("normal", CHOICE.Resolve("insane"));
}

TEST(LuaScriptOption, goodValuesAreKept)
{
    CHECK_SAME("false", TOGGLE.Resolve("false"));
    CHECK_SAME("10", INTEGER_NUMBER.Resolve("10"));
    CHECK_SAME("hard", CHOICE.Resolve("hard"));
}

TEST(LuaScriptOption, numbersGoOntoTheStepsWithoutFloatNoise)
{
    CHECK_SAME("7", INTEGER_NUMBER.Resolve("6.6"));
    CHECK_SAME("0.7", FRACTION_NUMBER.Resolve("0.7000000000000001"));
    CHECK_SAME("0.3", FRACTION_NUMBER.Resolve("0.31"));
    CHECK_SAME("1", FRACTION_NUMBER.Resolve("1.0"));
    CHECK_SAME(9, FRACTION_NUMBER.StepCount);
}

TEST(LuaScriptOption, nativeValuesAreTyped)
{
    LuaScriptOptionValue toggleValue = TOGGLE.ToNativeValue("true");
    CHECK_SAME(LuaScriptOptionType::Boolean, toggleValue.OptionType);
    CHECK_SAME(1LL, (long long)toggleValue.Integer);

    LuaScriptOptionValue integerValue = INTEGER_NUMBER.ToNativeValue("7");
    CHECK_SAME(LuaScriptOptionType::Integer, integerValue.OptionType);
    CHECK_SAME(7LL, (long long)integerValue.Integer);

    // A whole number from a fractional slider is still an integer
    CHECK_SAME(LuaScriptOptionType::Integer, FRACTION_NUMBER.ToNativeValue("1").OptionType);
    LuaScriptOptionValue fractionValue = FRACTION_NUMBER.ToNativeValue("0.3");
    CHECK_SAME(LuaScriptOptionType::Number, fractionValue.OptionType);
    CHECK_SAME(0.3, fractionValue.Number);

    LuaScriptOptionValue choiceValue = CHOICE.ToNativeValue("hard");
    CHECK_SAME(LuaScriptOptionType::String, choiceValue.OptionType);
    CHECK_SAME(std::string("hard"), choiceValue.Text);
}

TEST(LuaScriptOption, resolvingValuesDropsUndeclaredKeys)
{
    Values values = ResolveValues({TOGGLE, INTEGER_NUMBER, CHOICE}, {{"speed", "3"}, {"gone", "x"}});

    CHECK_SAME((Values {{"hud", "true"}, {"speed", "3"}, {"mode", "normal"}}), values);
}

TEST(LuaScriptOption, layoutValuesAreAUuidOrDontChange)
{
    Option layout = Option::Layout("map", "Map");
    std::string id = "123e4567-e89b-12d3-a456-426614174000";

    CHECK_SAME("", layout.Resolve(NONE));
    CHECK_SAME("", layout.Resolve("not a layout"));
    CHECK_SAME(id, layout.Resolve(id));
    // Kept in the canonical form
    CHECK_SAME(id, layout.Resolve(Upper(id)));
}

TEST(LuaScriptOption, scriptsOnlySeeWhetherALayoutWasPicked)
{
    Option layout = Option::Layout("map", "Map");

    LuaScriptOptionValue picked = layout.ToNativeValue("123e4567-e89b-12d3-a456-426614174000");
    CHECK_SAME(LuaScriptOptionType::Layout, picked.OptionType);
    CHECK_SAME(1LL, (long long)picked.Integer);
    CHECK_SAME(0LL, (long long)layout.ToNativeValue("").Integer);
}

TEST(LuaScriptOption, aHeadingGoesBeforeEachNewSection)
{
    std::vector<Option> options = {
        Option::Toggle("a", "A", false),
        Option::Toggle("b", "B", false, "Sun"),
        Option::Toggle("c", "C", false, "Sun"),
        Option::Toggle("d", "D", false),
        Option::Toggle("e", "E", false, "Sun"),
        Option::Toggle("f", "F", false, "Bloom"),
    };

    std::vector<std::optional<std::string>> headings;
    for (int i = 0; i < (int)options.size(); i++)
        headings.push_back(SectionHeadingBefore(options, i));
    CHECK_SAME((std::vector<std::optional<std::string>> {NONE, "Sun", NONE, NONE, "Sun", "Bloom"}), headings);
    CHECK_SAME(std::optional<std::string>("Sun"), SectionHeadingBefore(std::vector<Option>(options.begin() + 1, options.end()), 0));
    CHECK_NONE(SectionHeadingBefore(options, (int)options.size()));
}
