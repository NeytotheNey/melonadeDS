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
