package me.magnum.melonds.domain.repositories

import kotlinx.coroutines.flow.StateFlow
import me.magnum.melonds.domain.model.rom.RomMetadataSnapshot

/**
 * What's known about the library's games, from local storage only: nothing here touches the network.
 */
interface RomMetadataRepository {
    /**
     * Kept up to date only while something collects it. This is an emulator, so collect it with the lifecycle: with no collector left, for example
     * during a game, no database work runs for it. The value stays readable either way.
     */
    val romMetadata: StateFlow<RomMetadataSnapshot>

    /**
     * Makes the next sync fetch again the games fetched over two weeks ago, found or not. What they have stays until then.
     */
    suspend fun markMetadataForRefresh()

    fun clearDownloadedArt()
}
