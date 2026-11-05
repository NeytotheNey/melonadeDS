package me.magnum.melonds.ui.romlist.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.calculateEndPadding
import androidx.compose.foundation.layout.calculateStartPadding
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.unit.dp
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.RomLibraryMetadata
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.common.component.romlist.RomMosaicTile
import me.magnum.melonds.ui.romlist.DEFAULT_MAX_FEATURED
import me.magnum.melonds.ui.romlist.DEFAULT_MOSAIC_COLUMNS
import me.magnum.melonds.ui.romlist.MosaicTile
import me.magnum.melonds.ui.romlist.RomIcon
import me.magnum.melonds.ui.romlist.arrangeMosaic
import me.magnum.melonds.ui.romlist.ui.preview.PreviewRomLibrary
import me.magnum.melonds.ui.theme.MelonTheme

private val MosaicGutter = 6.dp

/**
 * The library as full-bleed art tiles. The two most recently played games are promoted to the top as wide tiles, the rest are squares,
 * arranged by [arrangeMosaic] so the rows stay full.
 */
@Composable
fun RomMosaic(
    modifier: Modifier = Modifier,
    roms: List<Rom>?,
    isRefreshing: Boolean,
    onRefresh: () -> Unit,
    contentPadding: PaddingValues = PaddingValues(0.dp),
    onRomClick: (Rom) -> Unit,
    onRomConfigClick: (Rom) -> Unit,
    retrieveRomIcon: suspend (Rom) -> RomIcon,
    retrieveRomMetadata: suspend (Rom) -> RomLibraryMetadata,
) {
    RomLibraryContainer(
        modifier = modifier,
        roms = roms,
        isRefreshing = isRefreshing,
        onRefresh = onRefresh,
        contentPadding = contentPadding,
    ) { loadedRoms ->
        val tiles = remember(loadedRoms) { buildMosaicTiles(loadedRoms) }
        val layoutDirection = LocalLayoutDirection.current

        LazyVerticalGrid(
            modifier = Modifier.fillMaxSize(),
            columns = GridCells.Fixed(DEFAULT_MOSAIC_COLUMNS),
            contentPadding = PaddingValues(
                start = contentPadding.calculateStartPadding(layoutDirection) + MosaicGutter,
                top = contentPadding.calculateTopPadding() + MosaicGutter,
                end = contentPadding.calculateEndPadding(layoutDirection) + MosaicGutter,
                bottom = contentPadding.calculateBottomPadding() + MosaicGutter,
            ),
            horizontalArrangement = Arrangement.spacedBy(MosaicGutter),
            verticalArrangement = Arrangement.spacedBy(MosaicGutter),
        ) {
            items(
                items = tiles,
                key = { it.item.uri.toString() },
                // The arrangement already keeps rows full; the guard only matters if the span no longer fits for some other reason
                span = { tile -> GridItemSpan(minOf(tile.columnSpan, maxCurrentLineSpan)) },
            ) { tile ->
                val rom = tile.item
                RomMosaicTile(
                    modifier = Modifier.animateItem(),
                    rom = rom,
                    isFeatured = tile.isFeatured,
                    onClick = { onRomClick(rom) },
                    onLongClick = { onRomConfigClick(rom) },
                    retrieveTitleIcon = { retrieveRomIcon(rom) },
                    retrieveMetadata = { retrieveRomMetadata(rom) },
                )
            }
        }
    }
}

/** Features the two most recently played games, whatever the list is currently sorted by. */
private fun buildMosaicTiles(roms: List<Rom>): List<MosaicTile<Rom>> {
    val featured = roms
        .filter { it.lastPlayed != null }
        .sortedByDescending { it.lastPlayed }
        .take(DEFAULT_MAX_FEATURED)
    val featuredUris = featured.map { it.uri }.toSet()
    // Featured games lead in play order; the arrangement then fills each featured row with the next games in the user's own sort order
    val ordered = featured + roms.filter { it.uri !in featuredUris }
    return arrangeMosaic(ordered, columns = DEFAULT_MOSAIC_COLUMNS, maxFeatured = DEFAULT_MAX_FEATURED) { it.uri in featuredUris }
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
            onRomClick = { },
            onRomConfigClick = { },
            retrieveRomIcon = { PreviewRomLibrary.icon },
            retrieveRomMetadata = { PreviewRomLibrary.metadataFor(it) },
        )
    }
}
