package me.magnum.melonds.common

import android.content.Context
import android.net.Uri

class UriPermissionManager(private val context: Context) {

    fun persistDirectoryPermissions(directoryUri: Uri, permission: Permission) {
        val flags = permission.toFlags()
        context.contentResolver.takePersistableUriPermission(directoryUri, flags)
    }

    /**
     * Gives back a persisted directory permission, if the app holds it
     */
    fun releaseDirectoryPermissions(directoryUri: Uri, permission: Permission) {
        val flags = permission.toFlags()
        try {
            context.contentResolver.releasePersistableUriPermission(directoryUri, flags)
        } catch (e: SecurityException) {
            // Not held, so there's nothing to release
        }
    }

    fun persistFilePermissions(fileUri: Uri, permission: Permission) {
        val flags = permission.toFlags()
        context.contentResolver.takePersistableUriPermission(fileUri, flags)
    }
}