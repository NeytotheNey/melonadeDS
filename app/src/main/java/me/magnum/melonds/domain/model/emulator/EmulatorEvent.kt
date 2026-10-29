package me.magnum.melonds.domain.model.emulator

sealed class EmulatorEvent {
    data class RumbleStart(val duration: Int) : EmulatorEvent()
    data object RumbleStop : EmulatorEvent()
    data class LuaScriptStopped(val scriptName: String, val error: String) : EmulatorEvent()
    data class LuaSwapScreens(val swapped: Boolean) : EmulatorEvent()
    data class LuaShowControls(val visible: Boolean) : EmulatorEvent()
    data class LuaFastForward(val enabled: Boolean) : EmulatorEvent()
    data class Stop(val reason: Reason) : EmulatorEvent() {
        enum class Reason {
            GBAModeNotSupported,
            BadExceptionRegion,
            PowerOff,
        }
    }
}