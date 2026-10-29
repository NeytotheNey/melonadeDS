package me.magnum.melonds.impl

import me.magnum.melonds.domain.model.emulator.LuaScriptOption
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class LuaScriptHeaderTest {

    private fun parse(source: String) = LuaScriptHeader.parse(source.encodeToByteArray())

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
        val padding = "-- " + "x".repeat(LuaScriptHeader.READ_LIMIT - 20) + "\n"
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
}
