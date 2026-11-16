package me.magnum.melonds.ui.emulator

import me.magnum.melonds.domain.model.camera.WideCameraRequest
import org.junit.Assert.assertEquals
import org.junit.Test

class LuaWideCameraRequestsTest {

    private val zoomOut = WideCameraRequest(zoom = 1.5f)
    private val panRight = WideCameraRequest(pan = 40f)
    private val hold = WideCameraRequest(holdNative = true)

    @Test
    fun theNewestRequestWinsAndStoppingFallsBack() {
        val requests = LuaWideCameraRequests()
        requests.onScriptRequest(0, WideCameraRequest(zoom = 1.25f))
        assertEquals(zoomOut, requests.onScriptRequest(1, zoomOut))

        assertEquals(WideCameraRequest(zoom = 1.25f), requests.onScriptStopped(1))
        assertEquals(WideCameraRequest.NONE, requests.onScriptStopped(0))
    }

    @Test
    fun eachAxisOfThePanComesFromTheNewestScriptAskingForIt() {
        val requests = LuaWideCameraRequests()
        requests.onScriptRequest(0, WideCameraRequest(pan = 10f, panY = 20f))
        // pan(30) with y left out: y stays the older script's
        assertEquals(WideCameraRequest(pan = 30f, panY = 20f), requests.onScriptRequest(1, WideCameraRequest(pan = 30f)))
        assertEquals(WideCameraRequest(pan = 10f, panY = 20f), requests.onScriptStopped(1))
        // Only a vertical pan still counts as a request
        assertEquals(WideCameraRequest(panY = -5f), requests.onScriptRequest(0, WideCameraRequest(panY = -5f)))
    }

    @Test
    fun zoomAndPanCanComeFromDifferentScripts() {
        val requests = LuaWideCameraRequests()
        requests.onScriptRequest(0, zoomOut)

        assertEquals(WideCameraRequest(zoom = 1.5f, pan = 40f), requests.onScriptRequest(1, panRight))
    }

    @Test
    fun anyScriptHoldingNativeHoldsIt() {
        val requests = LuaWideCameraRequests()
        requests.onScriptRequest(0, hold)
        val request = requests.onScriptRequest(1, zoomOut)

        assertEquals(WideCameraRequest(zoom = 1.5f, holdNative = true), request)
        assertEquals(zoomOut, requests.onScriptStopped(0))
    }

    @Test
    fun aChangedRequestBecomesTheMostRecent() {
        val requests = LuaWideCameraRequests()
        requests.onScriptRequest(0, zoomOut)
        requests.onScriptRequest(1, WideCameraRequest(zoom = 2f))

        assertEquals(WideCameraRequest(zoom = 1.5f, pan = 10f), requests.onScriptRequest(0, WideCameraRequest(zoom = 1.5f, pan = 10f)))
    }

    @Test
    fun askingForNothingDropsOnlyThatScriptsRequest() {
        val requests = LuaWideCameraRequests()
        requests.onScriptRequest(0, zoomOut)
        requests.onScriptRequest(1, panRight)

        assertEquals(zoomOut, requests.onScriptRequest(1, WideCameraRequest.NONE))
        assertEquals(WideCameraRequest.NONE, requests.onScriptRequest(0, WideCameraRequest.NONE))
    }

    @Test
    fun aLateRequestFromAStoppedScriptIsIgnoredUntilItStartsAgain() {
        val requests = LuaWideCameraRequests()
        requests.onScriptStopped(0)

        assertEquals(WideCameraRequest.NONE, requests.onScriptRequest(0, hold))

        requests.onScriptStarted(0)
        assertEquals(hold, requests.onScriptRequest(0, hold))
    }

    @Test
    fun aSessionEndDropsEverything() {
        val requests = LuaWideCameraRequests()
        requests.onScriptRequest(0, zoomOut)
        requests.onScriptStopped(1)

        assertEquals(WideCameraRequest.NONE, requests.onSessionEnded())
        // Script 1's stop belonged to the old session
        assertEquals(panRight, requests.onScriptRequest(1, panRight))
    }
}
