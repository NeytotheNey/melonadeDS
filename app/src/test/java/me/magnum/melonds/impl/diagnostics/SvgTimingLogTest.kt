package me.magnum.melonds.impl.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Test

class SvgTimingLogTest {

    @Test
    fun loadKindFollowsTheLastRasterizedSize() {
        assertEquals("first", SvgTimingLog.loadKind(0, 0, 1080, 2400))
        assertEquals("resize", SvgTimingLog.loadKind(1080, 2400, 2400, 1080))
        assertEquals("reload", SvgTimingLog.loadKind(1080, 2400, 1080, 2400))
    }

    @Test
    fun lineHasEveryTimeInMillisecondsWithOneDecimal() {
        val line = SvgTimingLog.formatLine(
            source = "renderer",
            backgroundName = "Pearl Tech",
            loadKind = "first",
            width = 1080,
            height = 2400,
            isSvgNanos = 1_250_000,
            parseNanos = 3_460_000,
            rasterNanos = 45_000_000,
            uploadNanos = 7_000_000,
        )

        assertEquals("renderer bg=\"Pearl Tech\" load=first target=1080x2400 isSvg=1.3ms parse=3.5ms raster=45.0ms upload=7.0ms total=56.7ms", line)
    }

    @Test
    fun lineSaysWhenTheParseWasCachedAndLeavesOutAMissingUpload() {
        val line = SvgTimingLog.formatLine(
            source = "editor",
            backgroundName = "Hex",
            loadKind = "resize",
            width = 400,
            height = 300,
            isSvgNanos = 0,
            parseNanos = null,
            rasterNanos = 2_000_000,
            uploadNanos = null,
        )

        assertEquals("editor bg=\"Hex\" load=resize target=400x300 isSvg=0.0ms parse=cached raster=2.0ms total=2.0ms", line)
    }
}
