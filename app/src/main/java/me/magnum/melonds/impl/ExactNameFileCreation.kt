package me.magnum.melonds.impl

/**
 * Creates a file that must get exactly the name asked for, kept free of Android so it can be unit tested.
 *
 * A provider that still sees a file of that name (one it hid from the listing that said the file was missing) gives the new one another name,
 * such as "game.sav (1)", and the game would then use a blank file while the real one sits next to it; that new file is deleted again.
 */
object ExactNameFileCreation {

    sealed class Result<out T> {
        data class Created<T>(val file: T) : Result<T>()

        /**
         * The provider gave the new file another name (null if it couldn't be read); the file was deleted again
         */
        data class Renamed(val createdName: String?) : Result<Nothing>()

        data object Failed : Result<Nothing>()
    }

    fun <T> create(requestedName: String, create: () -> T?, nameOf: (T) -> String?, delete: (T) -> Unit): Result<T> {
        val file = create() ?: return Result.Failed
        val createdName = nameOf(file)
        if (createdName != requestedName) {
            delete(file)
            return Result.Renamed(createdName)
        }
        return Result.Created(file)
    }
}
