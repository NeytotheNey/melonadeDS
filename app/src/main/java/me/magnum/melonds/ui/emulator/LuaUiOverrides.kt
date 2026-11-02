package me.magnum.melonds.ui.emulator

import me.magnum.melonds.ui.emulator.model.LuaUiSetting

/**
 * Per-setting stack of script requests over a player baseline; the newest running script wins, the player's own change clears them, and it's
 * main-thread only.
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
    // A stopped script stays off until the player switches it back on, so a request of its still on its way is ignored
    private val stoppedScripts = mutableSetOf<Int>()

    /**
     * A script asked for [value]; [currentValue] becomes the baseline if no script holds the setting yet.
     *
     * @return the value the UI should show.
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
     * A script was switched off.
     *
     * @return the value the UI should show for each setting the script held.
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
     * The player switched a stopped script back on, so its new requests count again.
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
     * A session starts with the player's own values for some settings ([playerValues]), which become the baselines of its first requests.
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
     * The session ended, so every script stopped and everything is forgotten.
     *
     * @return the baseline of each setting a script still held.
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
     * Every session starts with fast-forward off, so the toggle's possibly stale value must not become the baseline.
     */
    private fun resetFastForwardBaseline(settings: Map<LuaUiSetting, SettingState>) {
        settings.getValue(LuaUiSetting.FAST_FORWARD).knownBaseline = false
    }
}
