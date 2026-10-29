package me.magnum.melonds.domain.model.emulator

import me.magnum.melonds.domain.model.layout.LayoutConfiguration
import org.junit.Assert.assertEquals
import org.junit.Test
import java.util.UUID

class LuaLayoutChoiceTest {

    private fun layout(id: UUID?, name: String?) = LayoutConfiguration().copy(id = id, name = name)

    private val idA = UUID.fromString("00000000-0000-0000-0000-00000000000a")
    private val idB = UUID.fromString("00000000-0000-0000-0000-00000000000b")
    private val idC = UUID.fromString("00000000-0000-0000-0000-00000000000c")

    @Test
    fun namedLayoutsKeepTheirNameInOrder() {
        val choices = listOf(layout(idA, "Default"), layout(idB, "Map")).toLuaLayoutChoices()

        assertEquals(listOf(LuaLayoutChoice(idA, "Default"), LuaLayoutChoice(idB, "Map")), choices)
    }

    @Test
    fun aSingleUnnamedLayoutGetsNoNumber() {
        val choices = listOf(layout(idA, "Default"), layout(idB, null)).toLuaLayoutChoices()

        assertEquals(LuaLayoutChoice(idB, null, unnamedNumber = 0), choices[1])
    }

    @Test
    fun severalUnnamedLayoutsAreNumberedAndBlankNamesCountAsNone() {
        val choices = listOf(layout(idA, null), layout(idB, "Map"), layout(idC, "  ")).toLuaLayoutChoices()

        assertEquals(
            listOf(LuaLayoutChoice(idA, null, 1), LuaLayoutChoice(idB, "Map", 0), LuaLayoutChoice(idC, null, 2)),
            choices,
        )
    }

    @Test
    fun layoutsWithoutAnIdAreLeftOut() {
        assertEquals(listOf(LuaLayoutChoice(idA, "Map")), listOf(layout(null, "New"), layout(idA, "Map")).toLuaLayoutChoices())
    }
}
