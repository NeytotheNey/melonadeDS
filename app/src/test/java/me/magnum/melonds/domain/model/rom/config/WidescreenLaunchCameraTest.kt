package me.magnum.melonds.domain.model.rom.config

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class WidescreenLaunchCameraTest {

    @Test
    fun withoutTheCameraTheCoreRendersAtTheShownWidth() {
        val launch = WidescreenLaunch.resolve(RomModsConfig(wideViewWidth = 342, wideCameraMaxWidth = 768), null, isWideRenderer = true)

        assertFalse(launch.isCameraOn)
        assertEquals(342, launch.getRenderWidth(342))
        assertEquals(0, launch.getRenderWidth(0))
    }

    @Test
    fun withTheCameraTheCoreRendersAtTheWidestView() {
        val launch = WidescreenLaunch.resolve(RomModsConfig(wideViewWidth = 342, wideCamera = true), null, isWideRenderer = true)

        assertTrue(launch.isCameraOn)
        assertEquals(RomModsConfig.DEFAULT_WIDE_CAMERA_MAX_WIDTH, launch.getRenderWidth(342))
        // Never narrower than shown, and a variant that isn't wide stays at 256
        assertEquals(600, launch.getRenderWidth(600))
        assertEquals(0, launch.getRenderWidth(0))
    }

    @Test
    fun theMarginLetsTheWidestViewFillTheScreen() {
        val launch = WidescreenLaunch.resolve(RomModsConfig(wideViewWidth = 342, wideCamera = true, wideCameraMaxWidth = 512), null, isWideRenderer = true)

        // ceil(96 * (512 / 342 - 1)) = ceil(47.7)
        assertEquals(48, launch.getRenderMargin(342))
        // 192 * 512 / 342 rows fit in 192 + 2 * 48
        assertTrue(192f * 512f / 342f <= 192 + 2 * launch.getRenderMargin(342))
        // No room to zoom out, or a variant that isn't wide: none
        assertEquals(0, launch.getRenderMargin(512))
        assertEquals(0, launch.getRenderMargin(600))
        assertEquals(0, launch.getRenderMargin(0))
    }

    @Test
    fun theMarginIsWholeRowsAndCapped() {
        // 96 * (684 - 342) / 342 is exactly 96, so it isn't rounded up to 97
        val exact = WidescreenLaunch.resolve(RomModsConfig(wideViewWidth = 342, wideCamera = true, wideCameraMaxWidth = 684), null, isWideRenderer = true)
        assertEquals(96, exact.getRenderMargin(342))
        // 96 * (768 / 300 - 1) is about 150, over the cap
        val capped = WidescreenLaunch.resolve(RomModsConfig(wideViewWidth = 300, wideCamera = true, wideCameraMaxWidth = 768), null, isWideRenderer = true)
        assertEquals(128, capped.getRenderMargin(300))
    }

    @Test
    fun withoutTheCameraThereIsNoMargin() {
        val launch = WidescreenLaunch.resolve(RomModsConfig(wideViewWidth = 342, wideCameraMaxWidth = 768), null, isWideRenderer = true)

        assertEquals(0, launch.getRenderMargin(342))
        assertEquals(0, WidescreenLaunch.OFF.getRenderMargin(342))
    }

    @Test
    fun anUnusableMaxWidthFallsBackToTheDefault() {
        assertEquals(RomModsConfig.DEFAULT_WIDE_CAMERA_MAX_WIDTH, RomModsConfig(wideCameraMaxWidth = 1000).validWideCameraMaxWidth)
        assertEquals(RomModsConfig.DEFAULT_WIDE_CAMERA_MAX_WIDTH, RomModsConfig(wideCameraMaxWidth = 513).validWideCameraMaxWidth)
        assertEquals(640, RomModsConfig(wideCameraMaxWidth = 640).validWideCameraMaxWidth)
    }

    @Test
    fun theCameraNeedsWidescreenAndAWideRenderer() {
        assertFalse(WidescreenLaunch.resolve(RomModsConfig(wideCamera = true), null, isWideRenderer = true).isCameraOn)
        assertFalse(WidescreenLaunch.resolve(RomModsConfig(wideViewWidth = 342, wideCamera = true), null, isWideRenderer = false).isCameraOn)
    }
}
