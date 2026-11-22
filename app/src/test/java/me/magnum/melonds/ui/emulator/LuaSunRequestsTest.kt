package me.magnum.melonds.ui.emulator

import me.magnum.melonds.domain.model.render.Sun
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LuaSunRequestsTest {

    private val sunA = Sun(strength = 0.5f)
    private val sunB = Sun(
        strength = 0.8f, directionX = 0.6f, directionY = 0.8f, directionZ = 0f, upX = 0f, upY = 1f, upZ = 0f, length = 64f, steps = 32,
        bias = 2f, softness = 3f, color = 0x102030,
    )
    private val off = Sun(strength = 0f)

    @Test
    fun theNewestRequestWinsAndStoppingFallsBack() {
        val requests = LuaSunRequests()

        assertEquals(sunA, requests.onScriptRequest(0, sunA))
        assertEquals(sunB, requests.onScriptRequest(1, sunB))

        assertEquals(sunA, requests.onScriptStopped(1))
        assertNull(requests.onScriptStopped(0))
    }

    @Test
    fun stoppingAnOlderScriptKeepsTheNewestRequest() {
        val requests = LuaSunRequests()
        requests.onScriptRequest(0, sunA)
        requests.onScriptRequest(1, sunB)

        assertEquals(sunB, requests.onScriptStopped(0))
    }

    @Test
    fun aRepeatedRequestBecomesTheMostRecent() {
        val requests = LuaSunRequests()
        requests.onScriptRequest(0, sunA)
        requests.onScriptRequest(1, sunB)

        assertEquals(sunA, requests.onScriptRequest(0, sunA))
    }

    @Test
    fun askingForNoneDropsOnlyThatScriptsRequest() {
        val requests = LuaSunRequests()
        requests.onScriptRequest(0, sunA)
        requests.onScriptRequest(1, sunB)

        assertEquals(sunA, requests.onScriptRequest(1, off))
        assertNull(requests.onScriptRequest(0, off))
    }

    @Test
    fun aLateRequestFromAStoppedScriptIsIgnoredUntilItStartsAgain() {
        val requests = LuaSunRequests()
        requests.onScriptStopped(0)

        assertNull(requests.onScriptRequest(0, sunA))

        requests.onScriptStarted(0)
        assertEquals(sunA, requests.onScriptRequest(0, sunA))
    }

    @Test
    fun sessionEndForgetsRequestsAndStoppedScripts() {
        val requests = LuaSunRequests()
        requests.onScriptRequest(0, sunA)
        requests.onScriptStopped(1)

        assertNull(requests.onSessionEnded())
        assertNull(requests.sun)
        assertEquals(sunB, requests.onScriptRequest(1, sunB))
    }

    @Test
    fun sunShadowsNeedStrengthStepsAndLength() {
        assertFalse(sunA.isOff)
        assertTrue(sunA.copy(strength = 0f).isOff)
        assertTrue(sunA.copy(steps = 0).isOff)
        assertTrue(sunA.copy(length = 0f).isOff)
        assertTrue(off.isOff)

        val requests = LuaSunRequests()
        assertNull(requests.onScriptRequest(0, off))
    }

    // Both the same-frame cache and the flow to the renderers tell changes apart by equality. up counts too: the march doesn't use it, but
    // the characters' shadows will
    @Test
    fun everyValueCountsAsAChange() {
        assertNotEquals(sunB, sunB.copy(strength = 0.6f))
        assertNotEquals(sunB, sunB.copy(directionX = 0.5f))
        assertNotEquals(sunB, sunB.copy(directionY = 0.7f))
        assertNotEquals(sunB, sunB.copy(directionZ = 0.1f))
        assertNotEquals(sunB, sunB.copy(upX = 0.1f))
        assertNotEquals(sunB, sunB.copy(upY = 0.9f))
        assertNotEquals(sunB, sunB.copy(upZ = 0.1f))
        assertNotEquals(sunB, sunB.copy(length = 96f))
        assertNotEquals(sunB, sunB.copy(steps = 64))
        assertNotEquals(sunB, sunB.copy(bias = 1f))
        assertNotEquals(sunB, sunB.copy(softness = 1f))
        assertNotEquals(sunB, sunB.copy(color = 0x102031))
        assertEquals(sunB, sunB.copy())
    }
}
