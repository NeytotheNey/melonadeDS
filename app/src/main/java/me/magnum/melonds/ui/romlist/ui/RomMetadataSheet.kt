package me.magnum.melonds.ui.romlist.ui

import android.view.KeyEvent
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.animateScrollBy
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.InputMode
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalInputModeManager
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.launch
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.RomLibraryMetadata
import me.magnum.melonds.ui.common.CarriedOverKeyFilter
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.common.component.romlist.RomArtThumbnail
import me.magnum.melonds.ui.common.component.romlist.libraryFocusBorder
import me.magnum.melonds.ui.common.component.romlist.relativeAgeText
import me.magnum.melonds.ui.romlist.RomIcon
import me.magnum.melonds.ui.romlist.RomSheetContent
import me.magnum.melonds.ui.romlist.RomSheetCredit
import me.magnum.melonds.ui.romlist.RomSheetFact
import me.magnum.melonds.ui.romlist.RomSheetFactLabel
import me.magnum.melonds.ui.romlist.RomSheetFactValue
import me.magnum.melonds.ui.romlist.RomSheetNote
import me.magnum.melonds.ui.romlist.buildRomSheetContent
import me.magnum.melonds.ui.romlist.ui.preview.PreviewRomLibrary
import me.magnum.melonds.ui.theme.MelonTheme

/** Enough for the facts to breathe, without stretching across a tablet or the unfolded Fold. */
private val SheetMaxWidth = 560.dp
private val SheetArtWidth = 96.dp
private val SheetScrollStep = 96.dp

/**
 * What the library knows about a ROM, with the buttons to play it or change its settings. Nothing here reaches past [metadata], which comes from the shared
 * snapshot.
 *
 * @param isHashUnmatched Whether a loaded hash library has no entry for this ROM
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RomMetadataSheet(
    rom: Rom,
    metadata: RomLibraryMetadata,
    isHashUnmatched: Boolean,
    hasWebApiKey: Boolean,
    onDismissRequest: () -> Unit,
    onPlay: () -> Unit,
    onSettings: () -> Unit,
    retrieveRomIcon: suspend (Rom) -> RomIcon,
) {
    val content = rememberSheetContent(rom, metadata, isHashUnmatched, hasWebApiKey)
    val playFocusRequester = remember { FocusRequester() }
    val scrollState = rememberScrollState()
    val coroutineScope = rememberCoroutineScope()
    val scrollStep = with(LocalDensity.current) { SheetScrollStep.toPx() }
    // The press that opened this can still be going on: a controller can keep sending repeats after the sheet opened, and they'd look like a press of Play
    val carriedOverKeyFilter = remember { CarriedOverKeyFilter() }

    ModalBottomSheet(
        onDismissRequest = onDismissRequest,
        sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true),
        sheetMaxWidth = SheetMaxWidth,
        shape = RoundedCornerShape(topStart = 28.dp, topEnd = 28.dp),
        containerColor = MaterialTheme.colorScheme.surfaceContainer,
        // Material's own handle is clickable, so a D-pad could land on it and close the sheet. Swipes are the sheet's own, so they work on this one as well
        dragHandle = null,
    ) {
        // The sheet is a window of its own, which gets the window focus a moment after its content is composed. Touch mode has no focus to give, so a sheet
        // opened by touch only gets it with the first key press
        val windowInfo = LocalWindowInfo.current
        val inputModeManager = LocalInputModeManager.current
        LaunchedEffect(Unit) {
            snapshotFlow { windowInfo.isWindowFocused && inputModeManager.inputMode == InputMode.Keyboard }
                .collect { canTakeFocus ->
                    if (canTakeFocus) {
                        playFocusRequester.requestFocus()
                    }
                }
        }

        Column(
            modifier = Modifier.fillMaxWidth().onPreviewKeyEvent { event ->
                val nativeEvent = event.nativeKeyEvent
                val isDown = nativeEvent.action == KeyEvent.ACTION_DOWN
                when {
                    carriedOverKeyFilter.shouldIgnore(nativeEvent.keyCode, isDown, nativeEvent.repeatCount) -> true
                    // Only Play and Settings can have the focus, side by side, so up and down are free to reach what doesn't fit on the screen
                    event.key == Key.DirectionUp || event.key == Key.DirectionDown -> {
                        if (isDown) {
                            val distance = if (event.key == Key.DirectionUp) -scrollStep else scrollStep
                            coroutineScope.launch { scrollState.animateScrollBy(distance) }
                        }
                        true
                    }
                    else -> false
                }
            },
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            SheetDragHandle()
            RomMetadataSheetBody(
                content = content,
                scrollState = scrollState,
                playFocusRequester = playFocusRequester,
                onPlay = onPlay,
                onSettings = onSettings,
            ) {
                RomArtThumbnail(
                    modifier = Modifier.width(SheetArtWidth),
                    rom = rom,
                    retrieveTitleIcon = { retrieveRomIcon(rom) },
                    metadata = metadata,
                )
            }
        }
    }
}

@Composable
private fun rememberSheetContent(rom: Rom, metadata: RomLibraryMetadata, isHashUnmatched: Boolean, hasWebApiKey: Boolean): RomSheetContent {
    return remember(rom, metadata, isHashUnmatched, hasWebApiKey) {
        buildRomSheetContent(
            customName = rom.config.customName,
            name = rom.name,
            fileName = rom.fileName,
            lastPlayedAt = rom.lastPlayed?.time,
            metadata = metadata,
            isHashUnmatched = isHashUnmatched,
            isHashMissing = rom.retroAchievementsHash.isBlank(),
            hasWebApiKey = hasWebApiKey,
            now = System.currentTimeMillis(),
        )
    }
}

@Composable
private fun SheetDragHandle() {
    Box(
        modifier = Modifier
            .padding(top = 12.dp, bottom = 16.dp)
            .size(width = 32.dp, height = 4.dp)
            .background(MaterialTheme.colorScheme.outline.copy(alpha = 0.6f), RoundedCornerShape(2.dp)),
    )
}

/**
 * The buttons and credit stay put, and what's above them scrolls when the sheet doesn't fit, as in landscape or on the cover screen.
 */
@Composable
private fun ColumnScope.RomMetadataSheetBody(
    content: RomSheetContent,
    scrollState: ScrollState,
    playFocusRequester: FocusRequester?,
    onPlay: () -> Unit,
    onSettings: () -> Unit,
    art: @Composable () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .weight(1f, fill = false)
            .verticalScroll(scrollState)
            .padding(horizontal = 20.dp),
    ) {
        Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            art()
            Column(Modifier.weight(1f)) {
                Text(
                    text = content.title,
                    fontSize = 20.sp,
                    lineHeight = 24.sp,
                    fontWeight = FontWeight.Medium,
                    modifier = Modifier.padding(bottom = 2.dp),
                )
                Text(
                    text = content.subtitle,
                    fontSize = 13.sp,
                    lineHeight = 18.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }

        Column(
            modifier = Modifier.padding(top = 16.dp, bottom = 12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            content.facts.chunked(2).forEach { rowFacts ->
                Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    rowFacts.forEach { fact ->
                        SheetFact(Modifier.weight(1f), fact)
                    }
                    if (rowFacts.size == 1) {
                        Box(Modifier.weight(1f))
                    }
                }
            }
            content.note?.let {
                SheetFactLayout(
                    modifier = Modifier.fillMaxWidth(),
                    label = stringResource(R.string.rom_sheet_fact_metadata),
                ) {
                    Text(
                        text = sheetNoteText(it),
                        fontSize = 14.sp,
                        lineHeight = 20.sp,
                    )
                }
            }
        }
    }

    Row(
        modifier = Modifier.fillMaxWidth().padding(start = 20.dp, top = 4.dp, end = 20.dp),
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Button(
            modifier = Modifier
                .weight(1f)
                .height(40.dp)
                .then(if (playFocusRequester != null) Modifier.focusRequester(playFocusRequester) else Modifier)
                .libraryFocusBorder(CircleShape, color = MaterialTheme.colorScheme.onPrimary, includeChildren = true),
            onClick = onPlay,
            contentPadding = PaddingValues(horizontal = 24.dp),
        ) {
            SheetButtonContent(
                icon = { Icon(Icons.Default.PlayArrow, contentDescription = null, modifier = Modifier.size(18.dp)) },
                text = stringResource(R.string.play),
            )
        }
        FilledTonalButton(
            modifier = Modifier
                .height(40.dp)
                .libraryFocusBorder(CircleShape, color = MaterialTheme.colorScheme.onSecondaryContainer, includeChildren = true),
            onClick = onSettings,
            contentPadding = PaddingValues(horizontal = 24.dp),
        ) {
            SheetButtonContent(
                icon = { Icon(Icons.Default.Settings, contentDescription = null, modifier = Modifier.size(18.dp)) },
                text = stringResource(R.string.settings),
            )
        }
    }

    Text(
        modifier = Modifier
            .fillMaxWidth()
            .padding(start = 20.dp, top = 14.dp, end = 20.dp, bottom = 24.dp),
        text = sheetCreditText(content.credit),
        fontSize = 11.sp,
        lineHeight = 15.sp,
        textAlign = TextAlign.Center,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

@Composable
private fun SheetButtonContent(icon: @Composable () -> Unit, text: String) {
    icon()
    Box(Modifier.width(8.dp))
    Text(
        text = text,
        style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Medium),
        maxLines = 1,
    )
}

@Composable
private fun SheetFact(modifier: Modifier, fact: RomSheetFact) {
    SheetFactLayout(modifier, stringResource(fact.label.stringRes())) {
        Text(
            text = sheetFactValueText(fact.value),
            style = TextStyle(
                fontSize = 14.sp,
                lineHeight = 20.sp,
                fontWeight = FontWeight.Medium,
                fontFeatureSettings = "tnum",
            ),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun SheetFactLayout(modifier: Modifier, label: String, value: @Composable () -> Unit) {
    Column(modifier) {
        Text(
            text = label.uppercase(),
            fontSize = 11.sp,
            lineHeight = 15.sp,
            letterSpacing = 0.55.sp,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 1,
        )
        value()
    }
}

private fun RomSheetFactLabel.stringRes(): Int = when (this) {
    RomSheetFactLabel.PUBLISHER -> R.string.rom_sheet_fact_publisher
    RomSheetFactLabel.DEVELOPER -> R.string.rom_sheet_fact_developer
    RomSheetFactLabel.GENRE -> R.string.rom_sheet_fact_genre
    RomSheetFactLabel.RELEASED -> R.string.rom_sheet_fact_released
    RomSheetFactLabel.ACHIEVEMENTS -> R.string.rom_sheet_fact_achievements
    RomSheetFactLabel.LAST_PLAYED -> R.string.rom_sheet_fact_last_played
    RomSheetFactLabel.FILE -> R.string.rom_sheet_fact_file
}

@Composable
private fun sheetFactValueText(value: RomSheetFactValue): String {
    return when (value) {
        is RomSheetFactValue.Text -> value.text
        is RomSheetFactValue.AchievementCount -> {
            val inSet = stringResource(R.string.rom_sheet_achievements_in_set, value.total)
            value.earned?.let { stringResource(R.string.rom_sheet_achievements_earned, inSet, it) } ?: inSet
        }
        RomSheetFactValue.HasAchievements -> stringResource(R.string.rom_sheet_has_achievements)
        RomSheetFactValue.NoAchievementSet -> stringResource(R.string.rom_sheet_no_achievement_set)
        is RomSheetFactValue.Played -> relativeAgeText(value.age)
        RomSheetFactValue.NeverPlayed -> stringResource(R.string.rom_sheet_never_played)
    }
}

@Composable
private fun sheetNoteText(note: RomSheetNote): String {
    return stringResource(
        when (note) {
            RomSheetNote.NOT_FOUND_SHOWING_FILE_NAME -> R.string.rom_sheet_note_not_found_file_name
            RomSheetNote.NOT_FOUND_SHOWING_BANNER -> R.string.rom_sheet_note_not_found_banner
            RomSheetNote.HASH_MISSING -> R.string.rom_sheet_note_hash_missing
            RomSheetNote.NO_WEB_API_KEY -> R.string.rom_sheet_note_no_key
            RomSheetNote.NOT_DOWNLOADED_YET -> R.string.rom_sheet_note_not_downloaded
            RomSheetNote.FETCH_PENDING -> R.string.rom_sheet_note_fetch_pending
        },
    )
}

@Composable
private fun sheetCreditText(credit: RomSheetCredit): String {
    return when (credit) {
        is RomSheetCredit.Cached -> credit.days?.let { stringResource(R.string.rom_sheet_credit_cached, daysAgoText(it)) }
            ?: stringResource(R.string.rom_sheet_credit_cached_no_age)
        RomSheetCredit.DueForRefresh -> stringResource(R.string.rom_sheet_credit_due_for_refresh)
        is RomSheetCredit.NotFoundChecked -> credit.days?.let { stringResource(R.string.rom_sheet_credit_not_found, daysAgoText(it)) }
            ?: stringResource(R.string.rom_sheet_credit_not_found_no_age)
        is RomSheetCredit.FetchPending -> credit.days?.let { stringResource(R.string.rom_sheet_credit_fetch_pending, daysAgoText(it)) }
            ?: stringResource(R.string.rom_sheet_credit_fetch_pending_no_age)
        RomSheetCredit.HashNotFound -> stringResource(R.string.rom_sheet_credit_hash_not_found)
        RomSheetCredit.HashMissing -> stringResource(R.string.rom_sheet_credit_hash_missing)
        RomSheetCredit.TitleOnlyNoKey -> stringResource(R.string.rom_sheet_credit_title_only_no_key)
        RomSheetCredit.TitleOnlyNotDownloaded -> stringResource(R.string.rom_sheet_credit_title_only_not_downloaded)
        RomSheetCredit.NoKey -> stringResource(R.string.rom_sheet_credit_no_key)
        RomSheetCredit.NotDownloaded -> stringResource(R.string.rom_sheet_credit_not_downloaded)
    }
}

@Composable
private fun daysAgoText(days: Long): String {
    return if (days == 0L) {
        stringResource(R.string.relative_today)
    } else {
        pluralStringResource(R.plurals.relative_days_ago, days.toInt(), days.toInt())
    }
}

/** A dialog window doesn't render in a preview, so this shows the sheet's insides on its color. */
@Composable
private fun PreviewSheet(rom: Rom, isHashUnmatched: Boolean = false, hasWebApiKey: Boolean = true) {
    MelonTheme {
        val metadata = PreviewRomLibrary.metadataFor(rom)
        Surface(
            modifier = Modifier.fillMaxSize(),
            color = MaterialTheme.colorScheme.surfaceContainer,
            shape = RoundedCornerShape(topStart = 28.dp, topEnd = 28.dp),
        ) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                SheetDragHandle()
                RomMetadataSheetBody(
                    content = rememberSheetContent(rom, metadata, isHashUnmatched, hasWebApiKey),
                    scrollState = rememberScrollState(),
                    playFocusRequester = null,
                    onPlay = { },
                    onSettings = { },
                ) {
                    RomArtThumbnail(
                        modifier = Modifier.width(SheetArtWidth),
                        rom = rom,
                        retrieveTitleIcon = { PreviewRomLibrary.icon },
                        metadata = metadata,
                    )
                }
            }
        }
    }
}

@MelonPreviewSet
@Composable
private fun PreviewRomMetadataSheetKnownGame() {
    PreviewSheet(PreviewRomLibrary.roms[0])
}

@MelonPreviewSet
@Composable
private fun PreviewRomMetadataSheetUnknownRom() {
    PreviewSheet(PreviewRomLibrary.roms[7], isHashUnmatched = true)
}

@MelonPreviewSet
@Composable
private fun PreviewRomMetadataSheetNoKey() {
    PreviewSheet(PreviewRomLibrary.roms[5], hasWebApiKey = false)
}
