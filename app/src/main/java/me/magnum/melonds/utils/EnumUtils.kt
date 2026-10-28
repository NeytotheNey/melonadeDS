package me.magnum.melonds.utils

fun <T : Enum<T>> findEnumValueIgnoreCase(enumValues: Array<T>, value: String): T {
    enumValues.forEach {
        if (it.name.equals(value, true))
            return it
    }
    throw IllegalArgumentException("Value $value does not represent an enum entry")
}

inline fun <reified T> enumValueOfIgnoreCase(value: String): T where T : Enum<T> {
    return findEnumValueIgnoreCase(enumValues(), value)
}

/**
 * Like [enumValueOfIgnoreCase], but returns null when [value] is null or doesn't match any entry.
 */
inline fun <reified T> enumValueOfIgnoreCaseOrNull(value: String?): T? where T : Enum<T> {
    return enumValues<T>().firstOrNull { it.name.equals(value, true) }
}
