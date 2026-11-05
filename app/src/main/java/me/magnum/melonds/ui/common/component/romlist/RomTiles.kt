package me.magnum.melonds.ui.common.component.romlist

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.EmojiEvents
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalInspectionMode
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import coil.compose.AsyncImage
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.RomIconFiltering
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.RomLibraryMetadata
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.common.component.text.CaptionText
import me.magnum.melonds.ui.romlist.RomIcon
import me.magnum.melonds.ui.romlist.ui.preview.PreviewRomLibrary
import me.magnum.melonds.ui.theme.MelonTheme

/** Portrait DS cases are a little taller than wide. */
private const val BoxArtAspectRatio = 0.9f
private const val FeaturedTileAspectRatio = 2f
private val TileCornerRadius = 12.dp

/**
 * A grid card: box art (or the banner icon on a plain card when there is none), the title and a publisher/year caption.
 * Tapping plays the ROM, a long press opens its settings.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun RomGridTile(
    modifier: Modifier,
    rom: Rom,
    onClick: () -> Unit,
    onLongClick: () -> Unit,
    retrieveTitleIcon: suspend () -> RomIcon,
    retrieveMetadata: suspend () -> RomLibraryMetadata,
) {
    val romIcon by rememberRomIcon(rom, retrieveTitleIcon)
    val metadata by rememberRomMetadata(rom, retrieveMetadata)

    Column(
        modifier = modifier
            .clip(RoundedCornerShape(TileCornerRadius))
            .combinedClickable(
                onClick = onClick,
                onLongClick = onLongClick,
                onLongClickLabel = stringResource(R.string.rom_tile_settings),
            )
            .padding(4.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(BoxArtAspectRatio)
                .clip(RoundedCornerShape(TileCornerRadius)),
        ) {
            RomTileArt(
                modifier = Modifier.fillMaxSize(),
                romIcon = romIcon,
                metadata = metadata,
                fallbackIconSize = 48.dp,
            )
            if (metadata?.tileImageUrl != null) {
                BannerIconBadge(
                    modifier = Modifier.align(Alignment.BottomStart).padding(6.dp),
                    romIcon = romIcon,
                )
            }
            if (metadata?.hasAchievements == true) {
                AchievementsBadge(Modifier.align(Alignment.TopEnd).padding(6.dp))
            }
        }
        Column(modifier = Modifier.padding(horizontal = 2.dp)) {
            Text(
                text = rom.config.customName ?: rom.name,
                style = MaterialTheme.typography.titleSmall,
                maxLines = 2,
                minLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
            CaptionText(
                text = metadata?.caption() ?: rom.fileName,
                style = MaterialTheme.typography.bodySmall,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

/**
 * A mosaic cell: the art fills the tile and the title sits on a scrim at the bottom. A featured tile is twice as wide and carries a
 * "Continue" hint, since featured tiles are the most recently played games.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun RomMosaicTile(
    modifier: Modifier,
    rom: Rom,
    isFeatured: Boolean,
    onClick: () -> Unit,
    onLongClick: () -> Unit,
    retrieveTitleIcon: suspend () -> RomIcon,
    retrieveMetadata: suspend () -> RomLibraryMetadata,
) {
    val romIcon by rememberRomIcon(rom, retrieveTitleIcon)
    val metadata by rememberRomMetadata(rom, retrieveMetadata)

    Box(
        modifier = modifier
            .aspectRatio(if (isFeatured) FeaturedTileAspectRatio else 1f)
            .clip(RoundedCornerShape(TileCornerRadius))
            .combinedClickable(
                onClick = onClick,
                onLongClick = onLongClick,
                onLongClickLabel = stringResource(R.string.rom_tile_settings),
            ),
    ) {
        RomTileArt(
            modifier = Modifier.fillMaxSize(),
            romIcon = romIcon,
            metadata = metadata,
            fallbackIconSize = if (isFeatured) 64.dp else 48.dp,
        )
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(
                    Brush.verticalGradient(
                        0.4f to Color.Transparent,
                        1f to Color.Black.copy(alpha = 0.75f),
                    )
                ),
        )
        Column(
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(10.dp),
        ) {
            if (isFeatured && rom.lastPlayed != null) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(2.dp),
                ) {
                    Icon(
                        modifier = Modifier.size(14.dp),
                        imageVector = Icons.Default.PlayArrow,
                        contentDescription = null,
                        tint = Color.White,
                    )
                    Text(
                        text = stringResource(R.string.rom_tile_continue),
                        style = MaterialTheme.typography.labelSmall,
                        color = Color.White,
                    )
                }
            }
            Text(
                text = rom.config.customName ?: rom.name,
                style = if (isFeatured) MaterialTheme.typography.titleMedium else MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.SemiBold,
                color = Color.White,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
        }
        if (metadata?.hasAchievements == true) {
            AchievementsBadge(Modifier.align(Alignment.TopEnd).padding(6.dp))
        }
    }
}

/**
 * The picture on a tile: the box art or RetroAchievements icon when metadata has one, otherwise a tinted card with the 32x32 banner icon
 * centred and drawn pixel-sharp, which is also what every tile shows until metadata loads.
 */
@Composable
private fun RomTileArt(
    modifier: Modifier,
    romIcon: RomIcon?,
    metadata: RomLibraryMetadata?,
    fallbackIconSize: Dp,
) {
    val imageUrl = metadata?.tileImageUrl
    if (imageUrl != null && !LocalInspectionMode.current) {
        AsyncImage(
            modifier = modifier.background(MaterialTheme.colorScheme.surfaceContainerHigh),
            model = imageUrl,
            contentDescription = null,
            contentScale = ContentScale.Crop,
        )
    } else {
        Box(
            modifier = modifier.background(MaterialTheme.colorScheme.surfaceContainerHigh),
            contentAlignment = Alignment.Center,
        ) {
            BannerIconImage(
                modifier = Modifier.size(fallbackIconSize),
                romIcon = romIcon,
            )
        }
    }
}

@Composable
private fun BannerIconBadge(modifier: Modifier, romIcon: RomIcon?) {
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(6.dp))
            .background(MaterialTheme.colorScheme.surface.copy(alpha = 0.85f))
            .padding(3.dp),
    ) {
        BannerIconImage(
            modifier = Modifier.size(24.dp),
            romIcon = romIcon,
        )
    }
}

@Composable
private fun AchievementsBadge(modifier: Modifier) {
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(6.dp))
            .background(MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.9f))
            .padding(3.dp),
    ) {
        Icon(
            modifier = Modifier.size(14.dp),
            imageVector = Icons.Default.EmojiEvents,
            contentDescription = stringResource(R.string.rom_tile_has_achievements),
            tint = MaterialTheme.colorScheme.onPrimaryContainer,
        )
    }
}

@Composable
private fun BannerIconImage(modifier: Modifier, romIcon: RomIcon?) {
    Image(
        modifier = modifier,
        bitmap = romIcon?.bitmap?.asImageBitmap() ?: ImageBitmap(1, 1),
        contentDescription = null,
        filterQuality = when (romIcon?.filtering) {
            RomIconFiltering.NONE -> FilterQuality.None
            RomIconFiltering.LINEAR, null -> DrawScope.DefaultFilterQuality
        },
    )
}

@Composable
private fun rememberRomIcon(rom: Rom, retrieveTitleIcon: suspend () -> RomIcon): State<RomIcon?> {
    return produceState<RomIcon?>(initialValue = null, key1 = rom.uri) {
        value = retrieveTitleIcon()
    }
}

@Composable
private fun rememberRomMetadata(rom: Rom, retrieveMetadata: suspend () -> RomLibraryMetadata): State<RomLibraryMetadata?> {
    return produceState<RomLibraryMetadata?>(initialValue = null, key1 = rom.uri) {
        value = retrieveMetadata()
    }
}

@MelonPreviewSet
@Composable
private fun PreviewRomGridTile() {
    MelonTheme {
        Row(Modifier.padding(8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            PreviewRomLibrary.roms.take(3).forEach { rom ->
                RomGridTile(
                    modifier = Modifier.weight(1f),
                    rom = rom,
                    onClick = { },
                    onLongClick = { },
                    retrieveTitleIcon = { PreviewRomLibrary.icon },
                    retrieveMetadata = { PreviewRomLibrary.metadataFor(rom) },
                )
            }
        }
    }
}

@Preview(showBackground = true)
@Composable
private fun PreviewRomMosaicTiles() {
    MelonTheme {
        Row(Modifier.padding(8.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            RomMosaicTile(
                modifier = Modifier.weight(2f),
                rom = PreviewRomLibrary.roms.first(),
                isFeatured = true,
                onClick = { },
                onLongClick = { },
                retrieveTitleIcon = { PreviewRomLibrary.icon },
                retrieveMetadata = { PreviewRomLibrary.metadataFor(PreviewRomLibrary.roms.first()) },
            )
            RomMosaicTile(
                modifier = Modifier.weight(1f),
                rom = PreviewRomLibrary.roms[1],
                isFeatured = false,
                onClick = { },
                onLongClick = { },
                retrieveTitleIcon = { PreviewRomLibrary.icon },
                retrieveMetadata = { PreviewRomLibrary.metadataFor(PreviewRomLibrary.roms[1]) },
            )
        }
    }
}
