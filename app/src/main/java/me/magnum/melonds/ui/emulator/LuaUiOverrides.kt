package me.magnum.melonds.ui.emulator

/**
 * The UI settings Lua scripts can change.
 */
enum class LuaUiSetting {
    SWAP_SCREENS,
    SHOW_CONTROLS,
    FAST_FORWARD,
}

/**
 * Keeps track of what Lua scripts asked the UI for, so it can be put back once they stop. Each setting is handled on its own:
 * - The baseline is the player's own value from just before the first script request for that setting.
 * - Each running script's latest request is kept by script ID. The value in effect is the most recent request from a script that still runs,
 *   or the baseline when there is none.
 * - When a script stops, its requests are dropped, and the setting goes to the most recent remaining request, or back to the baseline.
 * - When the player changes the setting by hand, the player wins: every script request for it is dropped, and the player's value becomes the
 *   baseline the next time a script asks.
 *
 * Only knows about values. The caller reads and changes the actual UI, and only changes it when the value differs. Not thread-safe: used on
 * the main thread only.
 */
class LuaUiOverrides {

    private class SettingState {
        var baseline = false
        // Used as the baseline of the next first request instead of the UI's value, when that value can't be trusted yet
        var knownBaseline: Boolean? = null
        // Ordered from oldest to most recent request
        val requests = LinkedHashMap<Int, Boolean>()
    }

    private val settings = LuaUiSetting.entries.associateWith { SettingState() }.also { resetFastForwardBaseline(it) }
    // A stopped script stays off until the player switches it back on (a reset doesn't), so a request of its that was still on its way is
    // ignored
    private val stoppedScripts = mutableSetOf<Int>()

    /**
     * A script asked for [value]. [currentValue] is what the UI shows now, which becomes the baseline if no script holds the setting yet.
     * Returns the value the UI should show.
     */
    fun onScriptRequest(setting: LuaUiSetting, scriptId: Int, value: Boolean, currentValue: Boolean): Boolean {
        if (scriptId in stoppedScripts) {
            return currentValue
        }

        val state = settings.getValue(setting)
        if (state.requests.isEmpty()) {
            state.baseline = state.knownBaseline ?: currentValue
            state.knownBaseline = null
        }
        // Removed first, so it moves to the end as the most recent request
        state.requests.remove(scriptId)
        state.requests[scriptId] = value
        return value
    }

    /**
     * A script was switched off. Returns the value the UI should show for each setting the script held.
     */
    fun onScriptStopped(scriptId: Int): Map<LuaUiSetting, Boolean> {
        stoppedScripts.add(scriptId)
        val newValues = mutableMapOf<LuaUiSetting, Boolean>()
        settings.forEach { (setting, state) ->
            if (state.requests.remove(scriptId) != null) {
                newValues[setting] = state.requests.values.lastOrNull() ?: state.baseline
            }
        }
        return newValues
    }

    /**
     * The player switched a stopped script back on. Its new requests count again, under the same rules.
     */
    fun onScriptStarted(scriptId: Int) {
        stoppedScripts.remove(scriptId)
    }

    /**
     * The player changed [setting] by hand, so no script holds it anymore.
     */
    fun onPlayerChanged(setting: LuaUiSetting) {
        val state = settings.getValue(setting)
        state.requests.clear()
        // The UI now shows the player's own value
        state.knownBaseline = null
    }

    /**
     * A session starts with the player's own values for some settings ([playerValues]), shown before any of its scripts run. They're the
     * baselines of the session's first requests for those settings, unless the player changes them first
     */
    fun onSessionStarted(playerValues: Map<LuaUiSetting, Boolean>) {
        playerValues.forEach { (setting, value) ->
            settings.getValue(setting).knownBaseline = value
        }
    }

    /**
     * The value scripts hold for [setting], or null if none does.
     */
    fun getScriptValue(setting: LuaUiSetting): Boolean? {
        return settings.getValue(setting).requests.values.lastOrNull()
    }

    /**
     * The session ended, so every script stopped. Returns the baseline of each setting a script still held, and forgets everything.
     */
    fun onSessionEnded(): Map<LuaUiSetting, Boolean> {
        val newValues = mutableMapOf<LuaUiSetting, Boolean>()
        settings.forEach { (setting, state) ->
            if (state.requests.isNotEmpty()) {
                newValues[setting] = state.baseline
                state.requests.clear()
            }
        }
        stoppedScripts.clear()
        resetFastForwardBaseline(settings)
        return newValues
    }

    /**
     * Every session starts with fast-forward off. The toggle can still show the previous game's value when the next game's first requests
     * arrive (from on_load(), before the screen resets it), so that value must not become the baseline
     */
    private fun resetFastForwardBaseline(settings: Map<LuaUiSetting, SettingState>) {
        settings.getValue(LuaUiSetting.FAST_FORWARD).knownBaseline = false
    }
}
