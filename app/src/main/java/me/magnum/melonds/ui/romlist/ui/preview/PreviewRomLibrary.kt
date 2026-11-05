package me.magnum.melonds.ui.romlist.ui.preview

import android.net.Uri
import androidx.core.graphics.createBitmap
import androidx.core.graphics.set
import androidx.core.net.toUri
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import me.magnum.melonds.domain.model.RomIconFiltering
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.RomLibraryMetadata
import me.magnum.melonds.domain.model.rom.RomMetadataFetchStatus
import me.magnum.melonds.domain.model.rom.RomMetadataSnapshot
import me.magnum.melonds.domain.model.rom.config.RomConfig
import me.magnum.melonds.ui.romlist.RomIcon
import java.io.File
import java.util.Date

/**
 * Fictional games for the launcher previews. The metadata mirrors what RetroAchievements would give: some games have everything, some have a title
 * screen as well, some only a publisher, and some are unknown to it.
 */
object PreviewRomLibrary {

    // Which games are wide goes by the hash, and these two are. They're the ones with title art below
    private const val WIDE_TILE_HASH_1 = "preview-3-5"
    private const val WIDE_TILE_HASH_2 = "preview-5-3"

    // A little symmetric glyph, so that the banner icon reads as one
    private val bitmap = createBitmap(8, 8).apply {
        val pattern = listOf("00111100", "01222210", "12211221", "12111121", "12222221", "01211210", "00122100", "00011000")
        pattern.forEachIndexed { y, row ->
            row.forEachIndexed { x, pixel ->
                this[x, y] = when (pixel) {
                    '1' -> 0xFF2E6B3A.toInt()
                    '2' -> 0xFF98D494.toInt()
                    else -> 0xFFD6DED0.toInt()
                }
            }
        }
    }
    val icon = RomIcon(bitmap, RomIconFiltering.NONE)

    private val now = System.currentTimeMillis()

    val roms: List<Rom> = listOf(
        rom(1, "Highway 4: Mediocre Racing", "Nontendo", "Highway_4.nds", lastPlayedMinutesAgo = 20),
        rom(2, "Super Plumber Bros", "Nontendo", "plumber_bros.nds", lastPlayedMinutesAgo = 60 * 30),
        rom(3, "Pocket Beasts: Emerald Dusk", "Freak Games", "pocket_beasts_ed.nds", hash = WIDE_TILE_HASH_1),
        rom(4, "Brain Gym Daily", "Nontendo", "brain_gym.nds"),
        rom(5, "Castle of Echoes", "Monami", "castle_echoes.nds", lastPlayedMinutesAgo = 60 * 24 * 9, hash = WIDE_TILE_HASH_2),
        rom(6, "Rhythm Carnival DS", "Tempo Soft", "rhythm_carnival.nds"),
        rom(7, "DSiWare Puzzle Pop", "Someware", "puzzle_pop.nds", isDsiWare = true),
        rom(8, "demo_final_v2", "", "demo_final_v2.nds"),
        rom(9, "Starfall Tactics", "Quadrant", "starfall_tactics.nds"),
    )

    private val metadata: Map<Uri, RomLibraryMetadata> = mapOf(
        roms[0].uri to known(1, "Nontendo", "Nontendo EAD", "Racing", "2008-03-14", "day", boxArt = true, inSet = 42, earned = 7),
        roms[1].uri to known(2, "Nontendo", "Nontendo EAD", "Platformer", "2006-05-01", "month", boxArt = true, inSet = 25),
        roms[2].uri to known(3, "Freak Games", "Freak Games", "RPG", "2010-01-01", "year", boxArt = true, titleArt = true, inSet = 61),
        roms[3].uri to known(4, "Nontendo", null, "Puzzle", "2005-11-30", "day", boxArt = false),
        roms[4].uri to known(5, "Monami", "Monami", "Action", "2009-07-23", "day", boxArt = true, titleArt = true, inSet = 36, earned = 0),
        roms[8].uri to known(9, "Quadrant", "Quadrant", "Strategy", "2007-02-02", "day", boxArt = true, inSet = 12),
    )

    fun metadataFor(rom: Rom): RomLibraryMetadata = metadata[rom.uri] ?: RomLibraryMetadata.fromBanner(rom)

    val metadataSnapshot: StateFlow<RomMetadataSnapshot> = MutableStateFlow(
        RomMetadataSnapshot(
            metadataByGameHash = roms.associate { it.retroAchievementsHash to metadataFor(it) },
            unmatchedRomHashes = setOf(roms[7].retroAchievementsHash),
        ),
    )

    private fun known(
        id: Int,
        publisher: String,
        developer: String?,
        genre: String,
        released: String,
        granularity: String,
        boxArt: Boolean,
        titleArt: Boolean = false,
        inSet: Int? = null,
        earned: Int? = null,
    ): RomLibraryMetadata {
        return RomLibraryMetadata(
            publisher = publisher,
            developer = developer,
            genre = genre,
            releaseYear = released.take(4).toInt(),
            artFile = if (boxArt) boxArt(id) else null,
            hasAchievements = inSet != null,
            released = released,
            releasedGranularity = granularity,
            titleArtFile = if (titleArt) File("preview-title-$id.png") else null,
            achievementsInSet = inSet,
            achievementsEarned = earned,
            fetchStatus = RomMetadataFetchStatus.OK,
            fetchedAt = now - 3 * 24 * 60 * 60_000L,
        )
    }

    private fun rom(id: Int, name: String, publisher: String, fileName: String, lastPlayedMinutesAgo: Long? = null, isDsiWare: Boolean = false, hash: String = "preview-$id"): Rom {
        return Rom(
            name = name,
            developerName = publisher,
            fileName = fileName,
            uri = "content://preview/$id".toUri(),
            parentTreeUri = Uri.EMPTY,
            config = RomConfig(),
            lastPlayed = lastPlayedMinutesAgo?.let { Date(now - it * 60_000) },
            isDsiWareTitle = isDsiWare,
            retroAchievementsHash = hash,
        )
    }

    private fun boxArt(id: Int) = File("preview-box-$id.png")
}
