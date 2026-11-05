package me.magnum.melonds.domain.model.rom

/**
 * What's known about the games of the library, held in memory so a tile only has to do a map lookup.
 *
 * @param metadataByGameHash What RetroAchievements knows, by ROM hash, for the games it has something on
 * @param unmatchedRomHashes The hashes of library ROMs that a loaded hash library has no entry for. Empty as long as no hash library was loaded
 */
class RomMetadataSnapshot(
    private val metadataByGameHash: Map<String, RomLibraryMetadata>,
    private val unmatchedRomHashes: Set<String> = emptySet(),
) {

    fun metadataFor(rom: Rom): RomLibraryMetadata {
        return metadataFor(rom.retroAchievementsHash, rom.developerName)
    }

    fun metadataFor(gameHash: String, bannerPublisher: String): RomLibraryMetadata {
        val banner = RomLibraryMetadata.fromBanner(bannerPublisher)
        val known = metadataByGameHash[gameHash] ?: return banner
        return if (known.publisher != null) known else known.copy(publisher = banner.publisher)
    }

    /**
     * True when RetroAchievements really has no entry for this ROM. False says nothing either way, since the hash library may not be loaded yet.
     */
    fun isUnmatched(rom: Rom): Boolean {
        return isUnmatched(rom.retroAchievementsHash)
    }

    fun isUnmatched(gameHash: String): Boolean {
        return gameHash in unmatchedRomHashes
    }

    companion object {
        val EMPTY = RomMetadataSnapshot(emptyMap())
    }
}
