package me.magnum.melonds.ui.emulator

import me.magnum.melonds.domain.model.render.Shade
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LuaShadeRequestsTest {

    private val shadeA = Shade(strength = 0.5f)
    private val shadeB = Shade(strength = 0.8f, radius = 10f, range = 0.001f, color = 0x102030)
    private val off = Shade(strength = 0f)

    @Test
    fun theNewestRequestWinsAndStoppingFallsBack() {
        val requests = LuaShadeRequests()

        assertEquals(shadeA, requests.onScriptRequest(0, shadeA))
        assertEquals(shadeB, requests.onScriptRequest(1, shadeB))

        assertEquals(shadeA, requests.onScriptStopped(1))
        assertNull(requests.onScriptStopped(0))
    }

    @Test
    fun stoppingAnOlderScriptKeepsTheNewestRequest() {
        val requests = LuaShadeRequests()
        requests.onScriptRequest(0, shadeA)
        requests.onScriptRequest(1, shadeB)

        assertEquals(shadeB, requests.onScriptStopped(0))
    }

    @Test
    fun aRepeatedRequestBecomesTheMostRecent() {
        val requests = LuaShadeRequests()
        requests.onScriptRequest(0, shadeA)
        requests.onScriptRequest(1, shadeB)

        assertEquals(shadeA, requests.onScriptRequest(0, shadeA))
    }

    @Test
    fun askingForNoneDropsOnlyThatScriptsRequest() {
        val requests = LuaShadeRequests()
        requests.onScriptRequest(0, shadeA)
        requests.onScriptRequest(1, shadeB)

        assertEquals(shadeA, requests.onScriptRequest(1, off))
        assertNull(requests.onScriptRequest(0, off))
    }

    @Test
    fun aLateRequestFromAStoppedScriptIsIgnoredUntilItStartsAgain() {
        val requests = LuaShadeRequests()
        requests.onScriptStopped(0)

        assertNull(requests.onScriptRequest(0, shadeA))

        requests.onScriptStarted(0)
        assertEquals(shadeA, requests.onScriptRequest(0, shadeA))
    }

    @Test
    fun sessionEndForgetsRequestsAndStoppedScripts() {
        val requests = LuaShadeRequests()
        requests.onScriptRequest(0, shadeA)
        requests.onScriptStopped(1)

        assertNull(requests.onSessionEnded())
        assertNull(requests.shade)
        assertEquals(shadeB, requests.onScriptRequest(1, shadeB))
    }

    @Test
    fun shadingNeedsStrength() {
        assertFalse(shadeA.isOff)
        assertTrue(shadeA.copy(strength = 0f).isOff)
        assertTrue(off.isOff)

        val requests = LuaShadeRequests()
        assertNull(requests.onScriptRequest(0, off))
    }

    // Both the same-frame cache and the flow to the renderers tell changes apart by equality
    @Test
    fun everyValueCountsAsAChange() {
        assertNotEquals(shadeB, shadeB.copy(strength = 0.6f))
        assertNotEquals(shadeB, shadeB.copy(radius = 5f))
        assertNotEquals(shadeB, shadeB.copy(range = 0.002f))
        assertNotEquals(shadeB, shadeB.copy(color = 0x102031))
        assertEquals(shadeB, shadeB.copy())
    }
}
