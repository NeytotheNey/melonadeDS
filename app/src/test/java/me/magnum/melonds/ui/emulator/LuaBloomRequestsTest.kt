package me.magnum.melonds.ui.emulator

import me.magnum.melonds.domain.model.render.Bloom
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LuaBloomRequestsTest {

    private val bloomA = Bloom(strength = 1f, threshold = 0.7f, radius = 3f, vignette = 0f, vignetteSize = 0.6f)
    private val bloomB = Bloom(strength = 0.5f, threshold = 0.5f, radius = 4f, vignette = 0.4f, vignetteSize = 0.5f)
    private val off = Bloom(strength = 0f, threshold = 0.7f, radius = 3f, vignette = 0f, vignetteSize = 0.6f)

    @Test
    fun theNewestRequestWinsAndStoppingFallsBack() {
        val requests = LuaBloomRequests()

        assertEquals(bloomA, requests.onScriptRequest(0, bloomA))
        assertEquals(bloomB, requests.onScriptRequest(1, bloomB))

        assertEquals(bloomA, requests.onScriptStopped(1))
        assertNull(requests.onScriptStopped(0))
    }

    @Test
    fun stoppingAnOlderScriptKeepsTheNewestRequest() {
        val requests = LuaBloomRequests()
        requests.onScriptRequest(0, bloomA)
        requests.onScriptRequest(1, bloomB)

        assertEquals(bloomB, requests.onScriptStopped(0))
    }

    @Test
    fun aRepeatedRequestBecomesTheMostRecent() {
        val requests = LuaBloomRequests()
        requests.onScriptRequest(0, bloomA)
        requests.onScriptRequest(1, bloomB)

        assertEquals(bloomA, requests.onScriptRequest(0, bloomA))
    }

    @Test
    fun askingForNeitherDropsOnlyThatScriptsRequest() {
        val requests = LuaBloomRequests()
        requests.onScriptRequest(0, bloomA)
        requests.onScriptRequest(1, bloomB)

        assertEquals(bloomA, requests.onScriptRequest(1, off))
        assertNull(requests.onScriptRequest(0, off))
    }

    @Test
    fun aLateRequestFromAStoppedScriptIsIgnoredUntilItStartsAgain() {
        val requests = LuaBloomRequests()
        requests.onScriptStopped(0)

        assertNull(requests.onScriptRequest(0, bloomA))

        requests.onScriptStarted(0)
        assertEquals(bloomA, requests.onScriptRequest(0, bloomA))
    }

    @Test
    fun sessionEndForgetsRequestsAndStoppedScripts() {
        val requests = LuaBloomRequests()
        requests.onScriptRequest(0, bloomA)
        requests.onScriptStopped(1)

        assertNull(requests.onSessionEnded())
        assertNull(requests.bloom)
        assertEquals(bloomB, requests.onScriptRequest(1, bloomB))
    }

    @Test
    fun aGlowNeedsStrengthAndRadius() {
        assertTrue(bloomA.hasBloom)
        assertFalse(bloomA.copy(strength = 0f).hasBloom)
        assertFalse(bloomA.copy(radius = 0f).hasBloom)
        assertTrue(off.isOff)
        assertTrue(bloomA.copy(radius = 0f).isOff)
    }

    @Test
    fun aVignetteAloneKeepsItOn() {
        val vignetteOnly = off.copy(vignette = 0.5f)

        assertFalse(vignetteOnly.hasBloom)
        assertTrue(vignetteOnly.hasVignette)
        assertFalse(vignetteOnly.isOff)

        val requests = LuaBloomRequests()
        assertEquals(vignetteOnly, requests.onScriptRequest(0, vignetteOnly))
        assertNull(requests.onScriptStopped(0))
    }

    @Test
    fun aHazeAloneKeepsItOn() {
        val hazeOnly = off.copy(haze = 0.5f)

        assertFalse(hazeOnly.hasBloom)
        assertFalse(hazeOnly.hasVignette)
        assertTrue(hazeOnly.hasHaze)
        assertFalse(hazeOnly.isOff)
        assertFalse(off.hasHaze)

        val requests = LuaBloomRequests()
        assertEquals(hazeOnly, requests.onScriptRequest(0, hazeOnly))
        assertNull(requests.onScriptStopped(0))
    }

    // The glow, vignette and haze are one request, so the newest script's replaces all three, even one that only asks for the haze
    @Test
    fun theNewestRequestWinsForAllThreeTogether() {
        val hazeOnly = off.copy(haze = 0.8f, hazeColor = 0x102030)
        val requests = LuaBloomRequests()
        requests.onScriptRequest(0, bloomB.copy(haze = 0.3f))

        assertEquals(hazeOnly, requests.onScriptRequest(1, hazeOnly))
        assertEquals(bloomB.copy(haze = 0.3f), requests.onScriptStopped(1))
    }

    @Test
    fun onlyThreeDIsOnByDefault() {
        assertTrue(bloomA.onlyThreeD)
        // It doesn't turn anything on by itself
        assertTrue(off.copy(onlyThreeD = false).isOff)
    }

    // Both the same-frame cache and the flow to the renderers tell changes apart by equality
    @Test
    fun everyValueCountsAsAChange() {
        assertNotEquals(bloomA, bloomA.copy(strength = 1.5f))
        assertNotEquals(bloomA, bloomA.copy(threshold = 0.6f))
        assertNotEquals(bloomA, bloomA.copy(radius = 4f))
        assertNotEquals(bloomA, bloomA.copy(vignette = 0.2f))
        assertNotEquals(bloomA, bloomA.copy(vignetteSize = 0.5f))
        assertNotEquals(bloomA, bloomA.copy(onlyThreeD = false))
        assertNotEquals(bloomA, bloomA.copy(haze = 0.5f))
        assertNotEquals(bloomA, bloomA.copy(hazeColor = 0x102030))
        assertNotEquals(bloomA, bloomA.copy(hazeStart = 0.99f))
        assertNotEquals(bloomA, bloomA.copy(hazeFalloff = 0.01f))
        assertEquals(bloomA, bloomA.copy())
    }
}
