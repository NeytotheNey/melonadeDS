package me.magnum.melonds.ui.common.view

import androidx.annotation.DrawableRes
import me.magnum.melonds.R

/**
 * A hand-drawn pixel bitmap, one string per row with '#' for ink, and an optional smaller [compact] version for small faces.
 */
class PixelIcon(private val rows: List<String>, val compact: PixelIcon? = null) {
    val width = rows.first().length
    val height = rows.size

    operator fun get(x: Int, y: Int) = rows[y][x] == '#'

    /**
     * The same bitmap turned a quarter turn clockwise.
     */
    fun rotated(): PixelIcon {
        return PixelIcon(List(width) { y -> String(CharArray(height) { x -> rows[height - 1 - x][y] }) }, compact?.rotated())
    }

    /**
     * The same bitmap flipped left to right.
     */
    fun mirrored(): PixelIcon {
        return PixelIcon(rows.map { it.reversed() }, compact?.mirrored())
    }
}

/**
 * Hand-drawn icons and fonts for pixel buttons on their detail grid, looked up by drawable, with a compact version of at most 9×9 for big icons.
 */
object PixelIcons {
    private fun icon(vararg rows: String, compact: PixelIcon? = null) = PixelIcon(rows.toList(), compact)

    val ARROW_UP = icon(
        "...#...",
        "..###..",
        ".#####.",
        "#######",
    )
    val ARROW_RIGHT = ARROW_UP.rotated()
    val ARROW_DOWN = ARROW_RIGHT.rotated()
    val ARROW_LEFT = ARROW_DOWN.rotated()

    private val GEAR = icon(
        "....###....",
        ".##.###.##.",
        ".#########.",
        "..###.###..",
        "####...####",
        "###.....###",
        "####...####",
        "..###.###..",
        ".#########.",
        ".##.###.##.",
        "....###....",
        compact = icon(
            "....#....",
            ".#.###.#.",
            "..#####..",
            ".##...##.",
            "###...###",
            ".##...##.",
            "..#####..",
            ".#.###.#.",
            "....#....",
        ),
    )
    private val FAST_FORWARD = icon(
        "#.....#....",
        "##....##...",
        "###...###..",
        "####..####.",
        "#####.#####",
        "####..####.",
        "###...###..",
        "##....##...",
        "#.....#....",
        compact = icon(
            "#...#....",
            "##..##...",
            "###.###..",
            "####.####",
            "###.###..",
            "##..##...",
            "#...#....",
        ),
    )
    private val REWIND = FAST_FORWARD.mirrored()
    // An open eye for touch controls shown: almond lids with pointed corners and a solid iris with a highlight at its top left
    private val TOUCH = icon(
        "....#####....",
        "..##.....##..",
        ".#....##...#.",
        "#...#####...#",
        ".#...###...#.",
        "..##.....##..",
        "....#####....",
        compact = icon(
            "..#####..",
            ".#.....#.",
            "#...##..#",
            "#..###..#",
            ".#.....#.",
            "..#####..",
        ),
    )
    // A closed eye with lashes for touch controls hidden
    private val TOUCH_OFF = icon(
        "#...........#",
        ".##.......##.",
        "...#######...",
        "..#..#.#..#..",
        ".#...#.#...#.",
        compact = icon(
            "#.......#",
            ".#.....#.",
            "..#####..",
            ".#..#..#.",
        ),
    )
    private val REFRESH = icon(
        ".....#.....",
        ".....##....",
        "...#####...",
        ".##..##....",
        ".#...#.....",
        "#..........",
        "#..........",
        "#.........#",
        "#.........#",
        "#.........#",
        ".#.......#.",
        ".##.....##.",
        "...#####...",
        compact = icon(
            "..###.#..",
            ".#....##.",
            "#....###.",
            "#........",
            "#.......#",
            "#.......#",
            "#.......#",
            ".#.....#.",
            "..#####..",
        ),
    )
    private val SWAP_SCREENS = icon(
        "..#........",
        ".###.......",
        "#####......",
        "..#........",
        "..#.....#..",
        "..#.....#..",
        "..#.....#..",
        "........#..",
        "......#####",
        ".......###.",
        "........#..",
        compact = icon(
            "..#......",
            ".###.....",
            "#####....",
            "..#...#..",
            "..#...#..",
            "..#...#..",
            "....#####",
            ".....###.",
            "......#..",
        ),
    )
    private val QUICK_SAVE = icon(
        ".....#.....",
        ".....#.....",
        ".....#.....",
        ".....#.....",
        "..#..#..#..",
        "...#.#.#...",
        "....###....",
        ".....#.....",
        "#.........#",
        "#.........#",
        "###########",
        compact = icon(
            "....#....",
            "....#....",
            "....#....",
            "..#.#.#..",
            "...###...",
            "....#....",
            "#.......#",
            "#.......#",
            "#########",
        ),
    )
    private val QUICK_LOAD = icon(
        ".....#.....",
        "....###....",
        "...#.#.#...",
        "..#..#..#..",
        ".....#.....",
        ".....#.....",
        ".....#.....",
        ".....#.....",
        "#.........#",
        "#.........#",
        "###########",
        compact = icon(
            "....#....",
            "...###...",
            "..#.#.#..",
            "....#....",
            "....#....",
            "....#....",
            "#.......#",
            "#.......#",
            "#########",
        ),
    )
    private val MICROPHONE = icon(
        "...###...",
        "..#####..",
        "..#####..",
        "..#####..",
        "..#####..",
        "#.#####.#",
        "#.#####.#",
        "#..###..#",
        ".#.....#.",
        "..#####..",
        "....#....",
        "....#....",
        "..#####..",
        compact = icon(
            "..###..",
            ".#####.",
            ".#####.",
            ".#####.",
            "#.###.#",
            "#.....#",
            ".#####.",
            "...#...",
            ".#####.",
        ),
    )
    // A 45° slash with a one cell gap on each side
    private val MICROPHONE_OFF = icon(
        "...###...",
        "..#####..",
        "#.#####..",
        ".#.####..",
        "..#.###..",
        "#..#.##.#",
        "#.#.#.#.#",
        "#..#.#..#",
        ".#....#..",
        "..####.#.",
        "....#....",
        "....#....",
        "..#####..",
        compact = icon(
            "..###..",
            "#.####.",
            ".#.###.",
            "..#.##.",
            "#..#..#",
            "#...#.#",
            ".###.#.",
            "...#..#",
            ".#####.",
        ),
    )
    private val HINGE = icon(
        ".....###.....",
        "........#....",
        ".........#...",
        ".........#...",
        ".......#####.",
        "........###..",
        ".........#...",
        "##########...",
        "##########...",
        ".............",
        "#############",
        "#############",
        compact = icon(
            "..###....",
            ".....#...",
            "......#..",
            "....#####",
            ".....###.",
            "......#..",
            "#######..",
            ".........",
            "#########",
        ),
    )

    private val BY_DRAWABLE = mapOf(
        R.drawable.ic_button_hinge to HINGE,
        R.drawable.ic_settings to GEAR,
        R.drawable.ic_button_fast_forward to FAST_FORWARD,
        R.drawable.ic_button_touch to TOUCH,
        R.drawable.ic_button_touch_off to TOUCH_OFF,
        R.drawable.ic_refresh to REFRESH,
        R.drawable.ic_button_swap_screens to SWAP_SCREENS,
        R.drawable.ic_button_quick_save to QUICK_SAVE,
        R.drawable.ic_button_quick_load to QUICK_LOAD,
        R.drawable.ic_button_rewind to REWIND,
        R.drawable.ic_button_microphone to MICROPHONE,
        R.drawable.ic_button_microphone_off to MICROPHONE_OFF,
    )

    /**
     * The pixel version of an icon drawable, or null if it has none and should be rasterized instead.
     */
    fun forDrawable(@DrawableRes drawable: Int): PixelIcon? = BY_DRAWABLE[drawable]

    private fun font(vararg glyphs: Pair<Char, List<String>>) = glyphs.associate { (char, rows) -> char to PixelIcon(rows) }

    /**
     * Big blocky 5×7 letters for the single letter labels.
     */
    val LETTERS = font(
        'A' to listOf(".###.", "##.##", "##.##", "#####", "##.##", "##.##", "##.##"),
        'B' to listOf("####.", "##.##", "##.##", "####.", "##.##", "##.##", "####."),
        'L' to listOf("##...", "##...", "##...", "##...", "##...", "##...", "#####"),
        'R' to listOf("####.", "##.##", "##.##", "####.", "##.#.", "##.##", "##.##"),
        'X' to listOf("##.##", "##.##", ".###.", "..#..", ".###.", "##.##", "##.##"),
        'Y' to listOf("##.##", "##.##", "##.##", ".###.", "..#..", "..#..", "..#.."),
    )

    /**
     * Compact 3×5 letters for longer labels like START and SELECT.
     */
    val SMALL_LETTERS = font(
        'A' to listOf("###", "#.#", "###", "#.#", "#.#"),
        'C' to listOf("###", "#..", "#..", "#..", "###"),
        'E' to listOf("###", "#..", "##.", "#..", "###"),
        'L' to listOf("#..", "#..", "#..", "#..", "###"),
        'R' to listOf("##.", "#.#", "##.", "#.#", "#.#"),
        'S' to listOf("###", "#..", "###", "..#", "###"),
        'T' to listOf("###", ".#.", ".#.", ".#.", ".#."),
    )
}
