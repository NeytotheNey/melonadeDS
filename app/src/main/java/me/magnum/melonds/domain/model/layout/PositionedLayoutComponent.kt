package me.magnum.melonds.domain.model.layout

import me.magnum.melonds.domain.model.Rect

data class PositionedLayoutComponent(
    val rect: Rect,
    val component: LayoutComponent,
    val alpha: Float = 1f,
    val onTop: Boolean = false,
    /**
     * How far apart the buttons of the D-pad or the face buttons are, from 0 (default) to 1. The buttons keep their size, so [rect] is bigger to make room.
     * Other components ignore it.
     */
    val spacing: Float = 0f,
) {

    fun isScreen(): Boolean {
        return component.isScreen()
    }
}