package me.magnum.melonds.impl

import me.magnum.melonds.domain.model.rom.config.RomLuaScript

/**
 * The rules that keep a ROM's script list in step with its linked folder. Kept apart from [LuaScriptStorage] so they can be tested without
 * a device
 */
object LuaScriptFolderSync {

    private val NAME_ORDER = compareBy(String.CASE_INSENSITIVE_ORDER) { name: String -> name }.thenBy { it }

    /**
     * Returns [scripts] brought in step with the folder. With no folder linked the list is empty. When the folder can't be read
     * ([folderScriptNames] is null) the list is kept as it is, so a lost permission or a removed card doesn't lose the switches. Otherwise
     * the list is the folder's scripts sorted by name: a script that was already in the list keeps its switch, a new one starts switched
     * off, and one that is gone from the folder is dropped.
     *
     * @param folderScriptNames The names of the `.lua` files at the top of the folder, or null if it couldn't be read
     */
    fun sync(scripts: List<RomLuaScript>, hasFolder: Boolean, folderScriptNames: Collection<String>?): List<RomLuaScript> {
        if (!hasFolder) {
            return emptyList()
        }
        if (folderScriptNames == null) {
            return scripts
        }

        val enabledScripts = scripts.filter { it.enabled }.map { it.fileName }.toSet()
        // Some providers can list two documents with the same name. Scripts are known by name, so they're one entry
        return folderScriptNames.distinct().sortedWith(NAME_ORDER).map { RomLuaScript(fileName = it, enabled = it in enabledScripts) }
    }
}
