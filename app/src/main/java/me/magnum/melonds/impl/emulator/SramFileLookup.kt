package me.magnum.melonds.impl.emulator

/**
 * Finds a game's .sav in a listing of its save folder, kept free of Android so it can be unit tested.
 */
object SramFileLookup {

    sealed class Result<out T> {
        data class Found<T>(val file: T) : Result<T>()
        data object Missing : Result<Nothing>()

        /**
         * The save folder is the ROM's own folder but it lists as completely empty, which it can't be while the ROM opens: Android can hide
         * a folder for a few minutes and list it as empty with no error, and a new .sav made now would sit next to the real one.
         */
        data object RomFolderUnreadable : Result<Nothing>()
    }

    /**
     * Looks for [sramFileName] among [children], reading names through [nameOf] one at a time and stopping at the match, like
     * DocumentFile.findFile(); [isRomFolder] is true when the folder is the ROM's own folder rather than a separate save folder.
     *
     * Only an empty listing counts as unreadable. A listing without the ROM can also be a stale folder link (a ROM moved within a cloud tree
     * whose IDs don't change), so that case goes on to create the file, where [me.magnum.melonds.impl.ExactNameFileCreation] still catches a
     * hidden .sav.
     */
    fun <T> find(children: Collection<T>, nameOf: (T) -> String?, sramFileName: String, isRomFolder: Boolean): Result<T> {
        if (isRomFolder && children.isEmpty()) {
            return Result.RomFolderUnreadable
        }
        return children.firstOrNull { nameOf(it) == sramFileName }?.let { Result.Found(it) } ?: Result.Missing
    }
}
