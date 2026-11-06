package me.magnum.melonds.utils

import java.io.File
import java.io.IOException
import java.io.OutputStream
import java.nio.file.Files
import java.nio.file.NoSuchFileException
import java.nio.file.StandardCopyOption
import java.nio.file.attribute.BasicFileAttributes

private const val TEMP_SUFFIX = ".tmp"

/**
 * Replaces [destination] with what [write] writes, in one step: a kill or a full disk halfway leaves the old file as it was, and a reader never sees
 * a partial file. [write] returns whether it succeeded, and returning false (or throwing, which is passed on) leaves [destination] untouched. Returns
 * whether it was replaced. Each call has its own temporary file, so concurrent writes of the same file don't mix.
 */
internal fun writeFileAtomically(destination: File, write: (OutputStream) -> Boolean): Boolean {
    val tempFile = File.createTempFile("${destination.name}.", TEMP_SUFFIX, destination.parentFile)
    var isReplaced = false
    try {
        val isWritten = tempFile.outputStream().use { output ->
            val isWritten = write(output)
            if (isWritten) {
                // The rename mustn't reach the disk before the contents do
                output.flush()
                output.fd.sync()
            }
            isWritten
        }
        if (isWritten) {
            Files.move(tempFile.toPath(), destination.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE)
            isReplaced = true
        }
    } finally {
        if (!isReplaced) {
            tempFile.delete()
        }
    }
    return isReplaced
}

/**
 * The size of [file], or null only if it doesn't exist. Anything else that goes wrong throws, while [File.isFile] and [File.length] report it as there being no file.
 */
internal fun sizeIfFileExists(file: File): Long? {
    val attributes = try {
        Files.readAttributes(file.toPath(), BasicFileAttributes::class.java)
    } catch (_: NoSuchFileException) {
        return null
    }
    if (!attributes.isRegularFile) {
        throw IOException("${file.name} isn't a regular file")
    }
    return attributes.size()
}

/**
 * Deletes the temporary files that [writeFileAtomically] calls killed halfway left beside [destination]. They are never read, but each has its own name, so they would pile up. Only call this
 * while no write of [destination] is running, since it would delete that write's file too.
 */
internal fun deleteAtomicWriteLeftovers(destination: File) {
    destination.parentFile
        ?.listFiles { file -> file.name.startsWith("${destination.name}.") && file.name.endsWith(TEMP_SUFFIX) }
        ?.forEach { it.delete() }
}
