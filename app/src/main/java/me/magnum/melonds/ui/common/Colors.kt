package me.magnum.melonds.ui.common

import androidx.compose.material3.ButtonColors
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.SwitchColors
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.TextFieldColors
import androidx.compose.runtime.Composable

@Composable
fun melonSwitchColors(): SwitchColors {
    return SwitchDefaults.colors()
}

@Composable
fun melonButtonColors(): ButtonColors {
    return ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.primary, contentColor = MaterialTheme.colorScheme.onPrimary)
}

@Composable
fun melonTextButtonColors(): ButtonColors {
    return ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.primary)
}

@Composable
fun melonOutlinedTextFieldColors(): TextFieldColors {
    return OutlinedTextFieldDefaults.colors()
}
