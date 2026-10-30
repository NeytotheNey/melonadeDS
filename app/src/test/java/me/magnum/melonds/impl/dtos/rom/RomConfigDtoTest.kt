package me.magnum.melonds.impl.dtos.rom

import com.google.gson.Gson
import com.google.gson.JsonObject
import me.magnum.melonds.domain.model.rom.config.RomConfig
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class RomConfigDtoTest {

    private val gson = Gson()

    @Test
    fun swappedScreensAreSavedAndLoaded() {
        val json = gson.toJson(RomConfigDto.fromModel(RomConfig(screensSwapped = true)))

        assertTrue(gson.fromJson(json, RomConfigDto::class.java).toModel().screensSwapped)
    }

    @Test
    fun configSavedBeforeTheSwapWasKeptLoadsAsNotSwapped() {
        val json = gson.toJsonTree(RomConfigDto.fromModel(RomConfig(customName = "Animal Crossing"))).asJsonObject.apply {
            remove("screensSwapped")
        }

        val config = gson.fromJson(json, RomConfigDto::class.java).toModel()
        assertFalse(config.screensSwapped)
        assertEquals("Animal Crossing", config.customName)
    }

    @Test
    fun configWithoutModsOrSwapStillLoads() {
        // The oldest shape this DTO reads
        val json = JsonObject().apply {
            addProperty("runtimeConsoleType", "DEFAULT")
            addProperty("runtimeMicSource", "DEFAULT")
            add("gbaSlotConfig", JsonObject().apply { addProperty("type", "None") })
        }

        assertEquals(RomConfig(), gson.fromJson(json, RomConfigDto::class.java).toModel())
    }
}
