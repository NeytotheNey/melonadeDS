package me.magnum.melonds.impl

import me.magnum.melonds.domain.model.rom.config.RomLuaScript

/**
 * The rules that keep a ROM's script list in step with its linked folder, kept free of Android so they can be unit tested
 */
object LuaScriptFolderSync {

    /**
     * Returns the folder's scripts sorted by name, keeping the switch and options of known ones; with no folder the list is empty, and with an
     * unreadable folder ([folderScriptNames] null) it's kept as is so a lost permission doesn't lose the switches.
     */
    fun sync(scripts: List<RomLuaScript>, hasFolder: Boolean, folderScriptNames: Collection<String>?): List<RomLuaScript> {
        if (!hasFolder) {
            return emptyList()
        }
        if (folderScriptNames == null) {
            return scripts
        }

        val knownScripts = scripts.associateBy { it.fileName }
        // Some providers can list two documents with the same name, but scripts are known by name, so they're one entry
        return folderScriptNames.distinct().sortedWith(LuaScriptFolderContent.NAME_ORDER).map { knownScripts[it] ?: RomLuaScript(fileName = it, enabled = false) }
    }
}
