package me.magnum.melonds.impl

/**
 * The rules for what the scripts of a linked folder can use besides themselves: Lua modules for `require` and PNG images for
 * `melon.image.load`. Kept apart from [LuaScriptStorage] so they can be tested without a device.
 *
 * The folder is walked at launch, breadth first, at most [MAX_DEPTH] levels of subfolders deep and through at most [MAX_FOLDERS] subfolders,
 * with one listing per folder. Every part of a path (folder names, and file names without their extension) must be made of
 * `[A-Za-z0-9_-]`, or that folder or file is skipped. Modules are the `.lua` files in subfolders (the top-level ones are the scripts):
 * `lib/util.lua` is `lib.util` and `lib/init.lua` is `lib`. Images are the `.png` files anywhere in the tree, named by their path
 * (`img/hud.png`). Both are taken in path order until a cap is reached, and the ones left out keep the reason so the script's error can say
 * why.
 */
object LuaScriptFolderContent {

    const val MAX_DEPTH = 4
    const val MAX_FOLDERS = 64
    // Modules and images found by the walk. Past this the walk gives up, so a folder with thousands of files can't hold up the launch
    const val MAX_FOUND_FILES = 4096

    const val MAX_MODULE_SIZE = 1024 * 1024
    const val MAX_MODULES_SIZE = 2 * 1024 * 1024
    const val MAX_MODULE_COUNT = 256

    const val MAX_IMAGE_SIDE = 1024
    const val MAX_IMAGE_COUNT = 128
    const val MAX_IMAGE_PIXELS = 4 * 1024 * 1024

    // Shown after "module 'x' wasn't loaded: " and "image 'x' wasn't loaded: "
    const val REASON_MODULE_TOO_LARGE = "modules are limited to 1 MB each"
    const val REASON_MODULES_TOO_LARGE = "modules are limited to 2 MB in total"
    const val REASON_TOO_MANY_MODULES = "modules are limited to 256 files"
    const val REASON_IMAGE_TOO_LARGE = "images are limited to 1024x1024 pixels each"
    const val REASON_IMAGES_TOO_LARGE = "images are limited to 4M pixels in total"
    const val REASON_TOO_MANY_IMAGES = "images are limited to 128 files"
    const val REASON_UNREADABLE = "the file couldn't be read"
    const val REASON_NOT_AN_IMAGE = "the file couldn't be read as an image"

    private const val LUA_EXTENSION = ".lua"
    private const val PNG_EXTENSION = ".png"
    private const val INIT_MODULE = "init"
    private val NAME_PART = Regex("[A-Za-z0-9_-]+")
    private val NAME_ORDER = compareBy(String.CASE_INSENSITIVE_ORDER) { name: String -> name }.thenBy { it }

    /**
     * An entry of a folder listing. [handle] is whatever the lister needs to list or read it later
     */
    class Child<T>(val name: String, val isFolder: Boolean, val size: Long?, val handle: T)

    /**
     * A module or image found by [walk]. [path] is relative to the linked folder, with `/` between parts
     */
    class FoundFile<T>(val path: String, val size: Long?, val handle: T)

    class Module<T>(val name: String, val file: FoundFile<T>)

    /**
     * What [walk] found, in path order. [gaveUp] is set when a cap stopped it early
     */
    class WalkResult<T>(val modules: List<Module<T>>, val images: List<FoundFile<T>>, val gaveUp: Boolean)

    fun isNamePartAllowed(part: String): Boolean {
        return NAME_PART.matches(part)
    }

    /**
     * The module name `require` knows the file at [path] by, or null if it isn't a module: top-level files are scripts, and files with a
     * path part outside `[A-Za-z0-9_-]` are skipped
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
     * Walks the folder tree from [root]. [listChildren] lists a folder, or returns null if it can't be read (that folder is then skipped).
     * Returns null if the root itself can't be read
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
            // Some providers list two documents with the same name. Only the first one counts
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
     * One file per module name. `x.lua` wins over `x/init.lua`, like Lua's own search order, and otherwise the first in path order wins
     * (`x.lua` and `x.LUA`, say)
     */
    private fun <T> pickModules(modules: List<Module<T>>): List<Module<T>> {
        val order = compareBy<Module<T>> { removeExtension(it.file.path.substringAfterLast('/'), LUA_EXTENSION) == INIT_MODULE }
            .thenBy(NAME_ORDER) { it.file.path }
        return modules.sortedWith(order).distinctBy { it.name }
    }

    /**
     * Takes modules in path order until a cap is reached. For each module, [checkBeforeRead] says whether it's left out without reading it;
     * otherwise it's read with at most [readLimit] bytes and given to [take]. Once a module doesn't fit, every later one is left out too, so
     * the reads stop at about the total cap
     */
    class ModuleBudget {
        private var consideredCount = 0
        private var bytesLeft = MAX_MODULES_SIZE
        private var isFull = false

        /**
         * Why the next module is left out before reading it, or null to read it. [listedSize] is its size from the folder listing, if known
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
         * Takes a module of [size] bytes (read with [readLimit]). Returns null if it fits, or why it doesn't
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
     * Takes images in path order until a cap is reached, like [ModuleBudget]. [checkBeforeDecode] runs before the image's size is read, and
     * [take] once it's known
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
         * Takes an image of [width] x [height] pixels. Returns null if it fits, or why it doesn't
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
