package me.magnum.melonds.domain.model.emulator

sealed class EmulatorEvent {
    data class RumbleStart(val duration: Int) : EmulatorEvent()
    data object RumbleStop : EmulatorEvent()
    // scriptId is the script's position in the list sent at launch
    data class LuaScriptStopped(val scriptId: Int, val scriptName: String, val error: String) : EmulatorEvent()
    data class LuaSwapScreens(val scriptId: Int, val swapped: Boolean) : EmulatorEvent()
    data class LuaShowControls(val scriptId: Int, val visible: Boolean) : EmulatorEvent()
    data class LuaFastForward(val scriptId: Int, val enabled: Boolean) : EmulatorEvent()
    data class Stop(val reason: Reason) : EmulatorEvent() {
        enum class Reason {
            GBAModeNotSupported,
            BadExceptionRegion,
            PowerOff,
        }
    }
}