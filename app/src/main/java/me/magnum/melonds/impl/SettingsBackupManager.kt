package me.magnum.melonds.impl

import android.content.Context
import android.content.SharedPreferences
import android.net.Uri
import androidx.core.content.pm.PackageInfoCompat
import androidx.documentfile.provider.DocumentFile
import com.google.gson.Gson
import com.google.gson.JsonElement
import com.google.gson.JsonParser
import com.google.gson.reflect.TypeToken
import kotlinx.serialization.json.Json
import me.magnum.melonds.impl.SettingsBackupFormat.CONTROLLER_FILE
import me.magnum.melonds.impl.SettingsBackupFormat.DATA_FILES
import me.magnum.melonds.impl.SettingsBackupFormat.KEPT_PREF_KEYS
import me.magnum.melonds.impl.SettingsBackupFormat.LAST_VERSION_KEY
import me.magnum.melonds.impl.SettingsBackupFormat.LAYOUTS_FILE
import me.magnum.melonds.impl.SettingsBackupFormat.READY_MARKER
import me.magnum.melonds.impl.SettingsBackupFormat.ROM_DATA_FILE
import me.magnum.melonds.impl.SettingsBackupFormat.SETTINGS_FILE
import me.magnum.melonds.impl.SettingsBackupFormat.STAGING_DIR
import me.magnum.melonds.impl.SettingsBackupFormat.getBackupLong
import me.magnum.melonds.impl.SettingsBackupFormat.preferencesToJson
import me.magnum.melonds.impl.dtos.input.ControllerConfigurationDto
import me.magnum.melonds.impl.dtos.layout.LayoutConfigurationDto
import me.magnum.melonds.impl.dtos.rom.RomDto
import me.magnum.melonds.utils.PackageManagerCompat
import org.json.JSONObject
import org.json.JSONTokener
import java.io.File
import java.io.FileNotFoundException
import java.io.IOException

/**
 * Backs up the settings to a folder and stages restores from one (see [SettingsBackupFormat] for how a restore is applied).
 */
class SettingsBackupManager(
    private val context: Context,
    private val preferences: SharedPreferences,
    private val gson: Gson,
    private val json: Json,
) {

    /**
     * The backup was made by a newer version of the app, whose data the migrations can't convert back.
     */
    class BackupFromNewerVersionException : IOException("The backup was made by a newer version of the app")

    fun backup(treeUri: Uri) {
        val root = DocumentFile.fromTreeUri(context, treeUri) ?: throw IOException("Could not open the backup folder")

        // Typed, so a float that happens to be a whole number doesn't come back as an int
        val settingsJson = preferencesToJson(preferences.all.filterKeys { it !in KEPT_PREF_KEYS })

        // settings.json is removed first and written last so an interrupted backup can't be restored
        root.findFile(SETTINGS_FILE)?.let {
            if (!it.delete()) {
                throw IOException("Could not replace $SETTINGS_FILE")
            }
        }
        DATA_FILES.forEach { backupFile(root, it) }
        writeDocument(root, SETTINGS_FILE, settingsJson.toString().toByteArray())
    }

    /**
     * Checks the backup in [treeUri] and stages it; nothing changes until [PendingSettingsRestore] runs on the next start.
     */
    fun restore(treeUri: Uri) {
        val root = DocumentFile.fromTreeUri(context, treeUri) ?: throw IOException("Could not open the backup folder")

        // Read and check every file first, so a missing or broken file never gets staged
        val settingsText = readDocument(root, SETTINGS_FILE) ?: throw FileNotFoundException("$SETTINGS_FILE not found in the selected folder")
        val settingsJson = parseSettings(settingsText)

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
        // Also removes an earlier unapplied restore, marker first because deleteRecursively() has no order and a marker left with only some
        // of the files would apply a partial restore
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
     * Checks a data file the way its loader reads it (loaders fall back to empty data and the next save would wipe it); older-format files
     * are only checked to be JSON.
     */
    private fun validateDataFile(fileName: String, text: String, isCurrentFormat: Boolean) {
        if (fileName == CONTROLLER_FILE && isCurrentFormat) {
            // Read with kotlinx like SharedPreferencesSettingsRepository does, which also fails if the file isn't a single object
            json.decodeFromString<ControllerConfigurationDto>(text).toControllerConfiguration()
            return
        }

        val document = parseWholeDocument(text)
        val expectedStructure = if (fileName == CONTROLLER_FILE) document.isJsonObject else document.isJsonArray
        if (!expectedStructure) {
            throw IOException("$fileName doesn't have the expected structure")
        }
        if (!isCurrentFormat) {
            return
        }

        // Same parser and DTOs as InternalLayoutsRepository and FileSystemRomsRepository, on the text because Gson's tree path truncates numbers
        // the text path rejects
        when (fileName) {
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
     * Parses settings.json with the same parser [PendingSettingsRestore] uses, so anything it would reject fails here instead.
     */
    private fun parseSettings(text: String): JSONObject {
        val tokener = JSONTokener(text)
        val settingsJson = tokener.nextValue() as? JSONObject ?: throw IOException("$SETTINGS_FILE is not a JSON object")
        // nextClean() skips whitespace and returns the null character once the text has been read to the end
        if (tokener.nextClean() != '\u0000') {
            throw IOException("$SETTINGS_FILE has more than one JSON value")
        }
        return settingsJson
    }

    /**
     * Parses [text] as one JSON document and fails if anything other than whitespace comes after it, like Gson and kotlinx do.
     */
    private fun parseWholeDocument(text: String): JsonElement = JsonParser.parseString(text)

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
            // "wt" truncates, while some providers' "w" doesn't, which would leave the tail of a longer old backup
            val outputStream = runCatching { context.contentResolver.openOutputStream(existingDocument.uri, "wt") }.getOrNull()
            if (outputStream != null) {
                outputStream.use { it.write(content) }
                return
            }

            // The provider doesn't support "wt", so recreate the document for "w" to start from an empty file
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
