package me.magnum.melonds.ui.common.view

import androidx.annotation.ColorInt
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.input.ControllerStyle

/**
 * Colours for one button. [edge] is the lip for raised buttons, the ring for outlined buttons and the inner ring for tinted buttons.
 */
class ControllerButtonColors(
    @ColorInt val face: Int,
    @ColorInt val pressedFace: Int,
    @ColorInt val edge: Int,
    @ColorInt val label: Int,
)

/**
 * Colours for one pixel art button, by role. [lip] is the side of the lip, [glint] a small bright spot in the top-left corner, [ink] the label, arrow or
 * icon and [inkShadow] the hard drop shadow under it. 8-bit buttons use [outline] as their lip and [highlight] as their glint.
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
 * How [ControllerAppearance.Shading.PIXEL] buttons are drawn. 8-bit buttons have big pixels and three shades. 16-bit buttons have pixels half the size
 * and add a lip side, a dithered shadow and a glint. Both draw labels, arrows and icons on a detail grid of 16-bit sized pixels, so an 8-bit pixel is
 * [detailPerCell] detail pixels to a side. [lip] and [travel] are in the style's own pixels and the same on every button. Small views use smaller pixels,
 * so that they're always at least [minCells] of the style's pixels across.
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

    fun colorsFor(input: Input?, group: ControllerAppearance.ColorGroup?): PixelColors {
        if (group == ControllerAppearance.ColorGroup.DPAD) {
            return dpadColors
        }
        return input?.let { inputColors[it] } ?: colors
    }
}

/**
 * How the on-screen buttons look. [pixelArt] is only set, and only used, for [Shading.PIXEL].
 */
class ControllerAppearance(
    val shading: Shading,
    private val colors: ControllerButtonColors,
    private val inputColors: Map<Input, ControllerButtonColors> = emptyMap(),
    val pixelArt: PixelArt? = null,
) {
    enum class Shading {
        RAISED,
        OUTLINE,
        SOFT_TINT,
        PIXEL,
    }

    /**
     * Parts that share their colours even though their inputs differ. Only pixel styles use it: the D-pad's centre has no input, just like Start and
     * Select, but it should match the arms.
     */
    enum class ColorGroup {
        DPAD,
    }

    fun colorsFor(input: Input?): ControllerButtonColors {
        return input?.let { inputColors[it] } ?: colors
    }

    companion object {
        private const val WHITE = 0xFFFFFFFF.toInt()
        private const val PIXEL_8_OUTLINE = 0xFF121218

        private val CLASSIC = raised(0xFF8B8E97, 0xFF6E717A, 0xFF2C2E34, WHITE)
        private val DS_LITE = raised(0xFFE4E5EA, 0xFFC2C4CB, 0xFF8A8D96, 0xFF4A4D55.toInt())
        private val CHARCOAL = raised(0xFF3D4048, 0xFF2D3037, 0xFF111216, 0xFFC9CBD1.toInt())
        private val SUPER_COLOURS = ControllerAppearance(
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
        private val OUTLINE = ControllerAppearance(Shading.OUTLINE, colors(0x0DFFFFFF, 0x66FFFFFF, 0x99FFFFFF, WHITE))
        private val SOFT_TINT = ControllerAppearance(Shading.SOFT_TINT, colors(0x38FFFFFF, 0x1FFFFFFF, 0x73000000, WHITE))

        // 16-bit Classic: outline, lip, shadow, base, highlight, glint, ink and ink shadow
        private val PIXEL_16 = pixel(
            sixteenBit = true,
            colors = pixel16(0xFF2C2C2A, 0xFF5F5E5A, 0xFF888780, 0xFFB4B2A9, 0xFFD3D1C7, 0xFFF1EFE8, 0xFF2C2C2A, 0xFFF1EFE8),
            dpadColors = pixel16(0xFF121218, 0xFF2C2C2A, 0xFF444441, 0xFF5F5E5A, 0xFF888780, 0xFFB4B2A9, 0xFFD3D1C7, 0xFF121218),
            a = pixel16(0xFF501313, 0xFF791F1F, 0xFFA32D2D, 0xFFE24B4A, 0xFFF09595, 0xFFFCEBEB, 0xFFFCEBEB, 0xFF501313),
            b = pixel16(0xFF412402, 0xFF633806, 0xFFBA7517, 0xFFEF9F27, 0xFFFAC775, 0xFFFAEEDA, 0xFFFAEEDA, 0xFF412402),
            x = pixel16(0xFF042C53, 0xFF0C447C, 0xFF185FA5, 0xFF378ADD, 0xFF85B7EB, 0xFFE6F1FB, 0xFFE6F1FB, 0xFF042C53),
            y = pixel16(0xFF173404, 0xFF27500A, 0xFF3B6D11, 0xFF639922, 0xFF97C459, 0xFFEAF3DE, 0xFFEAF3DE, 0xFF173404),
        )
        // 8-bit Classic: shadow, base, highlight, ink and ink shadow, on a near-black outline and lip. The ink is the same as 16-bit's
        private val PIXEL_8 = pixel(
            sixteenBit = false,
            colors = pixel8(0xFF888780, 0xFFB4B2A9, 0xFFD3D1C7, 0xFF2C2C2A, 0xFFF1EFE8),
            dpadColors = pixel8(0xFF444441, 0xFF5F5E5A, 0xFF888780, 0xFFD3D1C7, 0xFF121218),
            a = pixel8(0xFFA32D2D, 0xFFE24B4A, 0xFFF09595, 0xFFFCEBEB, 0xFF501313),
            b = pixel8(0xFFBA7517, 0xFFEF9F27, 0xFFFAC775, 0xFFFAEEDA, 0xFF412402),
            x = pixel8(0xFF185FA5, 0xFF378ADD, 0xFF85B7EB, 0xFFE6F1FB, 0xFF042C53),
            y = pixel8(0xFF3B6D11, 0xFF639922, 0xFF97C459, 0xFFEAF3DE, 0xFF173404),
        )

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
            return ControllerAppearance(Shading.RAISED, colors(face, pressedFace, lip, label))
        }

        private fun colors(face: Long, pressedFace: Long, edge: Long, label: Int): ControllerButtonColors {
            return ControllerButtonColors(face.toInt(), pressedFace.toInt(), edge.toInt(), label)
        }

        /**
         * A pixel style. Its plain [ControllerButtonColors] aren't used to draw it, they're only there so that every appearance has them.
         */
        private fun pixel(sixteenBit: Boolean, colors: PixelColors, dpadColors: PixelColors, a: PixelColors, b: PixelColors, x: PixelColors, y: PixelColors): ControllerAppearance {
            val art = PixelArt(sixteenBit, colors, dpadColors, mapOf(Input.A to a, Input.B to b, Input.X to x, Input.Y to y))
            return ControllerAppearance(Shading.PIXEL, ControllerButtonColors(colors.base, colors.shadow, colors.lip, colors.ink), pixelArt = art)
        }

        private fun pixel16(outline: Long, lip: Long, shadow: Long, base: Long, highlight: Long, glint: Long, ink: Long, inkShadow: Long): PixelColors {
            return PixelColors(outline.toInt(), lip.toInt(), shadow.toInt(), base.toInt(), highlight.toInt(), glint.toInt(), ink.toInt(), inkShadow.toInt())
        }

        private fun pixel8(shadow: Long, base: Long, highlight: Long, ink: Long, inkShadow: Long): PixelColors {
            return pixel16(PIXEL_8_OUTLINE, PIXEL_8_OUTLINE, shadow, base, highlight, highlight, ink, inkShadow)
        }
    }
}
