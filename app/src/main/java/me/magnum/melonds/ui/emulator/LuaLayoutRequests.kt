package me.magnum.melonds.ui.emulator

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.emitAll
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.map
import me.magnum.melonds.domain.model.emulator.LuaScriptOption
import java.util.UUID

/**
 * Same rules as [LuaUiOverrides] for the layout a script asked for, with no baseline because null means the player's layout; main thread only.
 */
class LuaLayoutRequests {

    // Ordered from oldest to most recent request
    private val requests = LinkedHashMap<Int, UUID>()
    // Like LuaUiOverrides: a request still on its way from a stopped script is ignored
    private val stoppedScripts = mutableSetOf<Int>()

    /**
     * The layout scripts want shown now, or null for the player's own
     */
    val layoutId: UUID?
        get() = requests.values.lastOrNull()

    /**
     * A script asked for a layout, or for the player's layout back; [LuaLayoutRequest.None] keeps its earlier one.
     *
     * @return the layout scripts want shown now.
     */
    fun onScriptRequest(scriptId: Int, request: LuaLayoutRequest): UUID? {
        if (scriptId !in stoppedScripts && request != LuaLayoutRequest.None) {
            // Removed first, so it moves to the end as the most recent request
            requests.remove(scriptId)
            if (request is LuaLayoutRequest.Layout) {
                requests[scriptId] = request.id
            }
        }
        return this.layoutId
    }

    fun onScriptStopped(scriptId: Int): UUID? {
        stoppedScripts.add(scriptId)
        requests.remove(scriptId)
        return layoutId
    }

    fun onScriptStarted(scriptId: Int) {
        stoppedScripts.remove(scriptId)
    }

    /**
     * The player picked a layout by hand, so no script holds the layout anymore
     */
    fun onPlayerChanged(): UUID? {
        requests.clear()
        return layoutId
    }

    fun onSessionEnded(): UUID? {
        requests.clear()
        stoppedScripts.clear()
        return layoutId
    }
}

/**
 * What a script's `melon.screen.set_layout(optionKey)` asks for
 */
sealed interface LuaLayoutRequest {
    /**
     * set_layout(nil): the player's layout back
     */
    data object PlayerLayout : LuaLayoutRequest

    data class Layout(val id: UUID) : LuaLayoutRequest

    /**
     * Nothing to switch to (the option is "Don't change", its layout was deleted, or the key isn't a layout option), so the earlier request stays.
     */
    data object None : LuaLayoutRequest
}

/**
 * The request a script's `melon.screen.set_layout(optionKey)` makes: the layout the player picked for that option in [options] and [values].
 */
fun getLuaLayoutRequest(options: List<LuaScriptOption>, values: Map<String, String>, optionKey: String?, layoutExists: (UUID) -> Boolean): LuaLayoutRequest {
    if (optionKey == null) {
        return LuaLayoutRequest.PlayerLayout
    }
    val option = options.firstOrNull { it.key == optionKey } as? LuaScriptOption.Layout ?: return LuaLayoutRequest.None
    val layoutId = LuaScriptOption.Layout.getLayoutId(option.resolve(values[optionKey])) ?: return LuaLayoutRequest.None
    return if (layoutExists(layoutId)) LuaLayoutRequest.Layout(layoutId) else LuaLayoutRequest.None
}

/**
 * Emits when the player picks a layout by hand mid-session (global-setting change only; edits and deletions don't count).
 */
fun getPlayerLayoutPicks(romLayout: Flow<*>?, selectedGlobalLayoutIds: Flow<UUID>): Flow<Unit> {
    val globalPicks = selectedGlobalLayoutIds.distinctUntilChanged().drop(1).map { }
    return if (romLayout == null) {
        globalPicks
    } else {
        flow {
            // Nothing the player does mid-session changes the ROM's own layout
            romLayout.collect { }
            emitAll(globalPicks)
        }
    }
}
