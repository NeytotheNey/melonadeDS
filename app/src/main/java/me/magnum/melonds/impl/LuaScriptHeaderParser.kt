package me.magnum.melonds.impl

import me.magnum.melonds.domain.model.emulator.LuaScriptHeader
import me.magnum.melonds.domain.model.emulator.LuaScriptOption

/**
 * Reads a script's [LuaScriptHeader] from the leading comment block in its first [READ_LIMIT] bytes, without running any Lua.
 */
object LuaScriptHeaderParser {
    const val READ_LIMIT = 8 * 1024
    const val MAX_OPTIONS = 16
    const val MAX_LABEL_LENGTH = 60
    const val MAX_CHOICE_VALUES = 8
    const val MAX_CHOICE_VALUE_LENGTH = 24
    const val MAX_NUMBER_STEPS = 1000

    private const val MAX_DESCRIPTION_LENGTH = 120
    private val DESCRIPTION_KEY = Regex("description\\s*:\\s*(.*)", RegexOption.IGNORE_CASE)
    private val OPTION_KEY = Regex("option\\s*:\\s*(.*)", RegexOption.IGNORE_CASE)
    private val BLOCK_COMMENT_OPENER = Regex("^--\\[=*\\[")
    private val BLOCK_COMMENT_CLOSER = Regex("]=*]")

    private val OPTION_NAME = Regex("[a-z_][a-z0-9_]{0,31}")
    private val ATTRIBUTE_NAME = Regex("[a-z_]+")
    // Plain decimals only (no exponents, hex, inf or nan), short enough to stay exact
    private val NUMBER = Regex("-?\\d{1,12}(\\.\\d{1,6})?")
    private val COMMON_ATTRIBUTES = setOf("type", "label", "default")

    /**
     * Attributes each option type takes besides [COMMON_ATTRIBUTES], and its parser (null on a bad line)
     */
    private class OptionType(val attributes: Set<String>, val parse: (OptionLine) -> LuaScriptOption?)

    private val OPTION_TYPES = mapOf(
        "toggle" to OptionType(emptySet(), ::parseToggle),
        "number" to OptionType(setOf("min", "max", "step"), ::parseNumber),
        "choice" to OptionType(setOf("values"), ::parseChoice),
        "layout" to OptionType(emptySet(), ::parseLayout),
    )

    /**
     * An option line split up: its key, its label (the key if it has none), and its other attributes
     */
    private class OptionLine(val key: String, val label: String, val attributes: Map<String, String>) {
        operator fun get(name: String) = attributes[name]
    }

    fun parse(source: ByteArray): LuaScriptHeader {
        var lines = source.copyOf(minOf(source.size, READ_LIMIT)).decodeToString().removePrefix("\uFEFF").lines()
        // The last line was cut off, so it could be half an option
        if (source.size > READ_LIMIT) {
            lines = lines.dropLast(1)
        }

        var description: String? = null
        var firstLine: String? = null
        val options = mutableListOf<LuaScriptOption>()
        var unreadableOptionCount = 0
        var isInBlockComment = false

        for ((index, rawLine) in lines.withIndex()) {
            val line = rawLine.trim()
            val text = when {
                isInBlockComment -> {
                    val closerIndex = BLOCK_COMMENT_CLOSER.find(line)?.range?.first
                    if (closerIndex != null) {
                        isInBlockComment = false
                    }
                    if (closerIndex != null) line.substring(0, closerIndex) else line
                }
                index == 0 && line.startsWith("#!") -> continue
                line.isEmpty() -> continue
                line.startsWith("--") -> {
                    val opener = BLOCK_COMMENT_OPENER.find(line)
                    if (opener != null) {
                        val rest = line.substring(opener.range.last + 1)
                        val closerIndex = BLOCK_COMMENT_CLOSER.find(rest)?.range?.first
                        isInBlockComment = closerIndex == null
                        if (closerIndex != null) rest.substring(0, closerIndex) else rest
                    } else {
                        // Also turns separator lines like "------" into nothing
                        line.trimStart('-')
                    }
                }
                // Code: the leading comment block is over
                else -> break
            }.trim()

            if (text.isEmpty()) {
                continue
            }

            val optionMatch = OPTION_KEY.matchEntire(text)
            if (optionMatch != null) {
                val option = parseOption(optionMatch.groupValues[1])
                if (option == null || options.size >= MAX_OPTIONS || options.any { it.key == option.key }) {
                    unreadableOptionCount++
                } else {
                    options.add(option)
                }
                continue
            }

            val descriptionMatch = DESCRIPTION_KEY.matchEntire(text)
            if (descriptionMatch != null) {
                // The first non-empty description line wins, and an empty one is never used as the fallback either
                val descriptionText = descriptionMatch.groupValues[1].trim()
                if (description == null && descriptionText.isNotEmpty()) {
                    description = capDescription(descriptionText)
                }
                continue
            }
            // Banner lines like "-- ==========" have no letters or digits, so they aren't a description
            if (firstLine == null && text.any { it.isLetterOrDigit() }) {
                firstLine = text
            }
        }

        return LuaScriptHeader(
            description = description ?: firstLine?.let { capDescription(it) },
            options = options,
            unreadableOptionCount = unreadableOptionCount,
        )
    }

    /**
     * Parses what follows `option:`, or returns null on any malformed part (a bad key, an unknown type or attribute, an attribute given twice,
     * an unclosed quote, or values the type doesn't allow)
     */
    private fun parseOption(text: String): LuaScriptOption? {
        val key = text.takeWhile { !it.isWhitespace() }
        if (!OPTION_NAME.matches(key)) {
            return null
        }
        val attributes = parseAttributes(text.substring(key.length)) ?: return null
        val type = OPTION_TYPES[attributes["type"]] ?: return null
        if (!(COMMON_ATTRIBUTES + type.attributes).containsAll(attributes.keys)) {
            return null
        }

        val label = attributes["label"]?.trim() ?: ""
        if (label.length > MAX_LABEL_LENGTH) {
            return null
        }
        return type.parse(OptionLine(key, label.ifEmpty { key }, attributes))
    }

    /**
     * `name=value` pairs split by spaces, where a value runs to the next space or is quoted (`"..."`, which can hold spaces but not quotes)
     */
    private fun parseAttributes(text: String): Map<String, String>? {
        val attributes = mutableMapOf<String, String>()
        var position = 0
        while (true) {
            while (position < text.length && text[position].isWhitespace()) {
                position++
            }
            if (position >= text.length) {
                return attributes
            }

            val equalsIndex = text.indexOf('=', position)
            if (equalsIndex < 0) {
                return null
            }
            val name = text.substring(position, equalsIndex)
            if (!ATTRIBUTE_NAME.matches(name) || name in attributes) {
                return null
            }

            position = equalsIndex + 1
            val value = if (position < text.length && text[position] == '"') {
                val closingIndex = text.indexOf('"', position + 1)
                if (closingIndex < 0) {
                    return null
                }
                text.substring(position + 1, closingIndex).also {
                    position = closingIndex + 1
                    // Something right after the closing quote, like label="a"b
                    if (position < text.length && !text[position].isWhitespace()) {
                        return null
                    }
                }
            } else {
                val end = text.indexOfFirst(position) { it.isWhitespace() }
                text.substring(position, end).also { position = end }
            }
            attributes[name] = value
        }
    }

    private inline fun String.indexOfFirst(start: Int, predicate: (Char) -> Boolean): Int {
        for (index in start until length) {
            if (predicate(this[index])) {
                return index
            }
        }
        return length
    }

    private fun parseToggle(line: OptionLine): LuaScriptOption? {
        val default = when (line["default"]) {
            null, "false" -> false
            "true" -> true
            else -> return null
        }
        return LuaScriptOption.Toggle(line.key, line.label, default)
    }

    private fun parseNumber(line: OptionLine): LuaScriptOption? {
        val minText = line["min"]?.takeIf { NUMBER.matches(it) } ?: return null
        val maxText = line["max"]?.takeIf { NUMBER.matches(it) } ?: return null
        val stepText = (line["step"] ?: "1").takeIf { NUMBER.matches(it) } ?: return null
        val defaultText = line["default"]?.let { text -> text.takeIf { NUMBER.matches(it) } ?: return null }

        val min = minText.toDouble()
        val max = maxText.toDouble()
        val step = stepText.toDouble()
        val stepCount = LuaScriptOption.Number.getStepCount(min, max, step)
        if (!(min < max) || !(step > 0) || stepCount < 1 || stepCount > MAX_NUMBER_STEPS) {
            return null
        }

        val decimals = maxOf(getDecimals(minText), getDecimals(stepText))
        val option = LuaScriptOption.Number(line.key, line.label, min, max, step, min, decimals)
        val default = defaultText?.toDouble() ?: return option
        // The default has to be on the slider; between two steps it goes to the closest one
        val defaultStep = option.stepOf(default) ?: return null
        return option.copy(default = option.valueAt(defaultStep))
    }

    private fun getDecimals(number: String): Int {
        return number.substringAfter('.', "").length
    }

    private fun parseChoice(line: OptionLine): LuaScriptOption? {
        val values = line["values"]?.split(',')?.map { it.trim() } ?: return null
        if (values.isEmpty() || values.size > MAX_CHOICE_VALUES || values.distinct().size != values.size) {
            return null
        }
        if (values.any { it.isEmpty() || it.length > MAX_CHOICE_VALUE_LENGTH }) {
            return null
        }
        val default = line["default"] ?: values.first()
        if (default !in values) {
            return null
        }
        return LuaScriptOption.Choice(line.key, line.label, values, default)
    }

    private fun parseLayout(line: OptionLine): LuaScriptOption? {
        // The player's layouts aren't known to the script, so there's nothing it could name as the default
        if (line["default"] != null) {
            return null
        }
        return LuaScriptOption.Layout(line.key, line.label)
    }

    private fun capDescription(text: String): String {
        if (text.length <= MAX_DESCRIPTION_LENGTH) {
            return text
        }

        var cut = MAX_DESCRIPTION_LENGTH - 1
        // Don't split a surrogate pair
        if (Character.isHighSurrogate(text[cut - 1]) && Character.isLowSurrogate(text[cut])) {
            cut--
        }
        return text.take(cut).trimEnd() + "…"
    }
}
