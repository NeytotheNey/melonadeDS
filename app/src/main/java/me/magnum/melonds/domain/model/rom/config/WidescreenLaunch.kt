package me.magnum.melonds.domain.model.rom.config

import me.magnum.melonds.domain.model.layout.LayoutConfiguration
import me.magnum.melonds.domain.model.layout.UILayout
import java.util.UUID

/**
 * How widescreen starts for a session; [isWideSession] is fixed at launch and the width follows the shown layout.
 *
 * @param automaticViewWidth The Mods tab's width for a normal layout, or 0 when the session is never wide
 * @param wideLayoutId The wide layout the game uses instead of its normal one, or null to use the normal one
 * @param viewWidth The width the game starts at (EmulatorConfiguration.wideViewWidth), or 0 for the normal width
 */
data class WidescreenLaunch(val automaticViewWidth: Int, val wideLayoutId: UUID?, val viewWidth: Int) {

    val isWideSession get() = automaticViewWidth > 0

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
            return if (pickedLayout != null) {
                WidescreenLaunch(automaticWidth, pickedLayout.id, pickedLayout.commonWideAspect?.viewWidth ?: automaticWidth)
            } else {
                WidescreenLaunch(automaticWidth, null, automaticWidth)
            }
        }
    }
}
