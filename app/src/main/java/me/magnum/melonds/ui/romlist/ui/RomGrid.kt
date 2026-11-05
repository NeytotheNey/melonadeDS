package me.magnum.melonds.ui.romlist.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.calculateEndPadding
import androidx.compose.foundation.layout.calculateStartPadding
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.unit.dp
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.RomLibraryMetadata
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.common.component.romlist.RomGridTile
import me.magnum.melonds.ui.romlist.RomIcon
import me.magnum.melonds.ui.romlist.ui.preview.PreviewRomLibrary
import me.magnum.melonds.ui.theme.MelonTheme

/** Narrow enough for three columns on a phone in portrait, and more as the window widens. */
private val GridMinTileWidth = 110.dp
private val GridGutter = 12.dp

/**
 * The library as box-art cards. Columns adapt to the width, so a tablet or an unfolded phone gets more of them.
 */
@Composable
fun RomGrid(
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
        val layoutDirection = LocalLayoutDirection.current
        LazyVerticalGrid(
            modifier = Modifier.fillMaxSize(),
            columns = GridCells.Adaptive(GridMinTileWidth),
            contentPadding = PaddingValues(
                start = contentPadding.calculateStartPadding(layoutDirection) + GridGutter,
                top = contentPadding.calculateTopPadding() + GridGutter,
                end = contentPadding.calculateEndPadding(layoutDirection) + GridGutter,
                bottom = contentPadding.calculateBottomPadding() + GridGutter,
            ),
            horizontalArrangement = Arrangement.spacedBy(GridGutter),
            verticalArrangement = Arrangement.spacedBy(GridGutter),
        ) {
            items(
                items = loadedRoms,
                key = { it.uri.toString() },
            ) { rom ->
                RomGridTile(
                    modifier = Modifier.animateItem(),
                    rom = rom,
                    onClick = { onRomClick(rom) },
                    onLongClick = { onRomConfigClick(rom) },
                    retrieveTitleIcon = { retrieveRomIcon(rom) },
                    retrieveMetadata = { retrieveRomMetadata(rom) },
                )
            }
        }
    }
}

@MelonPreviewSet
@Composable
private fun PreviewRomGrid() {
    MelonTheme {
        RomGrid(
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
