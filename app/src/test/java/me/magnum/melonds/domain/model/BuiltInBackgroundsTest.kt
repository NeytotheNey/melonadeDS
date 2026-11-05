package me.magnum.melonds.domain.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.util.UUID

class BuiltInBackgroundsTest {

    @Test
    fun namesAreMappedFromTheFileNamesInOrder() {
        val expected = listOf(
            "crimson.svg" to "Crimson",
            "onyx.svg" to "Onyx",
            "pearl.svg" to "Pearl",
            "pearl-tech.svg" to "Pearl Tech",
            "atomic-purple.svg" to "Atomic Purple",
            "cobalt.svg" to "Cobalt",
            "glacier.svg" to "Glacier",
            "kiwi.svg" to "Kiwi",
            "dandelion.svg" to "Dandelion",
            "gunmetal.svg" to "Gunmetal",
            "coral.svg" to "Coral",
            "smoke.svg" to "Smoke",
            "carbon.svg" to "Carbon",
            "hex.svg" to "Hex",
        )

        assertEquals(expected, BuiltInBackgrounds.all.map { it.fileName to it.name })
    }

    @Test
    fun everyAssetIsListedAndEveryListedOneExists() {
        val assets = File("src/main/assets", BuiltInBackgrounds.ASSET_FOLDER).list()?.toSet().orEmpty()

        assertEquals(assets, BuiltInBackgrounds.all.map { it.fileName }.toSet())
    }

    @Test
    fun idsNeverChange() {
        // Saved layouts refer to these, so they're spelled out: if this fails, layouts have lost their built-in background
        assertEquals(UUID.fromString("d0a24168-0744-3d47-83b7-c9ef7e07ea91"), BuiltInBackgrounds.stableId("carbon.svg"))
        assertEquals(UUID.fromString("670c3731-2c26-3446-8235-506811e60c85"), BuiltInBackgrounds.stableId("pearl-tech.svg"))
        assertEquals(BuiltInBackgrounds.all.size, BuiltInBackgrounds.all.map { it.id }.toSet().size)
    }

    @Test
    fun onlyBuiltInIdsAreBuiltIn() {
        assertTrue(BuiltInBackgrounds.all.all { BuiltInBackgrounds.isBuiltIn(it.id) })
        assertFalse(BuiltInBackgrounds.isBuiltIn(UUID.randomUUID()))
        assertFalse(BuiltInBackgrounds.isBuiltIn(null))
    }

    @Test
    fun mergePutsBuiltInsFirstAndKeepsTheOrderOfBoth() {
        val builtIns = BuiltInBackgrounds.all.map { it.id }
        val userBackgrounds = listOf(UUID.randomUUID(), UUID.randomUUID())

        assertEquals(builtIns + userBackgrounds, BuiltInBackgrounds.merge(builtIns, userBackgrounds) { it })
    }

    @Test
    fun mergeDropsUserBackgroundsThatClaimABuiltInId() {
        val builtIns = BuiltInBackgrounds.all.map { it.id }
        val userBackground = UUID.randomUUID()

        assertEquals(builtIns + userBackground, BuiltInBackgrounds.merge(builtIns, listOf(builtIns.first(), userBackground)) { it })
    }
}
