package me.magnum.melonds.ui.romlist.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.calculateEndPadding
import androidx.compose.foundation.layout.calculateStartPadding
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlinx.coroutines.flow.StateFlow
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.SortingMode
import me.magnum.melonds.domain.model.SortingOrder
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.RomMetadataSnapshot
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.common.component.romlist.LocalLibraryFocus
import me.magnum.melonds.ui.common.component.romlist.MosaicEyebrow
import me.magnum.melonds.ui.common.component.romlist.RomMosaicTile
import me.magnum.melonds.ui.romlist.MosaicBand
import me.magnum.melonds.ui.romlist.MosaicPlacement
import me.magnum.melonds.ui.romlist.MosaicSectionLabel
import me.magnum.melonds.ui.romlist.MosaicTileSize
import me.magnum.melonds.ui.romlist.RomIcon
import me.magnum.melonds.ui.romlist.chooseMosaicSizes
import me.magnum.melonds.ui.romlist.hasTileAtEnd
import me.magnum.melonds.ui.romlist.isWideMosaicGame
import me.magnum.melonds.ui.romlist.mosaicColumnsFor
import me.magnum.melonds.ui.romlist.mosaicSectionLabel
import me.magnum.melonds.ui.romlist.packMosaic
import me.magnum.melonds.ui.romlist.ui.preview.PreviewRomLibrary
import me.magnum.melonds.ui.theme.MelonTheme

private val MosaicGutter = 6.dp
private val MosaicRowHeight = 88.dp

/**
 * The library as a dense grid of full-bleed art tiles: the most recently played games are 2x2, some of the games with a title screen 2x1, and the rest single
 * cells.
 * A lazy grid can't span rows, so the tiles are packed up front into bands that a lazy column lays out one by one, and D-pad focus then goes by where the
 * tiles really are.
 */
@Composable
fun RomMosaic(
    modifier: Modifier = Modifier,
    roms: List<Rom>?,
    isRefreshing: Boolean,
    onRefresh: () -> Unit,
    contentPadding: PaddingValues = PaddingValues(0.dp),
    emptyMessage: String? = null,
    onShowAll: (() -> Unit)? = null,
    sortingMode: SortingMode,
    sortingOrder: SortingOrder,
    onRomClick: (Rom) -> Unit,
    onRomPlay: (Rom) -> Unit,
    onRomConfigClick: (Rom) -> Unit,
    retrieveRomIcon: suspend (Rom) -> RomIcon,
    romMetadata: StateFlow<RomMetadataSnapshot>,
) {
    RomLibraryContainer(
        modifier = modifier,
        roms = roms,
        isRefreshing = isRefreshing,
        onRefresh = onRefresh,
        contentPadding = contentPadding,
        emptyMessage = emptyMessage,
        onShowAll = onShowAll,
    ) { loadedRoms ->
        val layoutDirection = LocalLayoutDirection.current
        // Stops with the activity, so this costs nothing during a game
        val metadataSnapshot by romMetadata.collectAsStateWithLifecycle()

        BoxWithConstraints(Modifier.fillMaxSize()) {
            val startPadding = contentPadding.calculateStartPadding(layoutDirection) + LibraryContentHorizontalPadding
            val endPadding = contentPadding.calculateEndPadding(layoutDirection) + LibraryContentHorizontalPadding
            val columns = mosaicColumnsFor((maxWidth - startPadding - endPadding).value, MosaicGutter.value)

            val listState = rememberLazyListState()
            val isAtTop by remember { derivedStateOf { listState.firstVisibleItemIndex == 0 && listState.firstVisibleItemScrollOffset == 0 } }

            // Title art arriving during a sync mustn't move tiles under someone looking at them, so new art only makes it pack again when it packs anyway or
            // from the top, where a new wide tile has nothing above it to move. The keys do that: away from the top the last one holds still
            val romsWideByArt = remember(loadedRoms, metadataSnapshot) {
                loadedRoms.filter { metadataSnapshot.metadataFor(it).titleArtFile != null && isWideMosaicGame(it.retroAchievementsHash) }.map { it.uri }.toSet()
            }
            val wideRoms = remember(loadedRoms, columns, if (isAtTop) romsWideByArt else null) { romsWideByArt }
            val bands = remember(loadedRoms, wideRoms, columns) {
                val entries = chooseMosaicSizes(
                    items = loadedRoms,
                    lastPlayedAt = { it.lastPlayed?.time },
                    hasTitleArt = { it.uri in wideRoms },
                )
                packMosaic(entries, columns)
            }
            val sectionLabel = remember(loadedRoms, sortingMode, sortingOrder) {
                mosaicSectionLabel(sortingMode, sortingOrder, loadedRoms.any { it.lastPlayed != null })
            }
            val firstFeaturedRom = bands.firstOrNull()?.tiles?.firstOrNull { it.size == MosaicTileSize.FEATURED }?.item
            val firstBandIndex = if (sectionLabel != null) 1 else 0

            // Packing again changes which band a tile is in, which is what the list's own position goes by, so the position is kept by a tile instead
            var anchorRomUri by rememberSaveable { mutableStateOf<String?>(null) }
            val currentBands by rememberUpdatedState(bands)
            val currentFirstBandIndex by rememberUpdatedState(firstBandIndex)
            LaunchedEffect(listState) {
                // Only while scrolling, since the position in between packing and getting back to the anchor says nothing
                snapshotFlow {
                    if (listState.isScrollInProgress) listState.firstVisibleItemIndex to (listState.firstVisibleItemScrollOffset == 0) else null
                }.collect { position ->
                    position ?: return@collect
                    val (index, isAtItemStart) = position
                    val band = currentBands.getOrNull((index - currentFirstBandIndex).coerceAtLeast(0))
                    anchorRomUri = when {
                        (index == 0 && isAtItemStart) || band == null -> null
                        band.tiles.any { it.item.uri.toString() == anchorRomUri } -> anchorRomUri
                        else -> band.tiles.first().item.uri.toString()
                    }
                }
            }
            LaunchedEffect(bands) {
                val index = bands.indexOfFirst { band -> band.tiles.any { it.item.uri.toString() == anchorRomUri } }
                if (index >= 0) {
                    listState.scrollToItem(index + firstBandIndex)
                }
            }

            val libraryFocus = LocalLibraryFocus.current
            val pendingFocusRomUri = libraryFocus?.pendingRomUri
            // The tile that's to take the focus back has to be there first. This comes after the anchor, which it overrules when both run
            LaunchedEffect(pendingFocusRomUri, bands) {
                val index = bands.indexOfFirst { band -> band.tiles.any { it.item.uri == pendingFocusRomUri } }
                if (index >= 0 && listState.layoutInfo.visibleItemsInfo.none { it.index == index + firstBandIndex }) {
                    listState.scrollToItem(index + firstBandIndex)
                }
            }

            LazyColumn(
                modifier = Modifier.fillMaxSize(),
                state = listState,
                contentPadding = PaddingValues(
                    start = startPadding,
                    top = contentPadding.calculateTopPadding() + LibraryContentTopPadding,
                    end = endPadding,
                    bottom = contentPadding.calculateBottomPadding() + LibraryContentBottomPadding,
                ),
                verticalArrangement = Arrangement.spacedBy(MosaicGutter),
            ) {
                sectionLabel?.let { label ->
                    item(key = "section-label", contentType = "section-label") {
                        MosaicSectionTitle(label)
                    }
                }
                items(
                    items = bands,
                    key = { it.tiles.first().item.uri.toString() },
                    contentType = { it.rowCount },
                ) { band ->
                    MosaicBandRow(
                        modifier = Modifier,
                        band = band,
                        columns = columns,
                    ) { tile ->
                        val rom = tile.item
                        RomMosaicTile(
                            modifier = Modifier,
                            rom = rom,
                            size = tile.size,
                            eyebrow = when {
                                tile.size != MosaicTileSize.FEATURED -> null
                                rom === firstFeaturedRom -> MosaicEyebrow.CONTINUE
                                else -> MosaicEyebrow.RECENT
                            },
                            upFocusRequester = libraryFocus?.controlsFocusRequester?.takeIf { band === bands.first() && tile.row == 0 },
                            isLastInRow = !hasTileAtEnd(band, tile),
                            onClick = { onRomClick(rom) },
                            onLongClick = { onRomConfigClick(rom) },
                            onContinueClick = { onRomPlay(rom) },
                            onSettingsClick = { onRomConfigClick(rom) },
                            retrieveTitleIcon = { retrieveRomIcon(rom) },
                            metadata = metadataSnapshot.metadataFor(rom),
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun MosaicSectionTitle(label: MosaicSectionLabel) {
    val text = when (label) {
        MosaicSectionLabel.RECENTLY_PLAYED -> R.string.rom_mosaic_label_recently_played
        MosaicSectionLabel.LEAST_RECENTLY_PLAYED -> R.string.rom_mosaic_label_least_recently_played
        MosaicSectionLabel.A_TO_Z -> R.string.rom_mosaic_label_a_to_z
        MosaicSectionLabel.Z_TO_A -> R.string.rom_mosaic_label_z_to_a
    }
    Text(
        modifier = Modifier.fillMaxWidth().padding(top = 8.dp, bottom = 2.dp),
        text = stringResource(text).uppercase(),
        style = MaterialTheme.typography.labelMedium.copy(fontSize = 12.sp, fontWeight = FontWeight.Medium, letterSpacing = 0.72.sp),
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/**
 * One band: tiles go on the band's own small grid, sized to its cells and the gaps they cover.
 */
@Composable
private fun MosaicBandRow(
    modifier: Modifier,
    band: MosaicBand<Rom>,
    columns: Int,
    tile: @Composable (MosaicPlacement<Rom>) -> Unit,
) {
    Layout(
        modifier = modifier,
        content = {
            band.tiles.forEach {
                key(it.item.uri) {
                    tile(it)
                }
            }
        },
    ) { measurables, constraints ->
        val gutter = MosaicGutter.roundToPx()
        val rowHeight = MosaicRowHeight.roundToPx()
        val width = constraints.maxWidth
        // Worked out from the band's width each time, so that rounding can't add up over the columns and every row ends at the same edge
        fun columnStart(column: Int) = column * (width + gutter) / columns

        val placeables = measurables.mapIndexed { index, measurable ->
            val placement = band.tiles[index]
            val tileWidth = columnStart(placement.column + placement.size.columnSpan) - gutter - columnStart(placement.column)
            val tileHeight = placement.size.rowSpan * rowHeight + (placement.size.rowSpan - 1) * gutter
            measurable.measure(Constraints.fixed(tileWidth, tileHeight))
        }

        layout(width, band.rowCount * rowHeight + (band.rowCount - 1) * gutter) {
            placeables.forEachIndexed { index, placeable ->
                val placement = band.tiles[index]
                placeable.placeRelative(columnStart(placement.column), placement.row * (rowHeight + gutter))
            }
        }
    }
}

@MelonPreviewSet
@Composable
private fun PreviewRomMosaic() {
    MelonTheme {
        RomMosaic(
            modifier = Modifier.fillMaxSize(),
            roms = PreviewRomLibrary.roms,
            isRefreshing = false,
            onRefresh = { },
            sortingMode = SortingMode.RECENTLY_PLAYED,
            sortingOrder = SortingOrder.DESCENDING,
            onRomClick = { },
            onRomPlay = { },
            onRomConfigClick = { },
            retrieveRomIcon = { PreviewRomLibrary.icon },
            romMetadata = PreviewRomLibrary.metadataSnapshot,
        )
    }
}
