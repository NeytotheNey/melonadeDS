package me.magnum.melonds.utils

import java.io.File
import java.io.OutputStream
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/**
 * Replaces [destination] with what [write] writes, in one step: a kill or a full disk halfway leaves the old file as it was, and a reader never sees
 * a partial file. [write] returns whether it succeeded, and returning false (or throwing, which is passed on) leaves [destination] untouched. Returns
 * whether it was replaced. Each call has its own temporary file, so concurrent writes of the same file don't mix.
 */
internal fun writeFileAtomically(destination: File, write: (OutputStream) -> Boolean): Boolean {
    val tempFile = File.createTempFile("${destination.name}.", ".tmp", destination.parentFile)
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
