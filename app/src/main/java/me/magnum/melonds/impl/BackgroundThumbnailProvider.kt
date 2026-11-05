package me.magnum.melonds.impl

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import me.magnum.melonds.domain.model.Background
import me.magnum.melonds.impl.diagnostics.SvgTimingLog
import me.magnum.melonds.utils.BackgroundDecoder
import me.magnum.melonds.utils.BitmapUtils
import me.magnum.melonds.utils.writeFileAtomically
import java.io.File
import java.util.UUID

class BackgroundThumbnailProvider(private val context: Context) {
    companion object {
        private const val THUMBNAIL_SIZE = 256
        private const val THUMBNAIL_CACHE_DIR = "background_thumbnails"
    }

    fun getBackgroundThumbnail(background: Background): Bitmap? {
        return loadThumbnailFromDisk(background)
    }

    /**
     * Drops the saved thumbnail of a background whose file was replaced, so that a new one is generated when it's next needed. Returns false if it
     * couldn't tell or do that, as when the external cache directory isn't available right now, in which case an old thumbnail may still be there.
     */
    fun invalidateThumbnail(backgroundId: UUID): Boolean {
        val thumbnailFile = getThumbnailFile(backgroundId) ?: return false
        return !thumbnailFile.exists() || thumbnailFile.delete()
    }

    private fun loadThumbnailFromDisk(background: Background): Bitmap? {
        val thumbnailFile = getThumbnailFile(background.id)
        if (thumbnailFile?.isFile == true) {
            // Null for a damaged file, which then gets replaced below
            BitmapFactory.decodeFile(thumbnailFile.absolutePath)?.let { return it }
        }

        val thumbnail = generateBackgroundThumbnail(background)
        if (thumbnail != null) {
            saveThumbnailToDisk(background, thumbnail)
        }

        return thumbnail
    }

    private fun saveThumbnailToDisk(background: Background, thumbnail: Bitmap) {
        try {
            val thumbnailFile = getThumbnailFile(background.id) ?: return
            writeFileAtomically(thumbnailFile) {
                thumbnail.compress(Bitmap.CompressFormat.PNG, 100, it)
            }
        } catch (_: Exception) {
            // Ignore errors
        }
    }

    private fun getThumbnailFile(backgroundId: UUID?): File? {
        val cacheDir = context.externalCacheDir?.let { File(it, THUMBNAIL_CACHE_DIR) } ?: return null
        return if (cacheDir.isDirectory || cacheDir.mkdirs()) {
            File(cacheDir, backgroundId.toString())
        } else {
            null
        }
    }

    private fun generateSvgThumbnail(background: Background, isSvgNanos: Long): Bitmap? {
        val parseStartNanos = System.nanoTime()
        val svg = BackgroundDecoder.parseSvg(context, background.uri) ?: return null
        val parseEndNanos = System.nanoTime()
        val thumbnail = svg.rasterizeThumbnail(THUMBNAIL_SIZE) ?: return null
        val rasterEndNanos = System.nanoTime()

        SvgTimingLog.log(
            source = SvgTimingLog.SOURCE_THUMBNAIL,
            backgroundName = background.name,
            lastWidth = 0,
            lastHeight = 0,
            width = thumbnail.width,
            height = thumbnail.height,
            isSvgNanos = isSvgNanos,
            parseNanos = parseEndNanos - parseStartNanos,
            rasterNanos = rasterEndNanos - parseEndNanos,
        )
        return thumbnail
    }

    private fun generateBackgroundThumbnail(background: Background): Bitmap? {
        val isSvgStartNanos = System.nanoTime()
        val isSvg = BackgroundDecoder.isSvg(context, background.uri)
        val isSvgNanos = System.nanoTime() - isSvgStartNanos
        if (isSvg) {
            return generateSvgThumbnail(background, isSvgNanos)
        }

        val options = BitmapFactory.Options()
        options.inJustDecodeBounds = true

        try {
            context.contentResolver.openInputStream(background.uri)?.use {
                BitmapFactory.decodeStream(it, null, options)
            }
        } catch (e: Exception) {
            e.printStackTrace()
            return null
        }

        if (options.outWidth == -1 || options.outHeight == -1) {
            return null
        }

        // Find thumbnail dimensions limiting the largest side to THUMBNAIL_SIZE
        val (thumbnailWidth: Int, thumbnailHeight: Int) = if (options.outWidth > options.outHeight) {
            (THUMBNAIL_SIZE to (THUMBNAIL_SIZE * (options.outHeight / options.outWidth.toFloat())).toInt())
        } else {
            ((THUMBNAIL_SIZE * (options.outWidth / options.outHeight.toFloat())).toInt() to THUMBNAIL_SIZE)
        }

        val sampleSize = BitmapUtils.calculateMinimumSampleSize(options.outWidth, options.outHeight, thumbnailWidth, thumbnailHeight)
        options.inJustDecodeBounds = false
        options.inSampleSize = sampleSize

        return try {
            context.contentResolver.openInputStream(background.uri)?.use {
                BitmapFactory.decodeStream(it, null, options)
            }
        } catch (e: Exception) {
            e.printStackTrace()
            null
        }
    }
}