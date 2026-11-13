package me.magnum.melonds.impl.emulator

open class SramLoadException(message: String) : Exception(message)

/**
 * The ROM opens but its folder lists as empty, or a new .sav came out renamed because the provider still sees the real one, so the folder is
 * taken as hidden and the game isn't started (see [SramFileLookup] and [me.magnum.melonds.impl.ExactNameFileCreation]).
 */
class RomFolderUnreadableException(message: String) : SramLoadException(message)
