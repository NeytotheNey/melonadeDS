package me.magnum.melonds.ui.common.component

import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import me.magnum.melonds.domain.model.emulator.LuaScriptOption
import me.magnum.melonds.ui.common.ProvideEnabledContentColor
import me.magnum.melonds.ui.common.component.text.CaptionText
import kotlin.math.roundToInt

/**
 * The control for one of a script's options: a switch, a slider or a dropdown. [value] is the option's value as text (as
 * LuaScriptOption.resolve() gives it), and [onValueChange] gets the new one. Sliders only report once they're let go
 */
@Composable
fun LuaScriptOptionControl(
    option: LuaScriptOption,
    value: String,
    onValueChange: (String) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    horizontalPadding: Dp = 16.dp,
) {
    when (option) {
        is LuaScriptOption.Toggle -> ToggleControl(option, value == "true", enabled, horizontalPadding, modifier) { onValueChange(it.toString()) }
        is LuaScriptOption.Number -> NumberControl(option, value, enabled, horizontalPadding, modifier, onValueChange)
        is LuaScriptOption.Choice -> ChoiceControl(option, value, enabled, horizontalPadding, modifier, onValueChange)
    }
}

@Composable
private fun ToggleControl(
    option: LuaScriptOption.Toggle,
    isOn: Boolean,
    enabled: Boolean,
    horizontalPadding: Dp,
    modifier: Modifier,
    onToggle: (Boolean) -> Unit,
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .clickable(enabled = enabled) { onToggle(!isOn) }
            .heightIn(min = 56.dp)
            .padding(horizontal = horizontalPadding, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ProvideEnabledContentColor(enabled) {
            Text(
                text = option.label,
                modifier = Modifier.weight(1f).padding(end = 8.dp),
                style = MaterialTheme.typography.bodyLarge,
            )
        }
        Switch(checked = isOn, onCheckedChange = null, enabled = enabled)
    }
}

@Composable
private fun NumberControl(
    option: LuaScriptOption.Number,
    value: String,
    enabled: Boolean,
    horizontalPadding: Dp,
    modifier: Modifier,
    onValueChange: (String) -> Unit,
) {
    val step = option.stepOf(value.toDoubleOrNull() ?: option.default) ?: 0
    // The slider's position while it's dragged, in steps
    var position by remember(option, step) { mutableFloatStateOf(step.toFloat()) }
    val shownValue = option.format(option.valueAt(position.roundToInt()))

    Column(
        modifier = modifier
            .fillMaxWidth()
            .heightIn(min = 56.dp)
            .padding(horizontal = horizontalPadding, vertical = 4.dp),
    ) {
        ProvideEnabledContentColor(enabled) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    text = option.label,
                    modifier = Modifier.weight(1f).padding(end = 8.dp),
                    style = MaterialTheme.typography.bodyLarge,
                )
                Text(text = shownValue, style = MaterialTheme.typography.bodyMedium)
            }
        }
        // With many steps the tick marks would run together, so they're left out
        val colors = if (option.stepCount > MAX_TICKED_STEPS) {
            SliderDefaults.colors(activeTickColor = Color.Transparent, inactiveTickColor = Color.Transparent)
        } else {
            SliderDefaults.colors()
        }
        Slider(
            value = position,
            onValueChange = { position = it },
            onValueChangeFinished = { onValueChange(option.format(option.valueAt(position.roundToInt()))) },
            modifier = Modifier.fillMaxWidth(),
            enabled = enabled,
            valueRange = 0f..option.stepCount.toFloat(),
            steps = option.stepCount - 1,
            colors = colors,
        )
    }
}

@Composable
private fun ChoiceControl(
    option: LuaScriptOption.Choice,
    value: String,
    enabled: Boolean,
    horizontalPadding: Dp,
    modifier: Modifier,
    onValueChange: (String) -> Unit,
) {
    var isMenuOpen by remember { mutableStateOf(false) }

    Box(modifier = modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable(enabled = enabled) { isMenuOpen = true }
                .focusable(enabled)
                .heightIn(min = 56.dp)
                .padding(horizontal = horizontalPadding, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            ProvideEnabledContentColor(enabled) {
                Column(modifier = Modifier.weight(1f).padding(end = 8.dp)) {
                    Text(text = option.label, style = MaterialTheme.typography.bodyLarge)
                    CaptionText(text = value, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
                Icon(imageVector = Icons.Default.ArrowDropDown, contentDescription = null)
            }
        }
        DropdownMenu(
            expanded = isMenuOpen,
            onDismissRequest = { isMenuOpen = false },
        ) {
            option.values.forEach { choice ->
                DropdownMenuItem(
                    text = { Text(choice) },
                    onClick = {
                        isMenuOpen = false
                        if (choice != value) {
                            onValueChange(choice)
                        }
                    },
                )
            }
        }
    }
}

private const val MAX_TICKED_STEPS = 20
