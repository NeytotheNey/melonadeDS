package me.magnum.melonds.ui.emulator

import me.magnum.melonds.domain.model.camera.WideCameraRequest

/**
 * What scripts ask of the WideMelon camera with melon.wide, with the same rules as [LuaTiltShiftRequests], taken a part at a time: the zoom and
 * the pan each come from the script that changed its requests most recently among those asking for one, and 1x is held while any script holds
 * it. A script asking for nothing drops its request, and a stopped script's requests go away. Main thread only.
 */
class LuaWideCameraRequests {

    // Ordered from oldest to most recent request
    private val requests = LinkedHashMap<Int, WideCameraRequest>()
    // A request still on its way from a stopped script is ignored
    private val stoppedScripts = mutableSetOf<Int>()

    /**
     * What the scripts ask for now, NONE for nothing
     */
    val request: WideCameraRequest
        get() = WideCameraRequest(
            zoom = requests.values.lastOrNull { it.zoom != null }?.zoom,
            pan = requests.values.lastOrNull { it.pan != null }?.pan,
            holdNative = requests.values.any { it.holdNative },
        )

    /**
     * @return what the scripts ask for now.
     */
    fun onScriptRequest(scriptId: Int, request: WideCameraRequest): WideCameraRequest {
        if (scriptId !in stoppedScripts) {
            // Removed first, so it moves to the end as the most recent request
            requests.remove(scriptId)
            if (!request.isOff) {
                requests[scriptId] = request
            }
        }
        return this.request
    }

    fun onScriptStopped(scriptId: Int): WideCameraRequest {
        stoppedScripts.add(scriptId)
        requests.remove(scriptId)
        return request
    }

    fun onScriptStarted(scriptId: Int) {
        stoppedScripts.remove(scriptId)
    }

    fun onSessionEnded(): WideCameraRequest {
        requests.clear()
        stoppedScripts.clear()
        return request
    }
}
