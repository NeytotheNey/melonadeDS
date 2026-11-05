package me.magnum.melonds.ui.common.component.romlist

import android.net.Uri
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.indication
import androidx.compose.foundation.interaction.InteractionSource
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.EmojiEvents
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.outlined.VideogameAsset
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.ripple
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.InputMode
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalInputModeManager
import androidx.compose.ui.platform.LocalInspectionMode
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import coil.compose.AsyncImage
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.RomIconFiltering
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.RomLibraryMetadata
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.romlist.MosaicTileSize
import me.magnum.melonds.ui.romlist.RelativeAge
import me.magnum.melonds.ui.romlist.RomIcon
import me.magnum.melonds.ui.romlist.RomTileStyle
import me.magnum.melonds.ui.romlist.relativeAge
import me.magnum.melonds.ui.romlist.romFileStem
import me.magnum.melonds.ui.romlist.romTileStyle
import me.magnum.melonds.ui.romlist.ui.preview.PreviewRomLibrary
import me.magnum.melonds.ui.theme.MelonTheme

/** Portrait DS cases are a little taller than wide. */
private const val BoxArtAspectRatio = 0.9f
private val TileCornerRadius = 12.dp
private val FeaturedTileCornerRadius = 16.dp
private val TileFocusBorderWidth = 3.dp
private val TileElevation = 2.dp
private const val TilePressedScale = 0.98f
private const val TilePressAnimationMs = 120
private val EmptyIcon by lazy { ImageBitmap(1, 1) }
private val ScrimTextShadow = Shadow(Color.Black.copy(alpha = 0.6f), Offset(0f, 2f), 4f)

/** The label on a featured mosaic tile. Only the first one offers to continue. */
enum class MosaicEyebrow {
    CONTINUE,
    RECENT,
}

/** Whether tiles shrink a little under the finger. Off when the system's animations are off. */
val LocalTilePressFeedback = staticCompositionLocalOf { true }

val LocalLibraryFocus = staticCompositionLocalOf<LibraryFocus?> { null }

/**
 * Where the library screen's focus goes when it has to go somewhere on purpose.
 */
@Stable
class LibraryFocus {
    /** The ROM whose tile takes the focus back, for example now that the sheet it opened has closed. */
    var pendingRomUri by mutableStateOf<Uri?>(null)

    /** On the sort chip, the way back up to the controls from the first row of tiles. */
    val controlsFocusRequester = FocusRequester()
}

/**
 * The requester to put on a ROM's tile, which also asks for the focus when [LocalLibraryFocus] names this ROM. A tile that's not composed has nothing
 * to give the focus to, and this way it isn't asked to.
 */
@Composable
internal fun rememberReturningFocusRequester(rom: Rom): FocusRequester {
    val focusRequester = remember { FocusRequester() }
    val libraryFocus = LocalLibraryFocus.current
    if (libraryFocus?.pendingRomUri == rom.uri) {
        LaunchedEffect(Unit) {
            if (focusRequester.requestFocus()) {
                libraryFocus.pendingRomUri = null
            }
        }
    }
    return focusRequester
}

/**
 * A grid card: box art (or the banner icon on a plain card when there is none), the title and publisher and year chips.
 * Tapping opens the metadata sheet, a long press opens the ROM's settings. A controller gets the same through [combinedClickable]: the confirm button opens
 * the sheet, and holding it down opens the settings.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun RomGridTile(
    modifier: Modifier,
    rom: Rom,
    onClick: () -> Unit,
    onLongClick: () -> Unit,
    retrieveTitleIcon: suspend () -> RomIcon,
    metadata: RomLibraryMetadata,
) {
    val romIcon by rememberRomIcon(rom, retrieveTitleIcon)
    val tileImage = rememberTileImage(metadata, metadata.tileImage)
    val style = romTileStyle(rom.config.customName, rom.name, rom.fileName, metadata, hasArt = tileImage.image != null)
    val interactionSource = remember { MutableInteractionSource() }
    var isFocused by remember { mutableStateOf(false) }
    val artShape = RoundedCornerShape(TileCornerRadius)

    Column(
        modifier = modifier
            .focusRequester(rememberReturningFocusRequester(rom))
            .onFocusChanged { isFocused = it.isFocused }
            .combinedClickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = onClick,
                onLongClick = onLongClick,
                onLongClickLabel = stringResource(R.string.rom_tile_settings),
                role = Role.Button,
            ),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(BoxArtAspectRatio)
                .tilePressScale(interactionSource)
                .shadow(TileElevation, artShape)
                .clip(artShape)
                .indication(interactionSource, ripple())
                .then(if (showsFocusRing(isFocused)) Modifier.border(TileFocusBorderWidth, MaterialTheme.colorScheme.primary, artShape) else Modifier),
        ) {
            RomTileArt(
                modifier = Modifier.fillMaxSize(),
                romIcon = romIcon,
                image = tileImage.image,
                onImageFailed = tileImage.onFailed,
                fallbackIconSize = 48.dp,
            )
            if (tileImage.image != null) {
                BannerIconBadge(
                    modifier = Modifier.align(Alignment.BottomStart).padding(6.dp),
                    romIcon = romIcon,
                )
            }
            if (metadata.hasAchievements) {
                AchievementsBadge(Modifier.align(Alignment.TopEnd).padding(6.dp))
            }
        }
        Text(
            text = if (style == RomTileStyle.NO_METADATA) romFileStem(rom.fileName) else metadata.displayTitle(rom),
            style = MaterialTheme.typography.bodySmall.copy(fontSize = 13.sp, lineHeight = 16.sp, fontWeight = FontWeight.Medium),
            maxLines = 2,
            minLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
        RomMetaChips(style, metadata)
    }
}

@Composable
private fun RomMetaChips(style: RomTileStyle, metadata: RomLibraryMetadata) {
    Row(
        modifier = Modifier.fillMaxWidth().height(16.dp),
        horizontalArrangement = Arrangement.spacedBy(4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (style == RomTileStyle.NO_METADATA) {
            Text(
                text = stringResource(R.string.rom_tile_no_metadata),
                style = RomMetaTextStyle,
                fontStyle = FontStyle.Italic,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
            )
        } else {
            metadata.publisher?.let {
                RomMetaChip(modifier = Modifier.weight(1f, fill = false), text = it)
            }
            metadata.releaseYear?.let {
                RomMetaChip(modifier = Modifier, text = it.toString())
            }
        }
    }
}

private val RomMetaTextStyle = TextStyle(fontSize = 10.sp, lineHeight = 16.sp)

@Composable
private fun RomMetaChip(modifier: Modifier, text: String) {
    Text(
        modifier = modifier
            .clip(RoundedCornerShape(4.dp))
            .background(MaterialTheme.colorScheme.surfaceContainerHigh)
            .padding(horizontal = 6.dp),
        text = text,
        style = RomMetaTextStyle,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
    )
}

/**
 * A mosaic cell, sized by whatever lays it out: the art fills the tile and the title sits on a scrim at the bottom. A featured tile also has a label, the
 * last-played time and a menu button, and the first one a pill that starts the game right away.
 *
 * The pill and the menu button sit inside the tile, where a D-pad only looks for what's beside the tile's edges, so it's pointed at them from the tile: down
 * goes in, up comes back out.
 *
 * @param upFocusRequester Where up goes from this tile, when not simply to whatever is above it
 * @param isLastInRow Whether nothing is beside this tile on the end side, so that the focus stops rather than go looking in other rows
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun RomMosaicTile(
    modifier: Modifier,
    rom: Rom,
    size: MosaicTileSize,
    eyebrow: MosaicEyebrow?,
    upFocusRequester: FocusRequester? = null,
    isLastInRow: Boolean = false,
    onClick: () -> Unit,
    onLongClick: () -> Unit,
    onContinueClick: () -> Unit,
    onSettingsClick: () -> Unit,
    retrieveTitleIcon: suspend () -> RomIcon,
    metadata: RomLibraryMetadata,
) {
    val isFeatured = size == MosaicTileSize.FEATURED
    val showContinue = eyebrow == MosaicEyebrow.CONTINUE
    val romIcon by rememberRomIcon(rom, retrieveTitleIcon)
    val tileImage = rememberTileImage(metadata, if (size == MosaicTileSize.WIDE) metadata.titleArtFile ?: metadata.tileImage else metadata.tileImage)
    val style = romTileStyle(rom.config.customName, rom.name, rom.fileName, metadata, hasArt = tileImage.image != null)
    val title = if (style == RomTileStyle.NO_METADATA) romFileStem(rom.fileName) else metadata.displayTitle(rom)
    val shape = RoundedCornerShape(if (isFeatured) FeaturedTileCornerRadius else TileCornerRadius)
    val interactionSource = remember { MutableInteractionSource() }
    val tileFocusRequester = rememberReturningFocusRequester(rom)
    val continueFocusRequester = remember { FocusRequester() }
    val settingsFocusRequester = remember { FocusRequester() }

    Box(
        modifier = modifier
            .tilePressScale(interactionSource)
            .clip(shape)
            .background(MaterialTheme.colorScheme.surfaceContainerHighest)
            .focusRequester(tileFocusRequester)
            .focusProperties {
                if (isFeatured) {
                    down = if (showContinue) continueFocusRequester else settingsFocusRequester
                }
                upFocusRequester?.let { up = it }
                if (isLastInRow) {
                    end = FocusRequester.Cancel
                }
            }
            .libraryFocusBorder(shape)
            .combinedClickable(
                interactionSource = interactionSource,
                indication = ripple(),
                onClick = onClick,
                onLongClick = onLongClick,
                onLongClickLabel = stringResource(R.string.rom_tile_settings),
                role = Role.Button,
            ),
    ) {
        val hasArt = tileImage.image != null
        // Art needs white text on a dark scrim. Without art there's nothing to read it against: a dark scrim on a tinted card looks dirty and white text on a
        // light theme's card can't be read, so the text goes in the theme's own colors
        val secondaryTextColor = if (hasArt) Color.White.copy(alpha = 0.85f) else MaterialTheme.colorScheme.onSurfaceVariant
        val focusRingColor = if (hasArt) Color.White else MaterialTheme.colorScheme.onSurface
        RomTileArt(
            modifier = Modifier.fillMaxSize(),
            romIcon = romIcon,
            image = tileImage.image,
            onImageFailed = tileImage.onFailed,
            fallbackIconSize = if (isFeatured) 48.dp else 36.dp,
            fallbackText = title.takeUnless { hasArt || isFeatured },
            fallbackColor = MaterialTheme.colorScheme.surfaceContainerHighest,
            // Keeps the icon of a featured tile between its label and its text
            fallbackPadding = if (isFeatured) PaddingValues(start = 8.dp, top = 32.dp, end = 8.dp, bottom = 88.dp) else PaddingValues(8.dp),
        )
        when {
            isFeatured -> {
                val lastPlayedAge = rom.lastPlayed?.let { relativeAge(it.time, System.currentTimeMillis()) }
                MosaicTileScrim(
                    padding = PaddingValues(start = 12.dp, top = 40.dp, end = if (showContinue) 12.dp else 44.dp, bottom = 12.dp),
                    spacing = 6.dp,
                    hasScrim = hasArt,
                ) {
                    Text(
                        text = title,
                        style = (if (hasArt) ScrimTextStyle else TextStyle(color = MaterialTheme.colorScheme.onSurface))
                            .copy(fontSize = 15.sp, lineHeight = 18.sp, fontWeight = FontWeight.Medium),
                        maxLines = 3,
                        overflow = TextOverflow.Ellipsis,
                    )
                    lastPlayedAge?.let {
                        Text(
                            text = relativeAgeText(it),
                            style = (if (hasArt) ScrimTextStyle else TextStyle()).copy(fontSize = 11.sp, lineHeight = 14.sp),
                            color = secondaryTextColor,
                            maxLines = 1,
                        )
                    }
                    if (showContinue) {
                        ContinuePill(
                            onClick = onContinueClick,
                            focusRequester = continueFocusRequester,
                            tileFocusRequester = tileFocusRequester,
                            focusRingColor = focusRingColor,
                        )
                    }
                }
                eyebrow?.let {
                    MosaicEyebrowLabel(Modifier.align(Alignment.TopStart).padding(10.dp), it, hasArt)
                }
                IconButton(
                    modifier = Modifier
                        .align(Alignment.BottomEnd)
                        .padding(end = 6.dp, bottom = 8.dp)
                        .size(36.dp)
                        .focusRequester(settingsFocusRequester)
                        .focusProperties {
                            up = tileFocusRequester
                            if (!showContinue) {
                                left = tileFocusRequester
                            }
                        }
                        .libraryFocusBorder(CircleShape, color = focusRingColor, includeChildren = true),
                    onClick = onSettingsClick,
                    colors = IconButtonDefaults.iconButtonColors(contentColor = secondaryTextColor),
                ) {
                    Icon(
                        modifier = Modifier.size(18.dp),
                        imageVector = Icons.Default.MoreVert,
                        contentDescription = stringResource(R.string.rom_settings),
                    )
                }
            }
            hasArt -> {
                MosaicTileScrim(
                    padding = PaddingValues(start = 8.dp, top = 28.dp, end = 8.dp, bottom = 7.dp),
                    spacing = 0.dp,
                ) {
                    Text(
                        text = title,
                        style = ScrimTextStyle.copy(fontSize = 11.sp, lineHeight = 13.sp, fontWeight = FontWeight.Medium),
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
        if (metadata.hasAchievements) {
            AchievementsBadge(Modifier.align(Alignment.TopEnd).padding(6.dp))
        }
    }
}

private val ScrimTextStyle = TextStyle(color = Color.White, shadow = ScrimTextShadow)

@Composable
private fun BoxScope.MosaicTileScrim(padding: PaddingValues, spacing: Dp, hasScrim: Boolean = true, content: @Composable ColumnScope.() -> Unit) {
    Column(
        modifier = Modifier
            .align(Alignment.BottomStart)
            .fillMaxWidth()
            .then(if (hasScrim) Modifier.background(Brush.verticalGradient(listOf(Color.Transparent, Color.Black.copy(alpha = 0.72f)))) else Modifier)
            .padding(padding),
        verticalArrangement = Arrangement.spacedBy(spacing),
        content = content,
    )
}

@Composable
private fun MosaicEyebrowLabel(modifier: Modifier, eyebrow: MosaicEyebrow, hasArt: Boolean) {
    val text = when (eyebrow) {
        MosaicEyebrow.CONTINUE -> R.string.rom_tile_continue
        MosaicEyebrow.RECENT -> R.string.rom_tile_recent
    }
    Text(
        modifier = modifier
            .clip(RoundedCornerShape(4.dp))
            .background(if (hasArt) Color.Black.copy(alpha = 0.6f) else MaterialTheme.colorScheme.secondaryContainer)
            .padding(horizontal = 7.dp, vertical = 2.dp),
        text = stringResource(text).uppercase(),
        color = if (hasArt) Color.White else MaterialTheme.colorScheme.onSecondaryContainer,
        fontSize = 10.sp,
        fontWeight = FontWeight.Bold,
        letterSpacing = 0.8.sp,
        lineHeight = 14.sp,
        maxLines = 1,
    )
}

@Composable
private fun ContinuePill(onClick: () -> Unit, focusRequester: FocusRequester, tileFocusRequester: FocusRequester, focusRingColor: Color) {
    val shape = CircleShape
    Row(
        modifier = Modifier
            .height(30.dp)
            .clip(shape)
            .background(MaterialTheme.colorScheme.primaryContainer)
            .focusRequester(focusRequester)
            .focusProperties { up = tileFocusRequester }
            .libraryFocusBorder(shape, color = focusRingColor)
            .clickable(role = Role.Button, onClick = onClick)
            .padding(start = 10.dp, end = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Icon(
            modifier = Modifier.size(12.dp),
            imageVector = Icons.Default.PlayArrow,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onPrimaryContainer,
        )
        Text(
            text = stringResource(R.string.rom_tile_continue),
            color = MaterialTheme.colorScheme.onPrimaryContainer,
            fontSize = 12.sp,
            lineHeight = 16.sp,
            fontWeight = FontWeight.SemiBold,
            maxLines = 1,
        )
    }
}

/**
 * The art on the metadata sheet. It's the same picture as on the tile, or the banner icon, without anything drawn over it.
 */
@Composable
fun RomArtThumbnail(
    modifier: Modifier,
    rom: Rom,
    retrieveTitleIcon: suspend () -> RomIcon,
    metadata: RomLibraryMetadata,
) {
    val romIcon by rememberRomIcon(rom, retrieveTitleIcon)
    val tileImage = rememberTileImage(metadata, metadata.tileImage)

    RomTileArt(
        modifier = modifier
            .aspectRatio(BoxArtAspectRatio)
            .clip(RoundedCornerShape(TileCornerRadius)),
        romIcon = romIcon,
        image = tileImage.image,
        onImageFailed = tileImage.onFailed,
        fallbackIconSize = 40.dp,
        fallbackColor = MaterialTheme.colorScheme.surfaceContainerHighest,
    )
}

@Composable
fun relativeAgeText(age: RelativeAge): String {
    return when (age) {
        RelativeAge.JustNow -> stringResource(R.string.relative_just_now)
        is RelativeAge.Hours -> stringResource(R.string.relative_hours_ago, age.hours)
        RelativeAge.Yesterday -> stringResource(R.string.relative_yesterday)
        is RelativeAge.Days -> pluralStringResource(R.plurals.relative_days_ago, age.days, age.days)
        RelativeAge.LastWeek -> stringResource(R.string.relative_last_week)
        is RelativeAge.Weeks -> pluralStringResource(R.plurals.relative_weeks_ago, age.weeks, age.weeks)
        RelativeAge.LastMonth -> stringResource(R.string.relative_last_month)
        is RelativeAge.Months -> pluralStringResource(R.plurals.relative_months_ago, age.months, age.months)
        RelativeAge.LastYear -> stringResource(R.string.relative_last_year)
        is RelativeAge.Years -> pluralStringResource(R.plurals.relative_years_ago, age.years, age.years)
    }
}

private class TileImage(val image: Any?, val onFailed: () -> Unit)

/**
 * The [image] to try for a tile, which becomes null once it has failed to load, so the tile goes back to the banner icon. A new image gets its own try,
 * and so does art downloaded again to the same path.
 */
@Composable
private fun rememberTileImage(metadata: RomLibraryMetadata, image: Any?): TileImage {
    var failedImage by remember(metadata.artModifiedAt) { mutableStateOf<Any?>(null) }
    return TileImage(image.takeIf { it != failedImage }) { failedImage = image }
}

/**
 * Marks what holds the controller's focus. The ripple's focus tint alone gets lost on top of the art. This must come before the clickable modifier, whose
 * focus it reports.
 *
 * @param includeChildren Also mark it while something inside has the focus, for a component that keeps its own clickable inside
 */
@Composable
internal fun Modifier.libraryFocusBorder(
    shape: Shape,
    color: Color = MaterialTheme.colorScheme.primary,
    includeChildren: Boolean = false,
): Modifier {
    var isFocused by remember { mutableStateOf(false) }
    val borderModifier = if (showsFocusRing(isFocused)) Modifier.border(TileFocusBorderWidth, color, shape) else Modifier
    return onFocusChanged { isFocused = if (includeChildren) it.hasFocus else it.isFocused }.then(borderModifier)
}

/**
 * Compose keeps a window's focus where it was when the window is covered or the finger takes over, as there's no D-pad to lose it to. That focus means nothing
 * to anyone looking at the screen then, like behind the scrim of a sheet that has the real focus.
 */
@Composable
private fun showsFocusRing(isFocused: Boolean): Boolean {
    return isFocused && LocalWindowInfo.current.isWindowFocused && LocalInputModeManager.current.inputMode == InputMode.Keyboard
}

/**
 * The small shrink under the finger. Only the layer changes as this runs, so nothing recomposes for it but the press itself.
 */
@Composable
private fun Modifier.tilePressScale(interactionSource: InteractionSource): Modifier {
    if (!LocalTilePressFeedback.current) {
        return this
    }
    val isPressed by interactionSource.collectIsPressedAsState()
    val scale by animateFloatAsState(if (isPressed) TilePressedScale else 1f, tween(TilePressAnimationMs), label = "tilePress")
    return graphicsLayer {
        scaleX = scale
        scaleY = scale
    }
}

/**
 * The picture on a tile: the downloaded art or RetroAchievements icon when metadata has one, otherwise a tinted card with the 32x32 banner icon
 * centered and drawn pixel-sharp, and [fallbackText] beneath it, which is also what every tile shows until metadata loads.
 */
@Composable
private fun RomTileArt(
    modifier: Modifier,
    romIcon: RomIcon?,
    image: Any?,
    onImageFailed: () -> Unit,
    fallbackIconSize: Dp,
    fallbackText: String? = null,
    fallbackColor: Color = MaterialTheme.colorScheme.surfaceContainerHigh,
    fallbackPadding: PaddingValues = PaddingValues(8.dp),
) {
    when {
        image != null && LocalInspectionMode.current -> {
            // Previews can't load files, so a game's art is a color of its own
            val hue = (image.hashCode() and 0xFF) / 255f * 360f
            Box(modifier.background(Brush.linearGradient(listOf(Color.hsl(hue, 0.6f, 0.45f), Color.hsl((hue + 35f) % 360f, 0.7f, 0.22f)))))
        }
        image != null -> {
            AsyncImage(
                modifier = modifier.background(fallbackColor),
                model = image,
                contentDescription = null,
                contentScale = ContentScale.Crop,
                onError = { onImageFailed() },
            )
        }
        else -> {
            Column(
                modifier = modifier.background(fallbackColor).padding(fallbackPadding),
                verticalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterVertically),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                BannerIconImage(
                    modifier = Modifier.size(fallbackIconSize),
                    romIcon = romIcon,
                )
                fallbackText?.let {
                    Text(
                        text = it,
                        fontSize = 10.sp,
                        lineHeight = 12.sp,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        textAlign = TextAlign.Center,
                        maxLines = 3,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
    }
}

@Composable
private fun BannerIconBadge(modifier: Modifier, romIcon: RomIcon?) {
    val shape = RoundedCornerShape(5.dp)
    Box(
        modifier = modifier
            .size(26.dp)
            .shadow(1.dp, shape)
            .clip(shape)
            .background(Color.Black.copy(alpha = 0.45f))
            .padding(1.dp),
    ) {
        BannerIconImage(
            modifier = Modifier.fillMaxSize(),
            romIcon = romIcon,
            placeholderTint = Color.White,
        )
    }
}

@Composable
private fun AchievementsBadge(modifier: Modifier) {
    Row(
        modifier = modifier
            .height(20.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(MaterialTheme.colorScheme.surface.copy(alpha = 0.85f))
            .padding(start = 4.dp, end = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            modifier = Modifier.size(12.dp),
            imageVector = Icons.Default.EmojiEvents,
            contentDescription = stringResource(R.string.rom_tile_has_achievements),
            tint = MaterialTheme.colorScheme.tertiary,
        )
        Box(Modifier.width(3.dp))
        Text(
            text = stringResource(R.string.rom_tile_ra),
            color = MaterialTheme.colorScheme.onSurface,
            fontSize = 10.sp,
            lineHeight = 12.sp,
            fontWeight = FontWeight.Bold,
            letterSpacing = 0.4.sp,
        )
    }
}

/**
 * The ROM's banner icon, or a generic one for a ROM that turned out not to have any. Nothing is drawn yet while it's still loading.
 */
@Composable
internal fun BannerIconImage(modifier: Modifier, romIcon: RomIcon?, placeholderTint: Color = MaterialTheme.colorScheme.onSurfaceVariant) {
    if (romIcon != null && romIcon.bitmap == null) {
        Icon(
            modifier = modifier,
            imageVector = Icons.Outlined.VideogameAsset,
            contentDescription = null,
            tint = placeholderTint,
        )
        return
    }

    Image(
        modifier = modifier,
        bitmap = romIcon?.bitmap?.asImageBitmap() ?: EmptyIcon,
        contentDescription = null,
        filterQuality = when (romIcon?.filtering) {
            RomIconFiltering.NONE -> FilterQuality.None
            RomIconFiltering.LINEAR, null -> DrawScope.DefaultFilterQuality
        },
    )
}

@Composable
internal fun rememberRomIcon(rom: Rom, retrieveTitleIcon: suspend () -> RomIcon): State<RomIcon?> {
    // Effects don't run in a preview
    val initialIcon = if (LocalInspectionMode.current) PreviewRomLibrary.icon else null
    return produceState(initialValue = initialIcon, key1 = rom.uri) {
        value = retrieveTitleIcon()
    }
}

@MelonPreviewSet
@Composable
private fun PreviewRomGridTile() {
    MelonTheme {
        Row(Modifier.padding(8.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            listOf(PreviewRomLibrary.roms[0], PreviewRomLibrary.roms[5], PreviewRomLibrary.roms[7]).forEach { rom ->
                RomGridTile(
                    modifier = Modifier.weight(1f),
                    rom = rom,
                    onClick = { },
                    onLongClick = { },
                    retrieveTitleIcon = { PreviewRomLibrary.icon },
                    metadata = PreviewRomLibrary.metadataFor(rom),
                )
            }
        }
    }
}

@MelonPreviewSet
@Composable
private fun PreviewRomMosaicTiles() {
    MelonTheme {
        Row(Modifier.padding(8.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            val rom = PreviewRomLibrary.roms.first()
            RomMosaicTile(
                modifier = Modifier.size(187.dp, 182.dp),
                rom = rom,
                size = MosaicTileSize.FEATURED,
                eyebrow = MosaicEyebrow.CONTINUE,
                onClick = { },
                onLongClick = { },
                onContinueClick = { },
                onSettingsClick = { },
                retrieveTitleIcon = { PreviewRomLibrary.icon },
                metadata = PreviewRomLibrary.metadataFor(rom),
            )
            RomMosaicTile(
                modifier = Modifier.size(90.dp, 88.dp),
                rom = PreviewRomLibrary.roms[7],
                size = MosaicTileSize.SINGLE,
                eyebrow = null,
                onClick = { },
                onLongClick = { },
                onContinueClick = { },
                onSettingsClick = { },
                retrieveTitleIcon = { PreviewRomLibrary.icon },
                metadata = PreviewRomLibrary.metadataFor(PreviewRomLibrary.roms[7]),
            )
        }
    }
}
