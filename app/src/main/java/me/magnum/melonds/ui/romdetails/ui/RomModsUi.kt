package me.magnum.melonds.ui.romdetails.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
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
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import me.magnum.melonds.R
import me.magnum.melonds.common.Permission
import me.magnum.melonds.common.contracts.DirectoryPickerContract
import me.magnum.melonds.domain.model.rom.config.RomLuaScript
import me.magnum.melonds.domain.model.rom.config.RomModsConfig
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.common.component.text.CaptionText
import me.magnum.melonds.ui.romdetails.model.RomConfigUiModel
import me.magnum.melonds.ui.romdetails.model.RomConfigUiState
import me.magnum.melonds.ui.romdetails.model.RomConfigUpdateEvent
import me.magnum.melonds.ui.romdetails.model.RomLuaScriptsUiModel
import me.magnum.melonds.ui.theme.MelonTheme

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
        // The widescreen section (Phase 2) goes here
        LuaScriptsSection(romConfig, onConfigUpdate)
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
        LuaScriptItem(
            script = script,
            description = scriptsUiModel.descriptions[script.fileName],
            isSwitchEnabled = !areScriptsBlocked,
            onToggle = { isOn -> onConfigUpdate(RomConfigUpdateEvent.LuaScriptToggle(script.fileName, isOn)) },
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
                        luaScripts = listOf(
                            RomLuaScript(fileName = "aim.lua", enabled = true),
                            RomLuaScript(fileName = "turbo.lua", enabled = false),
                        ),
                    ),
                    luaScripts = RomLuaScriptsUiModel(
                        folderPath = "/storage/emulated/0/Scripts/Hunters",
                        descriptions = mapOf("aim.lua" to "Aim with the right stick"),
                    ),
                ),
            ),
            onConfigUpdate = { },
        )
    }
}
