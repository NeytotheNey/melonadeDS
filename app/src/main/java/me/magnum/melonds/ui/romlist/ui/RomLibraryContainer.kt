package me.magnum.melonds.ui.romlist.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.gestures.LocalBringIntoViewSpec
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.material3.pulltorefresh.PullToRefreshDefaults
import androidx.compose.material3.pulltorefresh.rememberPullToRefreshState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.ui.common.PaddedListBringIntoViewSpec
import me.magnum.melonds.ui.common.component.romlist.libraryFocusBorder

/** The space around the library's content, below the controls. */
internal val LibraryContentHorizontalPadding = 16.dp
internal val LibraryContentTopPadding = 4.dp
internal val LibraryContentBottomPadding = 24.dp

/**
 * The pull-to-refresh, loading and empty states shared by the grid and mosaic views; [content] draws the loaded, non-empty library.
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
internal fun RomLibraryContainer(
    modifier: Modifier,
    roms: List<Rom>?,
    isRefreshing: Boolean,
    onRefresh: () -> Unit,
    contentPadding: PaddingValues,
    emptyMessage: String? = null,
    onShowAll: (() -> Unit)? = null,
    content: @Composable (List<Rom>) -> Unit,
) {
    val pullRefreshState = rememberPullToRefreshState()

    PullToRefreshBox(
        modifier = modifier,
        isRefreshing = isRefreshing,
        onRefresh = onRefresh,
        state = pullRefreshState,
        indicator = {
            PullToRefreshDefaults.Indicator(
                modifier = Modifier.align(Alignment.TopCenter).padding(top = contentPadding.calculateTopPadding()),
                isRefreshing = isRefreshing,
                state = pullRefreshState,
                color = MaterialTheme.colorScheme.primary,
            )
        },
    ) {
        when {
            roms == null -> Box(Modifier.fillMaxSize())
            roms.isEmpty() && !isRefreshing -> {
                LibraryEmptyMessage(
                    modifier = Modifier.fillMaxSize().padding(contentPadding).consumeWindowInsets(contentPadding),
                    message = emptyMessage,
                    onShowAll = onShowAll,
                )
            }
            else -> {
                val density = LocalDensity.current
                val bringIntoViewSpec = remember(density, contentPadding) {
                    with(density) {
                        PaddedListBringIntoViewSpec(
                            leadingPadding = contentPadding.calculateTopPadding().toPx(),
                            trailingPadding = contentPadding.calculateBottomPadding().toPx(),
                        )
                    }
                }

                // A focused tile would otherwise be allowed to stop underneath the top bar
                CompositionLocalProvider(LocalBringIntoViewSpec provides bringIntoViewSpec) {
                    content(roms)
                }
            }
        }
    }
}

/**
 * Says why there's nothing to show.
 *
 * @param onShowAll Takes back what's hiding the games, for when a filter is the reason
 */
@Composable
internal fun LibraryEmptyMessage(modifier: Modifier, message: String?, onShowAll: (() -> Unit)?) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterVertically),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            modifier = Modifier.padding(horizontal = 24.dp),
            text = message ?: stringResource(R.string.no_roms_found),
            style = MaterialTheme.typography.bodyLarge,
            textAlign = TextAlign.Center,
        )
        onShowAll?.let {
            TextButton(
                modifier = Modifier.libraryFocusBorder(CircleShape, includeChildren = true),
                onClick = it,
            ) {
                Text(stringResource(R.string.rom_library_show_all))
            }
        }
    }
}
