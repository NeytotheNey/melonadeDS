package me.magnum.melonds.impl

import me.magnum.melonds.domain.model.emulator.LuaScriptOption
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LuaScriptHeaderParserTest {

    private fun parse(source: String) = LuaScriptHeaderParser.parse(source.encodeToByteArray())

    @Test
    fun specExamplesAreRead() {
        val header = parse(
            """
            -- option: speed type=number min=1 max=10 step=1 default=5 label="Aim speed"
            -- option: hud type=toggle default=true label="Show HUD"
            -- option: mode type=choice values=easy,normal,hard default=normal label="Difficulty"
            print("hi")
            """.trimIndent()
        )

        assertEquals(
            listOf(
                LuaScriptOption.Number("speed", "Aim speed", min = 1.0, max = 10.0, step = 1.0, default = 5.0, decimals = 0),
                LuaScriptOption.Toggle("hud", "Show HUD", default = true),
                LuaScriptOption.Choice("mode", "Difficulty", listOf("easy", "normal", "hard"), default = "normal"),
            ),
            header.options,
        )
        assertEquals(0, header.unreadableOptionCount)
    }

    @Test
    fun descriptionLineWins() {
        assertEquals("Aims for you", parse("-- Aim helper\n-- description: Aims for you\n").description)
    }

    @Test
    fun firstCommentLineIsTheFallbackDescription() {
        assertEquals("Aim helper", parse("#!/usr/bin/lua\n-- =====\n-- Aim helper\n-- More text\n").description)
    }

    @Test
    fun optionLinesAreNeverTheDescription() {
        val header = parse("-- option: hud type=toggle\n-- Shows a HUD\n")

        assertEquals("Shows a HUD", header.description)
        assertEquals(1, header.options.size)
    }

    @Test
    fun noLeadingCommentMeansNoDescriptionOrOptions() {
        val header = parse("local x = 1\n-- option: hud type=toggle\n")

        assertNull(header.description)
        assertEquals(emptyList<LuaScriptOption>(), header.options)
    }

    @Test
    fun blockCommentsAreRead() {
        val header = parse("--[[\ndescription: In a block\noption: hud type=toggle\n]]\n")

        assertEquals("In a block", header.description)
        assertEquals(listOf(LuaScriptOption.Toggle("hud", "hud", default = false)), header.options)
    }

    @Test
    fun defaultsAreFilledIn() {
        val header = parse("-- option: a type=toggle\n-- option: b type=number min=0 max=4\n-- option: c type=choice values=x,y\n")

        assertEquals(
            listOf(
                LuaScriptOption.Toggle("a", "a", default = false),
                LuaScriptOption.Number("b", "b", min = 0.0, max = 4.0, step = 1.0, default = 0.0, decimals = 0),
                LuaScriptOption.Choice("c", "c", listOf("x", "y"), default = "x"),
            ),
            header.options,
        )
    }

    @Test
    fun quotedValuesCanHoldSpaces() {
        val header = parse("-- option: mode type=choice values=\"Easy mode, Hard mode\" default=\"Hard mode\" label=\"Game mode\"\n")

        assertEquals(listOf(LuaScriptOption.Choice("mode", "Game mode", listOf("Easy mode", "Hard mode"), default = "Hard mode")), header.options)
    }

    @Test
    fun badLinesAreCounted() {
        val badLines = listOf(
            // Bad keys
            "Speed type=toggle",
            "1speed type=toggle",
            "a${"b".repeat(32)} type=toggle",
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
            "a type=toggle label=\"${"x".repeat(61)}\"",
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
            "a type=choice values=${"v".repeat(25)}",
            "a type=choice values=x,y default=z",
        )
        val header = parse(badLines.joinToString("\n") { "-- option: $it" } + "\n-- option: good type=toggle\n")

        assertEquals(listOf(LuaScriptOption.Toggle("good", "good", default = false)), header.options)
        assertEquals(badLines.size, header.unreadableOptionCount)
    }

    @Test
    fun limitsThatAreJustMetAreFine() {
        val header = parse(
            "-- option: ${"a".repeat(32)} type=toggle label=\"${"x".repeat(60)}\"\n" +
                "-- option: b type=number min=0 max=1000 step=1\n" +
                "-- option: c type=choice values=1,2,3,4,5,6,7,${"v".repeat(24)}\n"
        )

        assertEquals(3, header.options.size)
        assertEquals(0, header.unreadableOptionCount)
    }

    @Test
    fun duplicateKeysAndOptionsOverSixteenAreCounted() {
        val lines = (1..17).map { "-- option: o$it type=toggle" } + "-- option: o1 type=toggle"
        val header = parse(lines.joinToString("\n"))

        assertEquals(16, header.options.size)
        assertEquals(2, header.unreadableOptionCount)
    }

    @Test
    fun onlyTheFirstEightKilobytesAreRead() {
        // The option line that crosses the limit is cut, so it's dropped rather than read half
        val padding = "-- " + "x".repeat(LuaScriptHeaderParser.READ_LIMIT - 20) + "\n"
        val header = parse(padding + "-- option: late type=choice values=aaaaaaaaaa,bbbbbbbbbb\n")

        assertEquals(emptyList<LuaScriptOption>(), header.options)
        assertEquals(0, header.unreadableOptionCount)
    }

    @Test
    fun numberDefaultsGoToTheClosestStep() {
        val option = parse("-- option: s type=number min=0 max=1 step=0.25 default=0.3\n").options.single() as LuaScriptOption.Number

        assertEquals(0.25, option.default, 0.0)
        assertEquals(4, option.stepCount)
        assertEquals(2, option.decimals)
    }

    @Test
    fun layoutOptionsAreRead() {
        val header = parse(
            """
            -- option: map_layout type=layout label="Layout while the map is open"
            -- option: plain type=layout
            """.trimIndent()
        )

        assertEquals(
            listOf(
                LuaScriptOption.Layout("map_layout", "Layout while the map is open"),
                LuaScriptOption.Layout("plain", "plain"),
            ),
            header.options,
        )
        assertEquals(0, header.unreadableOptionCount)
    }

    @Test
    fun layoutOptionsTakeNoDefaultOrOtherAttributes() {
        val header = parse(
            """
            -- option: a type=layout default=00000000-0000-0000-0000-000000000000
            -- option: b type=layout values=x
            """.trimIndent()
        )

        assertEquals(emptyList<LuaScriptOption>(), header.options)
        assertEquals(2, header.unreadableOptionCount)
    }

    @Test
    fun optionsTakeTheSectionAboveThem() {
        val header = parse(
            """
            -- option: first type=toggle
            -- section: Sun shadows
            -- option: sun type=toggle
            -- option: sun_length type=number min=0 max=1 step=0.5
            -- SECTION :  Bloom  
            -- option: bloom type=choice values=low,high
            -- option: bloom_layout type=layout
            """.trimIndent()
        )

        assertEquals(
            listOf(null, "Sun shadows", "Sun shadows", "Bloom", "Bloom"),
            header.options.map { it.section },
        )
        assertEquals(0, header.unreadableOptionCount)
    }

    @Test
    fun anEmptySectionLineEndsTheSection() {
        val header = parse("-- section: Bloom\n-- option: a type=toggle\n-- section:\n-- option: b type=toggle\n")

        assertEquals(listOf("Bloom", null), header.options.map { it.section })
    }

    @Test
    fun sectionLinesAreNeverTheDescription() {
        assertEquals("Shows a HUD", parse("-- section: Display\n-- Shows a HUD\n").description)
        assertNull(parse("-- section: Display\n-- option: a type=toggle\n").description)
    }

    @Test
    fun longSectionTitlesAreCut() {
        val title = "x".repeat(LuaScriptHeaderParser.MAX_LABEL_LENGTH + 10)
        val section = parse("-- section: $title\n-- option: a type=toggle\n").options.single().section

        assertEquals("x".repeat(LuaScriptHeaderParser.MAX_LABEL_LENGTH - 1) + "\u2026", section)
    }

    @Test
    fun sectionLinesDontCountTowardTheOptionLimit() {
        val lines = (1..16).flatMap { listOf("-- section: S$it", "-- option: o$it type=toggle") }
        val header = parse(lines.joinToString("\n"))

        assertEquals(16, header.options.size)
        assertEquals("S16", header.options.last().section)
        assertEquals(0, header.unreadableOptionCount)
    }

    private fun startsWithDescription(source: String) = LuaScriptHeaderParser.startsWithDescription(source.encodeToByteArray())

    @Test
    fun firstLineDescriptionMakesAScript() {
        assertTrue(startsWithDescription("-- description: Shows battle info\nlocal ui = require(\"pbi.ui\")\n"))
        assertTrue(startsWithDescription("--Description:Shows battle info"))
        assertTrue(startsWithDescription("  ---- description : Shows battle info  \n"))
    }

    @Test
    fun firstLineCheckSkipsTheBomAndCarriageReturn() {
        assertTrue(startsWithDescription("﻿-- description: Shows battle info\r\nprint(1)\r\n"))
        assertTrue(startsWithDescription("-- description: x\r"))
    }

    @Test
    fun descriptionMustBeOnTheFirstLine() {
        assertFalse(startsWithDescription("\n-- description: Shows battle info\n"))
        assertFalse(startsWithDescription("-- UI helpers\n-- description: Shows battle info\n"))
        assertFalse(startsWithDescription("#!/usr/bin/lua\n-- description: Shows battle info\n"))
        assertFalse(startsWithDescription("local M = {} -- description: x\n"))
        assertFalse(startsWithDescription(""))
    }

    @Test
    fun blockCommentOrEmptyDescriptionIsNotAScript() {
        assertFalse(startsWithDescription("--[[ description: Shows battle info ]]\n"))
        assertFalse(startsWithDescription("--[==[ description: Shows battle info\n]==]\n"))
        assertFalse(startsWithDescription("-- description:\nprint(1)\n"))
        assertFalse(startsWithDescription("-- description:   \r\n"))
        assertFalse(startsWithDescription("-- descriptions: x\n"))
    }

    @Test
    fun firstLineCheckReadsOnlyTheLimit() {
        val padding = " ".repeat(LuaScriptHeaderParser.FIRST_LINE_READ_LIMIT)
        assertFalse(startsWithDescription("--${padding}description: x\n"))
        assertTrue(startsWithDescription("-- description: x$padding\n"))
    }
}
