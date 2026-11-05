package me.magnum.melonds.ui.romlist.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.calculateEndPadding
import androidx.compose.foundation.layout.calculateStartPadding
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlinx.coroutines.flow.StateFlow
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.RomMetadataSnapshot
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.common.component.romlist.LocalLibraryFocus
import me.magnum.melonds.ui.common.component.romlist.RomGridTile
import me.magnum.melonds.ui.romlist.RomIcon
import me.magnum.melonds.ui.romlist.ui.preview.PreviewRomLibrary
import me.magnum.melonds.ui.theme.MelonTheme

/** Narrow enough for three columns on a phone, the Fold's cover screen included, and more as the window widens. */
private val GridMinTileWidth = 96.dp
private val GridColumnGap = 12.dp
private val GridRowGap = 14.dp

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
    emptyMessage: String? = null,
    onShowAll: (() -> Unit)? = null,
    onRomClick: (Rom) -> Unit,
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
        val gridState = rememberLazyGridState()

        // The tile that's to take the focus back has to be there first
        val pendingFocusRomUri = LocalLibraryFocus.current?.pendingRomUri
        LaunchedEffect(pendingFocusRomUri, loadedRoms) {
            val index = loadedRoms.indexOfFirst { it.uri == pendingFocusRomUri }
            if (index >= 0 && gridState.layoutInfo.visibleItemsInfo.none { it.index == index }) {
                gridState.scrollToItem(index)
            }
        }

        LazyVerticalGrid(
            modifier = Modifier.fillMaxSize(),
            state = gridState,
            columns = GridCells.Adaptive(GridMinTileWidth),
            contentPadding = PaddingValues(
                start = contentPadding.calculateStartPadding(layoutDirection) + LibraryContentHorizontalPadding,
                top = contentPadding.calculateTopPadding() + LibraryContentTopPadding,
                end = contentPadding.calculateEndPadding(layoutDirection) + LibraryContentHorizontalPadding,
                bottom = contentPadding.calculateBottomPadding() + LibraryContentBottomPadding,
            ),
            horizontalArrangement = Arrangement.spacedBy(GridColumnGap),
            verticalArrangement = Arrangement.spacedBy(GridRowGap),
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
                    metadata = metadataSnapshot.metadataFor(rom),
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
            romMetadata = PreviewRomLibrary.metadataSnapshot,
        )
    }
}
