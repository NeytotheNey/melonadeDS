package me.magnum.melonds.impl

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.provider.DocumentsContract
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import me.magnum.melonds.domain.model.emulator.LuaScriptHeader
import me.magnum.melonds.domain.model.emulator.LuaScriptImage
import me.magnum.melonds.domain.model.emulator.LuaScriptModule
import me.magnum.melonds.impl.diagnostics.LogTags
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream

/**
 * Logcat tag of the Kotlin side of Lua scripting, shared with the native side
 */
internal const val LUA_LOG_TAG = LogTags.LUA

/**
 * Reads each game's Lua scripts from its linked folder (see RomModsConfig.luaScriptFolder); the app never writes there and native never opens
 * script files.
 */
class LuaScriptStorage(private val context: Context) {

    /**
     * A script in a linked folder, [name] being its path in it (`aim.lua`, `my-mod/main.lua`)
     */
    class ScriptFile(val name: String, val uri: Uri)

    /**
     * The scripts in a linked folder, in path order, with [isComplete] false when some could be missing (see [LuaScriptFolderSync.sync])
     */
    class ScriptListing(val scripts: List<ScriptFile>, val isComplete: Boolean)

    sealed class ReadResult {
        class Success(val source: ByteArray) : ReadResult()
        data object FolderNotFound : ReadResult()
        data object FileNotFound : ReadResult()
        data object TooLarge : ReadResult()
        data object Failed : ReadResult()
    }

    /**
     * The folder's scripts (rules in [LuaScriptFolderContent.listScripts]), or null when the folder can't be read (lost permission, moved,
     * missing card, cloud still fetching); it walks the subfolders and reads the start of their `.lua` files, so never call it on the main
     * thread
     */
    fun listScripts(folderUri: Uri): ScriptListing? {
        return try {
            // Checked first because some providers list a missing folder as empty (and isDirectory is false once the permission is gone)
            val folder = DocumentFile.fromTreeUri(context, folderUri)?.takeIf { it.isDirectory } ?: return null
            val treeUri = folder.uri
            val listing = LuaScriptFolderContent.listScripts(
                DocumentsContract.getDocumentId(treeUri),
                { documentId -> listFolder(treeUri, documentId) },
                { file -> readDocument(DocumentsContract.buildDocumentUriUsingTree(treeUri, file.handle), LuaScriptHeaderParser.FIRST_LINE_READ_LIMIT) },
            ) ?: return null
            if (!listing.isComplete) {
                Log.w(LUA_LOG_TAG, "The script folder couldn't be listed whole: scripts in its subfolders that aren't listed are kept")
            }
            val scripts = listing.scripts.map { ScriptFile(it.path, DocumentsContract.buildDocumentUriUsingTree(treeUri, it.handle)) }
            ScriptListing(scripts, listing.isComplete)
        } catch (e: Exception) {
            Log.w(LUA_LOG_TAG, "Could not list the script folder", e)
            null
        }
    }

    /**
     * [listChildren] for a walk, where a folder that fails to list is skipped rather than ending the walk
     */
    private fun listFolder(treeUri: Uri, documentId: String): List<LuaScriptFolderContent.Child<String>>? {
        return try {
            listChildren(treeUri, documentId)
        } catch (e: Exception) {
            Log.w(LUA_LOG_TAG, "Could not list a folder in the script folder", e)
            null
        }
    }

    /**
     * Lists one folder of [treeUri] with a single query, using document IDs as handles; null when the provider says the list is incomplete
     */
    private fun listChildren(treeUri: Uri, documentId: String): List<LuaScriptFolderContent.Child<String>>? {
        // One query instead of DocumentFile's two calls per file, which are slow in a big folder
        val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, documentId)
        val projection = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE,
        )
        return context.contentResolver.query(childrenUri, projection, null, null, null)?.use { cursor ->
            // A partial or failed cloud listing isn't the folder's content, so it counts as unreadable and the script list is kept
            val extras = cursor.extras
            if (extras?.getBoolean(DocumentsContract.EXTRA_LOADING) == true || extras?.getString(DocumentsContract.EXTRA_ERROR) != null) {
                return null
            }
            buildList {
                while (cursor.moveToNext()) {
                    val childDocumentId = cursor.getString(0) ?: continue
                    val name = cursor.getString(1) ?: continue
                    val isFolder = cursor.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR
                    val size = if (cursor.isNull(3)) null else cursor.getLong(3)
                    add(LuaScriptFolderContent.Child(name, isFolder, size, childDocumentId))
                }
            }
        }
    }

    class FolderContent(val modules: List<LuaScriptModule>, val images: List<LuaScriptImage>)

    /**
     * Reads and decodes the scripts' modules and images (rules in [LuaScriptFolderContent]), or null if the folder can't be read; it lists up
     * to 65 folders and reads a few MB, so never call it on the main thread. [scriptPaths] are the game's scripts (its whole list), which
     * aren't modules.
     */
    fun readFolderContent(folderUri: Uri, scriptPaths: Set<String>): FolderContent? {
        return try {
            val folder = DocumentFile.fromTreeUri(context, folderUri)?.takeIf { it.isDirectory } ?: return null
            val treeUri = folder.uri
            val walk = LuaScriptFolderContent.walk(
                DocumentsContract.getDocumentId(treeUri),
                { documentId -> listFolder(treeUri, documentId) },
                scriptPaths,
            ) ?: return null
            if (walk.gaveUp) {
                Log.w(LUA_LOG_TAG, "The script folder has too many folders or files: the rest are left out")
            }

            val moduleBudget = LuaScriptFolderContent.ModuleBudget()
            val modules = walk.modules.map { module ->
                val file = module.file
                val reasonBeforeRead = moduleBudget.checkBeforeRead(file.size)
                if (reasonBeforeRead != null) {
                    return@map LuaScriptModule(module.name, file.path, null, reasonBeforeRead)
                }

                val source = readDocument(DocumentsContract.buildDocumentUriUsingTree(treeUri, file.handle), moduleBudget.readLimit)
                    ?: return@map LuaScriptModule(module.name, file.path, null, LuaScriptFolderContent.REASON_UNREADABLE)
                val reason = moduleBudget.take(source.size)
                LuaScriptModule(module.name, file.path, if (reason == null) source else null, reason)
            }

            val imageBudget = LuaScriptFolderContent.ImageBudget()
            val images = walk.images.map { file ->
                decodeImage(file.path, DocumentsContract.buildDocumentUriUsingTree(treeUri, file.handle), imageBudget)
            }

            FolderContent(modules, images)
        } catch (e: Exception) {
            Log.w(LUA_LOG_TAG, "Could not read the script folder's modules and images", e)
            null
        }
    }

    private fun readDocument(uri: Uri, limit: Int): ByteArray? {
        return try {
            context.contentResolver.openInputStream(uri)?.use { readAtMost(it, limit) }
        } catch (e: Exception) {
            Log.w(LUA_LOG_TAG, "Could not read a file in the script folder", e)
            null
        }
    }

    /**
     * Reads the image's size first, so one over the caps is never decoded, then decodes it without premultiplying
     */
    private fun decodeImage(path: String, uri: Uri, budget: LuaScriptFolderContent.ImageBudget): LuaScriptImage {
        fun leftOut(reason: String) = LuaScriptImage(path, 0, 0, null, reason)

        budget.checkBeforeDecode()?.let { return leftOut(it) }
        return try {
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            // Only fills in the bounds: with inJustDecodeBounds, decodeStream() always returns null
            val boundsStream = context.contentResolver.openInputStream(uri) ?: return leftOut(LuaScriptFolderContent.REASON_UNREADABLE)
            boundsStream.use { BitmapFactory.decodeStream(it, null, bounds) }
            val width = bounds.outWidth
            val height = bounds.outHeight
            if (width <= 0 || height <= 0) {
                return leftOut(LuaScriptFolderContent.REASON_NOT_AN_IMAGE)
            }
            budget.take(width, height)?.let { return leftOut(it) }

            val options = BitmapFactory.Options().apply {
                inPremultiplied = false
                inPreferredConfig = Bitmap.Config.ARGB_8888
                inScaled = false
            }
            val bitmap = context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, options) }
                ?: return leftOut(LuaScriptFolderContent.REASON_NOT_AN_IMAGE)
            try {
                // The file could have changed since its size was read, and the caps were checked against that size
                if (bitmap.width != width || bitmap.height != height) {
                    return leftOut(LuaScriptFolderContent.REASON_NOT_AN_IMAGE)
                }
                val pixels = IntArray(width * height)
                bitmap.getPixels(pixels, 0, width, 0, 0, width, height)
                LuaScriptImage(path, width, height, pixels, null)
            } finally {
                bitmap.recycle()
            }
        } catch (e: Exception) {
            Log.w(LUA_LOG_TAG, "Could not decode the image $path", e)
            leftOut(LuaScriptFolderContent.REASON_NOT_AN_IMAGE)
        } catch (e: OutOfMemoryError) {
            Log.w(LUA_LOG_TAG, "Not enough memory to decode the image $path", e)
            leftOut(LuaScriptFolderContent.REASON_NOT_AN_IMAGE)
        }
    }

    /**
     * Reads the given scripts in order from [folderUri], listing it once; with no folder every result is [ReadResult.FolderNotFound], and so
     * with an empty listing, which is taken as a hidden folder (see [LuaScriptFolderSync.sync]).
     */
    fun readScripts(folderUri: Uri?, fileNames: List<String>): List<Pair<String, ReadResult>> {
        val scriptFiles = folderUri?.let { listScripts(it) }?.scripts
            ?.takeUnless { listed -> LuaScriptFolderSync.isSuspiciouslyEmpty(fileNames.size, listed.map { it.name }) }
            ?.associateBy { it.name }
        return fileNames.map { fileName ->
            val result = if (scriptFiles == null) {
                ReadResult.FolderNotFound
            } else {
                scriptFiles[fileName]?.let { readScript(it) } ?: ReadResult.FileNotFound
            }
            if (result !is ReadResult.Success) {
                Log.w(LUA_LOG_TAG, "Skipping Lua script $fileName: $result")
            }
            fileName to result
        }
    }

    private fun readScript(scriptFile: ScriptFile): ReadResult {
        return try {
            // One byte over the limit is enough to know the file is too large, so a huge file is never read whole
            val content = context.contentResolver.openInputStream(scriptFile.uri)?.use { readAtMost(it, MAX_SCRIPT_SIZE + 1) } ?: return ReadResult.Failed
            if (content.size > MAX_SCRIPT_SIZE) ReadResult.TooLarge else ReadResult.Success(content)
        } catch (e: Exception) {
            Log.w(LUA_LOG_TAG, "Could not read Lua script ${scriptFile.name}", e)
            ReadResult.Failed
        }
    }

    /**
     * The script's header (see [LuaScriptHeader]) from the start of the file, or null if it can't be read
     */
    fun readHeader(scriptFile: ScriptFile): LuaScriptHeader? {
        return try {
            // One byte more than the header limit, so the parser knows the file goes on
            context.contentResolver.openInputStream(scriptFile.uri)?.use { LuaScriptHeaderParser.parse(readAtMost(it, LuaScriptHeaderParser.READ_LIMIT + 1)) }
        } catch (e: Exception) {
            null
        }
    }

    /**
     * Deletes the `files/scripts/` copies made before linked folders
     */
    fun deleteOldScriptCopies() {
        val oldScriptsFolder = File(context.filesDir, OLD_SCRIPTS_FOLDER)
        if (oldScriptsFolder.exists() && !oldScriptsFolder.deleteRecursively()) {
            Log.w(LUA_LOG_TAG, "Could not delete the old script copies")
        }
    }

    private fun readAtMost(inputStream: InputStream, limit: Int): ByteArray {
        val output = ByteArrayOutputStream()
        val buffer = ByteArray(8192)
        while (output.size() < limit) {
            val read = inputStream.read(buffer, 0, minOf(buffer.size, limit - output.size()))
            if (read < 0) {
                break
            }
            output.write(buffer, 0, read)
        }
        return output.toByteArray()
    }

    companion object {
        private const val OLD_SCRIPTS_FOLDER = "scripts"
        const val MAX_SCRIPT_SIZE = 1024 * 1024
    }
}
