package me.magnum.melonds.ui.romlist.ui.preview

import android.net.Uri
import androidx.core.graphics.createBitmap
import androidx.core.graphics.set
import androidx.core.net.toUri
import me.magnum.melonds.domain.model.RomIconFiltering
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.RomLibraryMetadata
import me.magnum.melonds.domain.model.rom.config.RomConfig
import me.magnum.melonds.ui.romlist.RomIcon
import java.net.URL
import java.util.Date

/**
 * Fictional games for the launcher previews. The metadata mirrors what RetroAchievements would give: some games have everything, some only
 * a title and icon, and some are unknown to it.
 */
object PreviewRomLibrary {

    private val bitmap = createBitmap(1, 1).apply { this[0, 0] = 0xFF5B8C5A.toInt() }
    val icon = RomIcon(bitmap, RomIconFiltering.NONE)

    private val now = System.currentTimeMillis()

    val roms: List<Rom> = listOf(
        rom(1, "Highway 4: Mediocre Racing", "Nontendo", "Highway_4.nds", lastPlayedMinutesAgo = 20),
        rom(2, "Super Plumber Bros", "Nontendo", "plumber_bros.nds", lastPlayedMinutesAgo = 60 * 30),
        rom(3, "Pocket Beasts: Emerald Dusk", "Freak Games", "pocket_beasts_ed.nds"),
        rom(4, "Brain Gym Daily", "Nontendo", "brain_gym.nds"),
        rom(5, "Castle of Echoes", "Monami", "castle_echoes.nds", lastPlayedMinutesAgo = 60 * 24 * 9),
        rom(6, "Rhythm Carnival DS", "Tempo Soft", "rhythm_carnival.nds"),
        rom(7, "DSiWare Puzzle Pop", "Someware", "puzzle_pop.nds", isDsiWare = true),
        rom(8, "Unknown Homebrew Demo", "", "demo_final_v2.nds"),
        rom(9, "Starfall Tactics", "Quadrant", "starfall_tactics.nds"),
    )

    private val metadata: Map<Uri, RomLibraryMetadata> = mapOf(
        roms[0].uri to RomLibraryMetadata("Nontendo", "Nontendo EAD", "Racing", 2008, boxArt(1), gameIcon(1), hasAchievements = true),
        roms[1].uri to RomLibraryMetadata("Nontendo", "Nontendo EAD", "Platformer", 2006, boxArt(2), gameIcon(2), hasAchievements = true),
        roms[2].uri to RomLibraryMetadata("Freak Games", "Freak Games", "RPG", 2010, boxArt(3), gameIcon(3), hasAchievements = true),
        roms[3].uri to RomLibraryMetadata("Nontendo", null, "Puzzle", 2005, null, gameIcon(4), hasAchievements = false),
        roms[4].uri to RomLibraryMetadata("Monami", "Monami", "Action", 2009, boxArt(5), gameIcon(5), hasAchievements = true),
        roms[5].uri to RomLibraryMetadata("Tempo Soft", null, "Rhythm", 2011, null, null, hasAchievements = false),
        roms[8].uri to RomLibraryMetadata("Quadrant", "Quadrant", "Strategy", 2007, boxArt(9), gameIcon(9), hasAchievements = true),
    )

    fun metadataFor(rom: Rom): RomLibraryMetadata = metadata[rom.uri] ?: RomLibraryMetadata.fromBanner(rom)

    private fun rom(id: Int, name: String, publisher: String, fileName: String, lastPlayedMinutesAgo: Long? = null, isDsiWare: Boolean = false): Rom {
        return Rom(
            name = name,
            developerName = publisher,
            fileName = fileName,
            uri = "content://preview/$id".toUri(),
            parentTreeUri = Uri.EMPTY,
            config = RomConfig(),
            lastPlayed = lastPlayedMinutesAgo?.let { Date(now - it * 60_000) },
            isDsiWareTitle = isDsiWare,
            retroAchievementsHash = "",
        )
    }

    private fun boxArt(id: Int) = URL("https://media.retroachievements.org/Images/preview-box-$id.png")
    private fun gameIcon(id: Int) = URL("https://media.retroachievements.org/Images/preview-icon-$id.png")
}
