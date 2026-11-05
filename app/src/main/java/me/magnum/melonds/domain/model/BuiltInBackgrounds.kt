package me.magnum.melonds.domain.model

import java.util.UUID

/**
 * A background that ships with the app, as an SVG file in the assets' [BuiltInBackgrounds.ASSET_FOLDER] folder.
 */
data class BuiltInBackground(val fileName: String, val name: String, val id: UUID)

/**
 * The backgrounds every user has without importing anything. They can't be deleted or changed, and aren't stored with the user's backgrounds.
 */
object BuiltInBackgrounds {
    const val ASSET_FOLDER = "backgrounds"

    // Layouts refer to backgrounds by ID, so changing this or a file name would make layouts lose their built-in background
    private const val ID_PREFIX = "melonade-builtin-background:"

    /**
     * All of them, in the order they're listed in.
     */
    val all: List<BuiltInBackground> = listOf(
        "crimson.svg",
        "onyx.svg",
        "pearl.svg",
        "pearl-tech.svg",
        "atomic-purple.svg",
        "cobalt.svg",
        "glacier.svg",
        "kiwi.svg",
        "dandelion.svg",
        "gunmetal.svg",
        "coral.svg",
        "smoke.svg",
        "carbon.svg",
        "hex.svg",
    ).map { BuiltInBackground(it, displayName(it), stableId(it)) }

    private val ids = all.mapTo(HashSet()) { it.id }

    fun isBuiltIn(id: UUID?): Boolean {
        return id != null && id in ids
    }

    /**
     * The built-in backgrounds followed by the user's. A user background with a built-in's ID is left out, so an ID always means one background.
     */
    fun <T> merge(builtInBackgrounds: List<T>, userBackgrounds: List<T>, idOf: (T) -> UUID?): List<T> {
        return builtInBackgrounds + userBackgrounds.filterNot { isBuiltIn(idOf(it)) }
    }

    /**
     * The name shown for a file: pearl-tech.svg is Pearl Tech.
     */
    internal fun displayName(fileName: String): String {
        return fileName.removeSuffix(".svg").split('-').joinToString(" ") { word -> word.replaceFirstChar { it.uppercaseChar() } }
    }

    /**
     * An ID that's the same on every install, derived from the file's name.
     */
    internal fun stableId(fileName: String): UUID {
        return UUID.nameUUIDFromBytes("$ID_PREFIX$fileName".toByteArray(Charsets.UTF_8))
    }
}
