package me.magnum.melonds.ui.emulator

import me.magnum.melonds.domain.model.render.Sun

/**
 * Same rules as [LuaTiltShiftRequests] for the sun shadows scripts ask for with `melon.screen.sun()`, where a script asking for none drops its
 * request; main thread only.
 */
class LuaSunRequests {

    // Ordered from oldest to most recent request
    private val requests = LinkedHashMap<Int, Sun>()
    // A request still on its way from a stopped script is ignored
    private val stoppedScripts = mutableSetOf<Int>()

    /**
     * The sun shadows scripts want now, or null for none
     */
    val sun: Sun?
        get() = requests.values.lastOrNull()

    /**
     * @return the sun shadows scripts want now.
     */
    fun onScriptRequest(scriptId: Int, request: Sun): Sun? {
        if (scriptId !in stoppedScripts) {
            // Removed first, so it moves to the end as the most recent request
            requests.remove(scriptId)
            if (!request.isOff) {
                requests[scriptId] = request
            }
        }
        return sun
    }

    fun onScriptStopped(scriptId: Int): Sun? {
        stoppedScripts.add(scriptId)
        requests.remove(scriptId)
        return sun
    }

    fun onScriptStarted(scriptId: Int) {
        stoppedScripts.remove(scriptId)
    }

    fun onSessionEnded(): Sun? {
        requests.clear()
        stoppedScripts.clear()
        return sun
    }
}
