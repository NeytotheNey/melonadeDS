package me.magnum.melonds.ui.emulator.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Checkbox
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.toMutableStateList
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.emulator.LuaScriptStatus
import me.magnum.melonds.ui.common.component.dialog.BaseDialog
import me.magnum.melonds.ui.common.component.dialog.DialogButton
import me.magnum.melonds.ui.common.component.text.CaptionText

/**
 * Lists the session's [scripts], ticked while they run, each script's id being its position.
 */
@Composable
fun LuaScriptsDialog(
    scripts: List<LuaScriptStatus>,
    onDisableScript: (scriptId: Int) -> Unit,
    onEnableScript: (scriptId: Int) -> Boolean,
    onOpenOptions: (scriptId: Int, script: LuaScriptStatus) -> Unit,
    onDismiss: () -> Unit,
) {
    val isRunning = remember(scripts) { scripts.map { it.isRunning }.toMutableStateList() }
    // Cleared when a script is switched back on, like the session's own copy
    val lastErrors = remember(scripts) { scripts.map { it.lastError }.toMutableStateList() }

    BaseDialog(
        title = stringResource(R.string.label_rom_config_mods),
        onDismiss = onDismiss,
        content = {
            Column {
                scripts.forEachIndexed { scriptId, script ->
                    LuaScriptRow(
                        script = script,
                        isRunning = isRunning[scriptId],
                        lastError = lastErrors[scriptId],
                        onToggle = {
                            if (isRunning[scriptId]) {
                                onDisableScript(scriptId)
                                isRunning[scriptId] = false
                            } else if (onEnableScript(scriptId)) {
                                // If it fails to load, its error shows the next time the dialog opens
                                isRunning[scriptId] = true
                                lastErrors[scriptId] = null
                            }
                        },
                        onOpenOptions = { onOpenOptions(scriptId, script) },
                    )
                }
            }
        },
        buttons = {
            DialogButton(text = stringResource(R.string.close), onClick = onDismiss)
        },
    )
}

@Composable
private fun LuaScriptRow(
    script: LuaScriptStatus,
    isRunning: Boolean,
    lastError: String?,
    onToggle: () -> Unit,
    onOpenOptions: () -> Unit,
) {
    // The tick already shows that a script runs, so its description takes the second line when it has one
    val state = when {
        isRunning -> script.description ?: stringResource(R.string.lua_script_running)
        lastError != null -> stringResource(R.string.lua_script_off_error, lastError)
        else -> stringResource(R.string.lua_script_off)
    }

    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            modifier = Modifier
                .weight(1f)
                .clickable(onClick = onToggle)
                .heightIn(min = 64.dp)
                .padding(start = 24.dp, end = 16.dp, top = 8.dp, bottom = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f).padding(end = 8.dp)) {
                Text(
                    text = script.name,
                    style = MaterialTheme.typography.bodyLarge,
                )
                CaptionText(text = state)
            }
            Checkbox(
                checked = isRunning,
                onCheckedChange = null,
            )
        }
        if (isRunning && script.options.isNotEmpty()) {
            TextButton(
                modifier = Modifier.padding(end = 8.dp),
                onClick = onOpenOptions,
            ) {
                Text(stringResource(R.string.lua_script_options_button))
            }
        }
    }
}
