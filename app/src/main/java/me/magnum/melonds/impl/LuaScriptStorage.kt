package me.magnum.melonds.impl

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.provider.DocumentsContract
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import me.magnum.melonds.domain.model.emulator.LuaScriptImage
import me.magnum.melonds.domain.model.emulator.LuaScriptModule
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream

/**
 * Reads the games' Lua scripts from the folder linked to each game (see RomModsConfig.luaScriptFolder). The app only ever reads that
 * folder: scripts are added and removed by the user, outside the app. The ROM details screen lists the folder, and the emulator reads the
 * enabled scripts at launch. Native never opens script files itself.
 */
class LuaScriptStorage(private val context: Context) {

    /**
     * A `.lua` file at the top of a linked folder
     */
    class ScriptFile(val name: String, val uri: Uri)

    sealed class ReadResult {
        class Success(val source: ByteArray) : ReadResult()
        data object FolderNotFound : ReadResult()
        data object FileNotFound : ReadResult()
        data object TooLarge : ReadResult()
        data object Failed : ReadResult()
    }

    fun isLuaScriptName(fileName: String): Boolean {
        return fileName.endsWith(LUA_EXTENSION, ignoreCase = true)
    }

    /**
     * The `.lua` files at the top of the folder, in no particular order. Subfolders are skipped. Returns null if the folder can't be read:
     * the permission was lost, it was moved or deleted, it's on a card that isn't in, or a cloud provider is still fetching it
     */
    fun listScripts(folderUri: Uri): List<ScriptFile>? {
        return try {
            // Some providers list a missing folder as empty, which would drop every script, so the folder itself is checked first.
            // isDirectory is also false when the permission is gone
            val folder = DocumentFile.fromTreeUri(context, folderUri)?.takeIf { it.isDirectory } ?: return null
            listChildren(folder.uri, DocumentsContract.getDocumentId(folder.uri))
                ?.filter { !it.isFolder && isLuaScriptName(it.name) }
                ?.map { ScriptFile(it.name, DocumentsContract.buildDocumentUriUsingTree(folder.uri, it.handle)) }
        } catch (e: Exception) {
            Log.w(TAG, "Could not list the script folder", e)
            null
        }
    }

    /**
     * Lists a folder of the tree [treeUri] with one query. The children's handles are their document IDs. Returns null if the provider says
     * the list isn't complete
     */
    private fun listChildren(treeUri: Uri, documentId: String): List<LuaScriptFolderContent.Child<String>>? {
        // DocumentFile asks the provider once per file for its name and once for its type, which is slow in a big folder, so the children are
        // queried in one go instead
        val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, documentId)
        val projection = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE,
        )
        return context.contentResolver.query(childrenUri, projection, null, null, null)?.use { cursor ->
            // Cloud providers can answer with part of the list (or none of it) while they fetch the rest, or with an error. That isn't the
            // folder's real content, so the folder counts as unreadable and the script list is kept
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
     * Reads the modules and images the scripts can use from the folder's tree (see [LuaScriptFolderContent] for the rules and caps). Images
     * are decoded here. It lists up to 65 folders and reads a few MB, so never on the main thread. Returns null if the folder can't be read
     */
    fun readFolderContent(folderUri: Uri): FolderContent? {
        return try {
            val folder = DocumentFile.fromTreeUri(context, folderUri)?.takeIf { it.isDirectory } ?: return null
            val treeUri = folder.uri
            val walk = LuaScriptFolderContent.walk(DocumentsContract.getDocumentId(treeUri)) { documentId ->
                try {
                    listChildren(treeUri, documentId)
                } catch (e: Exception) {
                    Log.w(TAG, "Could not list a folder in the script folder", e)
                    null
                }
            } ?: return null
            if (walk.gaveUp) {
                Log.w(TAG, "The script folder has too many folders or files: the rest are left out")
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
            Log.w(TAG, "Could not read the script folder's modules and images", e)
            null
        }
    }

    private fun readDocument(uri: Uri, limit: Int): ByteArray? {
        return try {
            context.contentResolver.openInputStream(uri)?.use { readAtMost(it, limit) }
        } catch (e: Exception) {
            Log.w(TAG, "Could not read a file in the script folder", e)
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
            Log.w(TAG, "Could not decode the image $path", e)
            leftOut(LuaScriptFolderContent.REASON_NOT_AN_IMAGE)
        } catch (e: OutOfMemoryError) {
            Log.w(TAG, "Not enough memory to decode the image $path", e)
            leftOut(LuaScriptFolderContent.REASON_NOT_AN_IMAGE)
        }
    }

    /**
     * Reads scripts for the emulator, in the order given. The folder is listed once, and each script is looked up in it by name
     *
     * @param folderUri The game's script folder. With none, every script is [ReadResult.FolderNotFound]
     */
    fun readScripts(folderUri: Uri?, fileNames: List<String>): List<Pair<String, ReadResult>> {
        val scriptFiles = folderUri?.let { listScripts(it) }?.associateBy { it.name }
        return fileNames.map { fileName ->
            val result = if (scriptFiles == null) {
                ReadResult.FolderNotFound
            } else {
                scriptFiles[fileName]?.let { readScript(it) } ?: ReadResult.FileNotFound
            }
            if (result !is ReadResult.Success) {
                Log.w(TAG, "Skipping Lua script $fileName: $result")
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
            Log.w(TAG, "Could not read Lua script ${scriptFile.name}", e)
            ReadResult.Failed
        }
    }

    /**
     * The script's header (see [LuaScriptHeader]), or null if the file can't be read. Only the start of the file is read
     */
    fun readHeader(scriptFile: ScriptFile): LuaScriptHeader? {
        return try {
            // One byte more than the header limit, so the parser knows the file goes on
            context.contentResolver.openInputStream(scriptFile.uri)?.use { LuaScriptHeader.parse(readAtMost(it, LuaScriptHeader.READ_LIMIT + 1)) }
        } catch (e: Exception) {
            null
        }
    }

    /**
     * Older versions copied picked scripts into `files/scripts/`. Scripts are read from a linked folder now, and those copies were only ever
     * made by the app, so they're deleted. Does nothing once they're gone
     */
    fun deleteOldScriptCopies() {
        val oldScriptsFolder = File(context.filesDir, OLD_SCRIPTS_FOLDER)
        if (oldScriptsFolder.exists() && !oldScriptsFolder.deleteRecursively()) {
            Log.w(TAG, "Could not delete the old script copies")
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
        private const val TAG = "melonade-lua"
        private const val OLD_SCRIPTS_FOLDER = "scripts"
        private const val LUA_EXTENSION = ".lua"
        const val MAX_SCRIPT_SIZE = 1024 * 1024
    }
}
