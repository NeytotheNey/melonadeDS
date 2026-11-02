package me.magnum.melonds.ui.theme

import android.content.Context
import android.content.res.Configuration
import androidx.annotation.ColorRes
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.ui.graphics.Color
import androidx.core.content.ContextCompat
import me.magnum.melonds.R

val gameMasteryColor: Color get() = Color(0xFFFFD700)

/**
 * The melonDS color scheme seeded from #F44336, read from the m3* colors in colors.xml so the Compose and View based screens always match.
 */
fun Context.melonColorScheme(isDarkTheme: Boolean): ColorScheme {
    // The requested mode isn't always the mode of this context, like for the achievement list, which is always dark
    val nightMode = if (isDarkTheme) Configuration.UI_MODE_NIGHT_YES else Configuration.UI_MODE_NIGHT_NO
    val colorContext = if (resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK == nightMode) {
        this
    } else {
        val configuration = Configuration(resources.configuration).apply {
            uiMode = (uiMode and Configuration.UI_MODE_NIGHT_MASK.inv()) or nightMode
        }
        createConfigurationContext(configuration)
    }
    fun color(@ColorRes id: Int) = Color(ContextCompat.getColor(colorContext, id))

    val primary = color(R.color.m3Primary)
    // The colors that aren't set here (the scrim and the fixed colors) keep the M3 defaults, like with lightColorScheme() and darkColorScheme()
    val baseScheme = if (isDarkTheme) darkColorScheme() else lightColorScheme()
    return baseScheme.copy(
        primary = primary,
        onPrimary = color(R.color.m3OnPrimary),
        primaryContainer = color(R.color.m3PrimaryContainer),
        onPrimaryContainer = color(R.color.m3OnPrimaryContainer),
        inversePrimary = color(R.color.m3InversePrimary),
        secondary = color(R.color.m3Secondary),
        onSecondary = color(R.color.m3OnSecondary),
        secondaryContainer = color(R.color.m3SecondaryContainer),
        onSecondaryContainer = color(R.color.m3OnSecondaryContainer),
        tertiary = color(R.color.m3Tertiary),
        onTertiary = color(R.color.m3OnTertiary),
        tertiaryContainer = color(R.color.m3TertiaryContainer),
        onTertiaryContainer = color(R.color.m3OnTertiaryContainer),
        error = color(R.color.m3Error),
        onError = color(R.color.m3OnError),
        errorContainer = color(R.color.m3ErrorContainer),
        onErrorContainer = color(R.color.m3OnErrorContainer),
        background = color(R.color.m3Background),
        onBackground = color(R.color.m3OnBackground),
        surface = color(R.color.m3Surface),
        onSurface = color(R.color.m3OnSurface),
        surfaceVariant = color(R.color.m3SurfaceVariant),
        onSurfaceVariant = color(R.color.m3OnSurfaceVariant),
        // lightColorScheme() and darkColorScheme() use the primary color by default
        surfaceTint = primary,
        inverseSurface = color(R.color.m3InverseSurface),
        inverseOnSurface = color(R.color.m3InverseOnSurface),
        outline = color(R.color.m3Outline),
        outlineVariant = color(R.color.m3OutlineVariant),
        surfaceBright = color(R.color.m3SurfaceBright),
        surfaceDim = color(R.color.m3SurfaceDim),
        surfaceContainer = color(R.color.m3SurfaceContainer),
        surfaceContainerHigh = color(R.color.m3SurfaceContainerHigh),
        surfaceContainerHighest = color(R.color.m3SurfaceContainerHighest),
        surfaceContainerLow = color(R.color.m3SurfaceContainerLow),
        surfaceContainerLowest = color(R.color.m3SurfaceContainerLowest),
    )
}
