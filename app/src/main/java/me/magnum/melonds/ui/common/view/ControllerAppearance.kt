package me.magnum.melonds.ui.common.view

import androidx.annotation.ColorInt
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.input.ControllerStyle

/**
 * Colors of one button, where [edge] is the lip, the ring or the inner ring depending on the shading.
 */
class ControllerButtonColors(
    @ColorInt val face: Int,
    @ColorInt val pressedFace: Int,
    @ColorInt val edge: Int,
    @ColorInt val label: Int,
)

/**
 * Colors of one pixel-art button by role, where [ink] is the label, arrow or icon and [inkShadow] its hard drop shadow.
 */
class PixelColors(
    @ColorInt val outline: Int,
    @ColorInt val lip: Int,
    @ColorInt val shadow: Int,
    @ColorInt val base: Int,
    @ColorInt val highlight: Int,
    @ColorInt val glint: Int,
    @ColorInt val ink: Int,
    @ColorInt val inkShadow: Int,
)

/**
 * Pixel-art parameters for [ControllerAppearance.Pixel]: 8-bit uses 4dp cells and three shades, 16-bit 2dp cells plus a lip side, dither and glint.
 */
class PixelArt(
    val sixteenBit: Boolean,
    private val colors: PixelColors,
    private val dpadColors: PixelColors,
    private val inputColors: Map<Input, PixelColors>,
) {
    val pixelSizeDp = if (sixteenBit) 2f else 4f
    val lip = if (sixteenBit) 4 else 2
    val travel = if (sixteenBit) 3 else 2
    val minCells = if (sixteenBit) 20 else 10
    val detailPerCell = if (sixteenBit) 1 else 2

    fun colorsFor(input: Input?, isDpad: Boolean): PixelColors {
        if (isDpad) {
            return dpadColors
        }
        return input?.let { inputColors[it] } ?: colors
    }
}

/**
 * How the on-screen buttons look: vector shapes in one of the [Shading]s, or pixel art.
 */
sealed class ControllerAppearance {
    enum class Shading {
        RAISED,
        OUTLINE,
        SOFT_TINT,
    }

    /**
     * Buttons drawn as vector shapes in [shading], in [colors] except for the inputs in [inputColors].
     */
    class Vector(
        val shading: Shading,
        private val colors: ControllerButtonColors,
        private val inputColors: Map<Input, ControllerButtonColors> = emptyMap(),
    ) : ControllerAppearance() {
        fun colorsFor(input: Input?): ControllerButtonColors {
            return input?.let { inputColors[it] } ?: colors
        }
    }

    /**
     * Buttons drawn as pixel art by [PixelButtonPainter].
     */
    class Pixel(val art: PixelArt) : ControllerAppearance()

    companion object {
        private const val WHITE = 0xFFFFFFFF.toInt()
        private const val PIXEL_8_OUTLINE = 0xFF121218

        private val CLASSIC = raised(0xFF8B8E97, 0xFF6E717A, 0xFF2C2E34, WHITE)
        private val DS_LITE = raised(0xFFE4E5EA, 0xFFC2C4CB, 0xFF8A8D96, 0xFF4A4D55.toInt())
        private val CHARCOAL = raised(0xFF3D4048, 0xFF2D3037, 0xFF111216, 0xFFC9CBD1.toInt())
        private val SUPER_COLOURS = Vector(
            Shading.RAISED,
            colors(0xFF4A4D55, 0xFF383B42, 0xFF1A1B1F, WHITE),
            mapOf(
                Input.X to colors(0xFF3E6FD6, 0xFF2F57AE, 0xFF1A3270, WHITE),
                Input.A to colors(0xFFD9423B, 0xFFB0322C, 0xFF661A16, WHITE),
                Input.B to colors(0xFFE8B828, 0xFFC29618, 0xFF6E540A, 0xFF4A3A06.toInt()),
                Input.Y to colors(0xFF2E9E5B, 0xFF237D47, 0xFF124A28, WHITE),
            ),
        )
        private val INDIGO = raised(0xFF5B57B5, 0xFF47439C, 0xFF221F52, WHITE)
        private val OUTLINE = Vector(Shading.OUTLINE, colors(0x0DFFFFFF, 0x66FFFFFF, 0x99FFFFFF, WHITE))
        private val SOFT_TINT = Vector(Shading.SOFT_TINT, colors(0x38FFFFFF, 0x1FFFFFFF, 0x73000000, WHITE))

        // The Classic pixel palette: outline, lip, shadow, base, highlight, glint, ink and ink shadow
        private val PIXEL_COLORS = pixel16(0xFF2C2C2A, 0xFF5F5E5A, 0xFF888780, 0xFFB4B2A9, 0xFFD3D1C7, 0xFFF1EFE8, 0xFF2C2C2A, 0xFFF1EFE8)
        private val PIXEL_DPAD_COLORS = pixel16(0xFF121218, 0xFF2C2C2A, 0xFF444441, 0xFF5F5E5A, 0xFF888780, 0xFFB4B2A9, 0xFFD3D1C7, 0xFF121218)
        private val PIXEL_INPUT_COLORS = mapOf(
            Input.A to pixel16(0xFF501313, 0xFF791F1F, 0xFFA32D2D, 0xFFE24B4A, 0xFFF09595, 0xFFFCEBEB, 0xFFFCEBEB, 0xFF501313),
            Input.B to pixel16(0xFF412402, 0xFF633806, 0xFFBA7517, 0xFFEF9F27, 0xFFFAC775, 0xFFFAEEDA, 0xFFFAEEDA, 0xFF412402),
            Input.X to pixel16(0xFF042C53, 0xFF0C447C, 0xFF185FA5, 0xFF378ADD, 0xFF85B7EB, 0xFFE6F1FB, 0xFFE6F1FB, 0xFF042C53),
            Input.Y to pixel16(0xFF173404, 0xFF27500A, 0xFF3B6D11, 0xFF639922, 0xFF97C459, 0xFFEAF3DE, 0xFFEAF3DE, 0xFF173404),
        )
        private val PIXEL_16 = pixel(sixteenBit = true)
        private val PIXEL_8 = pixel(sixteenBit = false)

        fun of(style: ControllerStyle): ControllerAppearance {
            return when (style) {
                ControllerStyle.CLASSIC -> CLASSIC
                ControllerStyle.DS_LITE -> DS_LITE
                ControllerStyle.CHARCOAL -> CHARCOAL
                ControllerStyle.SUPER_COLOURS -> SUPER_COLOURS
                ControllerStyle.INDIGO -> INDIGO
                ControllerStyle.OUTLINE -> OUTLINE
                ControllerStyle.SOFT_TINT -> SOFT_TINT
                ControllerStyle.PIXEL_8 -> PIXEL_8
                ControllerStyle.PIXEL_16 -> PIXEL_16
            }
        }

        private fun raised(face: Long, pressedFace: Long, lip: Long, label: Int): ControllerAppearance {
            return Vector(Shading.RAISED, colors(face, pressedFace, lip, label))
        }

        private fun colors(face: Long, pressedFace: Long, edge: Long, label: Int): ControllerButtonColors {
            return ControllerButtonColors(face.toInt(), pressedFace.toInt(), edge.toInt(), label)
        }

        /**
         * A pixel style in the Classic palette, which 8-bit draws on a near-black outline and lip with the highlight as its glint.
         */
        private fun pixel(sixteenBit: Boolean): ControllerAppearance {
            fun shade(colors: PixelColors) = if (sixteenBit) colors else colors.toEightBit()
            return Pixel(PixelArt(sixteenBit, shade(PIXEL_COLORS), shade(PIXEL_DPAD_COLORS), PIXEL_INPUT_COLORS.mapValues { shade(it.value) }))
        }

        private fun pixel16(outline: Long, lip: Long, shadow: Long, base: Long, highlight: Long, glint: Long, ink: Long, inkShadow: Long): PixelColors {
            return PixelColors(outline.toInt(), lip.toInt(), shadow.toInt(), base.toInt(), highlight.toInt(), glint.toInt(), ink.toInt(), inkShadow.toInt())
        }

        private fun PixelColors.toEightBit(): PixelColors {
            return PixelColors(PIXEL_8_OUTLINE.toInt(), PIXEL_8_OUTLINE.toInt(), shadow, base, highlight, highlight, ink, inkShadow)
        }
    }
}
