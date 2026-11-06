package me.magnum.melonds.impl

import com.google.gson.JsonIOException
import com.google.gson.JsonParseException
import com.google.gson.stream.MalformedJsonException
import me.magnum.melonds.utils.deleteAtomicWriteLeftovers
import me.magnum.melonds.utils.sizeIfFileExists
import me.magnum.melonds.utils.writeFileAtomically
import java.io.EOFException
import java.io.File
import java.io.FileReader
import java.io.IOException
import java.io.OutputStream
import java.io.Reader
import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.util.concurrent.atomic.AtomicReference

internal sealed interface RomDataRead<out T> {
    /**
     * There's no file, or it's empty.
     */
    data object Missing : RomDataRead<Nothing>

    data class Loaded<T>(val value: T) : RomDataRead<T>

    /**
     * The contents are damaged, so the file was moved to [movedTo].
     */
    data class SetAside(val movedTo: File, val cause: Throwable) : RomDataRead<Nothing>

    /**
     * The file couldn't be read, or couldn't be moved after finding it damaged. It's still in place, so it must not be saved over.
     */
    data class Unavailable(val cause: Throwable) : RomDataRead<Nothing>
}

/**
 * Keeps saves from destroying data that wasn't read. Nothing is saved until [allowSaves] says the list holds what was read, and nothing is saved for the rest of this
 * instance after a read gives [RomDataRead.Unavailable].
 */
internal class RomDataFile(
    private val file: File,
    private val now: () -> Long = System::currentTimeMillis,
    private val moveFile: (File, File) -> Unit = { source, target -> Files.move(source.toPath(), target.toPath()) },
    private val fileSize: (File) -> Long? = ::sizeIfFileExists,
) {

    private enum class SaveState { WAITING_FOR_READ, ALLOWED, DISABLED }

    private val saveState = AtomicReference(SaveState.WAITING_FOR_READ)

    val canSave: Boolean get() = saveState.get() == SaveState.ALLOWED

    fun <D, T> read(parse: (Reader) -> D?, toModel: (D) -> T): RomDataRead<T> {
        // No save has started yet, so anything left here is from a process that was killed
        if (saveState.get() == SaveState.WAITING_FOR_READ) {
            deleteAtomicWriteLeftovers(file)
        }

        val result = readAndClassify(parse, toModel)
        if (result is RomDataRead.Unavailable) {
            saveState.set(SaveState.DISABLED)
        }
        return result
    }

    /**
     * To call once the data from [read] is in the list that gets saved. Does nothing after [RomDataRead.Unavailable].
     */
    fun allowSaves() {
        saveState.compareAndSet(SaveState.WAITING_FOR_READ, SaveState.ALLOWED)
    }

    /**
     * Returns false without calling [write] if saving isn't allowed.
     */
    fun save(write: (OutputStream) -> Boolean): Boolean {
        return canSave && writeFileAtomically(file, write)
    }

    private fun <D, T> readAndClassify(parse: (Reader) -> D?, toModel: (D) -> T): RomDataRead<T> {
        val size = try {
            fileSize(file)
        } catch (e: Exception) {
            return RomDataRead.Unavailable(e)
        }
        if (size == null || size == 0L) {
            return RomDataRead.Missing
        }

        val parsed = try {
            FileReader(file).use(parse) ?: return setAside(IOException("${file.name} has no data, but isn't empty"))
        } catch (e: Throwable) {
            return if (isDamagedContent(e)) setAside(e) else RomDataRead.Unavailable(e)
        }

        return try {
            RomDataRead.Loaded(toModel(parsed))
        } catch (e: Exception) {
            setAside(e)
        } catch (e: Throwable) {
            RomDataRead.Unavailable(e)
        }
    }

    /**
     * Gson reports a failed disk read as a JsonSyntaxException too, unless it happens after the end of the JSON, so only the cause can tell it from bad JSON.
     */
    private fun isDamagedContent(error: Throwable): Boolean {
        if (error !is JsonParseException || error is JsonIOException) {
            return false
        }
        val cause = error.cause
        return cause !is IOException || cause is EOFException || cause is MalformedJsonException
    }

    private fun setAside(damage: Throwable): RomDataRead<Nothing> {
        return try {
            RomDataRead.SetAside(moveToUnusedName(), damage)
        } catch (e: Exception) {
            e.addSuppressed(damage)
            RomDataRead.Unavailable(e)
        }
    }

    private fun moveToUnusedName(): File {
        val name = "${file.name}.corrupt.${now()}"
        var target = File(file.parentFile, name)
        var attempt = 1
        while (true) {
            try {
                moveFile(file, target)
                return target
            } catch (_: FileAlreadyExistsException) {
                target = File(file.parentFile, "$name.${attempt++}")
            }
        }
    }
}
