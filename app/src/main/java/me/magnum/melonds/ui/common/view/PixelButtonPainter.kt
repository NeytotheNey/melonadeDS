package me.magnum.melonds.ui.common.view

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Rect
import android.graphics.RectF
import java.nio.ByteBuffer
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * Draws the parts of a [RaisedButtonView] as pixel art, for [ControllerAppearance.Shading.PIXEL]. Each part's shape is snapped to a grid of whole physical
 * pixels centred on that part, so symmetric shapes stay symmetric, and shaded with a few flat colours. Everything drawn on the face (labels, arrows and
 * icons) goes on a finer detail grid that lines up with it: [PixelArt.detailPerCell] detail cells to a side per cell. The result is cached as three small
 * bitmaps per part (its lip, its face and its pressed face), which are drawn scaled up with no filtering so the pixels stay sharp.
 */
class PixelButtonPainter(private val art: PixelArt, private val density: Float) {

    /**
     * The cached bitmaps of one part, which all cover [bounds] in the view. The pressed face is drawn [PixelArt.travel] cells lower.
     */
    private class PartBitmaps(val lip: Bitmap, val face: Bitmap, val pressedFace: Bitmap, val bounds: Rect, val pressedBounds: Rect)

    /**
     * A grid of cells that are either in or out. Cells outside the grid count as out.
     */
    private class Mask(val width: Int, val height: Int) {
        private val cells = BooleanArray(width * height)

        operator fun get(x: Int, y: Int): Boolean {
            return x in 0 until width && y in 0 until height && cells[y * width + x]
        }

        operator fun set(x: Int, y: Int, value: Boolean) {
            if (x in 0 until width && y in 0 until height) {
                cells[y * width + x] = value
            }
        }
    }

    /**
     * One part's shape snapped to its own grid of cells, whose top-left corner is at ([originX], [originY]) in view pixels. The grid has an empty border of
     * one cell around the shape and room for the lip below it.
     */
    private class PartGrid(val originX: Float, val originY: Float, val mask: Mask)

    /**
     * The interior cells of a part: the ones whose four neighbours are in the shape too. The rest of the shape is its outline. [detail] is the same on the
     * detail grid.
     */
    private class Interior(val cells: Mask, val detail: Mask)

    /**
     * The size of one detail cell and of one cell of the shape, in physical pixels. See [setViewSize].
     */
    private var detailSize = 1
    private var cellSize = 1

    /**
     * How far the lip goes below the faces, in physical pixels.
     */
    val lipHeight get() = art.lip * cellSize

    // No anti-aliasing, so each grid cell is either fully in or fully out
    private val maskPaint = Paint()
    private val bitmapPaint = Paint().apply { isFilterBitmap = false }
    private var bitmaps = emptyArray<PartBitmaps>()

    /**
     * Picks the pixel size for a view of the given size. It's the style's size, made smaller on small views so that they're still at least
     * [PixelArt.minCells] cells across. It's rounded down to whole physical pixels, so faces never get fewer cells than that. Call it before [lipHeight] or
     * [update] are used.
     */
    fun setViewSize(w: Int, h: Int) {
        val detailCells = art.minCells * art.detailPerCell
        detailSize = floor(min(art.pixelSizeDp / art.detailPerCell * density, min(w, h).toFloat() / detailCells)).toInt().coerceAtLeast(1)
        cellSize = detailSize * art.detailPerCell
    }

    /**
     * Rebuilds the bitmaps of every part. [faces] and [glyphs] are the parts' shapes in view pixels, and [transform] takes design units to view pixels.
     */
    fun update(parts: List<RaisedButtonView.Part>, faces: Array<Path>, glyphs: Array<Path>, transform: Matrix, isToggledOn: Boolean) {
        val grids = Array(parts.size) { snap(faces[it]) }
        separateDpadArms(parts, grids)
        val interiors = Array(parts.size) { interiorOf(grids[it].mask) }
        val letterScale = sharedLetterScale(parts, grids, interiors, transform)
        val arrowScale = sharedArrowScale(parts, grids, interiors, faces, glyphs)
        bitmaps = Array(parts.size) {
            val iconScale = if (parts[it].colorGroup == ControllerAppearance.ColorGroup.DPAD) arrowScale else Int.MAX_VALUE
            buildPart(parts[it], grids[it], interiors[it], faces[it], glyphs[it], transform, isToggledOn, letterScale, iconScale)
        }
    }

    fun drawLip(canvas: Canvas, index: Int) {
        val part = bitmaps.getOrNull(index) ?: return
        canvas.drawBitmap(part.lip, null, part.bounds, bitmapPaint)
    }

    fun drawFace(canvas: Canvas, index: Int, pressed: Boolean) {
        val part = bitmaps.getOrNull(index) ?: return
        if (pressed) {
            canvas.drawBitmap(part.pressedFace, null, part.pressedBounds, bitmapPaint)
        } else {
            canvas.drawBitmap(part.face, null, part.bounds, bitmapPaint)
        }
    }

    private fun snap(face: Path): PartGrid {
        val bounds = RectF().also { face.computeBounds(it, true) }
        val columns = (bounds.width() / cellSize).roundToInt() + 2
        val rows = (bounds.height() / cellSize).roundToInt() + 2
        val originX = bounds.centerX() - columns * cellSize / 2f
        val originY = bounds.centerY() - rows * cellSize / 2f
        val mask = rasterize(columns, rows + art.lip, originX, originY, cellSize) { it.drawPath(face, maskPaint) }
        return PartGrid(originX, originY, mask)
    }

    /**
     * The design gap between the D-pad's centre and its arms is much thinner than a cell, so their outlines would touch. This trims the inner end of each
     * arm until there's at least one empty cell between it and the centre. Only the drawing changes, not the hit areas.
     */
    private fun separateDpadArms(parts: List<RaisedButtonView.Part>, grids: Array<PartGrid>) {
        val centreIndex = parts.indexOfFirst { it.colorGroup == ControllerAppearance.ColorGroup.DPAD && it.input == null }
        if (centreIndex < 0) return

        // The centre's cells in view pixels, grown by one cell all round
        val centre = grids[centreIndex]
        val keepOut = RectF()
        for (y in 0 until centre.mask.height) {
            for (x in 0 until centre.mask.width) {
                if (centre.mask[x, y]) {
                    keepOut.union(centre.originX + x * cellSize, centre.originY + y * cellSize, centre.originX + (x + 1) * cellSize, centre.originY + (y + 1) * cellSize)
                }
            }
        }
        if (keepOut.isEmpty) return
        keepOut.inset(-cellSize + OVERLAP_TOLERANCE, -cellSize + OVERLAP_TOLERANCE)

        for (i in parts.indices) {
            if (i == centreIndex || parts[i].colorGroup != ControllerAppearance.ColorGroup.DPAD) continue
            val arm = grids[i]
            for (y in 0 until arm.mask.height) {
                for (x in 0 until arm.mask.width) {
                    val left = arm.originX + x * cellSize
                    val top = arm.originY + y * cellSize
                    if (keepOut.intersects(left, top, left + cellSize, top + cellSize)) {
                        arm.mask[x, y] = false
                    }
                }
            }
        }
    }

    private fun interiorOf(mask: Mask): Interior {
        val perCell = art.detailPerCell
        val cells = Mask(mask.width, mask.height)
        for (y in 0 until mask.height) {
            for (x in 0 until mask.width) {
                cells[x, y] = mask[x, y] && mask[x - 1, y] && mask[x + 1, y] && mask[x, y - 1] && mask[x, y + 1]
            }
        }
        val detail = Mask(mask.width * perCell, mask.height * perCell)
        for (y in 0 until detail.height) {
            for (x in 0 until detail.width) {
                detail[x, y] = cells[x / perCell, y / perCell]
            }
        }
        return Interior(cells, detail)
    }

    /**
     * Where a part's label is centred, in detail cells.
     */
    private fun labelCentre(part: RaisedButtonView.Part, grid: PartGrid, transform: Matrix): FloatArray {
        val centre = floatArrayOf(part.labelX, part.labelY)
        transform.mapPoints(centre)
        return floatArrayOf((centre[0] - grid.originX) / detailSize, (centre[1] - grid.originY) / detailSize)
    }

    /**
     * Single letters in one view, like A, B, X and Y, are all drawn the same size: the biggest that fits every one of them in the big font. Letters that
     * don't fit it at all fall back to the small font on their own.
     */
    private fun sharedLetterScale(parts: List<RaisedButtonView.Part>, grids: Array<PartGrid>, interiors: Array<Interior>, transform: Matrix): Int {
        var scale = Int.MAX_VALUE
        for (i in parts.indices) {
            val letter = parts[i].label?.singleOrNull()?.let { PixelIcons.LETTERS[it] } ?: continue
            val centre = labelCentre(parts[i], grids[i], transform)
            val fit = fitScale(listOf(letter), centre[0], centre[1], Int.MAX_VALUE, interiors[i].detail, true)
            if (fit > 0) {
                scale = min(scale, fit)
            }
        }
        return scale
    }

    /**
     * Where a part's icon or arrow is centred, in detail cells: on its glyph if it has one, otherwise on its face.
     */
    private fun iconCentre(part: RaisedButtonView.Part, grid: PartGrid, face: Path, glyph: Path): FloatArray {
        val bounds = RectF()
        if (part.glyph != null) glyph.computeBounds(bounds, true) else face.computeBounds(bounds, true)
        return floatArrayOf((bounds.centerX() - grid.originX) / detailSize, (bounds.centerY() - grid.originY) / detailSize)
    }

    /**
     * The biggest scale [icon] can have on a face, so that its larger side is at most [ICON_SIZE] of the face's smaller side, like the vector icons. It's
     * never below 1, so icons on small faces are left to the fit checks.
     */
    private fun iconScaleLimit(icon: PixelIcon, face: Path): Int {
        val bounds = RectF().also { face.computeBounds(it, true) }
        // Counted in unrounded detail cells, since rounding the cell size down would otherwise add cells and let icons grow at normal sizes
        val faceCells = min(bounds.width(), bounds.height()) / max(detailSize.toFloat(), art.pixelSizeDp / art.detailPerCell * density)
        return floor(faceCells * ICON_SIZE / max(icon.width, icon.height)).toInt().coerceAtLeast(1)
    }

    /**
     * The four D-pad arrows are all drawn the same size: the biggest that fits every arm, with its drop shadow, and keeps within [iconScaleLimit].
     */
    private fun sharedArrowScale(parts: List<RaisedButtonView.Part>, grids: Array<PartGrid>, interiors: Array<Interior>, faces: Array<Path>, glyphs: Array<Path>): Int {
        var scale = Int.MAX_VALUE
        for (i in parts.indices) {
            val part = parts[i]
            val arrow = part.pixelIcon
            if (part.colorGroup != ControllerAppearance.ColorGroup.DPAD || arrow == null) continue
            val centre = iconCentre(part, grids[i], faces[i], glyphs[i])
            val fit = fitScale(listOf(arrow), centre[0], centre[1], iconScaleLimit(arrow, faces[i]), interiors[i].detail, true)
            if (fit > 0) {
                scale = min(scale, fit)
            }
        }
        return scale
    }

    private fun buildPart(
        part: RaisedButtonView.Part,
        grid: PartGrid,
        partInterior: Interior,
        face: Path,
        glyph: Path,
        transform: Matrix,
        isToggledOn: Boolean,
        letterScale: Int,
        iconScale: Int,
    ): PartBitmaps {
        val colors = art.colorsFor(part.input, part.colorGroup)
        val mask = grid.mask
        val columns = mask.width
        val rows = mask.height
        val perCell = art.detailPerCell
        val interior = partInterior.cells
        val detailInterior = partInterior.detail

        // What's drawn on the face goes on the detail grid, with its drop shadow kept apart so that it can be left off
        val faceBounds = RectF().also { face.computeBounds(it, true) }
        val icon = if (isToggledOn) part.icon else part.offIcon ?: part.icon
        val pixelIcon = if (isToggledOn) part.pixelIcon else part.pixelOffIcon ?: part.pixelIcon
        // Dimmed icons are drawn in the shadow colour instead, since see-through pixels don't suit pixel art
        val isDimmed = icon != null && icon.alpha < 255
        val ink = Mask(detailInterior.width, detailInterior.height)
        val inkShadow = Mask(detailInterior.width, detailInterior.height)
        val iconAt = iconCentre(part, grid, face, glyph)
        // Hand-drawn icons and arrows grow with the face, up to [iconScale]. They're tried with their drop shadow, then without it, then their compact
        // version the same way. Only if none of those fit is the vector one rasterized
        val drewPixelIcon = listOfNotNull(pixelIcon, pixelIcon?.compact).any { option ->
            val maxScale = min(iconScaleLimit(option, face), iconScale)
            stamp(listOf(option), iconAt[0], iconAt[1], maxScale, detailInterior, ink, inkShadow, true) ||
                stamp(listOf(option), iconAt[0], iconAt[1], maxScale, detailInterior, ink, inkShadow, false)
        }
        if (!drewPixelIcon) {
            val raster = rasterize(detailInterior.width, detailInterior.height, grid.originX, grid.originY, detailSize) { canvas ->
                canvas.drawPath(glyph, maskPaint)
                if (icon != null) {
                    val alpha = icon.alpha
                    icon.alpha = 255
                    icon.draw(canvas)
                    icon.alpha = alpha
                }
            }
            for (y in 0 until raster.height) {
                for (x in 0 until raster.width) {
                    if (raster[x, y]) {
                        ink[x, y] = true
                        inkShadow[x + 1, y + 1] = true
                    }
                }
            }
        }
        if (part.label != null) {
            val centre = labelCentre(part, grid, transform)
            // Long labels fall back to shorter ones on buttons too small for them. Single letters use the big font at the view's shared size when they fit
            val candidates = listOf(part.label) + LABEL_FALLBACKS[part.label].orEmpty()
            candidates.firstOrNull { text ->
                val bigFits = text.length == 1 && stampText(text, PixelIcons.LETTERS, centre, letterScale, detailInterior, ink, inkShadow)
                bigFits || stampText(text, PixelIcons.SMALL_LETTERS, centre, Int.MAX_VALUE, detailInterior, ink, inkShadow)
            }
        }
        val inkColor = if (isDimmed) colors.shadow else colors.ink

        // The D-pad's centre gets a small round dent: a shadow on its top-left inner edge and a light bottom-right one. Cell x spans x to x + 1
        val hasDimple = part.colorGroup == ControllerAppearance.ColorGroup.DPAD && part.input == null
        val dimpleRadius = min(DIMPLE_RADIUS, faceBounds.width() / detailSize / 6f)
        val dimpleX = (faceBounds.centerX() - grid.originX) / detailSize - 0.5f
        val dimpleY = (faceBounds.centerY() - grid.originY) / detailSize - 0.5f

        fun isInDimple(x: Int, y: Int): Boolean {
            val dx = x - dimpleX
            val dy = y - dimpleY
            return dx * dx + dy * dy <= dimpleRadius * dimpleRadius
        }

        fun dimpleColor(x: Int, y: Int): Int? {
            if (!hasDimple || dimpleRadius < MIN_DIMPLE_RADIUS || !isInDimple(x, y)) return null
            if (isInDimple(x - 1, y) && isInDimple(x + 1, y) && isInDimple(x, y - 1) && isInDimple(x, y + 1)) return colors.base
            return if (x - dimpleX + y - dimpleY < 0) colors.shadow else colors.highlight
        }

        // How many steps it takes to reach a cell that isn't interior, where a cell touching the outline counts 1
        fun distance(x: Int, y: Int, dx: Int, dy: Int): Int {
            var steps = 1
            while (interior[x + dx * steps, y + dy * steps]) {
                steps++
            }
            return steps
        }

        fun shapeColor(x: Int, y: Int, pressed: Boolean): Int {
            if (!mask[x, y]) return Color.TRANSPARENT
            if (!interior[x, y]) return colors.outline

            if (!art.sixteenBit) {
                val topLeft = !interior[x, y - 1] || !interior[x - 1, y]
                val bottomRight = !interior[x, y + 1] || !interior[x + 1, y]
                return when {
                    topLeft && !bottomRight -> if (pressed) colors.shadow else colors.highlight
                    bottomRight && !topLeft -> if (pressed) colors.highlight else colors.shadow
                    else -> colors.base
                }
            }

            val up = distance(x, y, 0, -1)
            val toLeft = distance(x, y, -1, 0)
            val isDitherCell = (x + y) % 2 == 0
            if (pressed) {
                return if (up <= 2 || toLeft == 1 || (up == 3 && isDitherCell)) colors.lip else colors.shadow
            }
            val down = distance(x, y, 0, 1)
            val toRight = distance(x, y, 1, 0)
            return when {
                (up == 2 && toLeft in 2..4) || (up == 3 && toLeft == 2) -> colors.glint
                down <= 2 || toRight == 1 || (down == 3 && isDitherCell) -> colors.shadow
                up <= 2 || toLeft == 1 -> colors.highlight
                else -> colors.base
            }
        }

        // The lip is the shape moved down by 1 to lip cells, wherever the shape itself isn't. A pressed face covers the top of it
        fun isLip(x: Int, y: Int): Boolean {
            if (mask[x, y]) return false
            for (depth in 1..art.lip) {
                if (mask[x, y - depth]) return true
            }
            return false
        }

        fun isButton(x: Int, y: Int) = mask[x, y] || isLip(x, y)

        fun lipColor(x: Int, y: Int): Int {
            if (!isLip(x, y)) return Color.TRANSPARENT
            if (!art.sixteenBit) return colors.outline
            // 16-bit lips are outlined where they meet empty space
            return if (!isButton(x, y + 1) || !isButton(x - 1, y) || !isButton(x + 1, y)) colors.outline else colors.lip
        }

        val shapeColors = IntArray(columns * rows) { shapeColor(it % columns, it / columns, false) }
        val pressedShapeColors = IntArray(columns * rows) { shapeColor(it % columns, it / columns, true) }

        // Ink and its drop shadow go on top of the interior, at detail size
        fun faceColor(x: Int, y: Int, shape: IntArray): Int {
            val cell = (y / perCell) * columns + x / perCell
            if (!detailInterior[x, y]) return shape[cell]
            if (ink[x, y]) return inkColor
            if (inkShadow[x, y]) return colors.inkShadow
            return dimpleColor(x, y) ?: shape[cell]
        }

        val left = grid.originX.roundToInt()
        val top = grid.originY.roundToInt()
        val bounds = Rect(left, top, left + columns * cellSize, top + rows * cellSize)
        val pressedBounds = Rect(bounds).apply { offset(0, art.travel * cellSize) }
        return PartBitmaps(
            lip = bitmapOf(detailInterior.width, detailInterior.height) { x, y -> lipColor(x / perCell, y / perCell) },
            face = bitmapOf(detailInterior.width, detailInterior.height) { x, y -> faceColor(x, y, shapeColors) },
            pressedFace = bitmapOf(detailInterior.width, detailInterior.height) { x, y -> faceColor(x, y, pressedShapeColors) },
            bounds = bounds,
            pressedBounds = pressedBounds,
        )
    }

    /**
     * Draws something given in view pixels onto a grid of [width] by [height] cells of [size] pixels, whose top-left corner is at ([originX], [originY]) in
     * view pixels. A cell is in when at least half of it is covered.
     */
    private fun rasterize(width: Int, height: Int, originX: Float, originY: Float, size: Int, draw: (Canvas) -> Unit): Mask {
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ALPHA_8)
        val canvas = Canvas(bitmap)
        canvas.scale(1f / size, 1f / size)
        canvas.translate(-originX, -originY)
        draw(canvas)

        val alphas = ByteArray(bitmap.rowBytes * height)
        bitmap.copyPixelsToBuffer(ByteBuffer.wrap(alphas))
        val mask = Mask(width, height)
        for (y in 0 until height) {
            for (x in 0 until width) {
                mask[x, y] = (alphas[y * bitmap.rowBytes + x].toInt() and 0xFF) >= 128
            }
        }
        bitmap.recycle()
        return mask
    }

    private fun stampText(text: String, font: Map<Char, PixelIcon>, centre: FloatArray, maxScale: Int, interior: Mask, ink: Mask, inkShadow: Mask): Boolean {
        val letters = text.map { font[it] ?: return false }
        return stamp(letters, centre[0], centre[1], maxScale, interior, ink, inkShadow, true)
    }

    /**
     * The left or top edge of a box of [size] cells centred on [centre]. Centres sit on whole or half cells, so this works in half cells, and a box that
     * can't be centred exactly goes half a cell up or left.
     */
    private fun centredStart(size: Int, centre: Float) = Math.floorDiv((centre * 2).roundToInt() - size, 2)

    /**
     * Calls [cell] with the top-left corner of every ink cell of [icons] side by side with a one cell gap, at the given scale. Stops early, and returns
     * false, if [cell] returns false.
     */
    private fun forEachInkCell(icons: List<PixelIcon>, left: Int, top: Int, scale: Int, cell: (Int, Int) -> Boolean): Boolean {
        var iconLeft = left
        for (icon in icons) {
            for (y in 0 until icon.height) {
                for (x in 0 until icon.width) {
                    if (icon[x, y] && !cell(iconLeft + x * scale, top + y * scale)) return false
                }
            }
            iconLeft += (icon.width + 1) * scale
        }
        return true
    }

    /**
     * Returns the biggest whole scale, up to [maxScale], at which [icons] fit when centred on ([centreX], [centreY]) in detail cells, or 0 if they don't fit
     * at all. They fit when every ink cell, and its drop shadow if there is one, lands on [interior]. The ink and its shadow are centred together.
     */
    private fun fitScale(icons: List<PixelIcon>, centreX: Float, centreY: Float, maxScale: Int, interior: Mask, withShadow: Boolean): Int {
        val shadowSize = if (withShadow) 1 else 0
        val iconsWidth = icons.sumOf { it.width } + icons.size - 1
        val iconsHeight = icons.maxOf { it.height }
        for (scale in min(maxScale, interior.height / iconsHeight) downTo 1) {
            val left = centredStart(iconsWidth * scale + shadowSize, centreX)
            val top = centredStart(iconsHeight * scale + shadowSize, centreY)
            val fits = forEachInkCell(icons, left, top, scale) { cellLeft, cellTop ->
                (cellTop until cellTop + scale + shadowSize).all { y -> (cellLeft until cellLeft + scale + shadowSize).all { x -> interior[x, y] } }
            }
            if (fits) return scale
        }
        return 0
    }

    /**
     * Adds [icons] to [ink], and their drop shadow to [inkShadow] if [withShadow] is set, at the biggest scale [fitScale] allows. Returns false, and adds
     * nothing, if they don't fit at all.
     */
    private fun stamp(icons: List<PixelIcon>, centreX: Float, centreY: Float, maxScale: Int, interior: Mask, ink: Mask, inkShadow: Mask, withShadow: Boolean): Boolean {
        val scale = fitScale(icons, centreX, centreY, maxScale, interior, withShadow)
        if (scale == 0) return false

        val shadowSize = if (withShadow) 1 else 0
        val left = centredStart((icons.sumOf { it.width } + icons.size - 1) * scale + shadowSize, centreX)
        val top = centredStart(icons.maxOf { it.height } * scale + shadowSize, centreY)
        forEachInkCell(icons, left, top, scale) { cellLeft, cellTop ->
            for (y in cellTop until cellTop + scale) {
                for (x in cellLeft until cellLeft + scale) {
                    ink[x, y] = true
                    if (withShadow) {
                        inkShadow[x + 1, y + 1] = true
                    }
                }
            }
            true
        }
        return true
    }

    private fun bitmapOf(width: Int, height: Int, color: (Int, Int) -> Int): Bitmap {
        val pixels = IntArray(width * height) { color(it % width, it / width) }
        return Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888)
    }

    private companion object {
        // In view pixels, so that cells that only touch the keep-out area don't count as overlapping it
        const val OVERLAP_TOLERANCE = 0.5f
        // In detail cells. Smaller D-pads get a smaller dent, or none if it would be tiny
        const val DIMPLE_RADIUS = 3.5f
        // How big hand-drawn icons and arrows can get, as a share of the face's smaller side. The same as RaisedButtonView's vector icons
        const val ICON_SIZE = 0.55f
        const val MIN_DIMPLE_RADIUS = 1.5f

        val LABEL_FALLBACKS = mapOf(
            "START" to listOf("STA", "ST"),
            "SELECT" to listOf("SEL", "SL"),
        )
    }
}
