package me.magnum.melonds.utils

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.net.Uri
import android.provider.OpenableColumns
import com.caverock.androidsvg.PreserveAspectRatio
import com.caverock.androidsvg.RenderOptions
import com.caverock.androidsvg.SVG
import me.magnum.melonds.domain.model.layout.BackgroundMode
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Loads backgrounds that are SVG files. Raster backgrounds (PNG and JPEG) are decoded with BitmapFactory where they're used.
 */
object BackgroundDecoder {
    const val SVG_MIME_TYPE = "image/svg+xml"

    private const val SNIFF_BYTE_COUNT = 4096

    /**
     * Whether the file at [uri] is an SVG, going by its MIME type, then its name, then the start of its contents.
     */
    fun isSvg(context: Context, uri: Uri): Boolean {
        val mimeType = runCatching { context.contentResolver.getType(uri) }.getOrNull()
        if (mimeType != null) {
            // Also covers variants like image/svg or image/svg+xml-compressed
            if (mimeType.contains("svg", ignoreCase = true)) {
                return true
            }
            // Any other image type is a raster one, which saves reading the file
            if (mimeType.startsWith("image/", ignoreCase = true)) {
                return false
            }
        }

        if (SvgRasterSize.hasSvgExtension(getFileName(context, uri))) {
            return true
        }

        return runCatching {
            context.contentResolver.openInputStream(uri)?.use {
                val buffer = ByteArray(SNIFF_BYTE_COUNT)
                var length = 0
                while (length < buffer.size) {
                    val read = it.read(buffer, length, buffer.size - length)
                    if (read < 0) {
                        break
                    }
                    length += read
                }
                SvgRasterSize.looksLikeSvg(buffer, length)
            }
        }.getOrNull() ?: false
    }

    /**
     * Parses the SVG file at [uri] once, so it can be rasterized again at other sizes without reading the file again. Null if it can't be
     * read or parsed.
     */
    fun parseSvg(context: Context, uri: Uri): SvgBackground? {
        return runCatching {
            context.contentResolver.openInputStream(uri)?.use { SvgBackground(SVG.getFromInputStream(it)) }
        }.onFailure {
            it.printStackTrace()
        }.getOrNull()
    }

    private fun getFileName(context: Context, uri: Uri): String? {
        val displayName = runCatching {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
                if (cursor.moveToFirst()) cursor.getString(0) else null
            }
        }.getOrNull()
        return displayName ?: uri.lastPathSegment
    }
}

/**
 * A parsed SVG background, which can be rasterized at any size.
 */
class SvgBackground internal constructor(private val svg: SVG) {

    /**
     * The width over the height of the SVG, from its viewBox, else from its width and height. Null if it has neither, in which case it takes
     * the size of whatever it's rasterized to.
     */
    val aspectRatio: Float?

    // Whether the SVG's root says how its viewBox is fitted to the viewport, which is then kept for the stretch mode
    private val hasDeclaredPreserveAspectRatio: Boolean

    init {
        hasDeclaredPreserveAspectRatio = runCatching { svg.documentPreserveAspectRatio != null }.getOrDefault(false)

        val viewBox = runCatching { svg.documentViewBox }.getOrNull()
        aspectRatio = if (viewBox != null && viewBox.width() > 0f && viewBox.height() > 0f) {
            viewBox.width() / viewBox.height()
        } else {
            val documentWidth = runCatching { svg.documentWidth }.getOrDefault(-1f)
            val documentHeight = runCatching { svg.documentHeight }.getOrDefault(-1f)
            if (documentWidth > 0f && documentHeight > 0f) {
                // Without a viewBox the content would be drawn at its own size whatever the viewport, so give it one to scale it
                svg.setDocumentViewBox(0f, 0f, documentWidth, documentHeight)
                documentWidth / documentHeight
            } else {
                null
            }
        }

        // An absolute root width and height would replace the viewport given when rendering, drawing it at that size in the bitmap's corner,
        // so make it fill whatever it's rendered to (as Coil's SvgDecoder does). Without a viewBox the content stays at 1 unit per pixel
        runCatching {
            svg.setDocumentWidth("100%")
            svg.setDocumentHeight("100%")
        }
    }

    /**
     * Rasterizes the SVG for a [targetWidth] x [targetHeight] surface shown with [mode]. The fit modes get the largest bitmap with the SVG's
     * aspect ratio that fits in the surface, so placing it like a raster background shows it 1:1 with the screen's pixels. The stretch mode
     * gets a bitmap of the surface's size, with the SVG stretched to it unless its root declares its own preserveAspectRatio. Both sides are
     * kept at or under [maxSize]. Null if it can't be rasterized.
     */
    fun rasterize(targetWidth: Int, targetHeight: Int, mode: BackgroundMode, maxSize: Int = Int.MAX_VALUE): Bitmap? {
        val (width, height) = SvgRasterSize.calculate(aspectRatio, targetWidth, targetHeight, mode, maxSize) ?: return null
        // Null keeps the document's own. In the fit modes the bitmap already has the SVG's aspect ratio, so stretching only takes up rounding
        val preserveAspectRatio = if (mode == BackgroundMode.STRETCH && hasDeclaredPreserveAspectRatio) null else PreserveAspectRatio.STRETCH
        return render(width, height, preserveAspectRatio)
    }

    /**
     * Rasterizes the whole SVG with its aspect ratio kept and its longest side [size] long. Without an aspect ratio its contents are sized in
     * surface pixels, so drawing them at thumbnail size would make them huge: it's rasterized for a typical surface and then scaled down, which shows
     * it the way a screen would.
     */
    fun rasterizeThumbnail(size: Int): Bitmap? {
        val (width, height) = SvgRasterSize.thumbnailSize(aspectRatio, size) ?: return null
        if (aspectRatio != null) {
            return render(width, height, PreserveAspectRatio.STRETCH)
        }

        val surface = render(SvgRasterSize.THUMBNAIL_SURFACE_WIDTH, SvgRasterSize.THUMBNAIL_SURFACE_HEIGHT, PreserveAspectRatio.STRETCH) ?: return null
        return scaleDown(surface, width, height)
    }

    /**
     * Scales [bitmap], which is recycled, down to [width] x [height] a step at a time, since a single large step would skip over fine details.
     */
    private fun scaleDown(bitmap: Bitmap, width: Int, height: Int): Bitmap? {
        return runCatching {
            SvgRasterSize.downscaleSteps(bitmap.width, bitmap.height, width, height).fold(bitmap) { current, (stepWidth, stepHeight) ->
                Bitmap.createScaledBitmap(current, stepWidth, stepHeight, true).also {
                    if (it !== current) {
                        current.recycle()
                    }
                }
            }
        }.onFailure {
            it.printStackTrace()
        }.getOrNull()
    }

    private fun render(width: Int, height: Int, preserveAspectRatio: PreserveAspectRatio?): Bitmap? {
        val bitmap = runCatching { Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888) }.onFailure { it.printStackTrace() }.getOrNull()
            ?: return null

        return runCatching {
            val options = RenderOptions().viewPort(0f, 0f, width.toFloat(), height.toFloat())
            if (preserveAspectRatio != null) {
                options.preserveAspectRatio(preserveAspectRatio)
            }
            // A stale render in the layout editor can still be running when the next one starts
            synchronized(svg) {
                svg.renderToCanvas(Canvas(bitmap), options)
            }
            bitmap
        }.onFailure {
            it.printStackTrace()
            bitmap.recycle()
        }.getOrNull()
    }
}

/**
 * The sizing and detection rules for SVG backgrounds, kept free of Android types.
 */
internal object SvgRasterSize {
    // The surface a thumbnail of an SVG with no aspect ratio stands for, which also gives that thumbnail its portrait shape
    const val THUMBNAIL_SURFACE_WIDTH = 1080
    const val THUMBNAIL_SURFACE_HEIGHT = 1920

    /**
     * The size of a thumbnail with its longest side [size] long, for an SVG with [aspectRatio] (null for none). Null if it's empty.
     */
    fun thumbnailSize(aspectRatio: Float?, size: Int): Pair<Int, Int>? {
        return fitInside(aspectRatio ?: (THUMBNAIL_SURFACE_WIDTH / THUMBNAIL_SURFACE_HEIGHT.toFloat()), size, size)
    }

    /**
     * The sizes to scale through to go from one size to a smaller one: halving while that doesn't go past the target, then the target itself.
     */
    fun downscaleSteps(fromWidth: Int, fromHeight: Int, toWidth: Int, toHeight: Int): List<Pair<Int, Int>> {
        return buildList {
            var width = fromWidth
            var height = fromHeight
            while (width / 2 >= toWidth && height / 2 >= toHeight && (width / 2 != toWidth || height / 2 != toHeight)) {
                width /= 2
                height /= 2
                add(width to height)
            }
            add(toWidth to toHeight)
        }
    }

    fun hasSvgExtension(fileName: String?): Boolean {
        return fileName?.endsWith(".svg", ignoreCase = true) == true
    }

    /**
     * Whether the first [length] bytes of a file look like the start of an SVG document.
     */
    fun looksLikeSvg(bytes: ByteArray, length: Int): Boolean {
        if (length <= 0) {
            return false
        }
        val text = String(bytes, 0, minOf(length, bytes.size), Charsets.UTF_8)
        return text.contains("<svg", ignoreCase = true)
    }

    /**
     * The bitmap size an SVG with [aspectRatio] (null for none) is rasterized at for a [targetWidth] x [targetHeight] surface shown with
     * [mode], with both sides at or under [maxSize]. Null if the target is empty.
     */
    fun calculate(aspectRatio: Float?, targetWidth: Int, targetHeight: Int, mode: BackgroundMode, maxSize: Int): Pair<Int, Int>? {
        val size = if (mode == BackgroundMode.STRETCH) {
            if (targetWidth <= 0 || targetHeight <= 0) null else targetWidth to targetHeight
        } else {
            fitInside(aspectRatio, targetWidth, targetHeight)
        } ?: return null

        return clampToMaxSize(size.first, size.second, maxSize)
    }

    /**
     * The largest size with [aspectRatio] that fits in [maxWidth] x [maxHeight], or that whole size when there's no aspect ratio. Null if
     * it's empty.
     */
    fun fitInside(aspectRatio: Float?, maxWidth: Int, maxHeight: Int): Pair<Int, Int>? {
        if (maxWidth <= 0 || maxHeight <= 0) {
            return null
        }
        if (aspectRatio == null || !aspectRatio.isFinite() || aspectRatio <= 0f) {
            return maxWidth to maxHeight
        }

        return if (maxWidth / maxHeight.toFloat() > aspectRatio) {
            // Limited by the height
            max(1, (maxHeight * aspectRatio).roundToInt()).coerceAtMost(maxWidth) to maxHeight
        } else {
            maxWidth to max(1, (maxWidth / aspectRatio).roundToInt()).coerceAtMost(maxHeight)
        }
    }

    /**
     * Scales [width] x [height] down, keeping its ratio, until neither side is over [maxSize].
     */
    fun clampToMaxSize(width: Int, height: Int, maxSize: Int): Pair<Int, Int> {
        if (maxSize <= 0 || (width <= maxSize && height <= maxSize)) {
            return width to height
        }
        val scale = maxSize / max(width, height).toFloat()
        return max(1, (width * scale).roundToInt()).coerceAtMost(maxSize) to max(1, (height * scale).roundToInt()).coerceAtMost(maxSize)
    }
}
