package me.magnum.melonds.impl

import me.magnum.melonds.domain.model.emulator.LuaScriptOption
import me.magnum.melonds.domain.model.emulator.LuaScriptOptionValue
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
        assertEquals(LuaScriptOptionValue.TYPE_BOOLEAN, toggleValue.type)
        assertEquals(1L, toggleValue.integer)

        val integerValue = integerNumber.toNativeValue("7")
        assertEquals(LuaScriptOptionValue.TYPE_INTEGER, integerValue.type)
        assertEquals(7L, integerValue.integer)

        // A whole number from a fractional slider is still an integer
        assertEquals(LuaScriptOptionValue.TYPE_INTEGER, fractionNumber.toNativeValue("1").type)
        val fractionValue = fractionNumber.toNativeValue("0.3")
        assertEquals(LuaScriptOptionValue.TYPE_NUMBER, fractionValue.type)
        assertEquals(0.3, fractionValue.number, 0.0)

        val choiceValue = choice.toNativeValue("hard")
        assertEquals(LuaScriptOptionValue.TYPE_STRING, choiceValue.type)
        assertArrayEquals("hard".encodeToByteArray(), choiceValue.text)
    }

    @Test
    fun resolvingValuesDropsUndeclaredKeys() {
        val values = listOf(toggle, integerNumber, choice).resolveValues(mapOf("speed" to "3", "gone" to "x"))

        assertEquals(mapOf("hud" to "true", "speed" to "3", "mode" to "normal"), values)
    }
}
