package me.magnum.melonds.ui.common.preference

import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.*
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import me.magnum.melonds.ui.common.component.text.CaptionText

/**
 * A switch row. [isTwoLine] rows are as tall as an [ActionLauncherItem], with or without a [caption].
 * Unlike [ActionLauncherItem], [enabled] greys out only the switch, not the [name] or [caption].
 */
@Composable
fun SwitchItem(
    name: String,
    isOn: Boolean,
    onToggle: (Boolean) -> Unit,
    horizontalPadding: Dp = 16.dp,
    caption: String? = null,
    captionMaxLines: Int = Int.MAX_VALUE,
    enabled: Boolean = true,
    isTwoLine: Boolean = caption != null,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .run {
                if (enabled) {
                    clickable { onToggle(!isOn) }.focusable()
                } else {
                    this
                }
            }
            .heightIn(min = if (isTwoLine) 64.dp else 48.dp)
            .padding(start = horizontalPadding, end = horizontalPadding, top = 8.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = name,
                style = MaterialTheme.typography.bodyLarge,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (caption != null) {
                CaptionText(
                    text = caption,
                    maxLines = captionMaxLines,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        Spacer(Modifier.width(if (isTwoLine) 8.dp else 16.dp))
        Switch(
            checked = isOn,
            onCheckedChange = null,
            enabled = enabled,
        )
    }
}