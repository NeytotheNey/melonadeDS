package me.magnum.melonds.ui.emulator

import me.magnum.melonds.domain.model.render.Bloom

/**
 * Same rules as [LuaTiltShiftRequests] for the bloom and vignette scripts ask for with `melon.screen.bloom()` and `melon.screen.vignette()`, where
 * a script asking for neither drops its request; main thread only.
 */
class LuaBloomRequests {

    // Ordered from oldest to most recent request
    private val requests = LinkedHashMap<Int, Bloom>()
    // A request still on its way from a stopped script is ignored
    private val stoppedScripts = mutableSetOf<Int>()

    /**
     * The bloom and vignette scripts want now, or null for none
     */
    val bloom: Bloom?
        get() = requests.values.lastOrNull()

    /**
     * @return the bloom and vignette scripts want now.
     */
    fun onScriptRequest(scriptId: Int, request: Bloom): Bloom? {
        if (scriptId !in stoppedScripts) {
            // Removed first, so it moves to the end as the most recent request
            requests.remove(scriptId)
            if (!request.isOff) {
                requests[scriptId] = request
            }
        }
        return bloom
    }

    fun onScriptStopped(scriptId: Int): Bloom? {
        stoppedScripts.add(scriptId)
        requests.remove(scriptId)
        return bloom
    }

    fun onScriptStarted(scriptId: Int) {
        stoppedScripts.remove(scriptId)
    }

    fun onSessionEnded(): Bloom? {
        requests.clear()
        stoppedScripts.clear()
        return bloom
    }
}
