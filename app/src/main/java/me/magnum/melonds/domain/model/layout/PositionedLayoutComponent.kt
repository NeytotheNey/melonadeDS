package me.magnum.melonds.domain.model.layout

import me.magnum.melonds.domain.model.Rect

data class PositionedLayoutComponent(
    val rect: Rect,
    val component: LayoutComponent,
    val alpha: Float = 1f,
    val onTop: Boolean = false,
    /**
     * D-pad/ABXY spacing from 0 to 1; [rect] grows so the buttons keep their size.
     */
    val spacing: Float = 0f,
) {

    fun isScreen(): Boolean {
        return component.isScreen()
    }
}