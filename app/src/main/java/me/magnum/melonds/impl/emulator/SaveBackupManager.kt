package me.magnum.melonds.impl.emulator

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import me.magnum.melonds.common.uridelegates.UriHandler
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.repositories.SettingsRepository
import me.magnum.melonds.impl.ExactNameFileCreation
import me.magnum.melonds.impl.diagnostics.DebugLog
import java.io.File
import java.time.LocalDateTime
import java.util.concurrent.atomic.AtomicReference

/**
 * A backup of a game's .sav in the save-backups folder next to it (names in [SaveBackupNames])
 */
data class SaveBackup(val uri: Uri, val fileName: String, val time: LocalDateTime, val size: Long)

/**
 * Makes, lists, prunes and restores the save backups taken at game start and with each state load. Shared by the emulator and the ROM
 * details screen, and it knows which save the running game uses so a restore can't change it underneath. Every call does file work, so
 * call it off the main thread.
 */
class SaveBackupManager(
    private val context: Context,
    private val uriHandler: UriHandler,
    private val sramProvider: SramProvider,
    private val settingsRepository: SettingsRepository,
) {

    sealed class BackupResult {
        data object Created : BackupResult()
        /** The save matches the newest backup */
        data object Unchanged : BackupResult()
        /** No save, or an empty one (as for DSiWare) */
        data object NothingToBackUp : BackupResult()
        /** A backup with this second's name is already there */
        data object Skipped : BackupResult()
        data object Failed : BackupResult()
    }

    sealed class ListResult {
        /** Newest first */
        data class Found(val backups: List<SaveBackup>) : ListResult()
        data object Failed : ListResult()
    }

    enum class RestoreResult {
        RESTORED,
        GAME_RUNNING,
        /** The current save couldn't be backed up, so nothing was changed */
        BACKUP_FAILED,
        FAILED,
    }

    /**
     * The save the running game uses, from [markGameRunning]
     */
    class RunningGame internal constructor(val romUri: Uri, val directoryUri: Uri, val sramFileName: String)

    private class Child(val name: String, val size: Long, val uri: Uri, val isDirectory: Boolean)

    private class CachedFolder(val directoryUri: Uri, val folder: DocumentFile)

    // Taken by restores and by a game starting, so a game never reads its .sav halfway through a restore
    private val lock = Any()
    // Cleared without the lock, so stopping a game on the main thread never waits for a restore
    private val runningGame = AtomicReference<RunningGame?>(null)
    // The last save-backups folder used, so each state load doesn't list the save folder again
    @Volatile
    private var cachedFolder: CachedFolder? = null

    /**
     * Call before the game reads its .sav. Waits for a restore in progress to finish.
     */
    fun markGameRunning(rom: Rom, location: SramLocation): RunningGame {
        val game = RunningGame(rom.uri, location.directoryUri, location.sramFileName)
        synchronized(lock) {
            runningGame.set(game)
        }
        return game
    }

    /**
     * Clears [game] if it's still the running one, so a late call from an older session can't clear a newer one
     */
    fun markGameStopped(game: RunningGame) {
        runningGame.compareAndSet(game, null)
    }

    private fun isRunning(rom: Rom, target: SramTarget): Boolean {
        val game = runningGame.get() ?: return false
        return game.romUri == rom.uri || (game.directoryUri == target.directoryUri && game.sramFileName == target.sramFileName)
    }

    /**
     * Backs up the running game's save, at game start or right after a state load
     */
    fun backUpSave(location: SramLocation): BackupResult {
        synchronized(lock) {
            return backUpSave(SramTarget(location.directoryUri, location.sramFileName, location.sramUri), protectedName = null)
        }
    }

    /**
     * The ROM's backups, newest first
     */
    fun listBackups(rom: Rom): ListResult = synchronized(lock) {
        try {
            val target = sramProvider.findSramForRom(rom)
            // A folder that isn't there yet just means no backups
            val folder = getBackupFolder(target.directoryUri, create = false) ?: return ListResult.Found(emptyList())
            val children = listBackupFolder(target.directoryUri, folder, create = false) ?: return ListResult.Failed
            ListResult.Found(toBackups(target.sramFileName, children))
        } catch (e: Exception) {
            Log.w(DebugLog.TAG, "Could not list the save backups", e)
            cachedFolder = null
            ListResult.Failed
        }
    }

    /**
     * Backs up the current save, then writes [backup] over it (making the .sav if there's none). Refused while the ROM is running.
     */
    fun restoreBackup(rom: Rom, backup: SaveBackup): RestoreResult {
        synchronized(lock) {
            return try {
                val target = sramProvider.findSramForRom(rom)
                if (isRunning(rom, target)) {
                    return RestoreResult.GAME_RUNNING
                }

                // Read before the new backup is made, so pruning can't take it first
                val reportedLength = getReportedLength(backup.uri)
                val data = readFully(backup.uri)
                if (data == null || SaveBackupNames.checkRead(data, reportedLength) { true } != SaveBackupNames.ReadCheck.USABLE) {
                    Log.w(DebugLog.TAG, "Not restoring the save backup: read ${data?.size ?: "no"} of $reportedLength bytes")
                    return RestoreResult.FAILED
                }

                var backupResult = backUpSave(target, protectedName = backup.fileName)
                if (backupResult == BackupResult.Skipped) {
                    // A backup made this second may not hold the current save, so it's tried again in the next second
                    Thread.sleep(1000)
                    backupResult = backUpSave(target, protectedName = backup.fileName)
                }
                // A save is only overwritten once it's in a backup; NothingToBackUp means it's missing or read as empty
                when (backupResult) {
                    BackupResult.Failed, BackupResult.Skipped -> return RestoreResult.BACKUP_FAILED
                    BackupResult.Created, BackupResult.Unchanged, BackupResult.NothingToBackUp -> Unit
                }

                val sramUri = target.sramUri ?: sramProvider.createSram(target)
                if (writeFully(sramUri, data)) {
                    DebugLog.event { "Save backup ${backup.fileName} restored (${data.size} bytes)" }
                    RestoreResult.RESTORED
                } else {
                    RestoreResult.FAILED
                }
            } catch (e: Exception) {
                Log.w(DebugLog.TAG, "Could not restore the save backup", e)
                RestoreResult.FAILED
            }
        }
    }

    /**
     * Copies the save to a new timestamped backup unless it matches the newest one, then deletes that game's oldest beyond the "Backups to
     * keep per game" setting. [protectedName] is never deleted. Called under [lock].
     */
    private fun backUpSave(target: SramTarget, protectedName: String?): BackupResult {
        return try {
            val sramUri = target.sramUri ?: return BackupResult.NothingToBackUp
            // All of it first, judged by what was read, so a short or torn read never becomes a backup and an unreadable save never counts
            // as empty
            val reportedLength = getReportedLength(sramUri)
            val readData = readFully(sramUri)
            when (SaveBackupNames.checkRead(readData, reportedLength) { fileExists(sramUri) }) {
                SaveBackupNames.ReadCheck.NOTHING_TO_BACK_UP -> return BackupResult.NothingToBackUp
                SaveBackupNames.ReadCheck.UNUSABLE -> {
                    Log.w(DebugLog.TAG, "Not backing up the save: read ${readData?.size ?: "no"} of $reportedLength bytes")
                    return BackupResult.Failed
                }
                SaveBackupNames.ReadCheck.USABLE -> Unit
            }
            val data = readData ?: return BackupResult.Failed

            val folder = getBackupFolder(target.directoryUri, create = true) ?: return BackupResult.Failed
            val children = listBackupFolder(target.directoryUri, folder, create = true) ?: return BackupResult.Failed
            val backups = toBackups(target.sramFileName, children)

            val newest = backups.firstOrNull()
            // A listed size of 0 can mean unknown, so that one is read too
            val newestData = newest?.takeIf { it.size <= 0L || it.size == data.size.toLong() }?.let { readFully(it.uri) }
            if (SaveBackupNames.isSameAsNewest(data, newestData)) {
                prune(backups, protectedName)
                return BackupResult.Unchanged
            }

            val now = LocalDateTime.now().withNano(0)
            val backupName = SaveBackupNames.backupName(target.sramFileName, now)
            if (children.any { it.name == backupName }) {
                return BackupResult.Skipped
            }

            val created = ExactNameFileCreation.create(backupName, { folder.createFile("application/*", backupName) }, { it.name }, { it.delete() })
            val backupFile = when (created) {
                is ExactNameFileCreation.Result.Created -> created.file
                is ExactNameFileCreation.Result.Renamed -> {
                    Log.w(DebugLog.TAG, "The save backup was named ${created.createdName} instead of $backupName")
                    return BackupResult.Failed
                }
                ExactNameFileCreation.Result.Failed -> {
                    // Some providers make the file but return null; an empty one would count as the newest backup
                    folder.findFile(backupName)?.delete()
                    return BackupResult.Failed
                }
            }
            if (!writeFully(backupFile.uri, data)) {
                backupFile.delete()
                return BackupResult.Failed
            }

            val newBackup = SaveBackup(backupFile.uri, backupName, now, data.size.toLong())
            val all = SaveBackupNames.newestFirst(target.sramFileName, backups + newBackup) { it.fileName }.map { it.file }
            // The new one is kept even if the clock went back
            prune(all, protectedName, backupName)
            DebugLog.event { "Save backed up to $backupName (${data.size} bytes)" }
            BackupResult.Created
        } catch (e: Exception) {
            Log.w(DebugLog.TAG, "Could not back up the save", e)
            // The cached folder may be what failed, so the next backup looks it up again
            cachedFolder = null
            BackupResult.Failed
        }
    }

    private fun prune(newestFirst: List<SaveBackup>, vararg protectedNames: String?) {
        // Read each time, so a lowered count only deletes when the next backup is made
        val keep = settingsRepository.getSaveBackupsToKeep()
        SaveBackupNames.toPrune(newestFirst, keep, isProtected = { it.fileName in protectedNames }).forEach {
            val deleted = try {
                uriHandler.getUriDocument(it.uri)?.delete() == true
            } catch (e: Exception) {
                false
            }
            if (!deleted) {
                Log.w(DebugLog.TAG, "Could not delete the old save backup ${it.fileName}")
            }
        }
    }

    private fun toBackups(sramFileName: String, children: List<Child>): List<SaveBackup> {
        return SaveBackupNames.newestFirst(sramFileName, children.filterNot { it.isDirectory }) { it.name }.map {
            SaveBackup(it.file.uri, it.file.name, it.time, it.file.size)
        }
    }

    /**
     * The save-backups folder in the save folder, made if [create] and it's missing; null if it's missing or can't be made
     */
    private fun getBackupFolder(directoryUri: Uri, create: Boolean): DocumentFile? {
        cachedFolder?.takeIf { it.directoryUri == directoryUri }?.let { return it.folder }

        val rootDocument = uriHandler.getUriTreeDocument(directoryUri) ?: return null
        val rootChildren = listChildren(rootDocument) ?: return null
        val existing = rootChildren.firstOrNull { it.name == SaveBackupNames.FOLDER_NAME }
        val folder = if (existing != null) {
            if (!existing.isDirectory) {
                Log.w(DebugLog.TAG, "${SaveBackupNames.FOLDER_NAME} in the save folder is a file, not a folder")
                return null
            }
            toFolderDocument(existing.uri) ?: return null
        } else {
            if (!create) {
                return null
            }
            val created = ExactNameFileCreation.create(SaveBackupNames.FOLDER_NAME, { rootDocument.createDirectory(SaveBackupNames.FOLDER_NAME) }, { it.name }, { it.delete() })
            when (created) {
                is ExactNameFileCreation.Result.Created -> created.file
                is ExactNameFileCreation.Result.Renamed -> {
                    Log.w(DebugLog.TAG, "The save backup folder was named ${created.createdName} instead of ${SaveBackupNames.FOLDER_NAME}")
                    return null
                }
                ExactNameFileCreation.Result.Failed -> rootDocument.findFile(SaveBackupNames.FOLDER_NAME)?.takeIf { it.isDirectory } ?: return null
            }
        }
        cachedFolder = CachedFolder(directoryUri, folder)
        return folder
    }

    /**
     * Lists the backup folder; a cached folder that can't be listed (deleted, say) is looked up again once. Without [create], a folder that's
     * gone lists as empty.
     */
    private fun listBackupFolder(directoryUri: Uri, folder: DocumentFile, create: Boolean): List<Child>? {
        listChildren(folder)?.let { return it }
        if (cachedFolder?.folder !== folder) {
            return null
        }
        cachedFolder = null
        val freshFolder = getBackupFolder(directoryUri, create) ?: return if (create) null else emptyList()
        return listChildren(freshFolder)
    }

    private fun toFolderDocument(uri: Uri): DocumentFile? {
        return if (uri.scheme == "file") {
            uri.path?.let { DocumentFile.fromFile(File(it)) }
        } else {
            DocumentFile.fromTreeUri(context, uri)
        }
    }

    /**
     * Lists a folder with one query instead of DocumentFile's two per file; null if it can't be read or the provider says the list is
     * incomplete
     */
    private fun listChildren(folder: DocumentFile): List<Child>? {
        val folderUri = folder.uri
        if (folderUri.scheme == "file") {
            val directory = folderUri.path?.let { File(it) } ?: return null
            return directory.listFiles()?.map { Child(it.name, it.length(), Uri.fromFile(it), it.isDirectory) }
        }

        val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(folderUri, DocumentsContract.getDocumentId(folderUri))
        val projection = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE,
        )
        // SAF throws for a folder that was deleted rather than returning nothing
        val query = try {
            context.contentResolver.query(childrenUri, projection, null, null, null)
        } catch (e: Exception) {
            Log.w(DebugLog.TAG, "Could not list $folderUri", e)
            null
        }
        return query?.use { cursor ->
            val extras = cursor.extras
            if (extras?.getBoolean(DocumentsContract.EXTRA_LOADING) == true || extras?.getString(DocumentsContract.EXTRA_ERROR) != null) {
                return null
            }
            buildList {
                while (cursor.moveToNext()) {
                    val documentId = cursor.getString(0) ?: continue
                    val name = cursor.getString(1) ?: continue
                    val isDirectory = cursor.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR
                    val size = if (cursor.isNull(3)) 0L else cursor.getLong(3)
                    add(Child(name, size, DocumentsContract.buildDocumentUriUsingTree(folderUri, documentId), isDirectory))
                }
            }
        }
    }

    /**
     * The size the provider reports, or 0 when it's unknown, missing or empty
     */
    private fun getReportedLength(uri: Uri): Long {
        return try {
            uriHandler.getUriDocument(uri)?.length() ?: 0
        } catch (e: Exception) {
            0
        }
    }

    /**
     * True unless the provider says the file isn't there; asked only after a failed read, so when in doubt it counts as there
     */
    private fun fileExists(uri: Uri): Boolean {
        return try {
            uriHandler.getUriDocument(uri)?.exists() != false
        } catch (e: Exception) {
            true
        }
    }

    private fun readFully(uri: Uri): ByteArray? {
        return try {
            context.contentResolver.openInputStream(uri)?.use { it.readBytes() }
        } catch (e: Exception) {
            Log.w(DebugLog.TAG, "Could not read $uri", e)
            null
        }
    }

    /**
     * "wt" truncates, so an older, bigger file doesn't leave its tail behind
     */
    private fun writeFully(uri: Uri, data: ByteArray): Boolean {
        return try {
            context.contentResolver.openOutputStream(uri, "wt")?.use { it.write(data) } != null
        } catch (e: Exception) {
            Log.w(DebugLog.TAG, "Could not write $uri", e)
            false
        }
    }
}
