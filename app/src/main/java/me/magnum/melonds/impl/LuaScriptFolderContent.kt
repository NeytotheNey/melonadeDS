package me.magnum.melonds.impl

/**
 * Rules for the modules (`require`) and PNG images (`melon.image.load`) a linked folder offers its scripts, kept free of Android so they can be
 * unit tested.
 */
object LuaScriptFolderContent {

    const val MAX_DEPTH = 4
    const val MAX_FOLDERS = 64
    // Modules and images the walk finds before it gives up, so a folder with thousands of files can't hold up the launch
    const val MAX_FOUND_FILES = 4096

    private const val MEGA = 1024 * 1024

    const val MAX_MODULE_SIZE = MEGA
    const val MAX_MODULES_SIZE = 2 * MEGA
    const val MAX_MODULE_COUNT = 256

    const val MAX_IMAGE_SIDE = 1024
    const val MAX_IMAGE_COUNT = 128
    const val MAX_IMAGE_PIXELS = 4 * MEGA

    // Shown after "module 'x' wasn't loaded: " and "image 'x' wasn't loaded: "
    const val REASON_MODULE_TOO_LARGE = "modules are limited to ${MAX_MODULE_SIZE / MEGA} MB each"
    const val REASON_MODULES_TOO_LARGE = "modules are limited to ${MAX_MODULES_SIZE / MEGA} MB in total"
    const val REASON_TOO_MANY_MODULES = "modules are limited to $MAX_MODULE_COUNT files"
    const val REASON_IMAGE_TOO_LARGE = "images are limited to ${MAX_IMAGE_SIDE}x$MAX_IMAGE_SIDE pixels each"
    const val REASON_IMAGES_TOO_LARGE = "images are limited to ${MAX_IMAGE_PIXELS / MEGA}M pixels in total"
    const val REASON_TOO_MANY_IMAGES = "images are limited to $MAX_IMAGE_COUNT files"
    const val REASON_UNREADABLE = "the file couldn't be read"
    const val REASON_NOT_AN_IMAGE = "the file couldn't be read as an image"

    const val LUA_EXTENSION = ".lua"
    private const val PNG_EXTENSION = ".png"
    private const val INIT_MODULE = "init"
    private val NAME_PART = Regex("[A-Za-z0-9_-]+")

    /**
     * How scripts, modules and images are ordered by name: case-insensitive, then by exact name for ties
     */
    val NAME_ORDER: Comparator<String> = compareBy(String.CASE_INSENSITIVE_ORDER) { name: String -> name }.thenBy { it }

    /**
     * An entry of a folder listing, with whatever [handle] the lister needs to list or read it later
     */
    class Child<T>(val name: String, val isFolder: Boolean, val size: Long?, val handle: T)

    /**
     * A module or image found by [walk], with its [path] relative to the linked folder and `/` between parts
     */
    class FoundFile<T>(val path: String, val size: Long?, val handle: T)

    class Module<T>(val name: String, val file: FoundFile<T>)

    /**
     * What [walk] found, in path order, with [gaveUp] set when a cap stopped it early
     */
    class WalkResult<T>(val modules: List<Module<T>>, val images: List<FoundFile<T>>, val gaveUp: Boolean)

    fun isNamePartAllowed(part: String): Boolean {
        return NAME_PART.matches(part)
    }

    /**
     * Whether [fileName] is a script or module file
     */
    fun isLuaScriptName(fileName: String): Boolean {
        return fileName.endsWith(LUA_EXTENSION, ignoreCase = true)
    }

    /**
     * The module name `require` uses for [path] (`lib/util.lua` is `lib.util`, `lib/init.lua` is `lib`), or null for a top-level script or a
     * path part outside `[A-Za-z0-9_-]`
     */
    fun getModuleName(path: String): String? {
        val parts = path.split('/')
        if (parts.size < 2) {
            return null
        }
        val folders = parts.dropLast(1)
        val baseName = removeExtension(parts.last(), LUA_EXTENSION) ?: return null
        if (!folders.all { isNamePartAllowed(it) } || !isNamePartAllowed(baseName)) {
            return null
        }
        return if (baseName == INIT_MODULE) folders.joinToString(".") else (folders + baseName).joinToString(".")
    }

    /**
     * Whether the file at [path] is an image scripts can load
     */
    fun isImagePath(path: String): Boolean {
        val parts = path.split('/')
        val baseName = removeExtension(parts.last(), PNG_EXTENSION) ?: return false
        return parts.dropLast(1).all { isNamePartAllowed(it) } && isNamePartAllowed(baseName)
    }

    private fun removeExtension(fileName: String, extension: String): String? {
        if (!fileName.endsWith(extension, ignoreCase = true)) {
            return null
        }
        return fileName.dropLast(extension.length)
    }

    /**
     * Walks breadth first from [root], at most [MAX_DEPTH] deep and [MAX_FOLDERS] folders; a folder [listChildren] can't read is skipped, an
     * unreadable root returns null
     */
    fun <T> walk(root: T, listChildren: (T) -> List<Child<T>>?): WalkResult<T>? {
        val rootChildren = listChildren(root) ?: return null

        val modules = mutableListOf<Module<T>>()
        val images = mutableListOf<FoundFile<T>>()
        // Folders waiting to be listed, with their path and depth (1 for the root's subfolders)
        val folders = ArrayDeque<Triple<T, String, Int>>()
        var listedFolderCount = 0
        var gaveUp = false

        fun addChildren(children: List<Child<T>>, folderPath: String, depth: Int) {
            // Some providers list two documents with the same name, and only the first one counts
            for (child in children.distinctBy { it.name }.sortedWith(compareBy(NAME_ORDER) { it.name })) {
                val path = if (folderPath.isEmpty()) child.name else "$folderPath/${child.name}"
                if (child.isFolder) {
                    if (depth < MAX_DEPTH && isNamePartAllowed(child.name)) {
                        folders.addLast(Triple(child.handle, path, depth + 1))
                    }
                    continue
                }

                val moduleName = if (depth > 0) getModuleName(path) else null
                val isImage = moduleName == null && isImagePath(path)
                if (moduleName == null && !isImage) {
                    continue
                }
                if (modules.size + images.size >= MAX_FOUND_FILES) {
                    gaveUp = true
                    return
                }
                val file = FoundFile(path, child.size, child.handle)
                if (moduleName != null) {
                    modules.add(Module(moduleName, file))
                } else {
                    images.add(file)
                }
            }
        }

        addChildren(rootChildren, "", 0)
        while (!gaveUp && folders.isNotEmpty()) {
            if (listedFolderCount >= MAX_FOLDERS) {
                gaveUp = true
                break
            }
            val (folder, path, depth) = folders.removeFirst()
            listedFolderCount++
            listChildren(folder)?.let { addChildren(it, path, depth) }
        }

        return WalkResult(
            modules = pickModules(modules).sortedWith(compareBy(NAME_ORDER) { it.file.path }),
            images = images.sortedWith(compareBy(NAME_ORDER) { it.path }),
            gaveUp = gaveUp,
        )
    }

    /**
     * One file per module name: `x.lua` wins over `x/init.lua` like in Lua's own search order, otherwise the first in path order wins
     */
    private fun <T> pickModules(modules: List<Module<T>>): List<Module<T>> {
        val order = compareBy<Module<T>> { removeExtension(it.file.path.substringAfterLast('/'), LUA_EXTENSION) == INIT_MODULE }
            .thenBy(NAME_ORDER) { it.file.path }
        return modules.sortedWith(order).distinctBy { it.name }
    }

    /**
     * Takes modules in path order until a cap is hit; after the first one that doesn't fit, every later one is left out
     */
    class ModuleBudget {
        private var consideredCount = 0
        private var bytesLeft = MAX_MODULES_SIZE
        private var isFull = false

        /**
         * Why the next module is left out before reading it (given its [listedSize] from the folder listing, if known), or null to read it
         */
        fun checkBeforeRead(listedSize: Long?): String? {
            if (consideredCount >= MAX_MODULE_COUNT) {
                return REASON_TOO_MANY_MODULES
            }
            consideredCount++
            return when {
                listedSize != null && listedSize > MAX_MODULE_SIZE -> REASON_MODULE_TOO_LARGE
                isFull -> REASON_MODULES_TOO_LARGE
                else -> null
            }
        }

        /**
         * The most bytes to read of a module: one past what could still be taken, so a module that doesn't fit is never read whole
         */
        val readLimit: Int get() = minOf(MAX_MODULE_SIZE, bytesLeft) + 1

        /**
         * Takes a module of [size] bytes (read with [readLimit]), returning null if it fits or why it doesn't
         */
        fun take(size: Int): String? {
            if (size <= MAX_MODULE_SIZE && size <= bytesLeft) {
                bytesLeft -= size
                return null
            }

            isFull = true
            return if (size > MAX_MODULE_SIZE) REASON_MODULE_TOO_LARGE else REASON_MODULES_TOO_LARGE
        }
    }

    /**
     * Takes images in path order until a cap is hit, like [ModuleBudget], with [checkBeforeDecode] before the size is read and [take] after
     */
    class ImageBudget {
        private var consideredCount = 0
        private var pixelsLeft = MAX_IMAGE_PIXELS
        private var isFull = false

        fun checkBeforeDecode(): String? {
            if (consideredCount >= MAX_IMAGE_COUNT) {
                return REASON_TOO_MANY_IMAGES
            }
            consideredCount++
            return if (isFull) REASON_IMAGES_TOO_LARGE else null
        }

        /**
         * Takes an image of [width] x [height] pixels, returning null if it fits or why it doesn't
         */
        fun take(width: Int, height: Int): String? {
            if (width > MAX_IMAGE_SIDE || height > MAX_IMAGE_SIDE) {
                return REASON_IMAGE_TOO_LARGE
            }
            val pixelCount = width * height
            if (pixelCount > pixelsLeft) {
                isFull = true
                return REASON_IMAGES_TOO_LARGE
            }
            pixelsLeft -= pixelCount
            return null
        }
    }
}
