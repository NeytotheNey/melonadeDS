package me.magnum.melonds.ui.emulator

import me.magnum.melonds.domain.model.render.TiltShift
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LuaTiltShiftRequestsTest {

    private val blurA = TiltShift(top = 1f, bottom = 1f, center = 0.5f, width = 0.3f, radius = 6f)
    private val blurB = TiltShift(top = 0.5f, bottom = 0f, center = 0.4f, width = 0.2f, radius = 4f)
    private val off = TiltShift(top = 0f, bottom = 0f, center = 0.5f, width = 0.3f, radius = 6f)

    @Test
    fun theNewestRequestWinsAndStoppingFallsBack() {
        val requests = LuaTiltShiftRequests()

        assertEquals(blurA, requests.onScriptRequest(0, blurA))
        assertEquals(blurB, requests.onScriptRequest(1, blurB))

        assertEquals(blurA, requests.onScriptStopped(1))
        assertNull(requests.onScriptStopped(0))
    }

    @Test
    fun stoppingAnOlderScriptKeepsTheNewestRequest() {
        val requests = LuaTiltShiftRequests()
        requests.onScriptRequest(0, blurA)
        requests.onScriptRequest(1, blurB)

        assertEquals(blurB, requests.onScriptStopped(0))
    }

    @Test
    fun aRepeatedRequestBecomesTheMostRecent() {
        val requests = LuaTiltShiftRequests()
        requests.onScriptRequest(0, blurA)
        requests.onScriptRequest(1, blurB)

        assertEquals(blurA, requests.onScriptRequest(0, blurA))
    }

    @Test
    fun askingForNoBlurDropsOnlyThatScriptsRequest() {
        val requests = LuaTiltShiftRequests()
        requests.onScriptRequest(0, blurA)
        requests.onScriptRequest(1, blurB)

        assertEquals(blurA, requests.onScriptRequest(1, off))
        assertNull(requests.onScriptRequest(0, off))
    }

    @Test
    fun aLateRequestFromAStoppedScriptIsIgnoredUntilItStartsAgain() {
        val requests = LuaTiltShiftRequests()
        requests.onScriptStopped(0)

        assertNull(requests.onScriptRequest(0, blurA))

        requests.onScriptStarted(0)
        assertEquals(blurA, requests.onScriptRequest(0, blurA))
    }

    @Test
    fun sessionEndForgetsRequestsAndStoppedScripts() {
        val requests = LuaTiltShiftRequests()
        requests.onScriptRequest(0, blurA)
        requests.onScriptStopped(1)

        assertNull(requests.onSessionEnded())
        assertNull(requests.tiltShift)
        assertEquals(blurB, requests.onScriptRequest(1, blurB))
    }

    @Test
    fun theBandStaysOnTheScreen() {
        val blur = TiltShift(top = 1f, bottom = 1f, center = 0.1f, width = 0.5f, radius = 6f)

        assertEquals(0f, blur.bandTop, 0f)
        assertEquals(0.35f, blur.bandBottom, 0.0001f)
    }

    @Test
    fun aBandReachingAnEdgeLeavesNoBlurOnThatSide() {
        val blur = TiltShift(top = 1f, bottom = 0.5f, center = 0.1f, width = 0.5f, radius = 6f)

        assertEquals(0f, blur.topRadius, 0f)
        assertEquals(3f, blur.bottomRadius, 0.0001f)
        assertFalse(blur.isOff)
    }

    @Test
    fun aBlurWithNothingLeftToBlurIsOff() {
        assertTrue(off.isOff)
        assertTrue(TiltShift(top = 1f, bottom = 1f, center = 0.5f, width = 1f, radius = 6f).isOff)
        assertTrue(TiltShift(top = 1f, bottom = 1f, center = 0.5f, width = 0.3f, radius = 0f).isOff)
    }

    @Test
    fun theColorBoostFollowsTheStrongerBand() {
        val full = TiltShift(top = 1f, bottom = 0f, center = 0.5f, width = 0.3f, radius = 6f, saturation = 1.5f, contrast = 0.5f)
        assertEquals(1.5f, full.appliedSaturation, 0.0001f)
        assertEquals(0.5f, full.appliedContrast, 0.0001f)

        val half = TiltShift(top = 0.25f, bottom = 0.5f, center = 0.5f, width = 0.3f, radius = 6f, saturation = 1.5f, contrast = 0.5f)
        assertEquals(1.25f, half.appliedSaturation, 0.0001f)
        assertEquals(0.75f, half.appliedContrast, 0.0001f)
    }

    @Test
    fun noStrengthMeansNoBoostAndOff() {
        val blur = TiltShift(top = 0f, bottom = 0f, center = 0.5f, width = 0.3f, radius = 6f, saturation = 1.5f, contrast = 1.5f)

        assertEquals(1f, blur.appliedSaturation, 0f)
        assertEquals(1f, blur.appliedContrast, 0f)
        assertTrue(blur.isOff)
    }

    @Test
    fun aColorBoostKeepsItOnWithoutABlur() {
        assertFalse(TiltShift(top = 1f, bottom = 1f, center = 0.5f, width = 1f, radius = 6f, saturation = 1.3f).isOff)
        assertFalse(TiltShift(top = 1f, bottom = 1f, center = 0.5f, width = 0.3f, radius = 0f, contrast = 1.1f).isOff)

        val requests = LuaTiltShiftRequests()
        val boostOnly = TiltShift(top = 1f, bottom = 1f, center = 0.5f, width = 0.3f, radius = 0f, saturation = 1.3f)
        assertEquals(boostOnly, requests.onScriptRequest(0, boostOnly))
    }

    // Both the same-frame cache and the flow to the renderers tell changes apart by equality
    @Test
    fun theColorValuesCountAsAChange() {
        assertNotEquals(blurA, blurA.copy(saturation = 1.3f))
        assertNotEquals(blurA, blurA.copy(contrast = 1.1f))
        assertEquals(blurA, blurA.copy())
    }

    @Test
    fun aColorStrengthKeepsTheBoostOnWithoutBands() {
        val colorOnly = colorOnly(color = 0.5f)

        assertEquals(1.15f, colorOnly.appliedSaturation, 0.0001f)
        assertEquals(1.05f, colorOnly.appliedContrast, 0.0001f)
        assertTrue(colorOnly.hasColorBoost)
        assertFalse(colorOnly.hasBlur)
        assertFalse(colorOnly.isOff)
    }

    @Test
    fun noColorKeepsFollowingTheStrongerBand() {
        val noColor = TiltShift(top = 0.25f, bottom = 0.5f, center = 0.5f, width = 0.3f, radius = 6f, saturation = 1.5f, contrast = 0.5f)

        assertNull(noColor.color)
        assertEquals(1.25f, noColor.appliedSaturation, 0f)
        assertEquals(0.75f, noColor.appliedContrast, 0f)
        assertTrue(colorOnly(color = null).isOff)
    }

    // Blur fading in or out mustn't show in the colors
    @Test
    fun theSameColorGivesTheSameBoostWhateverTheBands() {
        val colorOnly = colorOnly(color = 0.6f)
        val blurred = colorOnly.copy(top = 1f, bottom = 0.25f)

        assertEquals(colorOnly.appliedSaturation, blurred.appliedSaturation, 0f)
        assertEquals(colorOnly.appliedContrast, blurred.appliedContrast, 0f)
        assertFalse(colorOnly.hasBlur)
        assertTrue(blurred.hasBlur)
    }

    @Test
    fun fullColorLooksLikeFullBandsWithoutOne() {
        val noColor = TiltShift(top = 1f, bottom = 1f, center = 0.5f, width = 0.3f, radius = 6f, saturation = 1.3f, contrast = 1.1f)
        val withColor = noColor.copy(color = 1f)

        assertEquals(noColor.appliedSaturation, withColor.appliedSaturation, 0f)
        assertEquals(noColor.appliedContrast, withColor.appliedContrast, 0f)
    }

    @Test
    fun colorZeroTurnsTheBoostOffButLeavesTheBlur() {
        val blurOnly = TiltShift(top = 1f, bottom = 1f, center = 0.5f, width = 0.3f, radius = 6f, saturation = 1.3f, contrast = 1.1f, color = 0f)

        assertEquals(1f, blurOnly.appliedSaturation, 0f)
        assertEquals(1f, blurOnly.appliedContrast, 0f)
        assertFalse(blurOnly.hasColorBoost)
        assertFalse(blurOnly.isOff)
    }

    @Test
    fun aColorWithNothingToDoIsOff() {
        assertTrue(colorOnly(color = 0f).isOff)
        assertTrue(colorOnly(color = 1f).copy(saturation = 1f, contrast = 1f).isOff)
    }

    @Test
    fun aBlurRadiusWithBandsAtTheEdgesLeavesOnlyTheColor() {
        val noRoom = TiltShift(top = 1f, bottom = 1f, center = 0.5f, width = 1f, radius = 6f, saturation = 1.3f, color = 1f)

        assertFalse(noRoom.hasBlur)
        assertFalse(noRoom.isOff)
    }

    @Test
    fun aColorOnlyRequestStaysUntilItsScriptStops() {
        val requests = LuaTiltShiftRequests()
        val colorOnly = colorOnly(color = 1f)

        assertEquals(colorOnly, requests.onScriptRequest(0, colorOnly))
        assertEquals(colorOnly, requests.tiltShift)
        assertNull(requests.onScriptStopped(0))
        assertNull(requests.tiltShift)
    }

    @Test
    fun aColorOnlyRequestFollowsTheSameRulesAsABlur() {
        val requests = LuaTiltShiftRequests()
        val colorOnly = colorOnly(color = 1f)

        requests.onScriptRequest(0, blurA)
        assertEquals(colorOnly, requests.onScriptRequest(1, colorOnly))
        assertEquals(blurA, requests.onScriptRequest(1, off))

        requests.onScriptStopped(2)
        assertEquals(blurA, requests.onScriptRequest(2, colorOnly))

        requests.onScriptRequest(1, colorOnly)
        assertNull(requests.onSessionEnded())
    }

    @Test
    fun theColorStrengthCountsAsAChange() {
        assertNotEquals(blurA, blurA.copy(color = 1f))
        assertNotEquals(blurA.copy(color = 1f), blurA.copy(color = 0.5f))
    }

    private fun colorOnly(color: Float?) = TiltShift(top = 0f, bottom = 0f, center = 0.5f, width = 0.3f, radius = 6f, saturation = 1.3f, contrast = 1.1f, color = color)
}
