package me.magnum.melonds.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

private val DefaultTypography = Typography()

val MelonTypography = Typography(
    bodyLarge = DefaultTypography.bodyLarge.copy(lineHeight = 20.sp),
    labelLarge = DefaultTypography.labelLarge.copy(fontWeight = FontWeight.Bold),
)
