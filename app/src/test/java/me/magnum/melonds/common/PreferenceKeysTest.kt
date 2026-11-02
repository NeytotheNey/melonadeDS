package me.magnum.melonds.common

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element
import java.io.File
import java.lang.reflect.Modifier
import javax.xml.parsers.DocumentBuilderFactory

/**
 * Checks the keys and defaults the preference screens keep as XML literals against [PreferenceKeys] and the defaults
 * SharedPreferencesSettingsRepository reads with
 */
class PreferenceKeysTest {

    // Gradle runs unit tests from the module directory; the fallback is for a runner started from the project root
    private val moduleDir = listOf(File("."), File("app")).first { File(it, "src/main/res/xml").isDirectory }

    // Read and written from code only
    private val keysWithoutAScreen = setOf(
        PreferenceKeys.DEBUG_LAST_REPORTED_EXIT_MS,
        PreferenceKeys.LAYOUT_EDITOR_SNAP_TO_GRID,
        PreferenceKeys.LAYOUT_EDITOR_GRID_SIZE,
        PreferenceKeys.LAYOUT_EDITOR_ITEM_PANEL_EXPANDED,
        PreferenceKeys.LAYOUT_EDITOR_MENU_BUTTON_PREFIX,
    )

    // Constant name to key
    private val preferenceKeys: Map<String, String> = PreferenceKeys::class.java.declaredFields
        .filter { Modifier.isStatic(it.modifiers) && it.type == String::class.java }
        .associate { it.name to it.get(null) as String }

    // android:key to android:defaultValue (null when the element has none) of every element in res/xml/pref_*.xml
    private val xmlPreferences: Map<String, String?> = File(moduleDir, "src/main/res/xml").listFiles { file -> file.name.matches(Regex("pref_.*\\.xml")) }!!
        .flatMap { file ->
            val elements = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file).getElementsByTagName("*")
            (0 until elements.length).map { elements.item(it) as Element }
        }
        .filter { it.hasAttribute("android:key") }
        .associate { element ->
            val default = if (element.hasAttribute("android:defaultValue")) element.getAttribute("android:defaultValue") else null
            element.getAttribute("android:key") to default
        }

    @Test
    fun everyKeyWithAScreenIsInThePreferenceXml() {
        assertTrue(preferenceKeys.size > keysWithoutAScreen.size)

        val missing = preferenceKeys.filterValues { it !in keysWithoutAScreen && it !in xmlPreferences }

        assertEquals(emptyMap<String, String>(), missing)
    }

    @Test
    fun xmlDefaultsMatchTheRepositoryDefaults() {
        val repositorySource = File(moduleDir, "src/main/java/me/magnum/melonds/impl/SharedPreferencesSettingsRepository.kt").readText()
        // Constant name to the defaults the repository reads it with (literal booleans, ints and strings only)
        val repositoryDefaults = Regex("""get(?:Boolean|Int|String)\(PreferenceKeys\.(\w+),\s*(true|false|-?\d+|"[^"]*")\)""")
            .findAll(repositorySource)
            .groupBy({ it.groupValues[1] }, { it.groupValues[2].trim('"') })
            .mapValues { (_, defaults) -> defaults.toSet() }
        assertTrue(repositoryDefaults.isNotEmpty())

        repositoryDefaults.forEach { (constant, defaults) ->
            val key = preferenceKeys.getValue(constant)
            assertEquals("Defaults of $key in the repository", 1, defaults.size)
            assertEquals("Default of $key", defaults.single(), xmlPreferences[key])
        }
    }
}
