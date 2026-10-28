package me.magnum.melonds.ui.romlist.ui

import android.net.Uri
import android.view.KeyEvent
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AccessTime
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.SortByAlpha
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.ConsoleType
import me.magnum.melonds.domain.model.SortingMode
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.ui.common.component.romlist.ConfigurableRomItem
import me.magnum.melonds.ui.romlist.RomIcon

@Composable
fun RomListScreen(
    roms: List<Rom>?,
    isRefreshing: Boolean,
    hasSearchDirectories: Boolean,
    onSearchQueryChange: (String?) -> Unit,
    onSortChange: (SortingMode) -> Unit,
    onFirmwareBoot: (ConsoleType) -> Unit,
    onRomSelected: (Rom) -> Unit,
    onRomConfigClick: (Rom) -> Unit,
    onRefresh: () -> Unit,
    onDirectorySelected: (Uri) -> Unit,
    onNavigateToSettings: () -> Unit,
    onNavigateToDsiWareManager: () -> Unit,
    retrieveRomIcon: suspend (Rom) -> RomIcon,
) {
    var isSearchActive by rememberSaveable { mutableStateOf(false) }
    var searchQuery by rememberSaveable { mutableStateOf("") }
    var showOverflowMenu by remember { mutableStateOf(false) }

    BackHandler(isSearchActive) {
        isSearchActive = false
    }

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
            if (isSearchActive) {
                SearchTopBar(
                    query = searchQuery,
                    onQueryChange = { query ->
                        searchQuery = query
                        onSearchQueryChange(query)
                    },
                    onClose = {
                        isSearchActive = false
                        searchQuery = ""
                        onSearchQueryChange(null)
                    },
                )
            } else {
                RomListTopBar(
                    showOverflowMenu = showOverflowMenu,
                    onOverflowMenuChange = { showOverflowMenu = it },
                    onSearchClick = { isSearchActive = true },
                    onSortChange = onSortChange,
                    onFirmwareBoot = onFirmwareBoot,
                    onNavigateToDsiWareManager = onNavigateToDsiWareManager,
                    onRefresh = onRefresh,
                    onNavigateToSettings = onNavigateToSettings,
                )
            }
        },
        containerColor = MaterialTheme.colorScheme.surface,
        contentWindowInsets = WindowInsets.safeDrawing,
    ) { padding ->
        if (hasSearchDirectories) {
            RomList(
                modifier = Modifier.fillMaxSize(),
                roms = roms,
                isRefreshing = isRefreshing,
                onRefresh = onRefresh,
                contentPadding = padding,
            ) { modifier, rom ->
                ConfigurableRomItem(
                    modifier = modifier.fillMaxWidth(),
                    rom = rom,
                    onClick = { onRomSelected(rom) },
                    onConfigClick = { onRomConfigClick(rom) },
                    retrieveTitleIcon = { retrieveRomIcon(rom) },
                )
            }
        } else {
            NoSearchDirectoriesContent(
                modifier = Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding),
                onDirectorySelected = onDirectorySelected,
            )
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun RomListTopBar(
    showOverflowMenu: Boolean,
    onOverflowMenuChange: (Boolean) -> Unit,
    onSearchClick: () -> Unit,
    onSortChange: (SortingMode) -> Unit,
    onFirmwareBoot: (ConsoleType) -> Unit,
    onNavigateToDsiWareManager: () -> Unit,
    onRefresh: () -> Unit,
    onNavigateToSettings: () -> Unit,
) {
    var showSortMenu by remember { mutableStateOf(false) }
    var showFirmwareMenu by remember { mutableStateOf(false) }

    val actionFocusRequesters = remember { List(5) { FocusRequester() } }
    var lastFocusedActionIndex by rememberSaveable { mutableIntStateOf(-1) }
    // Lets the action at [index] take the focus, and remembers it as the last focused one so the row can give the focus back to it
    fun Modifier.actionFocus(index: Int) = focusRequester(actionFocusRequesters[index]).onFocusChanged {
        if (it.isFocused) lastFocusedActionIndex = index
    }

    TopAppBar(
        title = {
            Text(stringResource(R.string.app_name))
        },
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

                // Sort menu
                Box {
                    IconButton(
                        modifier = Modifier.actionFocus(1),
                        onClick = { showSortMenu = true },
                    ) {
                        Icon(
                            painter = painterResource(R.drawable.ic_sort),
                            contentDescription = stringResource(R.string.action_sort_roms),
                        )
                    }
                    DropdownMenu(
                        expanded = showSortMenu,
                        onDismissRequest = { showSortMenu = false },
                    ) {
                        DropdownMenuItem(
                            text = { Text(stringResource(R.string.action_sort_alphabetically)) },
                            onClick = {
                                showSortMenu = false
                                onSortChange(SortingMode.ALPHABETICALLY)
                            },
                            leadingIcon = {
                                Icon(
                                    imageVector = Icons.Default.SortByAlpha,
                                    contentDescription = null,
                                )
                            },
                        )
                        DropdownMenuItem(
                            text = { Text(stringResource(R.string.action_sort_recently_played)) },
                            onClick = {
                                showSortMenu = false
                                onSortChange(SortingMode.RECENTLY_PLAYED)
                            },
                            leadingIcon = {
                                Icon(
                                    imageVector = Icons.Default.AccessTime,
                                    contentDescription = null,
                                )
                            },
                        )
                    }
                }

                // Firmware boot menu
                Box {
                    IconButton(
                        modifier = Modifier.actionFocus(2),
                        onClick = { showFirmwareMenu = true },
                    ) {
                        Icon(
                            painter = painterResource(R.drawable.ic_firmware),
                            contentDescription = stringResource(R.string.action_boot_firmware),
                        )
                    }
                    DropdownMenu(
                        expanded = showFirmwareMenu,
                        onDismissRequest = { showFirmwareMenu = false },
                    ) {
                        DropdownMenuItem(
                            text = { Text(stringResource(R.string.console_ds)) },
                            onClick = {
                                showFirmwareMenu = false
                                onFirmwareBoot(ConsoleType.DS)
                            },
                        )
                        DropdownMenuItem(
                            text = { Text(stringResource(R.string.console_dsi)) },
                            onClick = {
                                showFirmwareMenu = false
                                onFirmwareBoot(ConsoleType.DSi)
                            },
                        )
                    }
                }

                IconButton(
                    modifier = Modifier.actionFocus(3),
                    onClick = onNavigateToSettings,
                ) {
                    Icon(
                        imageVector = Icons.Default.Settings,
                        contentDescription = stringResource(R.string.settings),
                    )
                }

                // Overflow menu
                Box {
                    IconButton(
                        modifier = Modifier.actionFocus(4),
                        onClick = { onOverflowMenuChange(true) },
                    ) {
                        Icon(
                            imageVector = Icons.Default.MoreVert,
                            contentDescription = null,
                        )
                    }
                    DropdownMenu(
                        expanded = showOverflowMenu,
                        onDismissRequest = { onOverflowMenuChange(false) },
                    ) {
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
                    }
                }
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