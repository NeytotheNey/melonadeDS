package me.magnum.melonds.impl

import me.magnum.melonds.domain.model.rom.config.RomLuaScript

/**
 * The rules that keep a ROM's script list in step with its linked folder, kept free of Android so they can be unit tested
 */
object LuaScriptFolderSync {

    /**
     * Returns the folder's scripts sorted by name, keeping the switch and options of known ones; with no folder the list is empty, and with an
     * unreadable folder ([folderScriptNames] null) it's kept as is so a lost permission doesn't lose the switches.
     *
     * An empty listing while scripts are known also keeps the list: Android can hide a folder for a few minutes and list it as empty with no
     * error flag, and taking that as "every script was deleted" would lose the switches and options for good. The cost is that deleting every
     * script by hand leaves their entries until a script is added back or the folder is unlinked.
     *
     * A listing that isn't complete ([isListingComplete] false, see [LuaScriptFolderContent.listScripts]) keeps the known scripts in
     * subfolders (names with `/`) that it doesn't have; top-level scripts still follow the listing.
     */
    fun sync(
        scripts: List<RomLuaScript>,
        hasFolder: Boolean,
        folderScriptNames: Collection<String>?,
        isListingComplete: Boolean = true,
    ): List<RomLuaScript> {
        if (!hasFolder) {
            return emptyList()
        }
        if (folderScriptNames == null || isSuspiciouslyEmpty(scripts.size, folderScriptNames)) {
            return scripts
        }

        val knownScripts = scripts.associateBy { it.fileName }
        // Some providers can list two documents with the same name, but scripts are known by name, so they're one entry
        val names = LinkedHashSet(folderScriptNames)
        if (!isListingComplete) {
            for (script in scripts) {
                if (script.fileName.contains('/')) {
                    names.add(script.fileName)
                }
            }
        }
        return names.sortedWith(LuaScriptFolderContent.NAME_ORDER).map { knownScripts[it] ?: RomLuaScript(fileName = it, enabled = false) }
    }

    /**
     * Whether a listing of [folderScriptNames] should count as unreadable because it's empty while [knownScriptCount] scripts are expected in it
     */
    fun isSuspiciouslyEmpty(knownScriptCount: Int, folderScriptNames: Collection<String>): Boolean {
        return folderScriptNames.isEmpty() && knownScriptCount > 0
    }
}
