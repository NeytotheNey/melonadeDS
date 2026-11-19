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
        assertEquals(342, launch.getRenderWidth(342, 1f))
        assertEquals(342, launch.getRenderWidth(342, 2f))
        assertEquals(342, launch.getMaxRenderWidth(342))
        assertEquals(0, launch.getRenderWidth(0, 1.5f))
    }

    @Test
    fun withTheCameraTheWidestRenderIsTheWidestView() {
        val launch = WidescreenLaunch.resolve(RomModsConfig(wideViewWidth = 342, wideCamera = true), null, isWideRenderer = true)

        assertTrue(launch.isCameraOn)
        assertEquals(RomModsConfig.DEFAULT_WIDE_CAMERA_MAX_WIDTH, launch.getMaxRenderWidth(342))
        assertEquals(RomModsConfig.DEFAULT_WIDE_CAMERA_MAX_WIDTH, launch.getRenderWidth(342, 512f / 342f))
        // Never narrower than shown, and a variant that isn't wide stays at 256
        assertEquals(600, launch.getMaxRenderWidth(600))
        assertEquals(600, launch.getRenderWidth(600, 1.5f))
        assertEquals(0, launch.getRenderWidth(0, 1.5f))
    }

    @Test
    fun at1xTheCameraRendersAtTheShownWidthWithNoMargin() {
        val launch = WidescreenLaunch.resolve(RomModsConfig(wideViewWidth = 342, wideCamera = true, wideCameraMaxWidth = 512), null, isWideRenderer = true)

        // The same as with the camera off, so the camera costs nothing until the player zooms out
        assertEquals(342, launch.getRenderWidth(342, 1f))
        assertEquals(0, launch.getRenderMargin(342, 1f))
        // A zoom a float error past 1 doesn't render a whole step wider
        assertEquals(342, launch.getRenderWidth(342, 1.0001f))
    }

    @Test
    fun theRenderWidthFollowsTheZoomRoundedUpToTheNextStep() {
        val launch = WidescreenLaunch.resolve(RomModsConfig(wideViewWidth = 342, wideCamera = true, wideCameraMaxWidth = 640), null, isWideRenderer = true)

        // 342 * 1.25 = 427.5, rounded up to whole even columns
        assertEquals(428, launch.getRenderWidth(342, 1.25f))
        // Between steps renders for the step above, so a pinch only resizes when it crosses one
        assertEquals(428, launch.getRenderWidth(342, 1.1f))
        assertEquals(514, launch.getRenderWidth(342, 1.5f))
        assertEquals(514, launch.getRenderWidth(342, 1.3f))
        // 342 * 1.75 = 598.5 is the step's own width; past it is capped at the widest view
        assertEquals(600, launch.getRenderWidth(342, 1.75f))
        assertEquals(640, launch.getRenderWidth(342, 1.8f))
        assertEquals(640, launch.getRenderWidth(342, 3f))
        // Each width has room for its step's window: 342 * 1.25 columns and 192 * 1.25 rows
        assertTrue(launch.getRenderWidth(342, 1.25f) >= 342 * 1.25f)
        assertTrue(192 + 2 * launch.getRenderMargin(342, 1.25f) >= 192 * 1.25f)
    }

    @Test
    fun theMarginLetsTheWidestViewFillTheScreen() {
        val launch = WidescreenLaunch.resolve(RomModsConfig(wideViewWidth = 342, wideCamera = true, wideCameraMaxWidth = 512), null, isWideRenderer = true)
        val widest = 512f / 342f

        // ceil(96 * (512 / 342 - 1)) = ceil(47.7)
        assertEquals(48, launch.getRenderMargin(342, widest))
        // 192 * 512 / 342 rows fit in 192 + 2 * 48
        assertTrue(192f * 512f / 342f <= 192 + 2 * launch.getRenderMargin(342, widest))
        // The margin follows the render width: ceil(96 * 86 / 342) at 1.25
        assertEquals(25, launch.getRenderMargin(342, 1.25f))
        // No room to zoom out, or a variant that isn't wide: none
        assertEquals(0, launch.getRenderMargin(512, widest))
        assertEquals(0, launch.getRenderMargin(600, widest))
        assertEquals(0, launch.getRenderMargin(0, widest))
    }

    @Test
    fun theMarginIsWholeRowsAndCapped() {
        // 96 * (684 - 342) / 342 is exactly 96, so it isn't rounded up to 97
        val exact = WidescreenLaunch.resolve(RomModsConfig(wideViewWidth = 342, wideCamera = true, wideCameraMaxWidth = 684), null, isWideRenderer = true)
        assertEquals(96, exact.getRenderMargin(342, 2f))
        // 96 * (768 / 300 - 1) is about 150, over the cap
        val capped = WidescreenLaunch.resolve(RomModsConfig(wideViewWidth = 300, wideCamera = true, wideCameraMaxWidth = 768), null, isWideRenderer = true)
        assertEquals(128, capped.getRenderMargin(300, 3f))
    }

    @Test
    fun withoutTheCameraThereIsNoMargin() {
        val launch = WidescreenLaunch.resolve(RomModsConfig(wideViewWidth = 342, wideCameraMaxWidth = 768), null, isWideRenderer = true)

        assertEquals(0, launch.getRenderMargin(342, 2f))
        assertEquals(0, WidescreenLaunch.OFF.getRenderMargin(342, 2f))
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
