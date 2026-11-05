package me.magnum.melonds.domain.model.emulator

import me.magnum.melonds.domain.model.render.TiltShift

sealed class EmulatorEvent {
    data class RumbleStart(val duration: Int) : EmulatorEvent()
    data object RumbleStop : EmulatorEvent()
    // scriptId is the script's position in the list sent at launch
    data class LuaScriptStopped(val scriptId: Int, val scriptName: String, val error: String) : EmulatorEvent()
    data class LuaSwapScreens(val scriptId: Int, val swapped: Boolean) : EmulatorEvent()
    data class LuaShowControls(val scriptId: Int, val visible: Boolean) : EmulatorEvent()
    data class LuaFastForward(val scriptId: Int, val enabled: Boolean) : EmulatorEvent()
    // optionKey names one of the script's layout options, or is null for the player's layout
    data class LuaSetLayout(val scriptId: Int, val optionKey: String?) : EmulatorEvent()
    data class LuaTiltShift(val scriptId: Int, val tiltShift: TiltShift) : EmulatorEvent()
    data object LuaStateLoaded : EmulatorEvent()
    data class Stop(val reason: Reason) : EmulatorEvent() {
        enum class Reason {
            GBAModeNotSupported,
            BadExceptionRegion,
            PowerOff,
        }
    }
}