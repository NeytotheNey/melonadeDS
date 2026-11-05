package me.magnum.melonds.utils

import me.magnum.melonds.domain.model.layout.BackgroundMode
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SvgRasterSizeTest {

    @Test
    fun thumbnailWithNoAspectRatioIsPortraitLikeItsSurface() {
        assertEquals(144 to 256, SvgRasterSize.thumbnailSize(null, 256))
    }

    @Test
    fun thumbnailWithNoAspectRatioShowsDetailsAtTheirShareOfAScreen() {
        // The shells' edge frame is 70 units wide, drawn at 1 unit per surface pixel
        val (thumbnailWidth, _) = SvgRasterSize.thumbnailSize(null, 256)!!
        val frameInThumbnail = 70f * thumbnailWidth / SvgRasterSize.THUMBNAIL_SURFACE_WIDTH

        assertEquals(0.065f, frameInThumbnail / thumbnailWidth, 0.001f)
    }

    @Test
    fun thumbnailWithAnAspectRatioKeepsIt() {
        assertEquals(256 to 128, SvgRasterSize.thumbnailSize(2f, 256))
        assertEquals(64 to 256, SvgRasterSize.thumbnailSize(0.25f, 256))
        assertEquals(256 to 256, SvgRasterSize.thumbnailSize(1f, 256))
    }

    @Test
    fun downscalingHalvesUntilTheLastStepThenHitsTheTarget() {
        assertEquals(
            listOf(540 to 960, 270 to 480, 144 to 256),
            SvgRasterSize.downscaleSteps(SvgRasterSize.THUMBNAIL_SURFACE_WIDTH, SvgRasterSize.THUMBNAIL_SURFACE_HEIGHT, 144, 256),
        )
    }

    @Test
    fun downscalingEndsOnAnExactHalfOnce() {
        assertEquals(listOf(540 to 960), SvgRasterSize.downscaleSteps(1080, 1920, 540, 960))
    }

    @Test
    fun downscalingToTheSameSizeIsOneStep() {
        assertEquals(listOf(144 to 256), SvgRasterSize.downscaleSteps(144, 256, 144, 256))
    }

    @Test
    fun fitModesKeepTheAspectRatioInsideTheSurface() {
        // A 16:9 SVG on a 4:3 surface is limited by the width
        assertEquals(1600 to 900, SvgRasterSize.calculate(16f / 9f, 1600, 1200, BackgroundMode.FIT_CENTER, Int.MAX_VALUE))
        // A 3:4 SVG on a landscape surface is limited by the height
        assertEquals(810 to 1080, SvgRasterSize.calculate(3f / 4f, 1920, 1080, BackgroundMode.FIT_TOP, Int.MAX_VALUE))
    }

    @Test
    fun stretchUsesTheWholeSurfaceWhateverTheAspectRatio() {
        assertEquals(1920 to 1080, SvgRasterSize.calculate(1f, 1920, 1080, BackgroundMode.STRETCH, Int.MAX_VALUE))
    }

    @Test
    fun noAspectRatioTakesTheSurfaceSize() {
        assertEquals(1080 to 2400, SvgRasterSize.calculate(null, 1080, 2400, BackgroundMode.FIT_LEFT, Int.MAX_VALUE))
    }

    @Test
    fun sizeIsClampedToTheMaxTextureSizeKeepingItsRatio() {
        assertEquals(4096 to 2048, SvgRasterSize.calculate(null, 8192, 4096, BackgroundMode.STRETCH, 4096))
        assertEquals(1000 to 500, SvgRasterSize.clampToMaxSize(1000, 500, 4096))
    }

    @Test
    fun emptySurfaceGivesNoSize() {
        assertNull(SvgRasterSize.calculate(1f, 0, 1080, BackgroundMode.FIT_CENTER, Int.MAX_VALUE))
        assertNull(SvgRasterSize.calculate(1f, 1920, 0, BackgroundMode.STRETCH, Int.MAX_VALUE))
    }

    @Test
    fun thumbnailSizeKeepsTheLongestSideAtTheLimit() {
        assertEquals(256 to 144, SvgRasterSize.fitInside(16f / 9f, 256, 256))
        assertEquals(128 to 256, SvgRasterSize.fitInside(0.5f, 256, 256))
    }

    @Test
    fun svgIsDetectedFromTheNameAndContents() {
        assertTrue(SvgRasterSize.hasSvgExtension("background.SVG"))
        assertFalse(SvgRasterSize.hasSvgExtension("background.png"))
        assertFalse(SvgRasterSize.hasSvgExtension(null))

        val svg = "<?xml version=\"1.0\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\"/>".toByteArray()
        assertTrue(SvgRasterSize.looksLikeSvg(svg, svg.size))
        val png = byteArrayOf(0x89.toByte(), 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A)
        assertFalse(SvgRasterSize.looksLikeSvg(png, png.size))
        assertFalse(SvgRasterSize.looksLikeSvg(svg, 0))
    }
}
