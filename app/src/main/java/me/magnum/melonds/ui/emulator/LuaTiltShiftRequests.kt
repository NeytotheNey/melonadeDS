package me.magnum.melonds.ui.emulator

import me.magnum.melonds.domain.model.render.TiltShift

/**
 * Same rules as [LuaLayoutRequests] for the blur scripts ask for with `melon.screen.tilt_shift()`, where a script asking for none drops its request;
 * main thread only.
 */
class LuaTiltShiftRequests {

    // Ordered from oldest to most recent request
    private val requests = LinkedHashMap<Int, TiltShift>()
    // A request still on its way from a stopped script is ignored
    private val stoppedScripts = mutableSetOf<Int>()

    /**
     * The blur scripts want now, or null for none
     */
    val tiltShift: TiltShift?
        get() = requests.values.lastOrNull()

    /**
     * @return the blur scripts want now.
     */
    fun onScriptRequest(scriptId: Int, request: TiltShift): TiltShift? {
        if (scriptId !in stoppedScripts) {
            // Removed first, so it moves to the end as the most recent request
            requests.remove(scriptId)
            if (!request.isOff) {
                requests[scriptId] = request
            }
        }
        return tiltShift
    }

    fun onScriptStopped(scriptId: Int): TiltShift? {
        stoppedScripts.add(scriptId)
        requests.remove(scriptId)
        return tiltShift
    }

    fun onScriptStarted(scriptId: Int) {
        stoppedScripts.remove(scriptId)
    }

    fun onSessionEnded(): TiltShift? {
        requests.clear()
        stoppedScripts.clear()
        return tiltShift
    }
}
