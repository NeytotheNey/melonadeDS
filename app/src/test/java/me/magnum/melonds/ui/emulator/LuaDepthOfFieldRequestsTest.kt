package me.magnum.melonds.ui.emulator

import me.magnum.melonds.domain.model.render.DepthOfField
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LuaDepthOfFieldRequestsTest {

    private val dofA = DepthOfField(strength = 1f, radius = 4f)
    private val dofB = DepthOfField(strength = 0.5f, radius = 6f, focusX = 100f, focusY = 120f, focusDepth = 0.4f, range = 0.01f, falloff = 0.05f)
    private val off = DepthOfField(strength = 0f, radius = 4f)

    @Test
    fun theNewestRequestWinsAndStoppingFallsBack() {
        val requests = LuaDepthOfFieldRequests()

        assertEquals(dofA, requests.onScriptRequest(0, dofA))
        assertEquals(dofB, requests.onScriptRequest(1, dofB))

        assertEquals(dofA, requests.onScriptStopped(1))
        assertNull(requests.onScriptStopped(0))
    }

    @Test
    fun stoppingAnOlderScriptKeepsTheNewestRequest() {
        val requests = LuaDepthOfFieldRequests()
        requests.onScriptRequest(0, dofA)
        requests.onScriptRequest(1, dofB)

        assertEquals(dofB, requests.onScriptStopped(0))
    }

    @Test
    fun aRepeatedRequestBecomesTheMostRecent() {
        val requests = LuaDepthOfFieldRequests()
        requests.onScriptRequest(0, dofA)
        requests.onScriptRequest(1, dofB)

        assertEquals(dofA, requests.onScriptRequest(0, dofA))
    }

    @Test
    fun askingForNoneDropsOnlyThatScriptsRequest() {
        val requests = LuaDepthOfFieldRequests()
        requests.onScriptRequest(0, dofA)
        requests.onScriptRequest(1, dofB)

        assertEquals(dofA, requests.onScriptRequest(1, off))
        assertNull(requests.onScriptRequest(0, off))
    }

    @Test
    fun aLateRequestFromAStoppedScriptIsIgnoredUntilItStartsAgain() {
        val requests = LuaDepthOfFieldRequests()
        requests.onScriptStopped(0)

        assertNull(requests.onScriptRequest(0, dofA))

        requests.onScriptStarted(0)
        assertEquals(dofA, requests.onScriptRequest(0, dofA))
    }

    @Test
    fun sessionEndForgetsRequestsAndStoppedScripts() {
        val requests = LuaDepthOfFieldRequests()
        requests.onScriptRequest(0, dofA)
        requests.onScriptStopped(1)

        assertNull(requests.onSessionEnded())
        assertNull(requests.depthOfField)
        assertEquals(dofB, requests.onScriptRequest(1, dofB))
    }

    @Test
    fun aBlurNeedsStrengthAndRadius() {
        assertFalse(dofA.isOff)
        assertTrue(dofA.copy(strength = 0f).isOff)
        assertTrue(dofA.copy(radius = 0f).isOff)
        assertTrue(off.isOff)

        val requests = LuaDepthOfFieldRequests()
        assertNull(requests.onScriptRequest(0, dofA.copy(radius = 0f)))
    }

    // Both the same-frame cache and the flow to the renderers tell changes apart by equality
    @Test
    fun everyValueCountsAsAChange() {
        assertNotEquals(dofB, dofB.copy(strength = 0.6f))
        assertNotEquals(dofB, dofB.copy(radius = 5f))
        assertNotEquals(dofB, dofB.copy(focusX = 101f))
        assertNotEquals(dofB, dofB.copy(focusY = 121f))
        assertNotEquals(dofB, dofB.copy(focusDepth = -1f))
        assertNotEquals(dofB, dofB.copy(range = 0.02f))
        assertNotEquals(dofB, dofB.copy(falloff = 0.06f))
        assertEquals(dofB, dofB.copy())
    }
}
