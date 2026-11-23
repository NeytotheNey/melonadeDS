package me.magnum.melonds.domain.model.emulator

/**
 * What a script says about itself in its leading comment block (before any code, after an optional `#!` line), read by LuaScriptHeaderParser:
 * a `-- description: <text>` line (or else the block's first line with a letter or digit) and option lines like these, where a layout option
 * has no default and starts as "Don't change". A `-- section: <title>` line puts the options after it under that heading, until the next
 * section line (an empty one ends the section):
 *   ```
 *   -- section: Gameplay
 *   -- option: speed type=number min=1 max=10 step=1 default=5 label="Aim speed"
 *   -- option: mode type=choice values=easy,normal,hard default=normal label="Difficulty"
 *   -- section: Display
 *   -- option: hud type=toggle default=true label="Show HUD"
 *   -- option: map_layout type=layout label="Layout while the map is open"
 *   ```
 *
 * @param options The options that could be read, in the order they're declared
 * @param unreadableOptionCount How many option lines broke the rules and were skipped
 */
data class LuaScriptHeader(
    val description: String?,
    val options: List<LuaScriptOption>,
    val unreadableOptionCount: Int,
)
