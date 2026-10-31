package me.magnum.melonds.ui.common.component.dialog

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.selection.LocalTextSelectionColors
import androidx.compose.foundation.text.selection.TextSelectionColors
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.TextFieldValue
import kotlinx.coroutines.flow.first
import me.magnum.melonds.R

@Composable
fun TextInputDialog(
    title: String,
    dialogState: TextInputDialogState,
    textValidator: (String) -> Boolean = { it.isNotEmpty() },
    onDelete: (() -> Unit)? = null,
) {
    if (dialogState.isDialogVisible) {
        var hasError by remember { mutableStateOf(false) }

        BaseDialog(
            title = title,
            onDismiss = dialogState::cancel,
            content = { padding ->
                CompositionLocalProvider(
                    LocalTextSelectionColors provides TextSelectionColors(
                        handleColor = MaterialTheme.colorScheme.primary,
                        backgroundColor = LocalTextSelectionColors.current.backgroundColor,
                    )
                ) {
                    val focusRequester = remember { FocusRequester() }
                    OutlinedTextField(
                        modifier = Modifier.fillMaxWidth()
                            .padding(padding)
                            .focusRequester(focusRequester),
                        value = dialogState.textField,
                        onValueChange = {
                            dialogState.textField = it
                            hasError = !textValidator(it.text)
                        },
                        isError = hasError,
                        singleLine = dialogState.singleLine,
                        keyboardOptions = dialogState.keyboardOptions,
                        keyboardActions = KeyboardActions(onDone = { if (!hasError) dialogState.confirm() }),
                    )

                    val keyboardController = LocalSoftwareKeyboardController.current
                    val windowInfo = LocalWindowInfo.current
                    LaunchedEffect(Unit) {
                        focusRequester.requestFocus()
                        if (dialogState.showKeyboard) {
                            // The keyboard is only shown for a focused window, and the dialog's window can get its focus after its first frame
                            snapshotFlow { windowInfo.isWindowFocused }.first { it }
                            keyboardController?.show()
                        }
                    }
                }
            },
            buttons = {
                DialogButton(
                    text = stringResource(R.string.cancel),
                    onClick = { dialogState.cancel() },
                )
                if (onDelete != null) {
                    DialogButton(
                        text = stringResource(R.string.delete),
                        onClick = {
                            onDelete()
                            dialogState.cancel()
                        },
                    )
                }
                DialogButton(
                    text = stringResource(R.string.ok),
                    enabled = !hasError,
                    onClick = { dialogState.confirm() },
                )
            }
        )
    }
}

@Composable
fun rememberTextInputDialogState(): TextInputDialogState {
    return remember {
        TextInputDialogState()
    }
}

class TextInputDialogState {

    internal var isDialogVisible by mutableStateOf(false)
    internal var textField by mutableStateOf(TextFieldValue())
    internal var keyboardOptions by mutableStateOf(KeyboardOptions(imeAction = ImeAction.Done))
    internal var showKeyboard by mutableStateOf(false)
    internal var singleLine by mutableStateOf(false)
    private var onConfirmCallback by mutableStateOf<((String) -> Unit)?>(null)
    private var onCancelCallback by mutableStateOf<(() -> Unit)?>(null)

    fun show(
        initialText: String,
        onConfirm: (String) -> Unit,
        onCancel: () -> Unit = { },
        keyboardOptions: KeyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
        highlightInitialText: Boolean = false,
        // Opens the keyboard straight away. Otherwise the field is only focused
        showKeyboard: Boolean = false,
        // Enter does the keyboard action instead of adding a new line
        singleLine: Boolean = false,
    ) {
        if (isDialogVisible) return

        val selectionRange = if (highlightInitialText) {
            TextRange(0, initialText.length)
        } else {
            TextRange(initialText.length)
        }

        textField = TextFieldValue(text = initialText, selection = selectionRange)
        onConfirmCallback = onConfirm
        onCancelCallback = onCancel
        this.keyboardOptions = keyboardOptions
        this.showKeyboard = showKeyboard
        this.singleLine = singleLine
        isDialogVisible = true
    }

    internal fun cancel() {
        isDialogVisible = false
        onCancelCallback?.invoke()
        onConfirmCallback = null
        onCancelCallback = null
    }

    internal fun confirm() {
        onConfirmCallback?.invoke(textField.text)
        isDialogVisible = false
        onConfirmCallback = null
        onCancelCallback = null
    }
}