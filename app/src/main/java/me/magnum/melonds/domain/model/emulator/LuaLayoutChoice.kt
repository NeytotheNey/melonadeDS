package me.magnum.melonds.domain.model.emulator

import me.magnum.melonds.domain.model.layout.LayoutConfiguration
import java.util.UUID

/**
 * One of the player's layouts, as a layout option (LuaScriptOption.Layout) offers it.
 *
 * @param name The layout's name, or null if it has none
 * @param unnamedNumber For a layout with no name: its number among the unnamed ones (1, 2, ...), so they can be told apart, or 0 if it's the
 * only one. Always 0 for named layouts
 */
data class LuaLayoutChoice(val id: UUID, val name: String?, val unnamedNumber: Int = 0)

/**
 * The layouts in the order the layout list shows them. Layout names are optional and not unique, so unnamed layouts get a number when there's
 * more than one
 */
fun List<LayoutConfiguration>.toLuaLayoutChoices(): List<LuaLayoutChoice> {
    val layouts = filter { it.id != null }
    val unnamedCount = layouts.count { it.name.isNullOrBlank() }
    var unnamedIndex = 0
    return layouts.map { layout ->
        val name = layout.name?.takeIf { it.isNotBlank() }
        val unnamedNumber = if (name == null && unnamedCount > 1) ++unnamedIndex else 0
        LuaLayoutChoice(layout.id!!, name, unnamedNumber)
    }
}
