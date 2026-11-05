package me.magnum.melonds.domain.model

/**
 * How the ROM library is laid out on the launcher screen.
 */
enum class RomListViewMode {
    /** One row per ROM: banner icon, title and file name. */
    LIST,
    /** Uniform box-art cards in columns, title under each. */
    GRID,
    /** A dense grid of full-bleed art tiles, where the most recently played games take 2x2 and games with a title screen 2x1. */
    MOSAIC,
}
