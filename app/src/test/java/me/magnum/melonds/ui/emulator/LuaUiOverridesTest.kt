package me.magnum.melonds.ui.emulator

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LuaUiOverridesTest {

    private val swap = LuaUiSetting.SWAP_SCREENS
    private val fastForward = LuaUiSetting.FAST_FORWARD
    private val showControls = LuaUiSetting.SHOW_CONTROLS

    @Test
    fun stoppedScriptRestoresThePlayersValue() {
        val overrides = LuaUiOverrides()
        assertEquals(true, overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = false))

        assertEquals(mapOf(swap to false), overrides.onScriptStopped(0))
        assertNull(overrides.getScriptValue(swap))
    }

    @Test
    fun baselineIsTakenBeforeTheFirstRequestOnly() {
        val overrides = LuaUiOverrides()
        overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = false)
        // The UI now shows the script's value, which must not become the baseline
        overrides.onScriptRequest(swap, scriptId = 1, value = false, currentValue = true)

        overrides.onScriptStopped(1)
        assertEquals(mapOf(swap to false), overrides.onScriptStopped(0))
    }

    @Test
    fun stoppingOneScriptAppliesTheMostRecentRemainingRequest() {
        val overrides = LuaUiOverrides()
        overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = false)
        overrides.onScriptRequest(swap, scriptId = 1, value = false, currentValue = true)
        overrides.onScriptRequest(swap, scriptId = 2, value = true, currentValue = false)

        assertEquals(mapOf(swap to false), overrides.onScriptStopped(2))
        assertEquals(false, overrides.getScriptValue(swap))
    }

    @Test
    fun aRepeatedRequestBecomesTheMostRecent() {
        val overrides = LuaUiOverrides()
        overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = false)
        overrides.onScriptRequest(swap, scriptId = 1, value = false, currentValue = true)
        overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = false)

        assertEquals(mapOf(swap to true), overrides.onScriptStopped(1))
    }

    @Test
    fun settingsAreTrackedSeparately() {
        val overrides = LuaUiOverrides()
        overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = false)
        overrides.onScriptRequest(fastForward, scriptId = 1, value = true, currentValue = false)

        assertEquals(mapOf(swap to false), overrides.onScriptStopped(0))
        assertEquals(true, overrides.getScriptValue(fastForward))
    }

    @Test
    fun stoppingAScriptThatHeldNothingChangesNothing() {
        val overrides = LuaUiOverrides()
        overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = false)

        assertTrue(overrides.onScriptStopped(1).isEmpty())
    }

    @Test
    fun playerChangeWinsAndBecomesTheNewBaseline() {
        val overrides = LuaUiOverrides()
        overrides.onScriptRequest(fastForward, scriptId = 0, value = true, currentValue = false)
        overrides.onPlayerChanged(fastForward)

        // Nothing to undo once the player took over
        assertTrue(overrides.onScriptStopped(0).isEmpty())
        assertNull(overrides.getScriptValue(fastForward))

        // The player's value (on) is the baseline for the next request
        overrides.onScriptRequest(fastForward, scriptId = 1, value = false, currentValue = true)
        assertEquals(mapOf(fastForward to true), overrides.onScriptStopped(1))
    }

    @Test
    fun aLateRequestFromAStoppedScriptIsIgnored() {
        val overrides = LuaUiOverrides()
        overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = false)
        overrides.onScriptStopped(0)

        assertEquals(false, overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = false))
        assertNull(overrides.getScriptValue(swap))
    }

    @Test
    fun aScriptSwitchedBackOnCanRequestAgain() {
        val overrides = LuaUiOverrides()
        overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = false)
        overrides.onScriptStopped(0)
        overrides.onScriptStarted(0)

        // Its new on_load() request applies, and the player's value is the baseline again
        assertEquals(true, overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = false))
        assertEquals(true, overrides.getScriptValue(swap))
        assertEquals(mapOf(swap to false), overrides.onScriptStopped(0))
    }

    @Test
    fun aNewSessionsFastForwardBaselineIsOff() {
        val overrides = LuaUiOverrides()
        overrides.onScriptRequest(fastForward, scriptId = 0, value = true, currentValue = false)
        overrides.onSessionEnded()

        // The toggle still shows the last game's value when the next game's script asks
        overrides.onScriptRequest(fastForward, scriptId = 0, value = false, currentValue = true)
        assertEquals(mapOf(fastForward to false), overrides.onScriptStopped(0))
    }

    @Test
    fun playersFastForwardChangeBeforeAnyRequestIsTheBaseline() {
        val overrides = LuaUiOverrides()
        overrides.onPlayerChanged(fastForward)

        overrides.onScriptRequest(fastForward, scriptId = 0, value = false, currentValue = true)
        assertEquals(mapOf(fastForward to true), overrides.onScriptStopped(0))
    }

    @Test
    fun aScriptStoppedInOneSessionCanRequestInTheNext() {
        val overrides = LuaUiOverrides()
        overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = false)
        overrides.onScriptStopped(0)
        overrides.onSessionEnded()

        assertEquals(true, overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = false))
        assertEquals(true, overrides.getScriptValue(swap))
    }

    @Test
    fun hiddenControlsComeBackWhenTheScriptStops() {
        val overrides = LuaUiOverrides()
        assertEquals(false, overrides.onScriptRequest(showControls, scriptId = 0, value = false, currentValue = true))

        assertEquals(mapOf(showControls to true), overrides.onScriptStopped(0))
    }

    @Test
    fun sessionEndRestoresEveryHeldSettingAndForgetsThem() {
        val overrides = LuaUiOverrides()
        overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = false)
        overrides.onScriptRequest(fastForward, scriptId = 1, value = true, currentValue = false)

        assertEquals(mapOf(swap to false, fastForward to false), overrides.onSessionEnded())
        assertNull(overrides.getScriptValue(fastForward))
        assertTrue(overrides.onSessionEnded().isEmpty())
    }
}
