package me.magnum.melonds.ui.emulator

import me.magnum.melonds.domain.model.render.Shade

/**
 * Same rules as [LuaTiltShiftRequests] for the shading scripts ask for with `melon.screen.shade()`, where a script asking for none drops its
 * request; main thread only.
 */
class LuaShadeRequests {

    // Ordered from oldest to most recent request
    private val requests = LinkedHashMap<Int, Shade>()
    // A request still on its way from a stopped script is ignored
    private val stoppedScripts = mutableSetOf<Int>()

    /**
     * The shading scripts want now, or null for none
     */
    val shade: Shade?
        get() = requests.values.lastOrNull()

    /**
     * @return the shading scripts want now.
     */
    fun onScriptRequest(scriptId: Int, request: Shade): Shade? {
        if (scriptId !in stoppedScripts) {
            // Removed first, so it moves to the end as the most recent request
            requests.remove(scriptId)
            if (!request.isOff) {
                requests[scriptId] = request
            }
        }
        return shade
    }

    fun onScriptStopped(scriptId: Int): Shade? {
        stoppedScripts.add(scriptId)
        requests.remove(scriptId)
        return shade
    }

    fun onScriptStarted(scriptId: Int) {
        stoppedScripts.remove(scriptId)
    }

    fun onSessionEnded(): Shade? {
        requests.clear()
        stoppedScripts.clear()
        return shade
    }
}
