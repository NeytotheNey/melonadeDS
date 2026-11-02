package me.magnum.melonds.ui.common

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import me.magnum.melonds.R

/**
 * The layout's [name], or "Unnamed layout" for one without, numbered with [unnamedNumber] when it's above 0
 */
@Composable
fun layoutDisplayName(name: String?, unnamedNumber: Int = 0): String {
    return when {
        !name.isNullOrBlank() -> name
        unnamedNumber > 0 -> stringResource(R.string.layout_unnamed_numbered, unnamedNumber)
        else -> stringResource(R.string.layout_unnamed)
    }
}
