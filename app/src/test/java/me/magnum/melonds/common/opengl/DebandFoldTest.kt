package me.magnum.melonds.common.opengl

import me.magnum.melonds.domain.model.VideoFiltering
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The Fold inner screen at 8x: a 3408x3088 frame texture (10.5 Mtexels) shown in ~4.5 Mpx of screens
 */
class DebandFoldTest {

    private val frameTexels = 3408L * 3088L
    private val drawnPixels = 4_500_000L

    @Test
    fun foldsWhenEverythingAllowsIt() {
        assertTrue(shouldFoldDeband(isSwitchOn = true, canFilterFold = true, isLuaEffectActive = false, drawnPixels, frameTexels))
    }

    @Test
    fun switchOffKeepsThePrePass() {
        assertFalse(shouldFoldDeband(isSwitchOn = false, canFilterFold = true, isLuaEffectActive = false, drawnPixels, frameTexels))
    }

    @Test
    fun aFilterThatCantTakeItKeepsThePrePass() {
        assertFalse(shouldFoldDeband(isSwitchOn = true, canFilterFold = false, isLuaEffectActive = false, drawnPixels, frameTexels))
    }

    @Test
    fun aLuaEffectKeepsThePrePass() {
        assertFalse(shouldFoldDeband(isSwitchOn = true, canFilterFold = true, isLuaEffectActive = true, drawnPixels, frameTexels))
    }

    @Test
    fun screensDrawingAsManyPixelsAsTheFrameHasTexelsKeepThePrePass() {
        assertFalse(shouldFoldDeband(isSwitchOn = true, canFilterFold = true, isLuaEffectActive = false, frameTexels, frameTexels))
    }

    @Test
    fun anUpscaledFrameKeepsThePrePass() {
        // 1x frame (256x390) on a 1080x2400 phone's two screens
        assertFalse(shouldFoldDeband(isSwitchOn = true, canFilterFold = true, isLuaEffectActive = false, 1_440_000L, 256L * 390L))
    }

    @Test
    fun oneFewerPixelThanTexelsFolds() {
        assertTrue(shouldFoldDeband(isSwitchOn = true, canFilterFold = true, isLuaEffectActive = false, frameTexels - 1, frameTexels))
    }

    @Test
    fun nothingDrawnKeepsThePrePass() {
        assertFalse(shouldFoldDeband(isSwitchOn = true, canFilterFold = true, isLuaEffectActive = false, 0L, frameTexels))
    }

    @Test
    fun onlySharpAndLinearCanFold() {
        val foldable = VideoFiltering.values().filter { VideoFilterShaderProvider.getFilter(it, deband = true).canFoldDeband }
        assertEquals(setOf(VideoFiltering.SHARP, VideoFiltering.LINEAR), foldable.toSet())
        // Deband is a flag now, not a pre-pass in the list
        assertTrue(VideoFilterShaderProvider.getFilter(VideoFiltering.SHARP, deband = true).prePasses.isEmpty())
        assertTrue(VideoFilterShaderProvider.getFilter(VideoFiltering.MMPX, deband = true).deband)
    }
}
