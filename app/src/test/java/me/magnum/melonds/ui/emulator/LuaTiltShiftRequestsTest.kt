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
}
