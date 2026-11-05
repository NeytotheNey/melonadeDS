package me.magnum.melonds.ui.romlist.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.material3.pulltorefresh.PullToRefreshDefaults
import androidx.compose.material3.pulltorefresh.rememberPullToRefreshState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.rom.Rom

/**
 * The pull-to-refresh, loading and empty states shared by the grid and mosaic views; [content] draws the loaded, non-empty library.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun RomLibraryContainer(
    modifier: Modifier,
    roms: List<Rom>?,
    isRefreshing: Boolean,
    onRefresh: () -> Unit,
    contentPadding: PaddingValues,
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
                Box(
                    modifier = Modifier.fillMaxSize().padding(contentPadding).consumeWindowInsets(contentPadding),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        text = stringResource(R.string.no_roms_found),
                        style = MaterialTheme.typography.bodyLarge,
                    )
                }
            }
            else -> content(roms)
        }
    }
}
