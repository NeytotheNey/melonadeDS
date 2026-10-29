package me.magnum.melonds.ui.emulator.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.remember
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.emulator.LuaScriptOption
import me.magnum.melonds.ui.common.component.LuaScriptOptionControl
import me.magnum.melonds.ui.common.component.dialog.BaseDialog
import me.magnum.melonds.ui.common.component.dialog.DialogButton

/**
 * What the pause menu's script options dialog shows: the script with [scriptId] and its [options], set to [values]
 */
data class LuaScriptOptionsDialogState(
    val scriptId: Int,
    val scriptName: String,
    val options: List<LuaScriptOption>,
    val values: Map<String, String>,
)

/**
 * The options of a running script, with the same controls as the Mods tab. Nothing changes until the player saves, which restarts the script
 * with the new values
 */
@Composable
fun LuaScriptOptionsDialog(
    state: LuaScriptOptionsDialogState,
    onSave: (Map<String, String>) -> Unit,
    onDismiss: () -> Unit,
) {
    val values = remember(state) { mutableStateMapOf<String, String>().apply { putAll(state.values) } }

    BaseDialog(
        title = state.scriptName,
        onDismiss = onDismiss,
        content = {
            Column {
                state.options.forEach { option ->
                    LuaScriptOptionControl(
                        option = option,
                        value = values[option.key] ?: option.resolve(null),
                        onValueChange = { values[option.key] = it },
                        horizontalPadding = 24.dp,
                    )
                }
            }
        },
        buttons = {
            DialogButton(text = stringResource(R.string.cancel), onClick = onDismiss)
            DialogButton(text = stringResource(R.string.lua_script_options_save), onClick = { onSave(values.toMap()) })
        },
    )
}
