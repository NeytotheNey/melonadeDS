package me.magnum.melonds.domain.model.emulator

import java.math.BigDecimal
import java.math.RoundingMode
import java.util.UUID
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.roundToLong

/**
 * An option a script declares in its header (see LuaScriptHeader), which the player sets in the Mods tab or the pause menu. Values are kept
 * as text (in RomLuaScript.options) in the canonical form [resolve] gives, and the script sees them typed in `melon.options`.
 */
sealed class LuaScriptOption {
    abstract val key: String
    abstract val label: String

    /**
     * [stored] if it's a valid value for this option (a number is moved onto the slider's steps), otherwise the default. Always the canonical
     * text, so it can be stored as is
     */
    abstract fun resolve(stored: String?): String

    /**
     * The value as the script sees it, for a value [resolve] gave
     */
    abstract fun toNativeValue(value: String): LuaScriptOptionValue

    data class Toggle(override val key: String, override val label: String, val default: Boolean) : LuaScriptOption() {
        override fun resolve(stored: String?): String {
            return when (stored) {
                "true" -> "true"
                "false" -> "false"
                else -> default.toString()
            }
        }

        override fun toNativeValue(value: String): LuaScriptOptionValue {
            return LuaScriptOptionValue(key, LuaScriptOptionValue.TYPE_BOOLEAN, integer = if (value == "true") 1 else 0)
        }
    }

    /**
     * A slider from [min] to the last step that fits before [max], in steps of [step]. [decimals] is how many decimals the header used, so
     * values are shown and stored without float noise (0.3, not 0.30000000000000004)
     */
    data class Number(
        override val key: String,
        override val label: String,
        val min: Double,
        val max: Double,
        val step: Double,
        val default: Double,
        val decimals: Int,
    ) : LuaScriptOption() {

        /**
         * How many steps the slider has. At least 1
         */
        val stepCount: Int = getStepCount(min, max, step)

        fun valueAt(stepIndex: Int): Double {
            return round(min + stepIndex.coerceIn(0, stepCount) * step)
        }

        /**
         * The step closest to [value], or null if it's off the slider
         */
        fun stepOf(value: Double): Int? {
            if (!value.isFinite()) {
                return null
            }
            val position = (value - min) / step
            // A little room for float noise at both ends
            if (position < -STEP_TOLERANCE || position > stepCount + STEP_TOLERANCE) {
                return null
            }
            return position.roundToLong().toInt().coerceIn(0, stepCount)
        }

        fun format(value: Double): String {
            return BigDecimal(value).setScale(decimals, RoundingMode.HALF_UP).stripTrailingZeros().toPlainString().let {
                // BigDecimal gives "-0" for small negatives rounded to zero
                if (it == "-0") "0" else it
            }
        }

        override fun resolve(stored: String?): String {
            val step = stored?.toDoubleOrNull()?.let { stepOf(it) } ?: stepOf(default) ?: 0
            return format(valueAt(step))
        }

        override fun toNativeValue(value: String): LuaScriptOptionValue {
            val number = value.toDouble()
            // Whole numbers are Lua integers, so scripts can use them as indices and in string.format("%d")
            return if (number == floor(number) && abs(number) < MAX_EXACT_INTEGER) {
                LuaScriptOptionValue(key, LuaScriptOptionValue.TYPE_INTEGER, integer = number.toLong())
            } else {
                LuaScriptOptionValue(key, LuaScriptOptionValue.TYPE_NUMBER, number = number)
            }
        }

        private fun round(value: Double): Double {
            return BigDecimal(value).setScale(decimals, RoundingMode.HALF_UP).toDouble()
        }

        companion object {
            private const val STEP_TOLERANCE = 1e-6
            private const val MAX_EXACT_INTEGER = 9007199254740992.0

            /**
             * Whole steps from [min] that fit before [max]. 0 if none fits
             */
            fun getStepCount(min: Double, max: Double, step: Double): Int {
                if (!(step > 0) || !(max > min)) {
                    return 0
                }
                val steps = floor((max - min) / step + STEP_TOLERANCE)
                return if (steps > Int.MAX_VALUE) Int.MAX_VALUE else steps.toInt()
            }
        }
    }

    data class Choice(override val key: String, override val label: String, val values: List<String>, val default: String) : LuaScriptOption() {
        override fun resolve(stored: String?): String {
            return stored?.takeIf { it in values } ?: default
        }

        override fun toNativeValue(value: String): LuaScriptOptionValue {
            return LuaScriptOptionValue(key, LuaScriptOptionValue.TYPE_STRING, text = value.encodeToByteArray())
        }
    }

    /**
     * One of the player's layouts, which the script switches to with `melon.screen.set_layout(key)`. The value is the layout's UUID, or an
     * empty string for "Don't change" (the default). Whether the layout still exists is only checked when the script asks for it, so a
     * deleted layout's UUID can still be stored
     */
    data class Layout(override val key: String, override val label: String) : LuaScriptOption() {
        override fun resolve(stored: String?): String {
            return getLayoutId(stored)?.toString() ?: DONT_CHANGE
        }

        override fun toNativeValue(value: String): LuaScriptOptionValue {
            // The script only learns whether a layout was picked: the UUID means nothing to it
            return LuaScriptOptionValue(key, LuaScriptOptionValue.TYPE_LAYOUT, integer = if (getLayoutId(value) != null) 1 else 0)
        }

        companion object {
            const val DONT_CHANGE = ""

            /**
             * The layout a value names, or null for "Don't change" (or anything that isn't a UUID)
             */
            fun getLayoutId(value: String?): UUID? {
                if (value.isNullOrEmpty()) {
                    return null
                }
                return try {
                    UUID.fromString(value)
                } catch (e: IllegalArgumentException) {
                    null
                }
            }
        }
    }
}

/**
 * Every declared option's value for the script, from [stored] or the default. Keys that aren't declared are dropped
 */
fun List<LuaScriptOption>.resolveValues(stored: Map<String, String>): Map<String, String> {
    return associate { it.key to it.resolve(stored[it.key]) }
}
