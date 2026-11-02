package me.magnum.melonds.domain.model.emulator

import me.magnum.melonds.domain.model.layout.LayoutConfiguration
import java.util.UUID

/**
 * A layout offered by a layout option (LuaScriptOption.Layout).
 *
 * @param name The layout's name, or null if it has none
 * @param unnamedNumber Number that tells unnamed layouts apart (1, 2, ...), or 0 when named or the only unnamed one
 */
data class LuaLayoutChoice(val id: UUID, val name: String?, val unnamedNumber: Int = 0)

/**
 * Layouts in list order, numbering unnamed ones when there are several.
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
