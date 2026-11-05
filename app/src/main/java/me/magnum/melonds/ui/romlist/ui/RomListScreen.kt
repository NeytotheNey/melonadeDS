package me.magnum.melonds.ui.romlist.ui

import android.net.Uri
import android.provider.Settings
import android.view.KeyEvent
import androidx.activity.compose.BackHandler
import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.focusGroup
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.calculateEndPadding
import androidx.compose.foundation.layout.calculateStartPadding
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Sort
import androidx.compose.material.icons.filled.AccessTime
import androidx.compose.material.icons.filled.ArrowDownward
import androidx.compose.material.icons.filled.ArrowUpward
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.SortByAlpha
import androidx.compose.material.icons.outlined.FilterAlt
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusDirection
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.InputMode
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalInputModeManager
import androidx.compose.ui.platform.LocalInspectionMode
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LifecycleEventEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.StateFlow
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.ConsoleType
import me.magnum.melonds.domain.model.RomListViewMode
import me.magnum.melonds.domain.model.SortingMode
import me.magnum.melonds.domain.model.SortingOrder
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.RomMetadataSnapshot
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.common.component.romlist.ConfigurableRomItem
import me.magnum.melonds.ui.common.component.romlist.LibraryFocus
import me.magnum.melonds.ui.common.component.romlist.LocalLibraryFocus
import me.magnum.melonds.ui.common.component.romlist.LocalTilePressFeedback
import me.magnum.melonds.ui.common.component.romlist.libraryFocusBorder
import me.magnum.melonds.ui.romlist.LibraryEmptyReason
import me.magnum.melonds.ui.romlist.RomIcon
import me.magnum.melonds.ui.romlist.RomLibraryFilter
import me.magnum.melonds.ui.romlist.filterLibrary
import me.magnum.melonds.ui.romlist.libraryEmptyReason
import me.magnum.melonds.ui.romlist.splitAppName
import me.magnum.melonds.ui.romlist.ui.preview.PreviewRomLibrary
import me.magnum.melonds.ui.theme.MelonTheme

private val AppBarHeight = 56.dp
private val ChipShape = RoundedCornerShape(8.dp)
/** From here on the view switch and the chips share a row, which leaves more of a short landscape screen for the games. */
private val SingleRowControlsMinWidth = 600.dp
private const val FocusReturnTimeoutMs = 500L

@Composable
fun RomListScreen(
    roms: List<Rom>?,
    isRefreshing: Boolean,
    hasSearchDirectories: Boolean,
    onSearchQueryChange: (String?) -> Unit,
    sortingMode: SortingMode,
    sortingOrder: SortingOrder,
    onSortChange: (SortingMode) -> Unit,
    viewMode: RomListViewMode,
    onViewModeChange: (RomListViewMode) -> Unit,
    onFirmwareBoot: (ConsoleType) -> Unit,
    onRomSelected: (Rom) -> Unit,
    onRomConfigClick: (Rom) -> Unit,
    onRefresh: () -> Unit,
    onDirectorySelected: (Uri) -> Unit,
    onNavigateToSettings: () -> Unit,
    onNavigateToDsiWareManager: () -> Unit,
    retrieveRomIcon: suspend (Rom) -> RomIcon,
    romMetadata: StateFlow<RomMetadataSnapshot>,
    hasRetroAchievementsWebApiKey: () -> Boolean,
) {
    var isSearchActive by rememberSaveable { mutableStateOf(false) }
    var searchQuery by rememberSaveable { mutableStateOf("") }
    var showOverflowMenu by remember { mutableStateOf(false) }
    var filter by rememberSaveable { mutableStateOf(RomLibraryFilter.ALL) }
    var sheetRomUri by rememberSaveable { mutableStateOf<String?>(null) }
    // The ROM to go back to when this screen comes back from what its sheet started
    var resumeFocusRomUri by rememberSaveable { mutableStateOf<Uri?>(null) }

    // Stops with the activity, so this costs nothing during a game
    val metadataSnapshot by romMetadata.collectAsStateWithLifecycle()
    val visibleRoms = remember(roms, filter, if (filter == RomLibraryFilter.HAS_ACHIEVEMENTS) metadataSnapshot else null) {
        roms?.let {
            filterLibrary(
                items = it,
                filter = filter,
                isPlayed = { rom -> rom.lastPlayed != null },
                hasAchievements = { rom -> metadataSnapshot.metadataFor(rom).hasAchievements },
            )
        }
    }
    val emptyMessage = stringResource(libraryEmptyReason(!roms.isNullOrEmpty(), filter, searchQuery.isNotBlank()).message())
    val onShowAll = if (filter != RomLibraryFilter.ALL && !roms.isNullOrEmpty()) ({ filter = RomLibraryFilter.ALL }) else null
    val openSheet = { rom: Rom -> sheetRomUri = rom.uri.toString() }
    val playRom = { rom: Rom ->
        resumeFocusRomUri = rom.uri
        onRomSelected(rom)
    }
    val closeSearch = {
        isSearchActive = false
        searchQuery = ""
        onSearchQueryChange(null)
    }

    val libraryFocus = remember { LibraryFocus() }
    val inputModeManager = LocalInputModeManager.current
    val returnFocusTo = { romUri: Uri ->
        if (inputModeManager.inputMode == InputMode.Keyboard) {
            libraryFocus.pendingRomUri = romUri
        }
    }
    libraryFocus.pendingRomUri?.let { pendingRomUri ->
        LaunchedEffect(pendingRomUri) {
            // The tile asks for the focus and clears this as soon as it's there. One that isn't coming, say because the filter hides it now, mustn't take the
            // focus whenever it next scrolls by, and the focus shouldn't be left nowhere
            delay(FocusReturnTimeoutMs)
            libraryFocus.pendingRomUri = null
            libraryFocus.controlsFocusRequester.requestFocus()
        }
    }
    // Playing a game moves its tile when sorting by recently played, away from where the focus was
    LifecycleEventEffect(Lifecycle.Event.ON_RESUME) {
        resumeFocusRomUri?.let { returnFocusTo(it) }
        resumeFocusRomUri = null
    }

    if (roms != null && sheetRomUri != null && roms.none { it.uri.toString() == sheetRomUri }) {
        LaunchedEffect(Unit) {
            sheetRomUri = null
        }
    }

    BackHandler(isSearchActive) {
        closeSearch()
    }

    CompositionLocalProvider(
        LocalLibraryFocus provides libraryFocus,
        LocalTilePressFeedback provides rememberAnimationsEnabled(),
    ) {
        Scaffold(
            modifier = Modifier.onPreviewKeyEvent {
                if (it.nativeKeyEvent.action == KeyEvent.ACTION_DOWN) {
                    when (it.key) {
                        Key.Menu -> {
                            showOverflowMenu = !showOverflowMenu
                            true
                        }
                        else -> false
                    }
                } else {
                    false
                }
            },
            topBar = {
                Column(Modifier.background(MaterialTheme.colorScheme.surface)) {
                    if (isSearchActive) {
                        SearchTopBar(
                            query = searchQuery,
                            onQueryChange = { query ->
                                searchQuery = query
                                onSearchQueryChange(query)
                            },
                            onClose = closeSearch,
                        )
                    } else {
                        RomListTopBar(
                            showOverflowMenu = showOverflowMenu,
                            onOverflowMenuChange = { showOverflowMenu = it },
                            onSearchClick = { isSearchActive = true },
                            onFirmwareBoot = onFirmwareBoot,
                            onNavigateToDsiWareManager = onNavigateToDsiWareManager,
                            onRefresh = onRefresh,
                            onNavigateToSettings = onNavigateToSettings,
                        )
                    }
                    if (hasSearchDirectories) {
                        LibraryControls(
                            viewMode = viewMode,
                            onViewModeChange = onViewModeChange,
                            sortingMode = sortingMode,
                            sortingOrder = sortingOrder,
                            onSortChange = onSortChange,
                            filter = filter,
                            onFilterChange = { filter = it },
                        )
                    }
                }
            },
            containerColor = MaterialTheme.colorScheme.surface,
            contentWindowInsets = WindowInsets.safeDrawing,
        ) { padding ->
            if (hasSearchDirectories) {
                val layoutDirection = LocalLayoutDirection.current
                val contentPadding = PaddingValues(
                    start = padding.calculateStartPadding(layoutDirection),
                    end = padding.calculateEndPadding(layoutDirection),
                    bottom = padding.calculateBottomPadding(),
                )
                // The D-pad goes by bounds, so a list drawn behind the bars would count as being above the chips as well as below them, and down would never
                // reach it. Below the bars it's found from the chips, and this group decides what happens at its edges: up goes back to the chips, and
                // left and right stop, rather than go on to whatever else is on that side, like the search icon
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(top = padding.calculateTopPadding())
                        .focusProperties {
                            onExit = {
                                when (requestedFocusDirection) {
                                    FocusDirection.Left, FocusDirection.Right -> cancelFocusChange()
                                    FocusDirection.Up -> libraryFocus.controlsFocusRequester.requestFocus()
                                    else -> {}
                                }
                            }
                        }
                        .focusGroup(),
                ) {
                    when (viewMode) {
                        RomListViewMode.LIST -> {
                            val listState = rememberLazyListState()
                            // The tile that's to take the focus back has to be there first
                            LaunchedEffect(libraryFocus.pendingRomUri, visibleRoms) {
                                val index = visibleRoms?.indexOfFirst { it.uri == libraryFocus.pendingRomUri } ?: -1
                                if (index >= 0 && listState.layoutInfo.visibleItemsInfo.none { it.index == index }) {
                                    listState.scrollToItem(index)
                                }
                            }
                            RomList(
                                modifier = Modifier.fillMaxSize(),
                                roms = visibleRoms,
                                isRefreshing = isRefreshing,
                                onRefresh = onRefresh,
                                contentPadding = PaddingValues(
                                    start = padding.calculateStartPadding(layoutDirection),
                                    top = LibraryContentTopPadding,
                                    end = padding.calculateEndPadding(layoutDirection),
                                    bottom = padding.calculateBottomPadding() + LibraryContentBottomPadding,
                                ),
                                emptyMessage = emptyMessage,
                                onShowAll = onShowAll,
                                listState = listState,
                            ) { modifier, rom ->
                                ConfigurableRomItem(
                                    modifier = modifier.fillMaxWidth(),
                                    rom = rom,
                                    title = metadataSnapshot.metadataFor(rom).displayTitle(rom),
                                    showDivider = rom !== visibleRoms?.lastOrNull(),
                                    onClick = { openSheet(rom) },
                                    onConfigClick = { onRomConfigClick(rom) },
                                    retrieveTitleIcon = { retrieveRomIcon(rom) },
                                )
                            }
                        }
                        RomListViewMode.GRID -> RomGrid(
                            modifier = Modifier.fillMaxSize(),
                            roms = visibleRoms,
                            isRefreshing = isRefreshing,
                            onRefresh = onRefresh,
                            contentPadding = contentPadding,
                            emptyMessage = emptyMessage,
                            onShowAll = onShowAll,
                            onRomClick = openSheet,
                            onRomConfigClick = onRomConfigClick,
                            retrieveRomIcon = retrieveRomIcon,
                            romMetadata = romMetadata,
                        )
                        RomListViewMode.MOSAIC -> RomMosaic(
                            modifier = Modifier.fillMaxSize(),
                            roms = visibleRoms,
                            isRefreshing = isRefreshing,
                            onRefresh = onRefresh,
                            contentPadding = contentPadding,
                            emptyMessage = emptyMessage,
                            onShowAll = onShowAll,
                            sortingMode = sortingMode,
                            sortingOrder = sortingOrder,
                            onRomClick = openSheet,
                            onRomPlay = playRom,
                            onRomConfigClick = onRomConfigClick,
                            retrieveRomIcon = retrieveRomIcon,
                            romMetadata = romMetadata,
                        )
                    }
                }
            } else {
                NoSearchDirectoriesContent(
                    modifier = Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding),
                    onDirectorySelected = onDirectorySelected,
                )
            }
        }
    }

    sheetRomUri?.let { uri -> roms?.firstOrNull { it.uri.toString() == uri } }?.let { rom ->
        RomMetadataSheet(
            rom = rom,
            metadata = metadataSnapshot.metadataFor(rom),
            isHashUnmatched = metadataSnapshot.isUnmatched(rom),
            hasWebApiKey = remember(rom.uri) { hasRetroAchievementsWebApiKey() },
            onDismissRequest = {
                sheetRomUri = null
                returnFocusTo(rom.uri)
            },
            onPlay = {
                sheetRomUri = null
                playRom(rom)
            },
            onSettings = {
                sheetRomUri = null
                resumeFocusRomUri = rom.uri
                onRomConfigClick(rom)
            },
            retrieveRomIcon = retrieveRomIcon,
        )
    }
}

/**
 * Whether the system's animations are on. "Remove animations" sets the scale this reads to zero.
 */
@Composable
private fun rememberAnimationsEnabled(): Boolean {
    val context = LocalContext.current
    val isPreview = LocalInspectionMode.current
    return remember(context, isPreview) {
        isPreview || Settings.Global.getFloat(context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) != 0f
    }
}

@StringRes
private fun LibraryEmptyReason.message(): Int = when (this) {
    LibraryEmptyReason.NO_ROMS -> R.string.no_roms_found
    LibraryEmptyReason.NO_SEARCH_RESULTS -> R.string.rom_library_empty_search
    LibraryEmptyReason.NO_SEARCH_AND_FILTER_RESULTS -> R.string.rom_library_empty_search_and_filter
    LibraryEmptyReason.NOTHING_PLAYED -> R.string.rom_library_empty_played
    LibraryEmptyReason.EVERYTHING_PLAYED -> R.string.rom_library_empty_not_played
    LibraryEmptyReason.NO_ACHIEVEMENTS -> R.string.rom_library_empty_achievements
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun RomListTopBar(
    showOverflowMenu: Boolean,
    onOverflowMenuChange: (Boolean) -> Unit,
    onSearchClick: () -> Unit,
    onFirmwareBoot: (ConsoleType) -> Unit,
    onNavigateToDsiWareManager: () -> Unit,
    onRefresh: () -> Unit,
    onNavigateToSettings: () -> Unit,
) {
    val actionFocusRequesters = remember { List(2) { FocusRequester() } }
    var lastFocusedActionIndex by rememberSaveable { mutableIntStateOf(-1) }
    // Lets the action at [index] take the focus, and remembers it as the last focused one so the row can give the focus back to it
    fun Modifier.actionFocus(index: Int) = focusRequester(actionFocusRequesters[index]).onFocusChanged {
        if (it.isFocused) lastFocusedActionIndex = index
    }

    TopAppBar(
        title = { RomListTitle() },
        expandedHeight = AppBarHeight,
        colors = TopAppBarDefaults.topAppBarColors(
            containerColor = MaterialTheme.colorScheme.surface,
            scrolledContainerColor = MaterialTheme.colorScheme.surface,
            actionIconContentColor = MaterialTheme.colorScheme.onSurfaceVariant,
        ),
        windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Horizontal + WindowInsetsSides.Top),
        actions = {
            Row(
                modifier = Modifier.onFocusChanged { focusState ->
                    if (focusState.isFocused) {
                        if (lastFocusedActionIndex != -1) {
                            actionFocusRequesters[lastFocusedActionIndex].requestFocus()
                        } else {
                            actionFocusRequesters.first().requestFocus()
                        }
                    }
                }.focusable(),
            ) {
                IconButton(
                    modifier = Modifier.actionFocus(0),
                    onClick = onSearchClick,
                ) {
                    Icon(
                        imageVector = Icons.Default.Search,
                        contentDescription = stringResource(R.string.action_search_roms),
                    )
                }

                // Overflow menu, which also holds what no longer has its own icon in the bar
                Box {
                    IconButton(
                        modifier = Modifier.actionFocus(1),
                        onClick = { onOverflowMenuChange(true) },
                    ) {
                        Icon(
                            imageVector = Icons.Default.MoreVert,
                            contentDescription = stringResource(R.string.action_more_options),
                        )
                    }
                    DropdownMenu(
                        expanded = showOverflowMenu,
                        onDismissRequest = { onOverflowMenuChange(false) },
                    ) {
                        DropdownMenuItem(
                            text = { Text(stringResource(R.string.settings)) },
                            onClick = {
                                onOverflowMenuChange(false)
                                onNavigateToSettings()
                            },
                        )
                        DropdownMenuItem(
                            text = { Text(stringResource(R.string.dsiware_manager)) },
                            onClick = {
                                onOverflowMenuChange(false)
                                onNavigateToDsiWareManager()
                            },
                        )
                        DropdownMenuItem(
                            text = { Text(stringResource(R.string.action_refresh_rom_list)) },
                            onClick = {
                                onOverflowMenuChange(false)
                                onRefresh()
                            },
                        )
                        DropdownMenuItem(
                            text = { Text(stringResource(R.string.action_boot_ds_firmware)) },
                            onClick = {
                                onOverflowMenuChange(false)
                                onFirmwareBoot(ConsoleType.DS)
                            },
                            leadingIcon = {
                                Icon(
                                    painter = painterResource(R.drawable.ic_firmware),
                                    contentDescription = null,
                                )
                            },
                        )
                        DropdownMenuItem(
                            text = { Text(stringResource(R.string.action_boot_dsi_firmware)) },
                            onClick = {
                                onOverflowMenuChange(false)
                                onFirmwareBoot(ConsoleType.DSi)
                            },
                            leadingIcon = {
                                Icon(
                                    painter = painterResource(R.drawable.ic_firmware),
                                    contentDescription = null,
                                )
                            },
                        )
                    }
                }
            }
        },
    )
}

/**
 * The app's name with the "DS" in the primary color, keeping whatever a build type adds to it. One line whatever the width, as the cover screen is narrow.
 */
@Composable
private fun RomListTitle() {
    val parts = splitAppName(stringResource(R.string.app_name))
    val primary = MaterialTheme.colorScheme.primary
    val title = remember(parts, primary) {
        buildAnnotatedString {
            append(parts.prefix)
            withStyle(SpanStyle(color = primary)) {
                append(parts.accent)
            }
            append(parts.suffix)
        }
    }
    Text(
        text = title,
        fontSize = 22.sp,
        fontWeight = FontWeight.Medium,
        maxLines = 1,
        softWrap = false,
        overflow = TextOverflow.Ellipsis,
    )
}

@Composable
private fun LibraryControls(
    viewMode: RomListViewMode,
    onViewModeChange: (RomListViewMode) -> Unit,
    sortingMode: SortingMode,
    sortingOrder: SortingOrder,
    onSortChange: (SortingMode) -> Unit,
    filter: RomLibraryFilter,
    onFilterChange: (RomLibraryFilter) -> Unit,
) {
    BoxWithConstraints(
        modifier = Modifier
            .fillMaxWidth()
            .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Horizontal))
            .padding(start = 16.dp, top = 4.dp, end = 16.dp, bottom = 10.dp),
    ) {
        if (maxWidth >= SingleRowControlsMinWidth) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                ViewModeSwitch(Modifier.weight(1f), viewMode, onViewModeChange)
                LibraryChips(Modifier.weight(1f), sortingMode, sortingOrder, onSortChange, filter, onFilterChange)
            }
        } else {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                ViewModeSwitch(Modifier.fillMaxWidth(), viewMode, onViewModeChange)
                LibraryChips(Modifier.fillMaxWidth(), sortingMode, sortingOrder, onSortChange, filter, onFilterChange)
            }
        }
    }
}

@Composable
private fun ViewModeSwitch(modifier: Modifier, viewMode: RomListViewMode, onViewModeChange: (RomListViewMode) -> Unit) {
    val modes = listOf(RomListViewMode.LIST, RomListViewMode.GRID, RomListViewMode.MOSAIC)
    SingleChoiceSegmentedButtonRow(modifier) {
        modes.forEachIndexed { index, mode ->
            val shape = SegmentedButtonDefaults.itemShape(index, modes.size)
            SegmentedButton(
                modifier = Modifier.libraryFocusBorder(shape, includeChildren = true),
                selected = mode == viewMode,
                onClick = { onViewModeChange(mode) },
                shape = shape,
                label = {
                    Text(
                        text = stringResource(mode.label()),
                        style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Medium),
                        maxLines = 1,
                        softWrap = false,
                    )
                },
            )
        }
    }
}

@StringRes
private fun RomListViewMode.label(): Int = when (this) {
    RomListViewMode.LIST -> R.string.rom_list_view_list
    RomListViewMode.GRID -> R.string.rom_list_view_grid
    RomListViewMode.MOSAIC -> R.string.rom_list_view_mosaic
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun LibraryChips(
    modifier: Modifier,
    sortingMode: SortingMode,
    sortingOrder: SortingOrder,
    onSortChange: (SortingMode) -> Unit,
    filter: RomLibraryFilter,
    onFilterChange: (RomLibraryFilter) -> Unit,
) {
    FlowRow(
        modifier = modifier,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        SortChip(sortingMode, sortingOrder, onSortChange)
        FilterMenuChip(filter, onFilterChange)
    }
}

@Composable
private fun SortChip(sortingMode: SortingMode, sortingOrder: SortingOrder, onSortChange: (SortingMode) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        LibraryChip(
            text = stringResource(R.string.rom_library_sort_chip, stringResource(sortingMode.label())),
            selected = true,
            leadingIcon = Icons.AutoMirrored.Filled.Sort,
            trailingIcon = Icons.Default.ExpandMore,
            onClick = { expanded = true },
            modifier = LocalLibraryFocus.current?.let { Modifier.focusRequester(it.controlsFocusRequester) } ?: Modifier,
        )
        DropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
        ) {
            listOf(SortingMode.ALPHABETICALLY to Icons.Default.SortByAlpha, SortingMode.RECENTLY_PLAYED to Icons.Default.AccessTime).forEach { (mode, icon) ->
                DropdownMenuItem(
                    text = { Text(stringResource(mode.label())) },
                    onClick = {
                        expanded = false
                        onSortChange(mode)
                    },
                    leadingIcon = {
                        Icon(
                            imageVector = icon,
                            contentDescription = null,
                        )
                    },
                    trailingIcon = if (mode == sortingMode) {
                        {
                            Icon(
                                imageVector = if (sortingOrder == SortingOrder.ASCENDING) Icons.Default.ArrowUpward else Icons.Default.ArrowDownward,
                                contentDescription = null,
                            )
                        }
                    } else {
                        null
                    },
                )
            }
        }
    }
}

@StringRes
private fun SortingMode.label(): Int = when (this) {
    SortingMode.ALPHABETICALLY -> R.string.action_sort_alphabetically
    SortingMode.RECENTLY_PLAYED -> R.string.action_sort_recently_played
}

@Composable
private fun FilterMenuChip(filter: RomLibraryFilter, onFilterChange: (RomLibraryFilter) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        LibraryChip(
            text = if (filter == RomLibraryFilter.ALL) {
                stringResource(R.string.rom_library_filter_chip)
            } else {
                stringResource(R.string.rom_library_filter_chip_active, stringResource(filter.label()))
            },
            selected = filter != RomLibraryFilter.ALL,
            leadingIcon = Icons.Outlined.FilterAlt,
            trailingIcon = null,
            onClick = { expanded = true },
        )
        DropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
        ) {
            RomLibraryFilter.entries.forEach { option ->
                DropdownMenuItem(
                    text = { Text(stringResource(option.label())) },
                    onClick = {
                        expanded = false
                        onFilterChange(option)
                    },
                    trailingIcon = if (option == filter) {
                        {
                            Icon(
                                imageVector = Icons.Default.Check,
                                contentDescription = null,
                            )
                        }
                    } else {
                        null
                    },
                )
            }
        }
    }
}

@StringRes
private fun RomLibraryFilter.label(): Int = when (this) {
    RomLibraryFilter.ALL -> R.string.rom_filter_all
    RomLibraryFilter.PLAYED -> R.string.rom_filter_played
    RomLibraryFilter.NOT_PLAYED -> R.string.rom_filter_not_played
    RomLibraryFilter.HAS_ACHIEVEMENTS -> R.string.rom_filter_has_achievements
}

@Composable
private fun LibraryChip(text: String, selected: Boolean, leadingIcon: ImageVector, trailingIcon: ImageVector?, onClick: () -> Unit, modifier: Modifier = Modifier) {
    FilterChip(
        modifier = modifier.libraryFocusBorder(ChipShape, includeChildren = true),
        selected = selected,
        onClick = onClick,
        shape = ChipShape,
        label = {
            Text(
                text = text,
                style = MaterialTheme.typography.labelLarge.copy(fontSize = 13.sp, fontWeight = FontWeight.Medium),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        },
        leadingIcon = {
            Icon(
                modifier = Modifier.size(16.dp),
                imageVector = leadingIcon,
                contentDescription = null,
            )
        },
        trailingIcon = trailingIcon?.let {
            {
                Icon(
                    modifier = Modifier.size(16.dp),
                    imageVector = it,
                    contentDescription = null,
                )
            }
        },
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SearchTopBar(
    query: String,
    onQueryChange: (String) -> Unit,
    onClose: () -> Unit,
) {
    val focusRequester = remember { FocusRequester() }
    val keyboardController = LocalSoftwareKeyboardController.current

    LaunchedEffect(Unit) {
        focusRequester.requestFocus()
    }

    TopAppBar(
        navigationIcon = {
            IconButton(onClick = onClose) {
                Icon(
                    imageVector = Icons.Default.Close,
                    contentDescription = stringResource(R.string.close),
                )
            }
        },
        title = {
            TextField(
                modifier = Modifier.fillMaxWidth().focusRequester(focusRequester),
                value = query,
                onValueChange = onQueryChange,
                textStyle = MaterialTheme.typography.bodyLarge,
                placeholder = {
                    Text(
                        text = stringResource(R.string.hint_search_roms),
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                },
                singleLine = true,
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                keyboardActions = KeyboardActions(onSearch = { keyboardController?.hide() }),
                colors = TextFieldDefaults.colors(
                    focusedContainerColor = Color.Transparent,
                    unfocusedContainerColor = Color.Transparent,
                    focusedIndicatorColor = Color.Transparent,
                    unfocusedIndicatorColor = Color.Transparent,
                ),
            )
        },
        windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Horizontal + WindowInsetsSides.Top),
    )
}

@Composable
private fun PreviewScreen(viewMode: RomListViewMode) {
    MelonTheme {
        RomListScreen(
            roms = PreviewRomLibrary.roms,
            isRefreshing = false,
            hasSearchDirectories = true,
            onSearchQueryChange = { },
            sortingMode = SortingMode.RECENTLY_PLAYED,
            sortingOrder = SortingOrder.DESCENDING,
            onSortChange = { },
            viewMode = viewMode,
            onViewModeChange = { },
            onFirmwareBoot = { },
            onRomSelected = { },
            onRomConfigClick = { },
            onRefresh = { },
            onDirectorySelected = { },
            onNavigateToSettings = { },
            onNavigateToDsiWareManager = { },
            retrieveRomIcon = { PreviewRomLibrary.icon },
            romMetadata = PreviewRomLibrary.metadataSnapshot,
            hasRetroAchievementsWebApiKey = { true },
        )
    }
}

@MelonPreviewSet
@Composable
private fun PreviewRomListScreenList() {
    PreviewScreen(RomListViewMode.LIST)
}

@MelonPreviewSet
@Composable
private fun PreviewRomListScreenGrid() {
    PreviewScreen(RomListViewMode.GRID)
}

@MelonPreviewSet
@Composable
private fun PreviewRomListScreenMosaic() {
    PreviewScreen(RomListViewMode.MOSAIC)
}
