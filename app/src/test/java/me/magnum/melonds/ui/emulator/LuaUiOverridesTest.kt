package me.magnum.melonds.ui.emulator

import me.magnum.melonds.domain.model.emulator.LuaScriptOption
import me.magnum.melonds.ui.emulator.model.LuaUiSetting
import kotlinx.coroutines.flow.asFlow
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.UUID

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

    private val layoutA = UUID.fromString("00000000-0000-0000-0000-00000000000a")
    private val layoutB = UUID.fromString("00000000-0000-0000-0000-00000000000b")
    private val requestA = LuaLayoutRequest.Layout(layoutA)
    private val requestB = LuaLayoutRequest.Layout(layoutB)

    @Test
    fun theNewestLayoutRequestWinsAndStoppingFallsBack() {
        val requests = LuaLayoutRequests()
        assertEquals(layoutA, requests.onScriptRequest(0, requestA))
        assertEquals(layoutB, requests.onScriptRequest(1, requestB))

        assertEquals(layoutA, requests.onScriptStopped(1))
        // No baseline: the player's layout (null) once no script holds one
        assertNull(requests.onScriptStopped(0))
    }

    @Test
    fun aPlayerLayoutRequestDropsOnlyThatScriptsRequest() {
        val requests = LuaLayoutRequests()
        requests.onScriptRequest(0, requestA)
        requests.onScriptRequest(1, requestB)

        assertEquals(layoutA, requests.onScriptRequest(1, LuaLayoutRequest.PlayerLayout))
        assertNull(requests.onScriptRequest(0, LuaLayoutRequest.PlayerLayout))
    }

    @Test
    fun aRequestForNothingKeepsTheScriptsEarlierRequest() {
        val requests = LuaLayoutRequests()
        requests.onScriptRequest(0, requestA)
        requests.onScriptRequest(1, requestB)

        assertEquals(layoutB, requests.onScriptRequest(1, LuaLayoutRequest.None))
        // It doesn't make script 0's request the most recent either
        assertEquals(layoutB, requests.onScriptRequest(0, LuaLayoutRequest.None))
        assertEquals(layoutA, requests.onScriptStopped(1))
        // And it doesn't add one for a script that had none
        assertNull(requests.onScriptStopped(0))
        assertNull(requests.onScriptRequest(2, LuaLayoutRequest.None))
    }

    @Test
    fun aRepeatedLayoutRequestBecomesTheMostRecent() {
        val requests = LuaLayoutRequests()
        requests.onScriptRequest(0, requestA)
        requests.onScriptRequest(1, requestB)
        requests.onScriptRequest(0, requestA)

        assertEquals(layoutA, requests.onScriptStopped(2))
        assertEquals(layoutB, requests.onScriptStopped(0))
    }

    @Test
    fun thePlayerPickingALayoutDropsEveryRequest() {
        val requests = LuaLayoutRequests()
        requests.onScriptRequest(0, requestA)

        assertNull(requests.onPlayerChanged())
        assertNull(requests.layoutId)
        // A later request applies again
        assertEquals(layoutB, requests.onScriptRequest(0, requestB))
    }

    @Test
    fun aLateLayoutRequestFromAStoppedScriptIsIgnoredUntilItStartsAgain() {
        val requests = LuaLayoutRequests()
        requests.onScriptStopped(0)
        assertNull(requests.onScriptRequest(0, requestA))

        requests.onScriptStarted(0)
        assertEquals(layoutA, requests.onScriptRequest(0, requestA))
    }

    @Test
    fun sessionEndForgetsLayoutRequestsAndStoppedScripts() {
        val requests = LuaLayoutRequests()
        requests.onScriptRequest(0, requestA)
        requests.onScriptStopped(1)

        assertNull(requests.onSessionEnded())
        assertEquals(layoutB, requests.onScriptRequest(1, requestB))
    }

    @Test
    fun aLayoutRequestIsResolvedFromTheScriptsOptions() {
        val options = listOf(
            LuaScriptOption.Layout("map", "Map"),
            LuaScriptOption.Layout("unset", "Unset"),
            LuaScriptOption.Layout("deleted", "Deleted"),
            LuaScriptOption.Toggle("hud", "HUD", default = true),
        )
        val values = mapOf("map" to layoutA.toString(), "unset" to "", "deleted" to layoutB.toString(), "hud" to "true")
        val layoutExists = { id: UUID -> id == layoutA }

        assertEquals(requestA, getLuaLayoutRequest(options, values, "map", layoutExists))
        assertEquals(LuaLayoutRequest.PlayerLayout, getLuaLayoutRequest(options, values, null, layoutExists))
        // "Don't change", a deleted layout, a key that isn't a layout option, and an unknown key ask for nothing
        assertEquals(LuaLayoutRequest.None, getLuaLayoutRequest(options, values, "unset", layoutExists))
        assertEquals(LuaLayoutRequest.None, getLuaLayoutRequest(options, values, "deleted", layoutExists))
        assertEquals(LuaLayoutRequest.None, getLuaLayoutRequest(options, values, "hud", layoutExists))
        assertEquals(LuaLayoutRequest.None, getLuaLayoutRequest(options, values, "gone", layoutExists))
        // A value that was never stored resolves to "Don't change"
        assertEquals(LuaLayoutRequest.None, getLuaLayoutRequest(options, emptyMap(), "map", layoutExists))
    }

    private fun countPlayerLayoutPicks(romLayout: List<String>?, selectedGlobalLayoutIds: List<UUID>): Int {
        return runBlocking {
            getPlayerLayoutPicks(romLayout?.asFlow(), selectedGlobalLayoutIds.asFlow()).toList().size
        }
    }

    @Test
    fun changingTheGlobalLayoutIsAPickWhenTheRomUsesIt() {
        // The first value is the one the session started with, and the same id again (an edit) isn't a pick
        assertEquals(0, countPlayerLayoutPicks(null, listOf(layoutA)))
        assertEquals(1, countPlayerLayoutPicks(null, listOf(layoutA, layoutA, layoutB)))
        assertEquals(2, countPlayerLayoutPicks(null, listOf(layoutA, layoutB, layoutA)))
    }

    @Test
    fun theRomsOwnLayoutBeingDeletedIsNotAPick() {
        // The ROM's layout was edited, then deleted (its flow completes), so the global one takes over without the player picking it
        assertEquals(0, countPlayerLayoutPicks(listOf("rom layout", "rom layout edited"), listOf(layoutA)))
        // From then on, the global one is the player's, and changing it is a pick
        assertEquals(1, countPlayerLayoutPicks(listOf("rom layout"), listOf(layoutA, layoutB)))
    }

    @Test
    fun aSavedSwapIsTheBaselineOfTheSessionsScripts() {
        val overrides = LuaUiOverrides()
        // The game was left swapped, so the saved value is the first request's baseline even if the screen's value doesn't match it
        overrides.onSessionStarted(mapOf(swap to true))
        assertEquals(false, overrides.onScriptRequest(swap, scriptId = 0, value = false, currentValue = false))

        assertEquals(mapOf(swap to true), overrides.onScriptStopped(0))
    }

    @Test
    fun aSavedSwapIsPutBackWhenTheSessionEnds() {
        val overrides = LuaUiOverrides()
        overrides.onSessionStarted(mapOf(swap to true))
        overrides.onScriptRequest(swap, scriptId = 0, value = false, currentValue = true)

        assertEquals(mapOf(swap to true), overrides.onSessionEnded())
    }

    @Test
    fun scriptsStillWinOverASavedSwapUntilThePlayerSwaps() {
        val overrides = LuaUiOverrides()
        overrides.onSessionStarted(mapOf(swap to true))
        overrides.onScriptRequest(swap, scriptId = 0, value = false, currentValue = true)
        assertEquals(false, overrides.onScriptRequest(swap, scriptId = 1, value = false, currentValue = false))

        // The player swaps back to swapped, then a script asks again: the player's new value is its baseline
        overrides.onPlayerChanged(swap)
        assertNull(overrides.getScriptValue(swap))
        assertEquals(false, overrides.onScriptRequest(swap, scriptId = 0, value = false, currentValue = true))
        assertEquals(mapOf(swap to true), overrides.onScriptStopped(0))
    }

    @Test
    fun thePlayersSwapBeforeAnyRequestReplacesTheSavedOne() {
        val overrides = LuaUiOverrides()
        overrides.onSessionStarted(mapOf(swap to true))
        // The player unswaps before any script asks, so the screen's value is the player's from then on
        overrides.onPlayerChanged(swap)
        assertEquals(true, overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = false))

        assertEquals(mapOf(swap to false), overrides.onScriptStopped(0))
    }

    @Test
    fun theNextSessionsSavedSwapReplacesTheLastOne() {
        val overrides = LuaUiOverrides()
        overrides.onSessionStarted(mapOf(swap to true))
        overrides.onSessionEnded()

        // The next game wasn't left swapped, so a value the screen still showed from the last game doesn't count
        overrides.onSessionStarted(mapOf(swap to false))
        overrides.onScriptRequest(swap, scriptId = 0, value = true, currentValue = true)
        assertEquals(mapOf(swap to false), overrides.onScriptStopped(0))
    }
}
