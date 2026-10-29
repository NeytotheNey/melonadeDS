package me.magnum.melonds.ui.romdetails.ui

import android.app.Activity
import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
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
import androidx.compose.ui.res.stringArrayResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import me.magnum.melonds.R
import me.magnum.melonds.common.Permission
import me.magnum.melonds.common.contracts.FilePickerContract
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.config.RomConfig
import me.magnum.melonds.domain.model.rom.config.RomLuaScript
import me.magnum.melonds.domain.model.rom.config.RuntimeConsoleType
import me.magnum.melonds.domain.model.rom.config.RuntimeMicSource
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.common.component.dialog.TextInputDialog
import me.magnum.melonds.ui.common.component.text.CaptionText
import me.magnum.melonds.ui.common.component.dialog.rememberTextInputDialogState
import me.magnum.melonds.ui.common.preference.ActionLauncherItem
import me.magnum.melonds.ui.common.preference.SingleChoiceItem
import me.magnum.melonds.ui.layouts.LayoutSelectorActivity
import me.magnum.melonds.ui.romdetails.model.RomConfigUiModel
import me.magnum.melonds.ui.romdetails.model.RomConfigUiState
import me.magnum.melonds.ui.romdetails.model.RomConfigUpdateEvent
import me.magnum.melonds.ui.romdetails.model.RomGbaSlotConfigUiModel
import me.magnum.melonds.ui.theme.MelonTheme
import java.util.Date
import java.util.UUID

@Composable
fun RomConfigUi(
    modifier: Modifier,
    contentPadding: PaddingValues,
    rom: Rom,
    romConfigUiState: RomConfigUiState,
    onConfigUpdate: (RomConfigUpdateEvent) -> Unit,
) {
    when (romConfigUiState) {
        is RomConfigUiState.Loading -> Loading(modifier.padding(contentPadding))
        is RomConfigUiState.Ready -> Content(
            modifier = modifier,
            contentPadding = contentPadding,
            rom = rom,
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
    rom: Rom,
    romConfig: RomConfigUiModel,
    onConfigUpdate: (RomConfigUpdateEvent) -> Unit,
) {
    Column(
        modifier = modifier.verticalScroll(rememberScrollState()).padding(contentPadding),
    ) {
        val renameDialogState = rememberTextInputDialogState()
        ActionLauncherItem(
            name = stringResource(id = R.string.label_rom_config_custom_name),
            value = romConfig.customName ?: rom.name,
            onLaunchAction = {
                renameDialogState.show(
                    initialText = romConfig.customName ?: rom.name,
                    onConfirm = { newName -> onConfigUpdate(RomConfigUpdateEvent.CustomNameUpdate(newName.ifBlank { null })) },
                )
            }
        )
        TextInputDialog(
            title = stringResource(id = R.string.label_rom_config_custom_name),
            dialogState = renameDialogState,
            textValidator = { true },
            onDelete = {
                onConfigUpdate(RomConfigUpdateEvent.CustomNameUpdate(null))
            },
        )

        val consoleOptions = stringArrayResource(id = R.array.game_runtime_console_type_options)
        SingleChoiceItem(
            name = stringResource(id = R.string.label_rom_config_console),
            value = consoleOptions[romConfig.runtimeConsoleType.ordinal],
            items = consoleOptions.toList(),
            selectedItemIndex = romConfig.runtimeConsoleType.ordinal,
            onItemSelected = {
                onConfigUpdate(RomConfigUpdateEvent.RuntimeConsoleUpdate(RuntimeConsoleType.entries[it]))
            },
            enabled = !rom.isDsiWareTitle,
        )

        val micSourceOptions = stringArrayResource(id = R.array.game_runtime_mic_source_options)
        SingleChoiceItem(
            name = stringResource(id = R.string.microphone_source),
            value = micSourceOptions[romConfig.runtimeMicSource.ordinal],
            items = micSourceOptions.toList(),
            selectedItemIndex = romConfig.runtimeMicSource.ordinal,
            onItemSelected = {
                onConfigUpdate(RomConfigUpdateEvent.RuntimeMicSourceUpdate(RuntimeMicSource.entries[it]))
            }
        )

        val context = LocalContext.current
        val layoutSelectorLauncher = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            if (result.resultCode == Activity.RESULT_OK) {
                val layoutId = result.data?.getStringExtra(LayoutSelectorActivity.KEY_SELECTED_LAYOUT_ID)?.let { UUID.fromString(it) }
                onConfigUpdate(RomConfigUpdateEvent.LayoutUpdate(layoutId))
            }
        }
        ActionLauncherItem(
            name = stringResource(id = R.string.controller_layout),
            value = romConfig.layoutName ?: stringResource(id = R.string.use_global_layout),
            onLaunchAction = {
                val intent = Intent(context, LayoutSelectorActivity::class.java).apply {
                    putExtra(LayoutSelectorActivity.KEY_SELECTED_LAYOUT_ID, romConfig.layoutId?.toString())
                }
                layoutSelectorLauncher.launch(intent)
            }
        )

        val gbaSlotOptions = stringArrayResource(id = R.array.gba_slot_options)
        SingleChoiceItem(
            name = stringResource(id = R.string.label_rom_config_gba_slot),
            value = gbaSlotOptions[romConfig.gbaSlotConfig.type.ordinal],
            items = gbaSlotOptions.toList(),
            selectedItemIndex = romConfig.gbaSlotConfig.type.ordinal,
            onItemSelected = {
                onConfigUpdate(RomConfigUpdateEvent.GbaSlotTypeUpdated(RomGbaSlotConfigUiModel.Type.entries[it]))
            }
        )

        val gbaRomSelectorLauncher = rememberLauncherForActivityResult(FilePickerContract(Permission.READ)) { result ->
            if (result != null) {
                onConfigUpdate(RomConfigUpdateEvent.GbaRomPathUpdate(result))
            }
        }
        val gbaSaveSelectorLauncher = rememberLauncherForActivityResult(FilePickerContract(Permission.READ_WRITE)) { result ->
            if (result != null) {
                onConfigUpdate(RomConfigUpdateEvent.GbaSavePathUpdate(result))
            }
        }

        AnimatedVisibility(visible = romConfig.gbaSlotConfig.type == RomGbaSlotConfigUiModel.Type.GbaRom) {
            Column {
                ActionLauncherItem(
                    name = stringResource(id = R.string.label_rom_config_gba_rom_path),
                    value = romConfig.gbaSlotConfig.gbaRomPath ?: stringResource(id = R.string.not_set),
                    enabled = true,
                    onLaunchAction = {
                        gbaRomSelectorLauncher.launch(Pair(null, null))
                    }
                )

                ActionLauncherItem(
                    name = stringResource(id = R.string.label_rom_config_gba_save_path),
                    value = romConfig.gbaSlotConfig.gbaSavePath ?: stringResource(id = R.string.not_set),
                    enabled = true,
                    onLaunchAction = {
                        gbaSaveSelectorLauncher.launch(Pair(null, null))
                    }
                )
            }
        }

        ModsGroup(romConfig, onConfigUpdate)
    }
}

@Composable
private fun ModsGroup(
    romConfig: RomConfigUiModel,
    onConfigUpdate: (RomConfigUpdateEvent) -> Unit,
) {
    Column {
        Text(
            text = stringResource(id = R.string.label_rom_config_mods),
            modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 24.dp, bottom = 8.dp),
            style = MaterialTheme.typography.titleSmall,
            color = MaterialTheme.colorScheme.primary,
        )

        // The widescreen row (Phase 2) goes here
        LuaScriptsSection(romConfig, onConfigUpdate)
    }
}

@Composable
private fun LuaScriptsSection(
    romConfig: RomConfigUiModel,
    onConfigUpdate: (RomConfigUpdateEvent) -> Unit,
) {
    val luaScripts = romConfig.mods.luaScripts
    val areScriptsBlocked = romConfig.luaScripts.areScriptsBlockedByHardcore
    val isScriptFolderKnown = romConfig.luaScripts.isScriptFolderKnown
    var scriptToDelete by rememberSaveable { mutableStateOf<String?>(null) }

    if (areScriptsBlocked) {
        CaptionText(
            text = stringResource(id = R.string.lua_scripts_off_in_hardcore),
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        )
    }

    luaScripts.forEach { script ->
        LuaScriptItem(
            script = script,
            isFileMissing = script.fileName in romConfig.luaScripts.missingScripts,
            description = romConfig.luaScripts.descriptions[script.fileName],
            isSwitchEnabled = !areScriptsBlocked,
            isDeleteEnabled = isScriptFolderKnown,
            onToggle = { isOn -> onConfigUpdate(RomConfigUpdateEvent.LuaScriptToggle(script.fileName, isOn)) },
            onDelete = { scriptToDelete = script.fileName },
        )
    }

    // .lua files often have no reliable MIME type, so every file is shown and the name is checked once one is picked
    val scriptPickerLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            onConfigUpdate(RomConfigUpdateEvent.LuaScriptAdd(uri, replace = false))
        }
    }
    ActionLauncherItem(
        name = stringResource(id = R.string.lua_script_add),
        value = stringResource(id = R.string.lua_script_add_description),
        enabled = isScriptFolderKnown,
        onLaunchAction = {
            scriptPickerLauncher.launch(arrayOf("*/*"))
        },
    )

    scriptToDelete?.let { fileName ->
        AlertDialog(
            onDismissRequest = { scriptToDelete = null },
            text = { Text(stringResource(id = R.string.lua_script_delete_confirmation, fileName)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        scriptToDelete = null
                        onConfigUpdate(RomConfigUpdateEvent.LuaScriptDelete(fileName))
                    },
                ) {
                    Text(stringResource(id = R.string.delete).uppercase())
                }
            },
            dismissButton = {
                TextButton(onClick = { scriptToDelete = null }) {
                    Text(stringResource(id = R.string.cancel).uppercase())
                }
            },
        )
    }
}

@Composable
private fun LuaScriptItem(
    script: RomLuaScript,
    isFileMissing: Boolean,
    description: String?,
    isSwitchEnabled: Boolean,
    isDeleteEnabled: Boolean,
    onToggle: (Boolean) -> Unit,
    onDelete: () -> Unit,
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
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = script.fileName,
                style = MaterialTheme.typography.bodyLarge,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (isFileMissing) {
                CaptionText(text = stringResource(id = R.string.lua_script_file_missing))
            } else if (description != null) {
                CaptionText(
                    text = description,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        IconButton(onClick = onDelete, enabled = isDeleteEnabled) {
            Icon(
                imageVector = Icons.Default.Delete,
                contentDescription = stringResource(id = R.string.delete),
            )
        }
        Spacer(Modifier.width(8.dp))
        Switch(
            checked = script.enabled,
            onCheckedChange = null,
            enabled = isSwitchEnabled,
        )
    }
}

@MelonPreviewSet
@Composable
private fun PreviewRomConfigUi() {
    MelonTheme {
        RomConfigUi(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(0.dp),
            rom = Rom(
                name = "Professor Layton and the Unwound Future",
                developerName = "Nontendo",
                fileName = "layton.nds",
                uri = Uri.EMPTY,
                parentTreeUri = Uri.EMPTY,
                config = RomConfig(),
                lastPlayed = Date(),
                isDsiWareTitle = false,
                retroAchievementsHash = "",
            ),
            romConfigUiState = RomConfigUiState.Ready(
                RomConfigUiModel(
                    layoutName = "Default",
                    gbaSlotConfig = RomGbaSlotConfigUiModel(type = RomGbaSlotConfigUiModel.Type.GbaRom)
                ),
            ),
            onConfigUpdate = { },
        )
    }
}