package me.magnum.melonds.impl

/**
 * Which of [roms] can be dropped from the library because their file is gone. That only counts as known when the search directory a ROM belongs to can still
 * be reached: with a card unmounted or a permission lost every file looks missing, and dropping those entries would lose their play history and settings. A
 * ROM under no current search directory has no directory to vouch for that, so it stays as well.
 *
 * This only picks library entries, and nothing here touches any file.
 *
 * @param isUnderDirectory Whether a ROM belongs to a search directory
 * @param isDirectoryAccessible Whether a search directory exists and can be read, asked at most once for each and only if a ROM there looks missing
 */
fun <R, D> selectRemovableMissingRoms(
    roms: List<R>,
    searchDirectories: List<D>,
    isUnderDirectory: (R, D) -> Boolean,
    fileExists: (R) -> Boolean,
    isDirectoryAccessible: (D) -> Boolean,
): List<R> {
    val accessibility = HashMap<D, Boolean>()
    return roms.filter { rom ->
        if (fileExists(rom)) {
            return@filter false
        }
        val directory = searchDirectories.firstOrNull { isUnderDirectory(rom, it) } ?: return@filter false
        accessibility.getOrPut(directory) { isDirectoryAccessible(directory) }
    }
}
