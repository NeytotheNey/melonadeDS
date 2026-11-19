package me.magnum.melonds.impl.emulator

import java.time.LocalDateTime
import java.time.format.DateTimeFormatter
import java.time.format.DateTimeParseException
import java.time.format.ResolverStyle

/**
 * The rules for a game's save backups, kept free of Android so they can be unit tested. A backup of `Game.sav` made at 11:47:07 on
 * 2026-11-19 (local time) is `save-backups/Game.sav.20261119-114707.bak`, next to the .sav.
 */
object SaveBackupNames {

    const val FOLDER_NAME = "save-backups"
    const val MAX_BACKUPS = 3

    private const val EXTENSION = ".bak"
    private val TIMESTAMP_PATTERN = Regex("""\d{8}-\d{6}""")
    private val TIMESTAMP_FORMAT = DateTimeFormatter.ofPattern("uuuuMMdd-HHmmss").withResolverStyle(ResolverStyle.STRICT)

    data class Entry<T>(val file: T, val time: LocalDateTime)

    fun backupName(saveFileName: String, time: LocalDateTime): String {
        return "$saveFileName.${time.format(TIMESTAMP_FORMAT)}$EXTENSION"
    }

    /**
     * When the backup was made, or null if [fileName] isn't one of [saveFileName]'s backups. Games can share a folder, so the whole save name
     * plus the dot must match.
     */
    fun parseTime(saveFileName: String, fileName: String): LocalDateTime? {
        val prefix = "$saveFileName."
        // The length check stops "Game.sav.bak" from matching both ends with the same dot
        if (fileName.length < prefix.length + EXTENSION.length || !fileName.startsWith(prefix) || !fileName.endsWith(EXTENSION)) {
            return null
        }
        val timestamp = fileName.substring(prefix.length, fileName.length - EXTENSION.length)
        if (!TIMESTAMP_PATTERN.matches(timestamp)) {
            return null
        }
        return try {
            LocalDateTime.parse(timestamp, TIMESTAMP_FORMAT)
        } catch (e: DateTimeParseException) {
            null
        }
    }

    /**
     * [saveFileName]'s backups among [files], newest first by the time in their names (not the file times, which copies and cloud syncs
     * change)
     */
    fun <T> newestFirst(saveFileName: String, files: Collection<T>, nameOf: (T) -> String?): List<Entry<T>> {
        return files.mapNotNull { file ->
            val name = nameOf(file) ?: return@mapNotNull null
            parseTime(saveFileName, name)?.let { Entry(file, it) }
        }.sortedWith(compareByDescending<Entry<T>> { it.time }.thenByDescending { nameOf(it.file) })
    }

    /**
     * The backups to delete so only the newest [keep] stay, from a newest-first list. Protected ones are never deleted, even when that leaves
     * more than [keep].
     */
    fun <T> toPrune(newestFirst: List<T>, keep: Int = MAX_BACKUPS, isProtected: (T) -> Boolean = { false }): List<T> {
        return newestFirst.drop(keep).filterNot(isProtected)
    }

    enum class ReadCheck {
        USABLE,
        /** No file, or an empty one: nothing that a restore could lose */
        NOTHING_TO_BACK_UP,
        /** The file is there but couldn't be read, or read short */
        UNUSABLE,
    }

    /**
     * Judges a whole-file read by the bytes read, since a provider's reported length is optional and can be 0. [reportedLength] is only
     * checked when it isn't 0, and [fileExists] is only asked when the read failed.
     */
    fun checkRead(data: ByteArray?, reportedLength: Long, fileExists: () -> Boolean): ReadCheck {
        if (data == null) {
            return if (fileExists()) ReadCheck.UNUSABLE else ReadCheck.NOTHING_TO_BACK_UP
        }
        if (reportedLength > 0L && data.size.toLong() != reportedLength) {
            return ReadCheck.UNUSABLE
        }
        return if (data.isEmpty()) ReadCheck.NOTHING_TO_BACK_UP else ReadCheck.USABLE
    }

    /**
     * True when the save is the same as the newest backup, so no new one is needed
     */
    fun isSameAsNewest(save: ByteArray, newestBackup: ByteArray?): Boolean {
        return newestBackup != null && save.contentEquals(newestBackup)
    }
}
