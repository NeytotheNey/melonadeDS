package me.magnum.melonds.impl

import me.magnum.melonds.impl.LuaScriptFolderContent.Child
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LuaScriptFolderContentTest {

    /**
     * A folder tree for [LuaScriptFolderContent.walk]: each folder path ("" is the root) maps to its children's names, folders ending in "/"
     */
    private class FakeTree(private val folders: Map<String, List<String>>, private val unreadable: Set<String> = emptySet()) {
        val listed = mutableListOf<String>()

        fun list(path: String): List<Child<String>>? {
            listed.add(path)
            if (path in unreadable) {
                return null
            }
            return folders[path]?.map { name ->
                val isFolder = name.endsWith("/")
                val childName = name.removeSuffix("/")
                Child(childName, isFolder, size = 10L, handle = if (path.isEmpty()) childName else "$path/$childName")
            }
        }
    }

    private fun walk(tree: FakeTree) = LuaScriptFolderContent.walk("", tree::list)!!

    @Test
    fun moduleNamesComeFromThePath() {
        assertEquals("lib.util", LuaScriptFolderContent.getModuleName("lib/util.lua"))
        assertEquals("lib", LuaScriptFolderContent.getModuleName("lib/init.lua"))
        assertEquals("a.b.c", LuaScriptFolderContent.getModuleName("a/b/c.LUA"))
        assertEquals("a.b", LuaScriptFolderContent.getModuleName("a/b/init.lua"))
        assertEquals("my-lib.x_2", LuaScriptFolderContent.getModuleName("my-lib/x_2.lua"))
    }

    @Test
    fun topLevelFilesAndOddNamesAreNotModules() {
        assertNull(LuaScriptFolderContent.getModuleName("util.lua"))
        assertNull(LuaScriptFolderContent.getModuleName("lib/my util.lua"))
        assertNull(LuaScriptFolderContent.getModuleName("my lib/util.lua"))
        assertNull(LuaScriptFolderContent.getModuleName("lib/util.v2.lua"))
        assertNull(LuaScriptFolderContent.getModuleName("lib/.lua"))
        assertNull(LuaScriptFolderContent.getModuleName("lib/util.txt"))
        assertNull(LuaScriptFolderContent.getModuleName("lib/ütil.lua"))
    }

    @Test
    fun imagePaths() {
        assertTrue(LuaScriptFolderContent.isImagePath("hud.png"))
        assertTrue(LuaScriptFolderContent.isImagePath("img/hud.PNG"))
        assertFalse(LuaScriptFolderContent.isImagePath("img/hud icon.png"))
        assertFalse(LuaScriptFolderContent.isImagePath("my img/hud.png"))
        assertFalse(LuaScriptFolderContent.isImagePath("img/hud.jpg"))
    }

    @Test
    fun walkFindsModulesInSubfoldersAndImagesEverywhere() {
        val tree = FakeTree(
            mapOf(
                "" to listOf("main.lua", "hud.png", "lib/", "img/", "notes.txt"),
                "lib" to listOf("util.lua", "init.lua", "deep/"),
                "lib/deep" to listOf("thing.lua"),
                "img" to listOf("a.png", "b.png"),
            )
        )

        val result = walk(tree)

        assertEquals(
            listOf("lib.deep.thing" to "lib/deep/thing.lua", "lib" to "lib/init.lua", "lib.util" to "lib/util.lua"),
            result.modules.map { it.name to it.file.path },
        )
        assertEquals(listOf("hud.png", "img/a.png", "img/b.png"), result.images.map { it.path })
        assertFalse(result.gaveUp)
    }

    @Test
    fun walkSkipsFoldersWithOddNames() {
        val tree = FakeTree(
            mapOf(
                "" to listOf("my lib/", ".git/", "lib/"),
                "my lib" to listOf("util.lua"),
                ".git" to listOf("config.lua"),
                "lib" to listOf("util.lua"),
            )
        )

        val result = walk(tree)

        assertEquals(listOf("lib.util"), result.modules.map { it.name })
        assertEquals(listOf("", "lib"), tree.listed)
    }

    @Test
    fun walkStopsAtFourLevels() {
        val tree = FakeTree(
            mapOf(
                "" to listOf("a/"),
                "a" to listOf("b/", "one.lua"),
                "a/b" to listOf("c/"),
                "a/b/c" to listOf("d/"),
                "a/b/c/d" to listOf("e/", "four.lua"),
                "a/b/c/d/e" to listOf("five.lua"),
            )
        )

        val result = walk(tree)

        assertEquals(listOf("a.b.c.d.four", "a.one"), result.modules.map { it.name })
        assertFalse("a/b/c/d/e" in tree.listed)
    }

    @Test
    fun walkGivesUpAfter64Folders() {
        val folders = (1..100).map { "f$it/" }
        val tree = FakeTree(mapOf("" to folders) + (1..100).associate { "f$it" to listOf("m.lua") })

        val result = walk(tree)

        assertTrue(result.gaveUp)
        assertEquals(LuaScriptFolderContent.MAX_FOLDERS + 1, tree.listed.size)
        assertEquals(LuaScriptFolderContent.MAX_FOLDERS, result.modules.size)
    }

    @Test
    fun walkGivesUpAfterTooManyFiles() {
        val tree = FakeTree(mapOf("" to (1..5000).map { "i$it.png" }))

        val result = walk(tree)

        assertTrue(result.gaveUp)
        assertEquals(LuaScriptFolderContent.MAX_FOUND_FILES, result.images.size)
    }

    @Test
    fun unreadableRootIsNull() {
        assertNull(LuaScriptFolderContent.walk("", FakeTree(emptyMap(), unreadable = setOf(""))::list))
    }

    @Test
    fun unreadableSubfolderIsSkipped() {
        val tree = FakeTree(mapOf("" to listOf("a/", "b/"), "b" to listOf("x.lua")), unreadable = setOf("a"))

        assertEquals(listOf("b.x"), walk(tree).modules.map { it.name })
    }

    @Test
    fun plainModuleWinsOverInitModule() {
        val tree = FakeTree(mapOf("" to listOf("a/"), "a" to listOf("x/", "x.lua"), "a/x" to listOf("init.lua")))

        assertEquals(listOf("a.x" to "a/x.lua"), walk(tree).modules.map { it.name to it.file.path })
    }

    @Test
    fun sameNameListedTwiceIsOneFile() {
        val tree = FakeTree(mapOf("" to listOf("a/", "a/"), "a" to listOf("x.lua", "x.lua")))

        assertEquals(listOf("a.x"), walk(tree).modules.map { it.name })
        assertEquals(listOf("", "a"), tree.listed)
    }

    /**
     * File contents for [LuaScriptFolderContent.listScripts] by path, with the paths it read in [read]; a path not in [files] can't be read
     */
    private class FakeFiles(private val files: Map<String, String>) {
        val read = mutableListOf<String>()

        fun readStart(file: LuaScriptFolderContent.FoundFile<String>): ByteArray? {
            read.add(file.path)
            return files[file.handle]?.encodeToByteArray()
        }
    }

    private val modTree = FakeTree(
        mapOf(
            "" to listOf("aim.lua", "hud.png", "pokemon-battle-info/"),
            "pokemon-battle-info" to listOf("battle_info.lua", "pbi/", "img/"),
            "pokemon-battle-info/pbi" to listOf("ui.lua"),
            "pokemon-battle-info/img" to listOf("icons.png"),
        )
    )
    private val modFiles = FakeFiles(
        mapOf(
            "pokemon-battle-info/battle_info.lua" to "-- description: Shows battle info\nlocal ui = require(\"pbi.ui\")\n",
            "pokemon-battle-info/pbi/ui.lua" to "-- UI for battle_info.lua\nlocal M = {}\nreturn M\n",
        )
    )

    @Test
    fun nestedModListsOnlyItsScript() {
        val listing = LuaScriptFolderContent.listScripts("", modTree::list, modFiles::readStart)!!

        assertEquals(listOf("aim.lua", "pokemon-battle-info/battle_info.lua"), listing.scripts.map { it.path })
        assertTrue(listing.isComplete)
        // Top-level files are scripts without being read
        assertEquals(listOf("pokemon-battle-info/battle_info.lua", "pokemon-battle-info/pbi/ui.lua"), modFiles.read)
    }

    @Test
    fun nestedModsModuleAndImageHaveTheirFullNames() {
        val result = LuaScriptFolderContent.walk("", modTree::list, setOf("aim.lua", "pokemon-battle-info/battle_info.lua"))!!

        assertEquals(listOf("pokemon-battle-info.pbi.ui" to "pokemon-battle-info/pbi/ui.lua"), result.modules.map { it.name to it.file.path })
        assertEquals(listOf("hud.png", "pokemon-battle-info/img/icons.png"), result.images.map { it.path })
        assertEquals(listOf("aim.lua"), result.topLevelScripts.map { it.path })
        assertEquals(listOf("pokemon-battle-info/battle_info.lua", "pokemon-battle-info/pbi/ui.lua"), result.scriptCandidates.map { it.path })
    }

    @Test
    fun knownScriptPathsAreNotModules() {
        assertEquals(
            listOf("pokemon-battle-info.battle_info", "pokemon-battle-info.pbi.ui"),
            walk(modTree).modules.map { it.name },
        )
        assertEquals(
            listOf("pokemon-battle-info.pbi.ui"),
            LuaScriptFolderContent.walk("", modTree::list, setOf("pokemon-battle-info/battle_info.lua"))!!.modules.map { it.name },
        )
    }

    @Test
    fun knownScriptsDontCountTowardTheFileCap() {
        // "main.lua" comes after every "i<n>.png" in name order
        val tree = FakeTree(mapOf("" to listOf("mod/"), "mod" to listOf("main.lua") + (1..LuaScriptFolderContent.MAX_FOUND_FILES).map { "i$it.png" }))

        val asScript = LuaScriptFolderContent.walk("", tree::list, setOf("mod/main.lua"))!!
        assertFalse(asScript.gaveUp)
        assertEquals(LuaScriptFolderContent.MAX_FOUND_FILES, asScript.images.size)
        assertTrue(asScript.modules.isEmpty())

        assertTrue(walk(tree).gaveUp)
    }

    @Test
    fun subfolderScriptNameCanBeAnything() {
        val tree = FakeTree(mapOf("" to listOf("mod/"), "mod" to listOf("My Script.v2.LUA")))
        val files = FakeFiles(mapOf("mod/My Script.v2.LUA" to "-- description: x\n"))

        assertEquals(listOf("mod/My Script.v2.LUA"), LuaScriptFolderContent.listScripts("", tree::list, files::readStart)!!.scripts.map { it.path })
    }

    @Test
    fun flatFolderListsItsTopLevelScriptsWithoutReadingThem() {
        val tree = FakeTree(mapOf("" to listOf("b.lua", "A.lua", "notes.txt", "hud.png", "x.LUA")))
        val files = FakeFiles(emptyMap())

        val listing = LuaScriptFolderContent.listScripts("", tree::list, files::readStart)!!

        assertEquals(listOf("A.lua", "b.lua", "x.LUA"), listing.scripts.map { it.path })
        assertTrue(listing.isComplete)
        assertTrue(files.read.isEmpty())
    }

    @Test
    fun listingChecksAtMost256SubfolderFiles() {
        val names = (1..300).map { "s" + it.toString().padStart(3, '0') + ".lua" }
        val tree = FakeTree(mapOf("" to listOf("mods/"), "mods" to names))
        val files = FakeFiles(names.associate { "mods/$it" to "-- description: $it\n" })

        val listing = LuaScriptFolderContent.listScripts("", tree::list, files::readStart)!!

        assertEquals(LuaScriptFolderContent.MAX_SUBFOLDER_SCRIPT_CANDIDATES, listing.scripts.size)
        assertEquals("mods/s256.lua", listing.scripts.last().path)
        assertEquals(LuaScriptFolderContent.MAX_SUBFOLDER_SCRIPT_CANDIDATES, files.read.size)
        assertFalse(listing.isComplete)
    }

    @Test
    fun listingIsIncompleteWhenAFirstLineCantBeRead() {
        val tree = FakeTree(mapOf("" to listOf("mod/"), "mod" to listOf("a.lua", "b.lua")))
        val files = FakeFiles(mapOf("mod/b.lua" to "-- description: b\n"))

        val listing = LuaScriptFolderContent.listScripts("", tree::list, files::readStart)!!

        assertEquals(listOf("mod/b.lua"), listing.scripts.map { it.path })
        assertFalse(listing.isComplete)
    }

    @Test
    fun listingIsIncompleteWhenASubfolderCantBeListed() {
        val tree = FakeTree(mapOf("" to listOf("aim.lua", "a/", "b/"), "b" to listOf("x.lua")), unreadable = setOf("a"))
        val files = FakeFiles(mapOf("b/x.lua" to "-- description: x\n"))

        val listing = LuaScriptFolderContent.listScripts("", tree::list, files::readStart)!!

        assertEquals(listOf("aim.lua", "b/x.lua"), listing.scripts.map { it.path })
        assertFalse(listing.isComplete)
    }

    @Test
    fun listingIsIncompleteWhenTheWalkGivesUp() {
        val tree = FakeTree(mapOf("" to (1..100).map { "f$it/" }))

        assertFalse(LuaScriptFolderContent.listScripts("", tree::list, FakeFiles(emptyMap())::readStart)!!.isComplete)
    }

    @Test
    fun listingOfAnUnreadableRootIsNull() {
        val tree = FakeTree(emptyMap(), unreadable = setOf(""))

        assertNull(LuaScriptFolderContent.listScripts("", tree::list, FakeFiles(emptyMap())::readStart))
    }

    @Test
    fun topLevelScriptsAreListedPastTheFileCap() {
        val tree = FakeTree(mapOf("" to (1..5000).map { "i$it.png" } + "z.lua"))

        val listing = LuaScriptFolderContent.listScripts("", tree::list, FakeFiles(emptyMap())::readStart)!!

        assertEquals(listOf("z.lua"), listing.scripts.map { it.path })
        assertFalse(listing.isComplete)
    }

    @Test
    fun moduleBudgetLeavesOutLargeFilesAndStopsAtTheTotal() {
        val budget = LuaScriptFolderContent.ModuleBudget()
        val mb = 1024 * 1024

        // Too large by its listed size: not read, and the others still fit
        assertEquals(LuaScriptFolderContent.REASON_MODULE_TOO_LARGE, budget.checkBeforeRead(mb + 1L))
        assertNull(budget.checkBeforeRead(mb.toLong()))
        assertEquals(mb + 1, budget.readLimit)
        assertNull(budget.take(mb))
        assertNull(budget.checkBeforeRead(null))
        assertNull(budget.take(mb - 10))
        // 10 bytes left: a read of 11 means it doesn't fit, and every later module is left out
        assertEquals(11, budget.readLimit)
        assertNull(budget.checkBeforeRead(20))
        assertEquals(LuaScriptFolderContent.REASON_MODULES_TOO_LARGE, budget.take(11))
        assertEquals(LuaScriptFolderContent.REASON_MODULES_TOO_LARGE, budget.checkBeforeRead(1))
    }

    @Test
    fun moduleBudgetTakesAt256Files() {
        val budget = LuaScriptFolderContent.ModuleBudget()
        repeat(LuaScriptFolderContent.MAX_MODULE_COUNT) {
            assertNull(budget.checkBeforeRead(1))
            assertNull(budget.take(1))
        }
        assertEquals(LuaScriptFolderContent.REASON_TOO_MANY_MODULES, budget.checkBeforeRead(1))
    }

    @Test
    fun moduleOverOneMegabyteFoundByReadingIsTooLarge() {
        val budget = LuaScriptFolderContent.ModuleBudget()
        assertNull(budget.checkBeforeRead(null))
        assertEquals(LuaScriptFolderContent.REASON_MODULE_TOO_LARGE, budget.take(budget.readLimit))
    }

    @Test
    fun imageBudgetCaps() {
        val budget = LuaScriptFolderContent.ImageBudget()

        assertNull(budget.checkBeforeDecode())
        assertEquals(LuaScriptFolderContent.REASON_IMAGE_TOO_LARGE, budget.take(1025, 1))
        repeat(4) {
            assertNull(budget.checkBeforeDecode())
            assertNull(budget.take(1024, 1024))
        }
        assertNull(budget.checkBeforeDecode())
        assertEquals(LuaScriptFolderContent.REASON_IMAGES_TOO_LARGE, budget.take(1, 1))
        assertEquals(LuaScriptFolderContent.REASON_IMAGES_TOO_LARGE, budget.checkBeforeDecode())
    }

    @Test
    fun imageBudgetTakesAt128Files() {
        val budget = LuaScriptFolderContent.ImageBudget()
        repeat(LuaScriptFolderContent.MAX_IMAGE_COUNT) {
            assertNull(budget.checkBeforeDecode())
            assertNull(budget.take(1, 1))
        }
        assertEquals(LuaScriptFolderContent.REASON_TOO_MANY_IMAGES, budget.checkBeforeDecode())
    }
}
