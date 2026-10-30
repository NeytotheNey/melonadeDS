package me.magnum.melonds.ui.romdetails.ui

import android.app.Activity
import android.content.Intent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ExpandLess
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringArrayResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import me.magnum.melonds.R
import me.magnum.melonds.common.Permission
import me.magnum.melonds.common.contracts.DirectoryPickerContract
import me.magnum.melonds.domain.model.emulator.LuaLayoutChoice
import me.magnum.melonds.domain.model.emulator.LuaScriptOption
import me.magnum.melonds.domain.model.layout.LayoutConfiguration
import me.magnum.melonds.domain.model.layout.WidescreenAspect
import me.magnum.melonds.domain.model.rom.config.RomLuaScript
import me.magnum.melonds.domain.model.rom.config.RomModsConfig
import me.magnum.melonds.impl.LuaScriptHeader
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.common.component.LuaScriptOptionControl
import me.magnum.melonds.ui.common.component.text.CaptionText
import me.magnum.melonds.ui.common.preference.ActionLauncherItem
import me.magnum.melonds.ui.common.preference.SingleChoiceItem
import me.magnum.melonds.ui.layouts.LayoutSelectorActivity
import me.magnum.melonds.ui.romdetails.model.RomConfigUiModel
import me.magnum.melonds.ui.romdetails.model.RomConfigUiState
import me.magnum.melonds.ui.romdetails.model.RomConfigUpdateEvent
import me.magnum.melonds.ui.romdetails.model.RomLuaScriptsUiModel
import me.magnum.melonds.ui.theme.MelonTheme
import java.util.UUID

@Composable
fun RomModsUi(
    modifier: Modifier,
    contentPadding: PaddingValues,
    romConfigUiState: RomConfigUiState,
    onConfigUpdate: (RomConfigUpdateEvent) -> Unit,
) {
    when (romConfigUiState) {
        is RomConfigUiState.Loading -> Loading(modifier.padding(contentPadding))
        is RomConfigUiState.Ready -> Content(
            modifier = modifier,
            contentPadding = contentPadding,
            romConfig = romConfigUiState.romConfigUiModel,
            onConfigUpdate = onConfigUpdate,
        )
    }
}

@Composable
private fun Loading(modifier: Modifier) {
    Box(modifier) {
        CircularProgressIndicator(
            modifier = Modifier.align(Alignment.Center),
            color = MaterialTheme.colorScheme.onSurface,
        )
    }
}

@Composable
private fun Content(
    modifier: Modifier,
    contentPadding: PaddingValues,
    romConfig: RomConfigUiModel,
    onConfigUpdate: (RomConfigUpdateEvent) -> Unit,
) {
    Column(
        modifier = modifier.verticalScroll(rememberScrollState()).padding(contentPadding),
    ) {
        WidescreenSection(romConfig, onConfigUpdate)
        LuaScriptsSection(romConfig, onConfigUpdate)
    }
}

@Composable
private fun WidescreenSection(
    romConfig: RomConfigUiModel,
    onConfigUpdate: (RomConfigUpdateEvent) -> Unit,
) {
    val wideViewWidth = romConfig.mods.validWideViewWidth

    Text(
        text = stringResource(id = R.string.widescreen),
        modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 24.dp, bottom = 8.dp),
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.primary,
    )

    // The width is still kept, for when the renderer is switched to OpenGL
    if (!romConfig.isWidescreenSupported) {
        CaptionText(
            text = stringResource(id = R.string.widescreen_needs_opengl),
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        )
    }

    WidescreenSwitchItem(
        isOn = wideViewWidth != null,
        onToggle = { isOn -> onConfigUpdate(RomConfigUpdateEvent.WideViewWidthUpdate(if (isOn) WidescreenAspect.DEFAULT.viewWidth else null)) },
    )

    if (wideViewWidth != null) {
        val aspectOptions = stringArrayResource(id = R.array.widescreen_aspect_options)
        // A layout that was deleted or isn't marked wide anymore counts as automatic, like at launch
        val wideLayout = romConfig.wideLayouts?.firstOrNull { it.id != null && it.id == romConfig.mods.wideLayoutId }
        val automaticAspect = WidescreenAspect.fromViewWidth(wideViewWidth)
        WideLayoutItem(
            romConfig = romConfig,
            wideLayout = wideLayout,
            newLayoutWideAspect = wideLayout?.commonWideAspect ?: automaticAspect,
            onConfigUpdate = onConfigUpdate,
        )

        if (wideLayout != null) {
            // Each of the layout's variants has its own ratio
            InfoItem(
                name = stringResource(id = R.string.widescreen_aspect_ratio),
                value = stringResource(id = R.string.wide_layout_aspect_ratio),
            )
        } else {
            SingleChoiceItem(
                name = stringResource(id = R.string.widescreen_aspect_ratio),
                value = aspectOptions[automaticAspect.ordinal],
                items = aspectOptions.toList(),
                selectedItemIndex = automaticAspect.ordinal,
                onItemSelected = { onConfigUpdate(RomConfigUpdateEvent.WideViewWidthUpdate(WidescreenAspect.entries[it].viewWidth)) },
            )
        }
    }
}

@Composable
private fun WideLayoutItem(
    romConfig: RomConfigUiModel,
    wideLayout: LayoutConfiguration?,
    newLayoutWideAspect: WidescreenAspect,
    onConfigUpdate: (RomConfigUpdateEvent) -> Unit,
) {
    val context = LocalContext.current
    val layoutSelectorLauncher = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            val layoutId = result.data?.getStringExtra(LayoutSelectorActivity.KEY_SELECTED_LAYOUT_ID)?.let { UUID.fromString(it) }
            onConfigUpdate(RomConfigUpdateEvent.WideLayoutUpdate(layoutId))
        }
    }
    ActionLauncherItem(
        name = stringResource(id = R.string.wide_layout),
        value = when {
            wideLayout == null -> stringResource(id = R.string.wide_layout_automatic)
            wideLayout.name.isNullOrBlank() -> stringResource(id = R.string.lua_script_option_layout_unnamed)
            else -> wideLayout.name
        },
        // Until the layouts are read it isn't known which one is picked
        enabled = romConfig.wideLayouts != null,
        onLaunchAction = {
            // The stored pick, so that backing out keeps it. The picker falls back to automatic if that layout isn't wide (anymore)
            val intent = Intent(context, LayoutSelectorActivity::class.java).apply {
                putExtra(LayoutSelectorActivity.KEY_SELECTED_LAYOUT_ID, romConfig.mods.wideLayoutId?.toString())
                putExtra(LayoutSelectorActivity.KEY_WIDE_LAYOUTS_ONLY, true)
                // New layouts made from the picker start at the game's current ratio
                putExtra(LayoutSelectorActivity.KEY_NEW_LAYOUT_WIDE_ASPECT, newLayoutWideAspect.key)
            }
            layoutSelectorLauncher.launch(intent)
        },
    )

    if (romConfig.wideLayouts?.isEmpty() == true) {
        CaptionText(
            text = stringResource(id = R.string.wide_layout_none),
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        )
    }
}

/**
 * A row that only shows a value, which can't be changed here
 */
@Composable
private fun InfoItem(
    name: String,
    value: String,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = 64.dp)
            .padding(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 8.dp),
        verticalArrangement = Arrangement.Center,
    ) {
        Text(
            text = name,
            style = MaterialTheme.typography.bodyLarge,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        CaptionText(
            text = value,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun WidescreenSwitchItem(
    isOn: Boolean,
    onToggle: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onToggle(!isOn) }
            .focusable()
            .heightIn(min = 64.dp)
            .padding(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f).padding(end = 8.dp)) {
            Text(
                text = stringResource(id = R.string.widescreen_enable),
                style = MaterialTheme.typography.bodyLarge,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            CaptionText(text = stringResource(id = R.string.widescreen_description))
        }
        Switch(
            checked = isOn,
            onCheckedChange = null,
        )
    }
}

@Composable
private fun LuaScriptsSection(
    romConfig: RomConfigUiModel,
    onConfigUpdate: (RomConfigUpdateEvent) -> Unit,
) {
    val luaScripts = romConfig.mods.luaScripts
    val scriptsUiModel = romConfig.luaScripts
    val areScriptsBlocked = scriptsUiModel.areScriptsBlockedByHardcore

    Text(
        text = stringResource(id = R.string.lua_scripts),
        modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 24.dp, bottom = 8.dp),
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.primary,
    )

    if (areScriptsBlocked) {
        CaptionText(
            text = stringResource(id = R.string.lua_scripts_off_in_hardcore),
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        )
    }

    val folderPickerLauncher = rememberLauncherForActivityResult(DirectoryPickerContract(Permission.READ)) { uri ->
        if (uri != null) {
            onConfigUpdate(RomConfigUpdateEvent.LuaScriptFolderUpdate(uri))
        }
    }
    ScriptFolderItem(
        folderPath = scriptsUiModel.folderPath,
        onPickFolder = { folderPickerLauncher.launch(romConfig.mods.luaScriptFolder) },
        onRemoveFolder = { onConfigUpdate(RomConfigUpdateEvent.LuaScriptFolderUpdate(null)) },
    )

    val folderMessage = when {
        scriptsUiModel.isFolderMissing -> R.string.lua_script_folder_not_found
        scriptsUiModel.isFolderEmpty -> R.string.lua_script_folder_empty
        else -> null
    }
    if (folderMessage != null) {
        CaptionText(
            text = stringResource(id = folderMessage),
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        )
    }

    luaScripts.forEach { script ->
        val header = scriptsUiModel.headers[script.fileName]
        LuaScriptItem(
            script = script,
            description = header?.description,
            isSwitchEnabled = !areScriptsBlocked,
            onToggle = { isOn -> onConfigUpdate(RomConfigUpdateEvent.LuaScriptToggle(script.fileName, isOn)) },
        )
        if (header != null && (header.options.isNotEmpty() || header.unreadableOptionCount > 0)) {
            LuaScriptOptionsSection(
                script = script,
                header = header,
                layouts = scriptsUiModel.layouts,
                onOptionChange = { key, value -> onConfigUpdate(RomConfigUpdateEvent.LuaScriptOptionUpdate(script.fileName, key, value)) },
            )
        }
        scriptsUiModel.dataSizes[script.fileName]?.let { dataSize ->
            LuaScriptDataItem(
                scriptName = script.fileName,
                dataSize = dataSize,
                onClear = { onConfigUpdate(RomConfigUpdateEvent.LuaScriptDataClear(script.fileName)) },
            )
        }
    }
}

/**
 * The script's options, folded away until the player opens them
 */
@Composable
private fun LuaScriptOptionsSection(
    script: RomLuaScript,
    header: LuaScriptHeader,
    layouts: List<LuaLayoutChoice>?,
    onOptionChange: (key: String, value: String) -> Unit,
) {
    var isExpanded by rememberSaveable(script.fileName) { mutableStateOf(false) }

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { isExpanded = !isExpanded }
            .focusable()
            .heightIn(min = 48.dp)
            .padding(start = 32.dp, end = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = stringResource(id = R.string.lua_script_options),
            modifier = Modifier.weight(1f),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.primary,
        )
        Icon(
            imageVector = if (isExpanded) Icons.Default.ExpandLess else Icons.Default.ExpandMore,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.primary,
        )
    }

    if (isExpanded) {
        header.options.forEach { option ->
            LuaScriptOptionControl(
                option = option,
                value = option.resolve(script.options[option.key]),
                onValueChange = { onOptionChange(option.key, it) },
                modifier = Modifier.padding(start = 16.dp),
                layouts = layouts,
            )
        }
        if (header.unreadableOptionCount > 0) {
            CaptionText(
                text = pluralStringResource(id = R.plurals.lua_script_options_unreadable, count = header.unreadableOptionCount, header.unreadableOptionCount),
                modifier = Modifier.padding(start = 32.dp, end = 16.dp, top = 4.dp, bottom = 8.dp),
            )
        }
    }
}

@Composable
private fun LuaScriptDataItem(
    scriptName: String,
    dataSize: Long,
    onClear: () -> Unit,
) {
    var isConfirmationShown by rememberSaveable(scriptName) { mutableStateOf(false) }
    // Rounded up, so a few bytes don't show as 0 KB
    val sizeInKb = (dataSize + 1023) / 1024

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = 48.dp)
            .padding(start = 32.dp, end = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CaptionText(
            text = stringResource(id = R.string.lua_script_saved_data, sizeInKb),
            modifier = Modifier.weight(1f),
        )
        TextButton(onClick = { isConfirmationShown = true }) {
            Text(text = stringResource(id = R.string.lua_script_saved_data_clear))
        }
    }

    if (isConfirmationShown) {
        AlertDialog(
            onDismissRequest = { isConfirmationShown = false },
            title = { Text(text = stringResource(id = R.string.lua_script_saved_data_clear_title)) },
            text = { Text(text = stringResource(id = R.string.lua_script_saved_data_clear_message, scriptName)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        isConfirmationShown = false
                        onClear()
                    },
                ) {
                    Text(text = stringResource(id = R.string.lua_script_saved_data_clear))
                }
            },
            dismissButton = {
                TextButton(onClick = { isConfirmationShown = false }) {
                    Text(text = stringResource(id = R.string.cancel))
                }
            },
        )
    }
}

@Composable
private fun ScriptFolderItem(
    folderPath: String?,
    onPickFolder: () -> Unit,
    onRemoveFolder: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onPickFolder() }
            .focusable()
            .heightIn(min = 64.dp)
            .padding(start = 16.dp, end = 8.dp, top = 8.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = stringResource(id = R.string.lua_script_folder),
                style = MaterialTheme.typography.bodyLarge,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            CaptionText(
                text = folderPath ?: stringResource(id = R.string.lua_script_folder_not_set),
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
        }
        if (folderPath != null) {
            IconButton(onClick = onRemoveFolder) {
                Icon(
                    imageVector = Icons.Default.Close,
                    contentDescription = stringResource(id = R.string.lua_script_folder_remove),
                )
            }
        }
    }
}

@Composable
private fun LuaScriptItem(
    script: RomLuaScript,
    description: String?,
    isSwitchEnabled: Boolean,
    onToggle: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .run {
                if (isSwitchEnabled) {
                    clickable { onToggle(!script.enabled) }.focusable()
                } else {
                    this
                }
            }
            .heightIn(min = 64.dp)
            .padding(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f).padding(end = 8.dp)) {
            Text(
                text = script.fileName,
                style = MaterialTheme.typography.bodyLarge,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (description != null) {
                CaptionText(
                    text = description,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        Switch(
            checked = script.enabled,
            onCheckedChange = null,
            enabled = isSwitchEnabled,
        )
    }
}

@MelonPreviewSet
@Composable
private fun PreviewRomModsUi() {
    MelonTheme {
        RomModsUi(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(0.dp),
            romConfigUiState = RomConfigUiState.Ready(
                RomConfigUiModel(
                    mods = RomModsConfig(
                        wideViewWidth = WidescreenAspect.DEFAULT.viewWidth,
                        luaScripts = listOf(
                            RomLuaScript(fileName = "aim.lua", enabled = true, options = mapOf("speed" to "7")),
                            RomLuaScript(fileName = "turbo.lua", enabled = false),
                        ),
                    ),
                    luaScripts = RomLuaScriptsUiModel(
                        folderPath = "/storage/emulated/0/Scripts/Hunters",
                        headers = mapOf(
                            "aim.lua" to LuaScriptHeader(
                                description = "Aim with the right stick",
                                options = listOf(
                                    LuaScriptOption.Number("speed", "Aim speed", min = 1.0, max = 10.0, step = 1.0, default = 5.0, decimals = 0),
                                    LuaScriptOption.Toggle("hud", "Show HUD", default = true),
                                    LuaScriptOption.Choice("mode", "Difficulty", listOf("easy", "normal", "hard"), default = "normal"),
                                    LuaScriptOption.Layout("map_layout", "Layout while the map is open"),
                                ),
                                unreadableOptionCount = 1,
                            ),
                        ),
                        dataSizes = mapOf("aim.lua" to 3000L),
                    ),
                ),
            ),
            onConfigUpdate = { },
        )
    }
}
