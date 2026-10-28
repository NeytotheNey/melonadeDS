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

class ControllerAppearance(
    val shading: Shading,
    private val colors: ControllerButtonColors,
    private val inputColors: Map<Input, ControllerButtonColors> = emptyMap(),
) {
    enum class Shading {
        RAISED,
        OUTLINE,
        SOFT_TINT,
    }

    fun colorsFor(input: Input?): ControllerButtonColors {
        return input?.let { inputColors[it] } ?: colors
    }

    companion object {
        private const val WHITE = 0xFFFFFFFF.toInt()

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

        fun of(style: ControllerStyle): ControllerAppearance {
            return when (style) {
                ControllerStyle.CLASSIC -> CLASSIC
                ControllerStyle.DS_LITE -> DS_LITE
                ControllerStyle.CHARCOAL -> CHARCOAL
                ControllerStyle.SUPER_COLOURS -> SUPER_COLOURS
                ControllerStyle.INDIGO -> INDIGO
                ControllerStyle.OUTLINE -> OUTLINE
                ControllerStyle.SOFT_TINT -> SOFT_TINT
            }
        }

        private fun raised(face: Long, pressedFace: Long, lip: Long, label: Int): ControllerAppearance {
            return ControllerAppearance(Shading.RAISED, colors(face, pressedFace, lip, label))
        }

        private fun colors(face: Long, pressedFace: Long, edge: Long, label: Int): ControllerButtonColors {
            return ControllerButtonColors(face.toInt(), pressedFace.toInt(), edge.toInt(), label)
        }
    }
}
