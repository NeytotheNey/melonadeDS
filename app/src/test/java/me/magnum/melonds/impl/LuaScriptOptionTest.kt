package me.magnum.melonds.impl

import me.magnum.melonds.domain.model.emulator.LuaScriptOption
import me.magnum.melonds.domain.model.emulator.LuaScriptOptionType
import me.magnum.melonds.domain.model.emulator.resolveValues
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class LuaScriptOptionTest {

    private val toggle = LuaScriptOption.Toggle("hud", "Show HUD", default = true)
    private val integerNumber = LuaScriptOption.Number("speed", "Speed", min = 1.0, max = 10.0, step = 1.0, default = 5.0, decimals = 0)
    private val fractionNumber = LuaScriptOption.Number("zoom", "Zoom", min = 0.1, max = 1.0, step = 0.1, default = 0.5, decimals = 1)
    private val choice = LuaScriptOption.Choice("mode", "Mode", listOf("easy", "normal", "hard"), default = "normal")

    @Test
    fun badOrMissingValuesFallBackToTheDefault() {
        assertEquals("true", toggle.resolve(null))
        assertEquals("true", toggle.resolve("yes"))
        assertEquals("5", integerNumber.resolve("eleven"))
        assertEquals("5", integerNumber.resolve("11"))
        assertEquals("5", integerNumber.resolve("NaN"))
        assertEquals("normal", choice.resolve("insane"))
    }

    @Test
    fun goodValuesAreKept() {
        assertEquals("false", toggle.resolve("false"))
        assertEquals("10", integerNumber.resolve("10"))
        assertEquals("hard", choice.resolve("hard"))
    }

    @Test
    fun numbersGoOntoTheStepsWithoutFloatNoise() {
        assertEquals("7", integerNumber.resolve("6.6"))
        assertEquals("0.7", fractionNumber.resolve("0.7000000000000001"))
        assertEquals("0.3", fractionNumber.resolve("0.31"))
        assertEquals("1", fractionNumber.resolve("1.0"))
        assertEquals(9, fractionNumber.stepCount)
    }

    @Test
    fun nativeValuesAreTyped() {
        val toggleValue = toggle.toNativeValue("true")
        assertEquals(LuaScriptOptionType.BOOLEAN, toggleValue.optionType)
        assertEquals(1L, toggleValue.integer)

        val integerValue = integerNumber.toNativeValue("7")
        assertEquals(LuaScriptOptionType.INTEGER, integerValue.optionType)
        assertEquals(7L, integerValue.integer)

        // A whole number from a fractional slider is still an integer
        assertEquals(LuaScriptOptionType.INTEGER, fractionNumber.toNativeValue("1").optionType)
        val fractionValue = fractionNumber.toNativeValue("0.3")
        assertEquals(LuaScriptOptionType.NUMBER, fractionValue.optionType)
        assertEquals(0.3, fractionValue.number, 0.0)

        val choiceValue = choice.toNativeValue("hard")
        assertEquals(LuaScriptOptionType.STRING, choiceValue.optionType)
        assertArrayEquals("hard".encodeToByteArray(), choiceValue.text)
    }

    @Test
    fun resolvingValuesDropsUndeclaredKeys() {
        val values = listOf(toggle, integerNumber, choice).resolveValues(mapOf("speed" to "3", "gone" to "x"))

        assertEquals(mapOf("hud" to "true", "speed" to "3", "mode" to "normal"), values)
    }

    @Test
    fun layoutValuesAreAUuidOrDontChange() {
        val layout = LuaScriptOption.Layout("map", "Map")
        val id = "123e4567-e89b-12d3-a456-426614174000"

        assertEquals("", layout.resolve(null))
        assertEquals("", layout.resolve("not a layout"))
        assertEquals(id, layout.resolve(id))
        // Kept in the canonical form
        assertEquals(id, layout.resolve(id.uppercase()))
    }

    @Test
    fun scriptsOnlySeeWhetherALayoutWasPicked() {
        val layout = LuaScriptOption.Layout("map", "Map")

        val picked = layout.toNativeValue("123e4567-e89b-12d3-a456-426614174000")
        assertEquals(LuaScriptOptionType.LAYOUT, picked.optionType)
        assertEquals(1L, picked.integer)
        assertEquals(0L, layout.toNativeValue("").integer)
    }
}
