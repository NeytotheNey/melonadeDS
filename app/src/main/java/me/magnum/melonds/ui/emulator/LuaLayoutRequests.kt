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
 * The layout Lua scripts asked for with `melon.screen.set_layout()`, by the same rules as [LuaUiOverrides]: each running script's latest request
 * is kept, the most recent one from a script that still runs wins, a script's requests go when it stops, and the player picking a layout by
 * hand drops them all. No baseline is needed: with no request, the ROM's own layout (the player's) is shown.
 *
 * Kept apart from [LuaUiOverrides] because the layout is applied by the view model as the events are sent, while the screen applies the other
 * settings as it receives them. Not thread-safe: used on the main thread only.
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
     * A script asked for a layout, or for the player's layout back. A request for nothing (see [LuaLayoutRequest.None]) keeps the script's
     * earlier one. Returns the layout scripts want shown now
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
     * Nothing to switch to: the option is "Don't change", its layout was deleted, or the key isn't a layout option (native already refuses
     * those). It does nothing, so the script's earlier request stays
     */
    data object None : LuaLayoutRequest
}

/**
 * The request a script's `melon.screen.set_layout(optionKey)` makes: the layout the player picked for that layout option, from the script's
 * [options] and their [values]. [layoutExists] tells whether a layout is still there
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
 * Emits each time the player picks a layout by hand mid-session, which drops the scripts' layout requests. [romLayout] is the ROM's own layout,
 * which completes once it's deleted, or null if the ROM uses the global one. [selectedGlobalLayoutIds] is the global layout setting.
 *
 * Only a change of the global setting while it's the layout in use counts. Editing a layout isn't a pick, and neither is the ROM's layout being
 * deleted (the global one takes over) or the global one being deleted (the default is shown, but the setting stays)
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
