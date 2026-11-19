package me.magnum.melonds.impl.emulator

import android.net.Uri
import me.magnum.melonds.common.uridelegates.UriHandler
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.repositories.SettingsRepository
import me.magnum.melonds.impl.ExactNameFileCreation

/**
 * A ROM's save file and the folder it's in, kept for the session so the save can be backed up before a state load.
 */
data class SramLocation(val sramUri: Uri, val directoryUri: Uri)

class SramProvider(
    private val settingsRepository: SettingsRepository,
    private val uriHandler: UriHandler,
) {

    @Throws(SramLoadException::class)
    fun getSramForRom(rom: Rom): SramLocation {
        val rootDirUri = settingsRepository.getSaveFileDirectory(rom)

        val rootDocument = uriHandler.getUriTreeDocument(rootDirUri) ?: throw SramLoadException("Cannot create root document: $rootDirUri")
        val romDocument = uriHandler.getUriDocument(rom.uri)

        val romFileName = romDocument?.name ?: throw SramLoadException("Cannot determine SRAM file name: ${romDocument?.uri}")
        val sramFileName = romFileName.replaceAfterLast('.', "sav", "$romFileName.sav")

        // Saves next to the ROM: that folder can't list as empty while the ROM opens. Not checked for a ROM opened as a single file (no parent
        // tree) or for a separate save folder, which can be empty on its own
        val isRomFolder = rom.parentTreeUri?.let { uriHandler.getUriTreeDocument(it)?.uri } == rootDocument.uri
        // The same one listing findFile() does
        val lookup = SramFileLookup.find(rootDocument.listFiles().asList(), { it.name }, sramFileName, isRomFolder)
        val sramUri = when (lookup) {
            is SramFileLookup.Result.Found -> lookup.file.uri
            SramFileLookup.Result.RomFolderUnreadable -> throw RomFolderUnreadableException("The ROM's folder lists as empty: ${rootDocument.uri}")
            SramFileLookup.Result.Missing -> {
                val creation = ExactNameFileCreation.create(sramFileName, { rootDocument.createFile("application/*", sramFileName) }, { it.name }, { it.delete() })
                when (creation) {
                    is ExactNameFileCreation.Result.Created -> creation.file.uri
                    // The provider still sees a .sav of that name that the listing left out, so the real save is there but can't be reached now
                    is ExactNameFileCreation.Result.Renamed -> throw RomFolderUnreadableException("The new save was named ${creation.createdName} instead of $sramFileName")
                    // It looks like some devices create the file just fine but return null. As a fallback, check if the SRAM file was actually created or not
                    // Reference: https://www.ghisler.ch/board/viewtopic.php?p=370089#p370089
                    ExactNameFileCreation.Result.Failed -> rootDocument.findFile(sramFileName)?.uri
                        ?: throw SramLoadException("Could not create temporary SRAM file at ${rootDocument.uri}")
                }
            }
        }
        return SramLocation(sramUri, rootDirUri)
    }

    /**
     * The `<save>.bak` file next to the save, made if it's missing, or null when there's nothing to back up (no save yet, or an empty one, as
     * for DSiWare). Throws if the backup file can't be made.
     */
    @Throws(SramLoadException::class)
    fun getSramBackupUri(location: SramLocation): Uri? {
        val sramDocument = uriHandler.getUriDocument(location.sramUri)
        if (sramDocument == null || !sramDocument.exists() || sramDocument.length() <= 0L) {
            return null
        }

        val sramFileName = sramDocument.name ?: throw SramLoadException("Cannot determine SRAM file name: ${location.sramUri}")
        val backupFileName = "$sramFileName.bak"
        val rootDocument = uriHandler.getUriTreeDocument(location.directoryUri) ?: throw SramLoadException("Cannot open save folder: ${location.directoryUri}")

        rootDocument.findFile(backupFileName)?.let { return it.uri }
        val creation = ExactNameFileCreation.create(backupFileName, { rootDocument.createFile("application/*", backupFileName) }, { it.name }, { it.delete() })
        return when (creation) {
            is ExactNameFileCreation.Result.Created -> creation.file.uri
            is ExactNameFileCreation.Result.Renamed -> throw SramLoadException("The save backup was named ${creation.createdName} instead of $backupFileName")
            // Some devices create the file but return null, as with the save itself
            ExactNameFileCreation.Result.Failed -> rootDocument.findFile(backupFileName)?.uri
                ?: throw SramLoadException("Could not create save backup at ${rootDocument.uri}")
        }
    }
}
