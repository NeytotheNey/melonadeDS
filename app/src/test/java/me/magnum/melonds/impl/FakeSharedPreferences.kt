package me.magnum.melonds.impl

import android.content.SharedPreferences

/**
 * In-memory [SharedPreferences] for JVM tests; like the real one, clear() applies before the edit's other changes.
 */
class FakeSharedPreferences(initialValues: Map<String, Any> = emptyMap()) : SharedPreferences {

    private val values = initialValues.toMutableMap()

    override fun getAll(): Map<String, *> = values.toMap()
    override fun getString(key: String, defValue: String?) = values[key] as String? ?: defValue
    @Suppress("UNCHECKED_CAST")
    override fun getStringSet(key: String, defValues: Set<String>?) = values[key] as Set<String>? ?: defValues
    override fun getInt(key: String, defValue: Int) = values[key] as Int? ?: defValue
    override fun getLong(key: String, defValue: Long) = values[key] as Long? ?: defValue
    override fun getFloat(key: String, defValue: Float) = values[key] as Float? ?: defValue
    override fun getBoolean(key: String, defValue: Boolean) = values[key] as Boolean? ?: defValue
    override fun contains(key: String) = key in values
    override fun edit(): SharedPreferences.Editor = Editor()
    override fun registerOnSharedPreferenceChangeListener(listener: SharedPreferences.OnSharedPreferenceChangeListener) {}
    override fun unregisterOnSharedPreferenceChangeListener(listener: SharedPreferences.OnSharedPreferenceChangeListener) {}

    private inner class Editor : SharedPreferences.Editor {
        private val changes = mutableMapOf<String, Any?>()
        private var clearFirst = false

        override fun putString(key: String, value: String?) = put(key, value)
        override fun putStringSet(key: String, values: Set<String>?) = put(key, values?.toSet())
        override fun putInt(key: String, value: Int) = put(key, value)
        override fun putLong(key: String, value: Long) = put(key, value)
        override fun putFloat(key: String, value: Float) = put(key, value)
        override fun putBoolean(key: String, value: Boolean) = put(key, value)
        override fun remove(key: String) = put(key, null)
        override fun clear(): SharedPreferences.Editor {
            clearFirst = true
            return this
        }

        override fun commit(): Boolean {
            if (clearFirst) values.clear()
            changes.forEach { (key, value) -> if (value == null) values.remove(key) else values[key] = value }
            return true
        }

        override fun apply() {
            commit()
        }

        private fun put(key: String, value: Any?): SharedPreferences.Editor {
            changes[key] = value
            return this
        }
    }
}
