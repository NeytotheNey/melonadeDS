package me.magnum.melonds.impl.diagnostics

import me.magnum.melonds.BuildConfig

/**
 * The debug-only "Debug switches" setting: debug.melonade.* switches (see DebugProperties.h) typed in the app, so A/B runs work without adb.
 * They're given to the native side at game start and replace setprop values of the same name for that game.
 *
 * The text is `name=value` pairs separated by commas, spaces or new lines (the form [Result.describe] prints works too), each name with or
 * without the `debug.melonade.` prefix, and spaces around `=` allowed. A piece without `=` right after a comma continues the value before
 * it, so CPU lists still work: `cpusemu=4,5,6,7 hash3d=1`. Lines starting with `#` are skipped.
 * Names are lowercased; a name given twice keeps its last value.
 */
object DebugSwitches {
    private const val PREFIX = "debug.melonade."
    // Android's PROP_VALUE_MAX, with room for the terminator
    private const val MAX_VALUE_LENGTH = 91
    // PROP_NAME_MAX is 32, prefix and terminator included
    private const val MAX_NAME_LENGTH = 32 - PREFIX.length - 1
    private val NAME_PATTERN = Regex("[a-z0-9_]+")
    private val SPACED_EQUALS = Regex("""\s*=\s*""")
    private val WHITESPACE = Regex("""\s+""")

    data class Result(val switches: List<Pair<String, String>>, val errors: List<String>) {
        /**
         * The form the native side reads (DebugProperties::setOverrides): one `name=value` per line, names without the prefix.
         */
        fun toNativeString(): String = switches.joinToString("\n") { (name, value) -> "$name=$value" }

        /**
         * One line for logs and the setting's summary.
         */
        fun describe(): String = switches.joinToString(" ") { (name, value) -> "$name=$value" }
    }

    fun parse(text: String?): Result {
        if (text.isNullOrBlank()) {
            return Result(emptyList(), emptyList())
        }

        // Insertion order, so the native side and the summary list them as typed
        val switches = LinkedHashMap<String, String>()
        val errors = ArrayList<String>()

        text.split('\n', '\r').forEach { rawLine ->
            val line = rawLine.trim()
            if (line.isEmpty() || line.startsWith("#")) {
                return@forEach
            }

            var lastName: String? = null
            // Spaces around = belong to the pair, then commas and spaces (or tabs) both separate pairs
            line.replace(SPACED_EQUALS, "=").split(',').forEach { commaPiece ->
                commaPiece.trim().split(WHITESPACE).forEachIndexed pieces@{ index, piece ->
                    if (piece.isEmpty()) {
                        return@pieces
                    }

                    val equals = piece.indexOf('=')
                    if (equals < 0) {
                        // The rest of a comma-separated value, like a CPU list: only as the first thing after a comma
                        val name = lastName
                        if (name == null || index != 0) {
                            errors.add(piece)
                            lastName = null
                        } else {
                            switches[name] = switches.getValue(name) + "," + piece
                        }
                        return@pieces
                    }

                    val name = piece.substring(0, equals).lowercase().removePrefix(PREFIX)
                    val value = piece.substring(equals + 1)
                    if (!NAME_PATTERN.matches(name) || name.length > MAX_NAME_LENGTH || value.isEmpty() || value.contains('=')) {
                        errors.add(piece)
                        lastName = null
                        return@pieces
                    }

                    // Moved to the end, so a repeated name shows where its last value was typed
                    switches.remove(name)
                    switches[name] = value
                    lastName = name
                }
            }
        }

        val tooLong = switches.filterValues { it.length > MAX_VALUE_LENGTH }.keys
        tooLong.forEach { name ->
            errors.add("$name=${switches.getValue(name)}")
            switches.remove(name)
        }

        return Result(switches.toList(), errors)
    }

    // Switches the frontend reads itself instead of the core (debandfold, see docs/debug-switches.md): the Debug switches setting of the game
    // that started last, then setprop, the same order the native side reads them in
    @Volatile
    private var current = Result(emptyList(), emptyList())

    /**
     * Goes up every time [setCurrent] is called, so a reader on another thread knows when to read its switches again (once per game).
     */
    @Volatile
    var generation = 0
        private set

    /**
     * The Debug switches setting as it was given to the native side for the game that's starting.
     */
    fun setCurrent(result: Result) {
        current = result
        generation++
    }

    /**
     * Debug builds only (release builds always get [default]): the frontend-side switch [name] (without the prefix) as an int, from the Debug
     * switches setting, else `debug.melonade.<name>`, else [default]. Reads a system property, so ask it once per game, not every frame.
     */
    fun readInt(name: String, default: Int): Int {
        if (!BuildConfig.DEBUG) {
            return default
        }
        val value = current.switches.lastOrNull { it.first == name }?.second ?: systemProperty(PREFIX + name)
        return value?.trim()?.toIntOrNull() ?: default
    }

    // android.os.SystemProperties is hidden, but reading it is allowed for apps; null when it can't be read (or in JVM tests) or is empty
    private fun systemProperty(name: String): String? {
        return runCatching {
            Class.forName("android.os.SystemProperties").getMethod("get", String::class.java, String::class.java).invoke(null, name, "") as String
        }.getOrNull()?.takeIf { it.isNotEmpty() }
    }
}
