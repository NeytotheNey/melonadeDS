package me.magnum.melonds.impl

import android.content.Context
import android.content.SharedPreferences
import android.net.Uri
import androidx.core.content.pm.PackageInfoCompat
import androidx.documentfile.provider.DocumentFile
import com.google.gson.Gson
import com.google.gson.JsonParser
import com.google.gson.reflect.TypeToken
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.serialization.json.Json
import me.magnum.melonds.impl.PendingSettingsRestore.Companion.CONTROLLER_FILE
import me.magnum.melonds.impl.PendingSettingsRestore.Companion.DATA_FILES
import me.magnum.melonds.impl.PendingSettingsRestore.Companion.KEPT_PREF_KEYS
import me.magnum.melonds.impl.PendingSettingsRestore.Companion.LAST_VERSION_KEY
import me.magnum.melonds.impl.PendingSettingsRestore.Companion.LAYOUTS_FILE
import me.magnum.melonds.impl.PendingSettingsRestore.Companion.READY_MARKER
import me.magnum.melonds.impl.PendingSettingsRestore.Companion.ROM_DATA_FILE
import me.magnum.melonds.impl.PendingSettingsRestore.Companion.SETTINGS_FILE
import me.magnum.melonds.impl.PendingSettingsRestore.Companion.STAGING_DIR
import me.magnum.melonds.impl.PendingSettingsRestore.Companion.getBackupLong
import me.magnum.melonds.impl.PendingSettingsRestore.Companion.preferencesToJson
import me.magnum.melonds.impl.dtos.input.ControllerConfigurationDto
import me.magnum.melonds.impl.dtos.layout.LayoutConfigurationDto
import me.magnum.melonds.impl.dtos.rom.RomDto
import me.magnum.melonds.utils.PackageManagerCompat
import org.json.JSONObject
import java.io.File
import java.io.FileNotFoundException
import java.io.IOException
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Backs up the settings to a folder and restores them from one.
 *
 * A restore is done in two steps. [restore] checks the backup and copies it into an app-private staging folder, and then the app
 * restarts. On the next start, [PendingSettingsRestore] swaps the staged files and preferences in before anything else reads them. This
 * way the repositories, which keep their data in memory and save it back to the same files, can never write old data over a restore.
 */
@Singleton
class SettingsBackupManager @Inject constructor(
    @param:ApplicationContext private val context: Context,
    private val preferences: SharedPreferences,
    private val gson: Gson,
    private val json: Json,
) {

    /**
     * The backup was made by a newer version of the app. The migrations can't convert data back to an older format.
     */
    class BackupFromNewerVersionException : IOException("The backup was made by a newer version of the app")

    fun backup(treeUri: Uri) {
        val root = DocumentFile.fromTreeUri(context, treeUri) ?: throw IOException("Could not open the backup folder")

        // Typed, so a float that happens to be a whole number doesn't come back as an int
        val settingsJson = preferencesToJson(preferences.all.filterKeys { it !in KEPT_PREF_KEYS })

        // A restore needs settings.json, so it's removed first and written last. An interrupted backup then leaves a folder that can't be
        // restored, instead of one with files from two different backups
        root.findFile(SETTINGS_FILE)?.let {
            if (!it.delete()) {
                throw IOException("Could not replace $SETTINGS_FILE")
            }
        }
        DATA_FILES.forEach { backupFile(root, it) }
        writeDocument(root, SETTINGS_FILE, settingsJson.toString().toByteArray())
    }

    /**
     * Checks the backup in [treeUri] and stages it. Nothing the app uses is changed here. The app has to restart afterwards, and the
     * restore is applied by [PendingSettingsRestore] on the next start, even if that restart doesn't happen right away.
     */
    fun restore(treeUri: Uri) {
        val root = DocumentFile.fromTreeUri(context, treeUri) ?: throw IOException("Could not open the backup folder")

        // Read and check every file first, so a missing or broken file never gets staged
        val settingsText = readDocument(root, SETTINGS_FILE) ?: throw FileNotFoundException("$SETTINGS_FILE not found in the selected folder")
        // JSONObject ignores anything after the object, so check that the whole file is a single object first
        if (!parseWholeDocument(settingsText).isJsonObject) {
            throw IOException("$SETTINGS_FILE is not a JSON object")
        }
        val settingsJson = JSONObject(settingsText)

        val currentVersion = getCurrentVersion()
        // A backup without a version is treated as being in the current format
        val backupVersion = if (settingsJson.has(LAST_VERSION_KEY)) getBackupLong(settingsJson, LAST_VERSION_KEY) else currentVersion
        if (backupVersion > currentVersion) {
            throw BackupFromNewerVersionException()
        }

        val dataFiles = DATA_FILES.mapNotNull { fileName ->
            readDocument(root, fileName)?.let { fileName to it }
        }
        dataFiles.forEach { (fileName, text) ->
            validateDataFile(fileName, text, isCurrentFormat = backupVersion == currentVersion)
        }

        val stagingDir = File(context.filesDir, STAGING_DIR)
        // Also removes an earlier restore that was staged but never applied. Its marker goes first, because deleteRecursively() removes
        // files in no particular order, and a marker left with only some of the files would apply a partial restore
        File(stagingDir, READY_MARKER).delete()
        stagingDir.deleteRecursively()
        if (!stagingDir.mkdirs()) {
            throw IOException("Could not create the restore folder")
        }
        File(stagingDir, SETTINGS_FILE).writeText(settingsText)
        dataFiles.forEach { (fileName, text) ->
            File(stagingDir, fileName).writeText(text)
        }
        // Written last, so a restore that was only partly staged is never applied
        if (!File(stagingDir, READY_MARKER).createNewFile()) {
            throw IOException("Could not mark the restore as ready")
        }
    }

    /**
     * Checks a data file the way the app loads it, because the loaders quietly fall back to empty data or defaults when a file can't be
     * read, and the next save would then wipe the user's data. A backup from an older version is in an older format that only the
     * migrations understand, so for those only the JSON itself is checked.
     */
    private fun validateDataFile(fileName: String, text: String, isCurrentFormat: Boolean) {
        val document = parseWholeDocument(text)
        val expectedStructure = if (fileName == CONTROLLER_FILE) document.isJsonObject else document.isJsonArray
        if (!expectedStructure) {
            throw IOException("$fileName doesn't have the expected structure")
        }
        if (!isCurrentFormat) {
            return
        }

        // The same parsers and DTOs as SharedPreferencesSettingsRepository, InternalLayoutsRepository and FileSystemRomsRepository
        when (fileName) {
            CONTROLLER_FILE -> json.decodeFromString<ControllerConfigurationDto>(text).toControllerConfiguration()
            LAYOUTS_FILE -> {
                val layoutListType = object : TypeToken<List<LayoutConfigurationDto>>() {}.type
                val layouts = gson.fromJson<List<LayoutConfigurationDto>>(text, layoutListType) ?: throw IOException("$fileName is empty")
                layouts.forEach { it.toModel() }
            }
            ROM_DATA_FILE -> {
                val romListType = object : TypeToken<List<RomDto>>() {}.type
                val roms = gson.fromJson<List<RomDto>>(text, romListType) ?: throw IOException("$fileName is empty")
                roms.forEach { it.toModel() }
            }
        }
    }

    /**
     * Parses [text] as one JSON document and fails if anything other than whitespace comes after it, like Gson and kotlinx do.
     */
    private fun parseWholeDocument(text: String) = JsonParser.parseString(text)

    private fun getCurrentVersion(): Long {
        val packageInfo = PackageManagerCompat.getPackageInfo(context.packageManager, context.packageName, 0)
        return PackageInfoCompat.getLongVersionCode(packageInfo)
    }

    private fun backupFile(root: DocumentFile, fileName: String) {
        val source = File(context.filesDir, fileName)
        if (source.isFile) {
            writeDocument(root, fileName, source.readBytes())
        } else {
            // Remove a copy left by an older backup in the same folder, so a restore doesn't bring back data that no longer exists
            root.findFile(fileName)?.let {
                if (!it.delete()) {
                    throw IOException("Could not remove the old $fileName")
                }
            }
        }
    }

    private fun writeDocument(root: DocumentFile, fileName: String, content: ByteArray) {
        val existingDocument = root.findFile(fileName)
        if (existingDocument != null) {
            // "wt" truncates the file. Some providers don't truncate with the default "w" mode, which would leave the end of an older,
            // longer backup in the file
            val outputStream = runCatching { context.contentResolver.openOutputStream(existingDocument.uri, "wt") }.getOrNull()
            if (outputStream != null) {
                outputStream.use { it.write(content) }
                return
            }

            // The provider doesn't support "wt". Recreate the document instead, so that "w" starts from an empty file
            if (!existingDocument.delete()) {
                throw IOException("Could not replace $fileName")
            }
        }

        val document = root.createFile("application/json", fileName) ?: throw IOException("Could not create $fileName")
        val outputStream = context.contentResolver.openOutputStream(document.uri, "w") ?: throw IOException("Could not write $fileName")
        outputStream.use { it.write(content) }
    }

    private fun readDocument(root: DocumentFile, fileName: String): String? {
        val document = root.findFile(fileName) ?: return null
        val inputStream = context.contentResolver.openInputStream(document.uri) ?: throw IOException("Could not read $fileName")
        return inputStream.use { it.reader().readText() }
    }
}
