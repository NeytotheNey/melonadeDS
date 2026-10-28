package me.magnum.melonds.ui.layouts.ui

import android.content.Intent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.SnackbarResult
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.FocusRequester.Companion.FocusRequesterFactory.component1
import androidx.compose.ui.focus.FocusRequester.Companion.FocusRequesterFactory.component2
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.vector.rememberVectorPainter
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.layout.LayoutConfiguration
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.common.component.dialog.TextInputDialog
import me.magnum.melonds.ui.common.component.dialog.TextInputDialogState
import me.magnum.melonds.ui.common.component.dialog.rememberTextInputDialogState
import me.magnum.melonds.ui.layouteditor.LayoutEditorActivity
import me.magnum.melonds.ui.layouts.viewmodel.BaseLayoutsViewModel
import me.magnum.melonds.ui.theme.MelonTheme
import java.util.UUID

@Composable
fun LayoutsScreen(
    viewModel: BaseLayoutsViewModel,
    onNavigateBack: () -> Unit,
) {
    val context = LocalContext.current

    val layouts by viewModel.layouts.collectAsStateWithLifecycle()
    val selectedLayout by viewModel.selectedLayoutId.collectAsStateWithLifecycle()
    val layoutEditorLauncher = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { }
    val showCopyLayoutDialogState = rememberTextInputDialogState()

    LayoutsScreenContent(
        layouts = layouts ?: emptyList(),
        selectedLayoutId = selectedLayout.layoutId,
        onLayoutSelected = viewModel::setSelectedLayoutId,
        onCreateLayout = {
            val intent = Intent(context, LayoutEditorActivity::class.java)
            layoutEditorLauncher.launch(intent)
        },
        onEditLayout = { layoutId ->
            val intent = Intent(context, LayoutEditorActivity::class.java)
            intent.putExtra(LayoutEditorActivity.KEY_LAYOUT_ID, layoutId.toString())
            layoutEditorLauncher.launch(intent)
        },
        onCopyLayout = { layout ->
            showCopyLayoutDialogState.show(
                initialText = layout.name.orEmpty(),
                onConfirm = { viewModel.copyLayout(layout, it) },
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences, imeAction = ImeAction.Done),
                highlightInitialText = true,
            )
        },
        onDeleteLayout = viewModel::deleteLayout,
        onUndoDelete = viewModel::addLayout,
        onBackClick = onNavigateBack,
    )

    TextInputDialog(
        title = stringResource(R.string.layout_name),
        dialogState = showCopyLayoutDialogState,
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun LayoutsScreenContent(
    layouts: List<LayoutConfiguration>,
    selectedLayoutId: UUID?,
    onLayoutSelected: (UUID?) -> Unit,
    onCreateLayout: () -> Unit,
    onEditLayout: (UUID) -> Unit,
    onCopyLayout: (LayoutConfiguration) -> Unit,
    onDeleteLayout: (LayoutConfiguration) -> Unit,
    onUndoDelete: (LayoutConfiguration) -> Unit,
    onBackClick: () -> Unit,
) {
    val snackbarHostState = remember { SnackbarHostState() }
    val initialFocusRequester = remember { FocusRequester() }
    val deleteLayoutEvent = remember {
        MutableSharedFlow<LayoutConfiguration>(extraBufferCapacity = 10, onBufferOverflow = BufferOverflow.DROP_OLDEST)
    }

    LaunchedEffect(Unit) {
        initialFocusRequester.requestFocus()
    }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbarHostState) },
        topBar = {
            TopAppBar(
                navigationIcon = {
                    IconButton(onClick = onBackClick) {
                        Icon(
                            painter = rememberVectorPainter(Icons.AutoMirrored.Filled.ArrowBack),
                            contentDescription = stringResource(R.string.navigate_back),
                        )
                    }
                },
                title = { Text(stringResource(R.string.layouts)) },
                windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Horizontal + WindowInsetsSides.Top),
                actions = {
                    IconButton(onClick = onCreateLayout) {
                        Icon(
                            painter = rememberVectorPainter(Icons.Default.Add),
                            contentDescription = stringResource(R.string.action_layouts_new),
                        )
                    }
                },
            )
        },
        containerColor = MaterialTheme.colorScheme.surface,
        contentWindowInsets = WindowInsets.safeDrawing,
    ) { padding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize()
                .focusRequester(initialFocusRequester)
                .consumeWindowInsets(padding),
            contentPadding = padding,
        ) {
            items(
                items = layouts,
                key = { it.id ?: UUID.randomUUID() },
            ) { layout ->
                LayoutItem(
                    layout = layout,
                    isSelected = layout.id == selectedLayoutId,
                    onLayoutSelected = { onLayoutSelected(layout.id) },
                    onEditLayout = { layout.id?.let(onEditLayout) },
                    onCopyLayout = { onCopyLayout(layout) },
                    onDeleteLayout = {
                        deleteLayoutEvent.tryEmit(layout)
                        onDeleteLayout(layout)
                    },
                )
                HorizontalDivider()
            }
        }
    }

    val resources = LocalResources.current
    LaunchedEffect(deleteLayoutEvent) {
        deleteLayoutEvent.collect { layout ->
            val result = snackbarHostState.showSnackbar(
                message = resources.getString(R.string.named_layout_deleted, layout.name),
                actionLabel = resources.getString(R.string.undo),
                duration = SnackbarDuration.Short,
            )
            if (result == SnackbarResult.ActionPerformed) {
                onUndoDelete(layout)
            }
        }
    }
}

@Composable
private fun LayoutItem(
    layout: LayoutConfiguration,
    isSelected: Boolean,
    onLayoutSelected: () -> Unit,
    onEditLayout: () -> Unit,
    onCopyLayout: () -> Unit,
    onDeleteLayout: () -> Unit,
) {
    var showMenu by remember { mutableStateOf(false) }
    val (mainFocusRequester, optionsFocusRequester) = remember { FocusRequester.createRefs() }
    val isCustomLayout = layout.type == LayoutConfiguration.LayoutType.CUSTOM

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .focusRequester(mainFocusRequester)
            .focusProperties {
                end = if (isCustomLayout) optionsFocusRequester else FocusRequester.Default
            }
            .clickable(onClick = onLayoutSelected)
            .onKeyEvent {
                if (it.type == KeyEventType.KeyDown && it.key == Key.Menu) {
                    showMenu = true
                    true
                } else {
                    false
                }
            }
            .padding(start = 16.dp, end = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        RadioButton(
            selected = isSelected,
            onClick = null,
        )

        Text(
            modifier = Modifier
                .weight(1f)
                .padding(start = 32.dp),
            text = layout.name.orEmpty(),
            style = MaterialTheme.typography.bodyLarge,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )

        if (isCustomLayout) {
            Box {
                IconButton(
                    modifier = Modifier
                        .focusRequester(optionsFocusRequester)
                        .focusProperties {
                            start = mainFocusRequester
                        },
                    onClick = { showMenu = true },
                ) {
                    Icon(
                        imageVector = Icons.Default.MoreVert,
                        contentDescription = stringResource(R.string.options),
                    )
                }

                DropdownMenu(
                    expanded = showMenu,
                    onDismissRequest = { showMenu = false },
                ) {
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.edit)) },
                        onClick = {
                            showMenu = false
                            onEditLayout()
                        },
                    )

                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.copy)) },
                        onClick = {
                            showMenu = false
                            onCopyLayout()
                        },
                    )

                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.delete)) },
                        onClick = {
                            showMenu = false
                            onDeleteLayout()
                        },
                    )
                }
            }
        } else {
            // Placeholder box to ensure the UI has the same height for all items, even if they don't display the options dropdown. This height is equal to the minimum
            // interactive component size as defined by the minimumInteractiveComponentSize() modifier
            Box(Modifier.height(48.dp))
        }
    }
}

@MelonPreviewSet
@Composable
private fun PreviewLayoutsScreen() {
    MelonTheme {
        LayoutsScreenContent(
            layouts = listOf(
                LayoutConfiguration(
                    id = LayoutConfiguration.DEFAULT_ID,
                    name = "Default",
                    type = LayoutConfiguration.LayoutType.DEFAULT,
                    orientation = LayoutConfiguration.LayoutOrientation.FOLLOW_SYSTEM,
                    useCustomOpacity = false,
                    opacity = 50,
                    layoutVariants = emptyMap(),
                ),
                LayoutConfiguration(
                    id = UUID.randomUUID(),
                    name = "Custom Layout",
                    type = LayoutConfiguration.LayoutType.CUSTOM,
                    orientation = LayoutConfiguration.LayoutOrientation.FOLLOW_SYSTEM,
                    useCustomOpacity = false,
                    opacity = 50,
                    layoutVariants = emptyMap(),
                ),
            ),
            selectedLayoutId = LayoutConfiguration.DEFAULT_ID,
            onLayoutSelected = { },
            onCreateLayout = { },
            onEditLayout = { },
            onCopyLayout = { },
            onDeleteLayout = { },
            onUndoDelete = { },
            onBackClick = { }
        )
    }
}
