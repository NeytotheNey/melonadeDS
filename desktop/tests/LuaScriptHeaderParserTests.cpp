// LuaScriptHeaderParserTest.kt, case for case
#include <string>
#include <vector>

#include "LuaScriptTestText.h"
#include "Test.h"
#include "lua/LuaScriptHeaderParser.h"

using namespace Frontend;

namespace
{

using Option = LuaScriptOption;

LuaScriptHeader Parse(const std::string& source)
{
    return LuaScriptHeaderParser::Parse(source);
}

std::string Repeat(const char* text, int count)
{
    std::string repeated;
    for (int i = 0; i < count; i++)
        repeated += text;
    return repeated;
}

std::vector<std::optional<std::string>> Sections(const LuaScriptHeader& header)
{
    std::vector<std::optional<std::string>> sections;
    for (const Option& option : header.Options)
        sections.push_back(option.Section);
    return sections;
}

const std::optional<std::string> NONE;

}

TEST(LuaScriptHeaderParser, specExamplesAreRead)
{
    LuaScriptHeader header = Parse(
        "-- option: speed type=number min=1 max=10 step=1 default=5 label=\"Aim speed\"\n"
        "-- option: hud type=toggle default=true label=\"Show HUD\"\n"
        "-- option: mode type=choice values=easy,normal,hard default=normal label=\"Difficulty\"\n"
        "print(\"hi\")");

    CHECK_SAME((std::vector<Option> {
                   Option::Number("speed", "Aim speed", 1.0, 10.0, 1.0, 5.0, 0),
                   Option::Toggle("hud", "Show HUD", true),
                   Option::Choice("mode", "Difficulty", {"easy", "normal", "hard"}, "normal"),
               }),
        header.Options);
    CHECK_SAME(0, header.UnreadableOptionCount);
}

TEST(LuaScriptHeaderParser, descriptionLineWins)
{
    CHECK_SAME(std::optional<std::string>("Aims for you"), Parse("-- Aim helper\n-- description: Aims for you\n").Description);
}

TEST(LuaScriptHeaderParser, firstCommentLineIsTheFallbackDescription)
{
    CHECK_SAME(std::optional<std::string>("Aim helper"), Parse("#!/usr/bin/lua\n-- =====\n-- Aim helper\n-- More text\n").Description);
}

TEST(LuaScriptHeaderParser, optionLinesAreNeverTheDescription)
{
    LuaScriptHeader header = Parse("-- option: hud type=toggle\n-- Shows a HUD\n");

    CHECK_SAME(std::optional<std::string>("Shows a HUD"), header.Description);
    CHECK_SAME(1ull, header.Options.size());
}

TEST(LuaScriptHeaderParser, noLeadingCommentMeansNoDescriptionOrOptions)
{
    LuaScriptHeader header = Parse("local x = 1\n-- option: hud type=toggle\n");

    CHECK_NONE(header.Description);
    CHECK_SAME(std::vector<Option> {}, header.Options);
}

TEST(LuaScriptHeaderParser, blockCommentsAreRead)
{
    LuaScriptHeader header = Parse("--[[\ndescription: In a block\noption: hud type=toggle\n]]\n");

    CHECK_SAME(std::optional<std::string>("In a block"), header.Description);
    CHECK_SAME(std::vector<Option> {Option::Toggle("hud", "hud", false)}, header.Options);
}

TEST(LuaScriptHeaderParser, defaultsAreFilledIn)
{
    LuaScriptHeader header = Parse("-- option: a type=toggle\n-- option: b type=number min=0 max=4\n-- option: c type=choice values=x,y\n");

    CHECK_SAME((std::vector<Option> {
                   Option::Toggle("a", "a", false),
                   Option::Number("b", "b", 0.0, 4.0, 1.0, 0.0, 0),
                   Option::Choice("c", "c", {"x", "y"}, "x"),
               }),
        header.Options);
}

TEST(LuaScriptHeaderParser, quotedValuesCanHoldSpaces)
{
    LuaScriptHeader header = Parse("-- option: mode type=choice values=\"Easy mode, Hard mode\" default=\"Hard mode\" label=\"Game mode\"\n");

    CHECK_SAME(std::vector<Option> {Option::Choice("mode", "Game mode", {"Easy mode", "Hard mode"}, "Hard mode")}, header.Options);
}

TEST(LuaScriptHeaderParser, badLinesAreCounted)
{
    std::vector<std::string> badLines = {
        // Bad keys
        "Speed type=toggle",
        "1speed type=toggle",
        "a" + Repeat("b", 32) + " type=toggle",
        // Unknown type, missing type, unknown attribute, attribute twice
        "a type=slider",
        "a default=true",
        "a type=toggle colour=red",
        "a type=toggle default=true default=false",
        // Unclosed quote, junk after a quote, a bare word
        "a type=toggle label=\"Show",
        "a type=toggle label=\"Show\"x",
        "a type=toggle loud",
        // Label over 60 characters
        "a type=toggle label=\"" + Repeat("x", 61) + "\"",
        // Toggles
        "a type=toggle default=yes",
        // Numbers: missing min, min not below max, not plain decimals, zero step, over 1000 steps, default off the slider
        "a type=number max=10",
        "a type=number min=5 max=5",
        "a type=number min=1e3 max=2000",
        "a type=number min=0 max=nan",
        "a type=number min=0 max=10 step=0",
        "a type=number min=0 max=1001 step=1",
        "a type=number min=0 max=10 default=11",
        "a type=number min=0 max=1 step=2",
        // Choices: none, empty, duplicate, too many, too long, default not in the list
        "a type=choice",
        "a type=choice values=x,,y",
        "a type=choice values=x,x",
        "a type=choice values=1,2,3,4,5,6,7,8,9",
        "a type=choice values=" + Repeat("v", 25),
        "a type=choice values=x,y default=z",
    };
    std::string source;
    for (size_t i = 0; i < badLines.size(); i++)
        source += (i ? "\n" : "") + std::string("-- option: ") + badLines[i];
    LuaScriptHeader header = Parse(source + "\n-- option: good type=toggle\n");

    CHECK_SAME(std::vector<Option> {Option::Toggle("good", "good", false)}, header.Options);
    CHECK_SAME((int)badLines.size(), header.UnreadableOptionCount);
}

TEST(LuaScriptHeaderParser, limitsThatAreJustMetAreFine)
{
    LuaScriptHeader header = Parse("-- option: " + Repeat("a", 32) + " type=toggle label=\"" + Repeat("x", 60) + "\"\n"
        + "-- option: b type=number min=0 max=1000 step=1\n" + "-- option: c type=choice values=1,2,3,4,5,6,7," + Repeat("v", 24) + "\n");

    CHECK_SAME(3ull, header.Options.size());
    CHECK_SAME(0, header.UnreadableOptionCount);
}

TEST(LuaScriptHeaderParser, duplicateKeysAndOptionsOverSixteenAreCounted)
{
    std::string source;
    for (int i = 1; i <= 17; i++)
        source += "-- option: o" + std::to_string(i) + " type=toggle\n";
    LuaScriptHeader header = Parse(source + "-- option: o1 type=toggle");

    CHECK_SAME(16ull, header.Options.size());
    CHECK_SAME(2, header.UnreadableOptionCount);
}

TEST(LuaScriptHeaderParser, onlyTheFirstEightKilobytesAreRead)
{
    // The option line that crosses the limit is cut, so it's dropped rather than read half
    std::string padding = "-- " + Repeat("x", LuaScriptHeaderParser::READ_LIMIT - 20) + "\n";
    LuaScriptHeader header = Parse(padding + "-- option: late type=choice values=aaaaaaaaaa,bbbbbbbbbb\n");

    CHECK_SAME(std::vector<Option> {}, header.Options);
    CHECK_SAME(0, header.UnreadableOptionCount);
}

TEST(LuaScriptHeaderParser, numberDefaultsGoToTheClosestStep)
{
    LuaScriptHeader header = Parse("-- option: s type=number min=0 max=1 step=0.25 default=0.3\n");
    CHECK_SAME(1ull, header.Options.size());
    const Option& option = header.Options[0];

    CHECK(option.OptionKind == Option::Kind::Number);
    CHECK_SAME(0.25, option.NumberDefault);
    CHECK_SAME(4, option.StepCount);
    CHECK_SAME(2, option.Decimals);
}

TEST(LuaScriptHeaderParser, layoutOptionsAreRead)
{
    LuaScriptHeader header = Parse(
        "-- option: map_layout type=layout label=\"Layout while the map is open\"\n"
        "-- option: plain type=layout");

    CHECK_SAME((std::vector<Option> {
                   Option::Layout("map_layout", "Layout while the map is open"),
                   Option::Layout("plain", "plain"),
               }),
        header.Options);
    CHECK_SAME(0, header.UnreadableOptionCount);
}

TEST(LuaScriptHeaderParser, layoutOptionsTakeNoDefaultOrOtherAttributes)
{
    LuaScriptHeader header = Parse(
        "-- option: a type=layout default=00000000-0000-0000-0000-000000000000\n"
        "-- option: b type=layout values=x");

    CHECK_SAME(std::vector<Option> {}, header.Options);
    CHECK_SAME(2, header.UnreadableOptionCount);
}

TEST(LuaScriptHeaderParser, optionsTakeTheSectionAboveThem)
{
    LuaScriptHeader header = Parse(
        "-- option: first type=toggle\n"
        "-- section: Sun shadows\n"
        "-- option: sun type=toggle\n"
        "-- option: sun_length type=number min=0 max=1 step=0.5\n"
        "-- SECTION :  Bloom  \n"
        "-- option: bloom type=choice values=low,high\n"
        "-- option: bloom_layout type=layout");

    CHECK_SAME((std::vector<std::optional<std::string>> {NONE, "Sun shadows", "Sun shadows", "Bloom", "Bloom"}), Sections(header));
    CHECK_SAME(0, header.UnreadableOptionCount);
}

TEST(LuaScriptHeaderParser, anEmptySectionLineEndsTheSection)
{
    LuaScriptHeader header = Parse("-- section: Bloom\n-- option: a type=toggle\n-- section:\n-- option: b type=toggle\n");

    CHECK_SAME((std::vector<std::optional<std::string>> {"Bloom", NONE}), Sections(header));
}

TEST(LuaScriptHeaderParser, sectionLinesAreNeverTheDescription)
{
    CHECK_SAME(std::optional<std::string>("Shows a HUD"), Parse("-- section: Display\n-- Shows a HUD\n").Description);
    CHECK_NONE(Parse("-- section: Display\n-- option: a type=toggle\n").Description);
}

TEST(LuaScriptHeaderParser, longSectionTitlesAreCut)
{
    std::string title = Repeat("x", LuaScriptHeaderParser::MAX_LABEL_LENGTH + 10);
    LuaScriptHeader header = Parse("-- section: " + title + "\n-- option: a type=toggle\n");
    CHECK_SAME(1ull, header.Options.size());

    CHECK_SAME(std::optional<std::string>(Repeat("x", LuaScriptHeaderParser::MAX_LABEL_LENGTH - 1) + "…"), header.Options[0].Section);
}

TEST(LuaScriptHeaderParser, sectionLinesDontCountTowardTheOptionLimit)
{
    std::string source;
    for (int i = 1; i <= 16; i++)
        source += (i > 1 ? "\n" : "") + std::string("-- section: S") + std::to_string(i) + "\n-- option: o" + std::to_string(i) + " type=toggle";
    LuaScriptHeader header = Parse(source);

    CHECK_SAME(16ull, header.Options.size());
    CHECK_SAME(std::optional<std::string>("S16"), header.Options.back().Section);
    CHECK_SAME(0, header.UnreadableOptionCount);
}

namespace
{

bool StartsWithDescription(const std::string& source)
{
    return LuaScriptHeaderParser::StartsWithDescription(source);
}

}

TEST(LuaScriptHeaderParser, firstLineDescriptionMakesAScript)
{
    CHECK(StartsWithDescription("-- description: Shows battle info\nlocal ui = require(\"pbi.ui\")\n"));
    CHECK(StartsWithDescription("--Description:Shows battle info"));
    CHECK(StartsWithDescription("  ---- description : Shows battle info  \n"));
}

TEST(LuaScriptHeaderParser, firstLineCheckSkipsTheBomAndCarriageReturn)
{
    CHECK(StartsWithDescription("\xEF\xBB\xBF-- description: Shows battle info\r\nprint(1)\r\n"));
    CHECK(StartsWithDescription("-- description: x\r"));
}

TEST(LuaScriptHeaderParser, descriptionMustBeOnTheFirstLine)
{
    CHECK(!StartsWithDescription("\n-- description: Shows battle info\n"));
    CHECK(!StartsWithDescription("-- UI helpers\n-- description: Shows battle info\n"));
    CHECK(!StartsWithDescription("#!/usr/bin/lua\n-- description: Shows battle info\n"));
    CHECK(!StartsWithDescription("local M = {} -- description: x\n"));
    CHECK(!StartsWithDescription(""));
}

TEST(LuaScriptHeaderParser, blockCommentOrEmptyDescriptionIsNotAScript)
{
    CHECK(!StartsWithDescription("--[[ description: Shows battle info ]]\n"));
    CHECK(!StartsWithDescription("--[==[ description: Shows battle info\n]==]\n"));
    CHECK(!StartsWithDescription("-- description:\nprint(1)\n"));
    CHECK(!StartsWithDescription("-- description:   \r\n"));
    CHECK(!StartsWithDescription("-- descriptions: x\n"));
}

TEST(LuaScriptHeaderParser, firstLineCheckReadsOnlyTheLimit)
{
    std::string padding = Repeat(" ", LuaScriptHeaderParser::FIRST_LINE_READ_LIMIT);
    CHECK(!StartsWithDescription("--" + padding + "description: x\n"));
    CHECK(StartsWithDescription("-- description: x" + padding + "\n"));
}
