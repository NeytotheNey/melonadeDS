package me.magnum.melonds.ui.common.preference

import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import me.magnum.melonds.ui.common.ProvideEnabledContentColor
import me.magnum.melonds.ui.common.component.text.CaptionText

/**
 * A row showing a setting's [value] that launches [onLaunchAction] when tapped, with its end padding shrunk to 8 dp for an icon button in
 * [trailingContent]
 */
@Composable
fun ActionLauncherItem(
    name: String,
    value: String,
    enabled: Boolean = true,
    onLaunchAction: (() -> Unit)?,
    horizontalPadding: Dp = 16.dp,
    valueMaxLines: Int = 1,
    trailingContent: (@Composable () -> Unit)? = null,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .run {
                if (enabled && onLaunchAction != null) {
                    clickable { onLaunchAction() }.focusable()
                } else {
                    this
                }
            }
            .heightIn(min = 64.dp)
            .padding(start = horizontalPadding, end = if (trailingContent != null) 8.dp else horizontalPadding, top = 8.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            ProvideEnabledContentColor(enabled) {
                Text(
                    text = name,
                    style = MaterialTheme.typography.bodyLarge,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                CaptionText(
                    text = value,
                    maxLines = valueMaxLines,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        trailingContent?.invoke()
    }
}