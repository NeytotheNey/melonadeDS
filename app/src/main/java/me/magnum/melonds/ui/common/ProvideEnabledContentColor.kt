package me.magnum.melonds.ui.common

import androidx.compose.material3.LocalContentColor
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider

/**
 * Dims the content color of [content] to the M3 disabled alpha when [enabled] is false.
 */
@Composable
fun ProvideEnabledContentColor(enabled: Boolean, content: @Composable () -> Unit) {
    val contentColor = LocalContentColor.current
    CompositionLocalProvider(LocalContentColor provides if (enabled) contentColor else contentColor.copy(alpha = 0.38f), content = content)
}
