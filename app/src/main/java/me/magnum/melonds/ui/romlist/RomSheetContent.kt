package me.magnum.melonds.ui.romlist

import me.magnum.melonds.domain.model.rom.RomLibraryMetadata
import me.magnum.melonds.domain.model.rom.RomMetadataFetchStatus
import java.text.SimpleDateFormat
import java.util.GregorianCalendar
import java.util.Locale
import java.util.concurrent.TimeUnit

/**
 * What the metadata sheet says about a ROM, chosen without any UI so that "what's true and what's left out" can be tested. Everything here is either
 * read from the ROM and the metadata snapshot or left out.
 */
data class RomSheetContent(
    val title: String,
    val subtitle: String,
    val facts: List<RomSheetFact>,
    val note: RomSheetNote?,
    val credit: RomSheetCredit,
)

data class RomSheetFact(val label: RomSheetFactLabel, val value: RomSheetFactValue)

enum class RomSheetFactLabel {
    PUBLISHER,
    DEVELOPER,
    GENRE,
    RELEASED,
    ACHIEVEMENTS,
    LAST_PLAYED,
    FILE,
}

sealed interface RomSheetFactValue {
    data class Text(val text: String) : RomSheetFactValue
    /** @param earned Null when the user's data for this game was never fetched */
    data class AchievementCount(val total: Int, val earned: Int?) : RomSheetFactValue
    data object HasAchievements : RomSheetFactValue
    data object NoAchievementSet : RomSheetFactValue
    data class Played(val age: RelativeAge) : RomSheetFactValue
    data object NeverPlayed : RomSheetFactValue
}

/**
 * The full-width explanation on the sheet of a ROM RetroAchievements gave nothing for.
 */
enum class RomSheetNote {
    NOT_FOUND_SHOWING_FILE_NAME,
    NOT_FOUND_SHOWING_BANNER,
    HASH_MISSING,
    NO_WEB_API_KEY,
    NOT_DOWNLOADED_YET,
    FETCH_PENDING,
}

/**
 * The small line that says where the sheet's data came from. No key and no answer yet aren't the same as RetroAchievements not knowing a game. Ages are in
 * days, and null when the row doesn't say.
 */
sealed interface RomSheetCredit {
    data class Cached(val days: Long?) : RomSheetCredit
    data object DueForRefresh : RomSheetCredit
    data class NotFoundChecked(val days: Long?) : RomSheetCredit
    data class FetchPending(val days: Long?) : RomSheetCredit
    data object HashNotFound : RomSheetCredit
    data object HashMissing : RomSheetCredit
    data object TitleOnlyNoKey : RomSheetCredit
    data object TitleOnlyNotDownloaded : RomSheetCredit
    data object NoKey : RomSheetCredit
    data object NotDownloaded : RomSheetCredit
}

sealed interface RelativeAge {
    data object JustNow : RelativeAge
    data class Hours(val hours: Int) : RelativeAge
    data object Yesterday : RelativeAge
    data class Days(val days: Int) : RelativeAge
    data object LastWeek : RelativeAge
    data class Weeks(val weeks: Int) : RelativeAge
    data object LastMonth : RelativeAge
    data class Months(val months: Int) : RelativeAge
    data object LastYear : RelativeAge
    data class Years(val years: Int) : RelativeAge
}

/**
 * @param name The ROM's name, which a scan makes up from the file name when the banner has no title
 * @param isHashUnmatched Whether a loaded hash library has no entry for this ROM
 * @param isHashMissing Whether this ROM has no hash at all, so that it can never be looked up
 * @param now The current time, in epoch ms
 */
fun buildRomSheetContent(
    customName: String?,
    name: String,
    fileName: String,
    lastPlayedAt: Long?,
    metadata: RomLibraryMetadata,
    isHashUnmatched: Boolean,
    isHashMissing: Boolean,
    hasWebApiKey: Boolean,
    now: Long,
    locale: Locale = Locale.getDefault(),
): RomSheetContent {
    val style = romTileStyle(customName, name, fileName, metadata, hasArt = metadata.tileImage != null)
    val lastPlayed = RomSheetFact(
        label = RomSheetFactLabel.LAST_PLAYED,
        value = lastPlayedAt?.let { RomSheetFactValue.Played(relativeAge(it, now)) } ?: RomSheetFactValue.NeverPlayed,
    )

    val facts = if (metadata.hasRetroAchievementsData) {
        listOfNotNull(
            metadata.publisher?.let { RomSheetFact(RomSheetFactLabel.PUBLISHER, RomSheetFactValue.Text(it)) },
            metadata.developer?.let { RomSheetFact(RomSheetFactLabel.DEVELOPER, RomSheetFactValue.Text(it)) },
            metadata.genre?.let { RomSheetFact(RomSheetFactLabel.GENRE, RomSheetFactValue.Text(it)) },
            formatReleaseDate(metadata.released, metadata.releasedGranularity, locale)?.let { RomSheetFact(RomSheetFactLabel.RELEASED, RomSheetFactValue.Text(it)) },
            achievementsFact(metadata),
            lastPlayed,
        )
    } else {
        listOfNotNull(
            metadata.publisher?.let { RomSheetFact(RomSheetFactLabel.PUBLISHER, RomSheetFactValue.Text(it)) },
            RomSheetFact(RomSheetFactLabel.FILE, RomSheetFactValue.Text(fileName)),
            lastPlayed,
        )
    }

    return RomSheetContent(
        title = if (style == RomTileStyle.NO_METADATA) romFileStem(fileName) else metadata.displayTitle(customName, name, fileName),
        subtitle = metadata.caption() ?: fileName,
        facts = facts,
        note = if (metadata.hasRetroAchievementsData) null else sheetNote(metadata, style, isHashUnmatched, isHashMissing, hasWebApiKey),
        credit = sheetCredit(metadata, isHashUnmatched, isHashMissing, hasWebApiKey, now),
    )
}

/**
 * A game RetroAchievements knows but that was never opened with a login has nothing in the local cache, which says nothing about whether it has a set,
 * so that's left out. Opening it with a login stores the game together with its achievements, so a game on file without any really has none.
 */
private fun achievementsFact(metadata: RomLibraryMetadata): RomSheetFact? {
    val value = when {
        metadata.achievementsInSet != null -> RomSheetFactValue.AchievementCount(metadata.achievementsInSet, metadata.achievementsEarned)
        metadata.hasAchievements -> RomSheetFactValue.HasAchievements
        metadata.title != null -> RomSheetFactValue.NoAchievementSet
        else -> null
    }
    return value?.let { RomSheetFact(RomSheetFactLabel.ACHIEVEMENTS, it) }
}

private fun sheetNote(metadata: RomLibraryMetadata, style: RomTileStyle, isHashUnmatched: Boolean, isHashMissing: Boolean, hasWebApiKey: Boolean): RomSheetNote {
    return when {
        isHashMissing -> RomSheetNote.HASH_MISSING
        metadata.fetchStatus == RomMetadataFetchStatus.NOT_FOUND || isHashUnmatched -> {
            if (style == RomTileStyle.NO_METADATA) RomSheetNote.NOT_FOUND_SHOWING_FILE_NAME else RomSheetNote.NOT_FOUND_SHOWING_BANNER
        }
        // With no key there's no next sync to try again
        !hasWebApiKey -> RomSheetNote.NO_WEB_API_KEY
        metadata.fetchStatus == RomMetadataFetchStatus.FAILED -> RomSheetNote.FETCH_PENDING
        else -> RomSheetNote.NOT_DOWNLOADED_YET
    }
}

private fun sheetCredit(metadata: RomLibraryMetadata, isHashUnmatched: Boolean, isHashMissing: Boolean, hasWebApiKey: Boolean, now: Long): RomSheetCredit {
    // An image that failed for good clears the time of the fetch, so that the next manual refresh fetches the game again
    val days = metadata.fetchedAt?.takeIf { it > 0 }?.let { ageInDays(it, now) }
    return when (metadata.fetchStatus) {
        RomMetadataFetchStatus.OK -> RomSheetCredit.Cached(days)
        RomMetadataFetchStatus.NOT_FOUND -> RomSheetCredit.NotFoundChecked(days)
        // Both a failed fetch and a requested refresh leave a row this way, and the row's time doesn't say when what it holds was fetched
        RomMetadataFetchStatus.FAILED -> when {
            metadata.hasRetroAchievementsData -> if (hasWebApiKey) RomSheetCredit.DueForRefresh else RomSheetCredit.Cached(null)
            hasWebApiKey -> RomSheetCredit.FetchPending(days)
            else -> RomSheetCredit.NoKey
        }
        null -> when {
            isHashMissing -> RomSheetCredit.HashMissing
            isHashUnmatched -> RomSheetCredit.HashNotFound
            metadata.hasRetroAchievementsData -> if (hasWebApiKey) RomSheetCredit.TitleOnlyNotDownloaded else RomSheetCredit.TitleOnlyNoKey
            !hasWebApiKey -> RomSheetCredit.NoKey
            else -> RomSheetCredit.NotDownloaded
        }
    }
}

fun relativeAge(timestamp: Long, now: Long): RelativeAge {
    val elapsed = (now - timestamp).coerceAtLeast(0)
    val hours = TimeUnit.MILLISECONDS.toHours(elapsed).toInt()
    val days = TimeUnit.MILLISECONDS.toDays(elapsed).toInt()
    return when {
        hours < 1 -> RelativeAge.JustNow
        hours < 24 -> RelativeAge.Hours(hours)
        days < 2 -> RelativeAge.Yesterday
        days < 7 -> RelativeAge.Days(days)
        days < 14 -> RelativeAge.LastWeek
        days < 30 -> RelativeAge.Weeks(days / 7)
        days < 60 -> RelativeAge.LastMonth
        days < 365 -> RelativeAge.Months(days / 30)
        days < 730 -> RelativeAge.LastYear
        else -> RelativeAge.Years(days / 365)
    }
}

private fun ageInDays(timestamp: Long, now: Long): Long {
    return TimeUnit.MILLISECONDS.toDays((now - timestamp).coerceAtLeast(0))
}

/**
 * Shows no more of a date than RetroAchievements vouches for. A date with no granularity could be anything from a real day to a placeholder for a year.
 */
fun formatReleaseDate(released: String?, granularity: String?, locale: Locale = Locale.getDefault()): String? {
    val parts = released?.trim()?.let(RELEASE_DATE::matchEntire)?.groupValues ?: return null
    val year = parts[1]
    val month = parts[2].toIntOrNull()?.takeIf { it in 1..12 }
    val day = parts[3].toIntOrNull()?.takeIf { it in 1..31 }

    return when {
        granularity == GRANULARITY_DAY && month != null && day != null -> formatDate("MMM d, yyyy", year.toInt(), month, day, locale)
        (granularity == GRANULARITY_DAY || granularity == GRANULARITY_MONTH) && month != null -> formatDate("MMM yyyy", year.toInt(), month, 1, locale)
        else -> year
    }
}

private fun formatDate(pattern: String, year: Int, month: Int, day: Int, locale: Locale): String {
    return SimpleDateFormat(pattern, locale).format(GregorianCalendar(year, month - 1, day).time)
}

private const val GRANULARITY_DAY = "day"
private const val GRANULARITY_MONTH = "month"
private val RELEASE_DATE = Regex("^(\\d{4})(?:-(\\d{2})(?:-(\\d{2}))?)?(?:\\D.*)?$")
