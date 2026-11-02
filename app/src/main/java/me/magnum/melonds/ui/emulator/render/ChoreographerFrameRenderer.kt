package me.magnum.melonds.ui.emulator.render

interface ChoreographerFrameRenderer {

    companion object {
        /**
         * The frame period threshold from which deadlines can be applied. Frame periods under this value should not apply deadlines because the frame-rate is high enough that
         * we can miss some frames and keep a smooth experience.
         */
        const val DEADLINE_FRAME_TIME_THRESHOLD = 13333333L

        // Measured periods outside of this range (over 250Hz, or under 20Hz) aren't reported as the vsync period
        val VSYNC_PERIOD_RANGE_NS = 4_000_000L..50_000_000L

        /** This measured period if it's a plausible vsync period, or 0 when it can't be reported as one. */
        fun Long.asVsyncPeriodOrZero(): Long = takeIf { it in VSYNC_PERIOD_RANGE_NS } ?: 0L
    }

    fun startRendering()
    fun stopRendering()
}