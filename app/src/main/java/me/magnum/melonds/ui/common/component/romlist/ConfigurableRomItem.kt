package me.magnum.melonds.ui.common.component.romlist

import android.net.Uri
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.config.RomConfig
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.common.component.text.CaptionText
import me.magnum.melonds.ui.romlist.RomIcon
import me.magnum.melonds.ui.romlist.ui.preview.PreviewRomLibrary
import me.magnum.melonds.ui.theme.MelonTheme

/**
 * A row of the library's list view. Tapping it opens the metadata sheet and a long press, or holding the confirm button, opens the ROM's settings, which
 * the menu button also does.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun ConfigurableRomItem(
    modifier: Modifier,
    rom: Rom,
    title: String = rom.config.customName ?: rom.name,
    showDivider: Boolean = true,
    onClick: () -> Unit,
    onConfigClick: () -> Unit,
    retrieveTitleIcon: suspend () -> RomIcon,
) {
    val mainFocusRequester = rememberReturningFocusRequester(rom)
    val romDetailsFocusRequester = remember { FocusRequester() }

    Column(modifier) {
        Row(
            modifier = Modifier.focusRequester(mainFocusRequester)
                .focusProperties {
                    end = romDetailsFocusRequester
                }
                .libraryFocusBorder(RectangleShape)
                .combinedClickable(
                    onClick = onClick,
                    onLongClick = onConfigClick,
                    onLongClickLabel = stringResource(R.string.rom_tile_settings),
                ),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            RomRowContent(
                modifier = Modifier.weight(1f),
                rom = rom,
                title = title,
                retrieveTitleIcon = retrieveTitleIcon,
            )

            IconButton(
                modifier = Modifier.padding(end = 8.dp)
                    .focusRequester(romDetailsFocusRequester)
                    .focusProperties {
                        start = mainFocusRequester
                    },
                onClick = onConfigClick,
            ) {
                Icon(
                    modifier = Modifier.size(20.dp),
                    imageVector = Icons.Default.MoreVert,
                    contentDescription = stringResource(R.string.rom_settings),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        if (showDivider) {
            HorizontalDivider(
                modifier = Modifier.padding(horizontal = 16.dp),
                thickness = 1.dp,
                color = MaterialTheme.colorScheme.outlineVariant,
            )
        }
    }
}

@Composable
private fun RomRowContent(
    modifier: Modifier,
    rom: Rom,
    title: String,
    retrieveTitleIcon: suspend () -> RomIcon,
) {
    val romIcon by rememberRomIcon(rom, retrieveTitleIcon)

    Row(
        modifier = modifier.padding(start = 20.dp, top = 10.dp, end = 8.dp, bottom = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Box(
            modifier = Modifier
                .size(48.dp)
                .clip(RoundedCornerShape(6.dp))
                .background(MaterialTheme.colorScheme.surfaceContainerHigh),
        ) {
            BannerIconImage(
                modifier = Modifier.fillMaxSize(),
                romIcon = romIcon,
            )
        }
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                if (rom.isDsiWareTitle) {
                    // Base logo height on text size to allow it to scale with font scaling
                    val logoHeight = with(LocalDensity.current) { 12.sp.toDp() }
                    Image(
                        modifier = Modifier.height(logoHeight),
                        contentScale = ContentScale.FillHeight,
                        painter = painterResource(id = R.drawable.logo_dsiware),
                        contentDescription = null,
                    )
                }
                Text(
                    text = title,
                    style = TextStyle(fontSize = 18.sp, lineHeight = 22.sp),
                    overflow = TextOverflow.Ellipsis,
                    maxLines = 1,
                )
            }
            CaptionText(
                text = rom.fileName,
                style = TextStyle(fontSize = 12.sp, lineHeight = 16.sp),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@MelonPreviewSet
@Composable
private fun PreviewConfigurableRomItem() {
    MelonTheme {
        ConfigurableRomItem(
            modifier = Modifier.fillMaxWidth(),
            rom = Rom("Highway 4: Mediocre Racing", "Nontendo", "Highway_4.nds", Uri.EMPTY, Uri.EMPTY, RomConfig(), null, false, ""),
            onClick = {},
            onConfigClick = {},
            retrieveTitleIcon = { PreviewRomLibrary.icon },
        )
    }
}
