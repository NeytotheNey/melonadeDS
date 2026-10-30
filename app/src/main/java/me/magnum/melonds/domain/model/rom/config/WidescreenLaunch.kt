package me.magnum.melonds.domain.model.rom.config

import me.magnum.melonds.domain.model.layout.LayoutConfiguration
import me.magnum.melonds.domain.model.layout.UILayout
import java.util.UUID

/**
 * How widescreen is set up when a game starts. Whether the session can be wide at all is decided here and kept for the whole session:
 * switching renderer mid-game only changes how the frame fits in the layout's rect. The width itself follows the layout shown (see
 * [getViewWidth]).
 *
 * @param automaticViewWidth The Mods tab's width, used with a normal layout. 0 when the session is never wide (widescreen off, or another
 * renderer than OpenGL at launch)
 * @param wideLayoutId The wide layout the game uses instead of its normal one, or null to use the normal one
 * @param viewWidth The width the game starts at (EmulatorConfiguration.wideViewWidth), or 0 for the normal width. The variant shown isn't
 * known yet then, so a wide layout starts at its ratio if all its wide variants share one, and the width is corrected once it's shown
 */
data class WidescreenLaunch(val automaticViewWidth: Int, val wideLayoutId: UUID?, val viewWidth: Int) {

    val isWideSession get() = automaticViewWidth > 0

    /**
     * The width for [layout], the variant of [layoutConfiguration] that's shown: its ratio, or the normal width for a variant without one in a
     * wide layout. A normal layout gets the Mods tab's width. Always 0 when the session isn't wide
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
         * With widescreen on and the OpenGL renderer, the game's wide layout if [wideLayout] (what RomModsConfig.wideLayoutId points to, or
         * null if it wasn't found) is still wide. Otherwise the normal layout, with the Mods tab's own ratio
         */
        fun resolve(mods: RomModsConfig, wideLayout: LayoutConfiguration?, isOpenGlRenderer: Boolean): WidescreenLaunch {
            val automaticWidth = mods.validWideViewWidth
            if (!isOpenGlRenderer || automaticWidth == null) {
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
