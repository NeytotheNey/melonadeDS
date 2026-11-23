package me.magnum.melonds.ui.emulator

import me.magnum.melonds.domain.model.render.DepthOfField

/**
 * Same rules as [LuaTiltShiftRequests] for the depth of field scripts ask for with `melon.screen.depth_of_field()`, where a script asking for
 * none drops its request; main thread only.
 */
class LuaDepthOfFieldRequests {

    // Ordered from oldest to most recent request
    private val requests = LinkedHashMap<Int, DepthOfField>()
    // A request still on its way from a stopped script is ignored
    private val stoppedScripts = mutableSetOf<Int>()

    /**
     * The depth of field scripts want now, or null for none
     */
    val depthOfField: DepthOfField?
        get() = requests.values.lastOrNull()

    /**
     * @return the depth of field scripts want now.
     */
    fun onScriptRequest(scriptId: Int, request: DepthOfField): DepthOfField? {
        if (scriptId !in stoppedScripts) {
            // Removed first, so it moves to the end as the most recent request
            requests.remove(scriptId)
            if (!request.isOff) {
                requests[scriptId] = request
            }
        }
        return depthOfField
    }

    fun onScriptStopped(scriptId: Int): DepthOfField? {
        stoppedScripts.add(scriptId)
        requests.remove(scriptId)
        return depthOfField
    }

    fun onScriptStarted(scriptId: Int) {
        stoppedScripts.remove(scriptId)
    }

    fun onSessionEnded(): DepthOfField? {
        requests.clear()
        stoppedScripts.clear()
        return depthOfField
    }
}
