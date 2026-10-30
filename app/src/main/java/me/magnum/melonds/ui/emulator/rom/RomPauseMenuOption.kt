package me.magnum.melonds.ui.emulator.rom

import me.magnum.melonds.R
import me.magnum.melonds.ui.emulator.PauseMenuOption

enum class RomPauseMenuOption(override val textResource: Int) : PauseMenuOption {
    SETTINGS(R.string.settings),
    SAVE_STATE(R.string.save_state),
    LOAD_STATE(R.string.load_state),
    REWIND(R.string.rewind),
    CHEATS(R.string.cheats),
    MODS(R.string.label_rom_config_mods),
    VIEW_ACHIEVEMENTS(R.string.achievements),
    SWAP_SCREENS(R.string.swap_screens),
    RESET(R.string.reset),
    EXIT(R.string.exit)
}