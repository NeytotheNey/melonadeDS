package me.magnum.melonds.ui.theme

import android.content.Context
import android.os.Build
import android.util.TypedValue
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.platform.LocalContext
import me.magnum.melonds.R

@Composable
fun MelonTheme(
    isDarkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    val context = LocalContext.current
    val colorScheme = when {
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && context.isDynamicColorsApplied() -> {
            if (isDarkTheme) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        }
        isDarkTheme -> DarkMelonColors
        else -> LightMelonColors
    }

    MaterialTheme(
        colorScheme = colorScheme,
        typography = MelonTypography,
    ) {
        CompositionLocalProvider(LocalContentColor provides MaterialTheme.colorScheme.onSurface) {
            content()
        }
    }
}

/**
 * Whether dynamic colours have been applied to the theme of this context. This is decided when the activity is created (see MelonDSApplication), so
 * that the Compose and View based UIs always use the same colours.
 */
private fun Context.isDynamicColorsApplied(): Boolean {
    val typedValue = TypedValue()
    return theme.resolveAttribute(R.attr.melonDynamicColorsApplied, typedValue, true) && typedValue.data != 0
}
