package me.magnum.melonds.domain.model.rom.config

import me.magnum.melonds.domain.model.MAX_WIDE_VIEW_MARGIN
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.SCREEN_WIDTH
import me.magnum.melonds.domain.model.layout.LayoutConfiguration
import me.magnum.melonds.domain.model.layout.UILayout
import java.util.UUID

/**
 * How widescreen starts for a session; [isWideSession] is fixed at launch and the width follows the shown layout.
 *
 * @param automaticViewWidth The Mods tab's width for a normal layout, or 0 when the session is never wide
 * @param wideLayoutId The wide layout the game uses instead of its normal one, or null to use the normal one
 * @param viewWidth The width the game is shown at when it starts (the layout's), or 0 for the normal width
 * @param cameraMaxWidth The widest the WideMelon camera zooms out to, which the core renders at, or 0 when the camera is off
 * @param cameraReturnsToCenter Whether the camera eases back to the middle while the player moves
 */
data class WidescreenLaunch(
    val automaticViewWidth: Int,
    val wideLayoutId: UUID?,
    val viewWidth: Int,
    val cameraMaxWidth: Int = 0,
    val cameraReturnsToCenter: Boolean = true,
) {

    val isWideSession get() = automaticViewWidth > 0

    val isCameraOn get() = isWideSession && cameraMaxWidth > 0

    /**
     * The width the core renders at for a layout shown at [showWidth] (getViewWidth): the same without the camera or for a normal width, the
     * camera's widest view otherwise (never narrower than the shown width)
     */
    fun getRenderWidth(showWidth: Int): Int {
        return if (isCameraOn && showWidth > 0) maxOf(showWidth, cameraMaxWidth) else showWidth
    }

    /**
     * The camera's margin for a layout shown at [showWidth] (camera stage 2): the 3D rows the core renders above and below the top screen, so the
     * widest window (the render width) fills the screen's rect instead of being letterboxed. ceil(96 * (Wr / Wd - 1)), at most
     * MAX_WIDE_VIEW_MARGIN (342 shown with 512 rendered gives 48), and 0 without the camera, for a normal width or with no room to zoom out
     */
    fun getRenderMargin(showWidth: Int): Int {
        val renderWidth = getRenderWidth(showWidth)
        if (!isCameraOn || showWidth <= SCREEN_WIDTH || renderWidth <= showWidth) {
            return 0
        }
        // In whole numbers, so a ratio that lands on a row exactly isn't rounded up by float error
        val halfHeight = SCREEN_HEIGHT / 2
        val margin = (halfHeight * (renderWidth - showWidth) + showWidth - 1) / showWidth
        return margin.coerceAtMost(MAX_WIDE_VIEW_MARGIN)
    }

    /**
     * Width for the shown variant [layout]: its ratio, 0 for an unwide variant of a wide layout, else the Mods tab's width.
     */
    fun getViewWidth(layoutConfiguration: LayoutConfiguration, layout: UILayout): Int {
        if (!isWideSession) {
            return 0
        }
        val wideAspect = layout.wideAspect
        return when {
            wideAspect != null -> wideAspect.viewWidth
            layoutConfiguration.isWide -> 0
            else -> automaticViewWidth
        }
    }

    companion object {
        val OFF = WidescreenLaunch(0, null, 0)

        /**
         * Resolves the launch from the Mods config, the picked wide layout (null if not found) and the renderer.
         */
        fun resolve(mods: RomModsConfig, wideLayout: LayoutConfiguration?, isWideRenderer: Boolean): WidescreenLaunch {
            val automaticWidth = mods.validWideViewWidth
            if (!isWideRenderer || automaticWidth == null) {
                return OFF
            }

            val pickedLayout = wideLayout?.takeIf { it.id != null && it.id == mods.wideLayoutId && it.isWide }
            val cameraMaxWidth = if (mods.wideCamera) mods.validWideCameraMaxWidth else 0
            return if (pickedLayout != null) {
                val viewWidth = pickedLayout.commonWideAspect?.viewWidth ?: automaticWidth
                WidescreenLaunch(automaticWidth, pickedLayout.id, viewWidth, cameraMaxWidth, mods.wideCameraReturnToCenter)
            } else {
                WidescreenLaunch(automaticWidth, null, automaticWidth, cameraMaxWidth, mods.wideCameraReturnToCenter)
            }
        }
    }
}
