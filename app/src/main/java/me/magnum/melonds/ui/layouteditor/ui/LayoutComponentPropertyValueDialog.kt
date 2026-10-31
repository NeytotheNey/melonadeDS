package me.magnum.melonds.ui.layouteditor.ui

import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import me.magnum.melonds.R
import me.magnum.melonds.ui.common.component.dialog.TextInputDialog
import me.magnum.melonds.ui.common.component.dialog.rememberTextInputDialogState
import me.magnum.melonds.ui.layouteditor.model.LayoutComponentEditableProperty

@Composable
fun LayoutComponentPropertyValueDialog(
    editableProperty: LayoutComponentEditableProperty?,
    initialValue: Int,
    onValueChanged: (Int) -> Unit,
    onCancel: () -> Unit,
) {
    val dialogState = rememberTextInputDialogState()
    val currentOnValueChanged by rememberUpdatedState(onValueChanged)
    val currentOnCancel by rememberUpdatedState(onCancel)

    LaunchedEffect(editableProperty) {
        if (editableProperty != null) {
            dialogState.show(
                initialText = initialValue.toString(),
                onConfirm = { currentOnValueChanged(it.toInt()) },
                onCancel = { currentOnCancel() },
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done, keyboardType = KeyboardType.Number),
                // Typing replaces the current value
                highlightInitialText = true,
                showKeyboard = true,
                singleLine = true,
            )
        }
    }

    TextInputDialog(
        title = when (editableProperty) {
            LayoutComponentEditableProperty.SIZE -> stringResource(R.string.label_size)
            LayoutComponentEditableProperty.WIDTH -> stringResource(R.string.label_width)
            LayoutComponentEditableProperty.HEIGHT -> stringResource(R.string.label_height)
            null -> ""
        },
        dialogState = dialogState,
        textValidator = { it.toIntOrNull() != null },
    )
}
