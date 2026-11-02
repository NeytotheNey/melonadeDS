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
 * Draws a [RaisedButtonView]'s parts for [ControllerAppearance.Pixel] as cached pixel-art bitmaps snapped to a whole-pixel grid, with labels and
 * icons on a finer detail grid.
 */
class PixelButtonPainter(private val art: PixelArt, private val density: Float) {

    /**
     * One part's cached lip, face and pressed face bitmaps and where they're drawn.
     */
    private class PartBitmaps(val lip: Bitmap, val face: Bitmap, val pressedFace: Bitmap, val bounds: Rect, val pressedBounds: Rect)

    /**
     * A grid of cells that are either in or out, where cells outside the grid read as out.
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

        /**
         * Calls [action] with every cell's position, row by row.
         */
        inline fun forEachCell(action: (x: Int, y: Int) -> Unit) {
            for (y in 0 until height) {
                for (x in 0 until width) {
                    action(x, y)
                }
            }
        }

        /**
         * Sets every cell to what [value] gives for its position, row by row.
         */
        inline fun fill(value: (x: Int, y: Int) -> Boolean) {
            forEachCell { x, y -> this[x, y] = value(x, y) }
        }
    }

    /**
     * A part's shape snapped to its own cell grid at ([originX], [originY]) in view pixels, with a one-cell border and room for the lip.
     */
    private class PartGrid(val originX: Float, val originY: Float, val mask: Mask)

    /**
     * Cells whose four neighbors are also in the shape, on the cell grid and the [detail] grid.
     */
    private class Interior(val cells: Mask, val detail: Mask)

    /**
     * The D-pad center's round dent, centered on ([x], [y]) in detail cells.
     */
    private class Dimple(val x: Float, val y: Float, val radius: Float) {
        fun contains(cellX: Int, cellY: Int): Boolean {
            val dx = cellX - x
            val dy = cellY - y
            return dx * dx + dy * dy <= radius * radius
        }
    }

    /**
     * Colors one part: its shape and lip on the cell grid, and its ink, ink shadow and dimple on the detail grid over the interior.
     */
    private class PartShader(
        private val art: PixelArt,
        private val colors: PixelColors,
        private val mask: Mask,
        partInterior: Interior,
        private val ink: Mask,
        private val inkShadow: Mask,
        private val inkColor: Int,
        private val dimple: Dimple?,
    ) {
        private val interior = partInterior.cells
        private val detailInterior = partInterior.detail
        private val perCell = art.detailPerCell
        private val columns = mask.width
        private val shapeColors = IntArray(columns * mask.height) { shapeColor(it % columns, it / columns, false) }
        private val pressedShapeColors = IntArray(columns * mask.height) { shapeColor(it % columns, it / columns, true) }

        /**
         * The lip's color at a cell, transparent outside the lip.
         */
        fun lipColor(x: Int, y: Int): Int {
            if (!isLip(x, y)) return Color.TRANSPARENT
            if (!art.sixteenBit) return colors.outline
            // 16-bit lips are outlined where they meet empty space
            return if (!isButton(x, y + 1) || !isButton(x - 1, y) || !isButton(x + 1, y)) colors.outline else colors.lip
        }

        /**
         * The face's color at a detail cell, with ink and its drop shadow on top of the interior.
         */
        fun faceColor(x: Int, y: Int, pressed: Boolean): Int {
            val shape = if (pressed) pressedShapeColors else shapeColors
            val cell = (y / perCell) * columns + x / perCell
            if (!detailInterior[x, y]) return shape[cell]
            if (ink[x, y]) return inkColor
            if (inkShadow[x, y]) return colors.inkShadow
            return dimpleColor(x, y) ?: shape[cell]
        }

        private fun dimpleColor(x: Int, y: Int): Int? {
            val dimple = dimple ?: return null
            if (!dimple.contains(x, y)) return null
            if (dimple.contains(x - 1, y) && dimple.contains(x + 1, y) && dimple.contains(x, y - 1) && dimple.contains(x, y + 1)) return colors.base
            return if (x - dimple.x + y - dimple.y < 0) colors.shadow else colors.highlight
        }

        // How many steps it takes to reach a cell that isn't interior, where a cell touching the outline counts 1
        private fun distance(x: Int, y: Int, dx: Int, dy: Int): Int {
            var steps = 1
            while (interior[x + dx * steps, y + dy * steps]) {
                steps++
            }
            return steps
        }

        private fun shapeColor(x: Int, y: Int, pressed: Boolean): Int {
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

        // The lip is the shape moved down by 1 to lip cells, wherever the shape itself isn't
        private fun isLip(x: Int, y: Int): Boolean {
            if (mask[x, y]) return false
            for (depth in 1..art.lip) {
                if (mask[x, y - depth]) return true
            }
            return false
        }

        private fun isButton(x: Int, y: Int) = mask[x, y] || isLip(x, y)
    }

    /**
     * Where icons fit: their scale and the top-left corner of their box, drop shadow included, in detail cells.
     */
    private class Placement(val scale: Int, val left: Int, val top: Int)

    /**
     * The size of one detail cell and of one cell of the shape, in physical pixels.
     */
    private var detailSize = 1
    private var cellSize = 1

    /**
     * The style's detail cell size in physical pixels, before it's shrunk for small views and rounded.
     */
    private val nominalDetailSize = art.pixelSizeDp / art.detailPerCell * density

    /**
     * How far the lip goes below the faces, in physical pixels.
     */
    val lipHeight get() = art.lip * cellSize

    // No anti-aliasing, so each grid cell is either fully in or fully out
    private val maskPaint = Paint()
    private val bitmapPaint = Paint().apply { isFilterBitmap = false }
    private var bitmaps = emptyArray<PartBitmaps>()

    /**
     * Picks the cell size so small views still have at least [PixelArt.minCells] cells across; call it before [lipHeight] or [update] are used.
     */
    fun setViewSize(w: Int, h: Int) {
        val detailCells = art.minCells * art.detailPerCell
        detailSize = floor(min(nominalDetailSize, min(w, h).toFloat() / detailCells)).toInt().coerceAtLeast(1)
        cellSize = detailSize * art.detailPerCell
    }

    /**
     * Rebuilds every part's bitmaps from [faces] and [glyphs] in view pixels, with [transform] taking design units to view pixels.
     */
    fun update(parts: List<RaisedButtonView.Part>, faces: Array<Path>, glyphs: Array<Path>, transform: Matrix, isToggledOn: Boolean) {
        val grids = Array(parts.size) { snap(faces[it]) }
        separateDpadArms(parts, grids)
        val interiors = Array(parts.size) { interiorOf(grids[it].mask) }
        val letterScale = sharedLetterScale(parts, grids, interiors, transform)
        val arrowScale = sharedArrowScale(parts, grids, interiors, faces, glyphs)
        bitmaps = Array(parts.size) {
            val iconScale = if (parts[it].isDpad) arrowScale else Int.MAX_VALUE
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
     * Trims each D-pad arm so at least one empty cell separates it from the center; hit areas are unchanged.
     */
    private fun separateDpadArms(parts: List<RaisedButtonView.Part>, grids: Array<PartGrid>) {
        val centerIndex = parts.indexOfFirst { it.isDpad && it.input == null }
        if (centerIndex < 0) return

        // The center's cells in view pixels, grown by one cell all round
        val center = grids[centerIndex]
        val keepOut = RectF()
        center.mask.forEachCell { x, y ->
            if (center.mask[x, y]) {
                keepOut.union(center.originX + x * cellSize, center.originY + y * cellSize, center.originX + (x + 1) * cellSize, center.originY + (y + 1) * cellSize)
            }
        }
        if (keepOut.isEmpty) return
        keepOut.inset(-cellSize + OVERLAP_TOLERANCE, -cellSize + OVERLAP_TOLERANCE)

        for (i in parts.indices) {
            if (i == centerIndex || !parts[i].isDpad) continue
            val arm = grids[i]
            arm.mask.forEachCell { x, y ->
                val left = arm.originX + x * cellSize
                val top = arm.originY + y * cellSize
                if (keepOut.intersects(left, top, left + cellSize, top + cellSize)) {
                    arm.mask[x, y] = false
                }
            }
        }
    }

    private fun interiorOf(mask: Mask): Interior {
        val perCell = art.detailPerCell
        val cells = Mask(mask.width, mask.height)
        cells.fill { x, y -> mask[x, y] && mask[x - 1, y] && mask[x + 1, y] && mask[x, y - 1] && mask[x, y + 1] }
        val detail = Mask(mask.width * perCell, mask.height * perCell)
        detail.fill { x, y -> cells[x / perCell, y / perCell] }
        return Interior(cells, detail)
    }

    /**
     * Where a part's label is centered, in detail cells.
     */
    private fun labelCenter(part: RaisedButtonView.Part, grid: PartGrid, transform: Matrix): FloatArray {
        val center = floatArrayOf(part.labelX, part.labelY)
        transform.mapPoints(center)
        return grid.toDetail(center[0], center[1])
    }

    /**
     * Single letters in one view share the biggest big-font scale that fits all of them; letters that don't fit fall back to the small font.
     */
    private fun sharedLetterScale(parts: List<RaisedButtonView.Part>, grids: Array<PartGrid>, interiors: Array<Interior>, transform: Matrix): Int {
        var scale = Int.MAX_VALUE
        for (i in parts.indices) {
            val letter = parts[i].label?.singleOrNull()?.let { PixelIcons.LETTERS[it] } ?: continue
            val center = labelCenter(parts[i], grids[i], transform)
            fit(listOf(letter), center[0], center[1], Int.MAX_VALUE, interiors[i].detail, true)?.let {
                scale = min(scale, it.scale)
            }
        }
        return scale
    }

    /**
     * Where a part's icon or arrow is centered, in detail cells: on its glyph if it has one, otherwise on its face.
     */
    private fun iconCenter(part: RaisedButtonView.Part, grid: PartGrid, face: Path, glyph: Path): FloatArray {
        val bounds = RectF()
        if (part.glyph != null) glyph.computeBounds(bounds, true) else face.computeBounds(bounds, true)
        return grid.toDetail(bounds.centerX(), bounds.centerY())
    }

    /**
     * Converts a point in view pixels to detail cells on this grid.
     */
    private fun PartGrid.toDetail(x: Float, y: Float): FloatArray {
        return floatArrayOf((x - originX) / detailSize, (y - originY) / detailSize)
    }

    /**
     * The biggest scale that keeps [icon] within [RaisedButtonView.ICON_SIZE] of the face's smaller side, never below 1.
     */
    private fun iconScaleLimit(icon: PixelIcon, face: Path): Int {
        val bounds = RectF().also { face.computeBounds(it, true) }
        // Counted in unrounded detail cells, since rounding the cell size down would otherwise add cells and let icons grow at normal sizes
        val faceCells = min(bounds.width(), bounds.height()) / max(detailSize.toFloat(), nominalDetailSize)
        return floor(faceCells * RaisedButtonView.ICON_SIZE / max(icon.width, icon.height)).toInt().coerceAtLeast(1)
    }

    /**
     * The four D-pad arrows are all drawn the same size: the biggest that fits every arm, with its drop shadow, and keeps within [iconScaleLimit].
     */
    private fun sharedArrowScale(parts: List<RaisedButtonView.Part>, grids: Array<PartGrid>, interiors: Array<Interior>, faces: Array<Path>, glyphs: Array<Path>): Int {
        var scale = Int.MAX_VALUE
        for (i in parts.indices) {
            val part = parts[i]
            val arrow = part.pixelIcon
            if (!part.isDpad || arrow == null) continue
            val center = iconCenter(part, grids[i], faces[i], glyphs[i])
            fit(listOf(arrow), center[0], center[1], iconScaleLimit(arrow, faces[i]), interiors[i].detail, true)?.let {
                scale = min(scale, it.scale)
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
        val colors = art.colorsFor(part.input, part.isDpad)
        val columns = grid.mask.width
        val rows = grid.mask.height
        val perCell = art.detailPerCell
        val detailInterior = partInterior.detail

        // What's drawn on the face goes on the detail grid, with its drop shadow kept apart so that it can be left off
        val faceBounds = RectF().also { face.computeBounds(it, true) }
        val icon = part.iconFor(isToggledOn)
        val pixelIcon = part.pixelIconFor(isToggledOn)
        // Dimmed icons are drawn in the shadow color instead, since see-through pixels don't suit pixel art
        val isDimmed = icon != null && icon.alpha < 255
        val ink = Mask(detailInterior.width, detailInterior.height)
        val inkShadow = Mask(detailInterior.width, detailInterior.height)
        val iconAt = iconCenter(part, grid, face, glyph)
        // Hand-drawn icons, then their compact version, are tried with and then without their shadow before the vector icon is rasterized
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
            raster.forEachCell { x, y ->
                if (raster[x, y]) {
                    ink[x, y] = true
                    inkShadow[x + 1, y + 1] = true
                }
            }
        }
        if (part.label != null) {
            val center = labelCenter(part, grid, transform)
            // Long labels fall back to shorter ones on buttons too small for them
            val candidates = listOf(part.label) + LABEL_FALLBACKS[part.label].orEmpty()
            candidates.firstOrNull { text ->
                val bigFits = text.length == 1 && stampText(text, PixelIcons.LETTERS, center, letterScale, detailInterior, ink, inkShadow)
                bigFits || stampText(text, PixelIcons.SMALL_LETTERS, center, Int.MAX_VALUE, detailInterior, ink, inkShadow)
            }
        }
        val inkColor = if (isDimmed) colors.shadow else colors.ink

        // The D-pad's center gets a small round dent, shadowed top-left and lit bottom-right; cell x spans x to x + 1, hence the half-cell shift
        val dimpleRadius = min(DIMPLE_RADIUS, faceBounds.width() / detailSize / 6f)
        val faceCenter = grid.toDetail(faceBounds.centerX(), faceBounds.centerY())
        val hasDimple = part.isDpad && part.input == null && dimpleRadius >= MIN_DIMPLE_RADIUS
        val dimple = if (hasDimple) Dimple(faceCenter[0] - 0.5f, faceCenter[1] - 0.5f, dimpleRadius) else null
        val shader = PartShader(art, colors, grid.mask, partInterior, ink, inkShadow, inkColor, dimple)

        val left = grid.originX.roundToInt()
        val top = grid.originY.roundToInt()
        val bounds = Rect(left, top, left + columns * cellSize, top + rows * cellSize)
        val pressedBounds = Rect(bounds).apply { offset(0, art.travel * cellSize) }
        return PartBitmaps(
            lip = bitmapOf(detailInterior.width, detailInterior.height) { x, y -> shader.lipColor(x / perCell, y / perCell) },
            face = bitmapOf(detailInterior.width, detailInterior.height) { x, y -> shader.faceColor(x, y, false) },
            pressedFace = bitmapOf(detailInterior.width, detailInterior.height) { x, y -> shader.faceColor(x, y, true) },
            bounds = bounds,
            pressedBounds = pressedBounds,
        )
    }

    /**
     * Rasterizes [draw] onto a [width] by [height] grid of [size]-pixel cells at ([originX], [originY]); a cell is in when at least half-covered.
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
        mask.fill { x, y -> (alphas[y * bitmap.rowBytes + x].toInt() and 0xFF) >= 128 }
        bitmap.recycle()
        return mask
    }

    private fun stampText(text: String, font: Map<Char, PixelIcon>, center: FloatArray, maxScale: Int, interior: Mask, ink: Mask, inkShadow: Mask): Boolean {
        val letters = text.map { font[it] ?: return false }
        return stamp(letters, center[0], center[1], maxScale, interior, ink, inkShadow, true)
    }

    /**
     * The left or top edge of a [size]-cell box centered on [center], worked out in half cells and rounding up or left.
     */
    private fun centeredStart(size: Int, center: Float) = Math.floorDiv((center * 2).roundToInt() - size, 2)

    /**
     * Visits every ink cell of [icons] laid side by side with a one-cell gap; stops and returns false when [cell] returns false.
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
     * The biggest scale up to [maxScale] at which [icons] centered on ([centerX], [centerY]) land on [interior] with their shadow, and where they
     * go, or null.
     */
    private fun fit(icons: List<PixelIcon>, centerX: Float, centerY: Float, maxScale: Int, interior: Mask, withShadow: Boolean): Placement? {
        val shadowSize = if (withShadow) 1 else 0
        val iconsWidth = icons.sumOf { it.width } + icons.size - 1
        val iconsHeight = icons.maxOf { it.height }
        for (scale in min(maxScale, interior.height / iconsHeight) downTo 1) {
            val left = centeredStart(iconsWidth * scale + shadowSize, centerX)
            val top = centeredStart(iconsHeight * scale + shadowSize, centerY)
            val fits = forEachInkCell(icons, left, top, scale) { cellLeft, cellTop ->
                (cellTop until cellTop + scale + shadowSize).all { y -> (cellLeft until cellLeft + scale + shadowSize).all { x -> interior[x, y] } }
            }
            if (fits) return Placement(scale, left, top)
        }
        return null
    }

    /**
     * Stamps [icons] into [ink], and their shadow into [inkShadow] if [withShadow], at the biggest scale [fit] allows; returns false if none fits.
     */
    private fun stamp(icons: List<PixelIcon>, centerX: Float, centerY: Float, maxScale: Int, interior: Mask, ink: Mask, inkShadow: Mask, withShadow: Boolean): Boolean {
        val placement = fit(icons, centerX, centerY, maxScale, interior, withShadow) ?: return false
        val scale = placement.scale
        forEachInkCell(icons, placement.left, placement.top, scale) { cellLeft, cellTop ->
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
        // In detail cells; smaller D-pads get a smaller dent, or none if it would be tiny
        const val DIMPLE_RADIUS = 3.5f
        const val MIN_DIMPLE_RADIUS = 1.5f

        val LABEL_FALLBACKS = mapOf(
            "START" to listOf("STA", "ST"),
            "SELECT" to listOf("SEL", "SL"),
        )
    }
}
