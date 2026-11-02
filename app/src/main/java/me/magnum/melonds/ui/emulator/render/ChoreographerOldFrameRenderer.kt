package me.magnum.melonds.ui.emulator.render

import android.view.Choreographer
import me.magnum.melonds.ui.emulator.render.ChoreographerFrameRenderer.Companion.asVsyncPeriodOrZero

private const val DEFAULT_FRAME_PERIOD_NS = 16666666L // 16.6ms (60 FPS)

class ChoreographerOldFrameRenderer(private val frameRenderCoordinator: FrameRenderCoordinator) : ChoreographerFrameRenderer, Choreographer.FrameCallback {

    private var framePeriodNs = DEFAULT_FRAME_PERIOD_NS
    // Whether framePeriodNs has been measured since rendering started, rather than being the default
    private var isFramePeriodMeasured = false

    private var framePeriodSampleCount = 0
    private var lastFrameTimeNs = 0L
    private val framePeriodSamples = LongArray(11)

    override fun startRendering() {
        // Reset frame period every time the rendering starts
        framePeriodNs = DEFAULT_FRAME_PERIOD_NS
        isFramePeriodMeasured = false
        lastFrameTimeNs = 0
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun stopRendering() {
        Choreographer.getInstance().removeFrameCallback(this)
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (lastFrameTimeNs > 0L) {
            framePeriodSamples[framePeriodSampleCount++] = frameTimeNanos - lastFrameTimeNs
            if (framePeriodSampleCount == framePeriodSamples.size) {
                // Calculate the median of intervals between frame times to determine the frame period
                framePeriodSamples.sort()
                val median = framePeriodSamples[framePeriodSamples.size / 2]
                framePeriodNs = median
                isFramePeriodMeasured = true
                framePeriodSampleCount = 0
            }
        }
        lastFrameTimeNs = frameTimeNanos

        val frameDeadline = framePeriodNs.takeIf { it > ChoreographerFrameRenderer.DEADLINE_FRAME_TIME_THRESHOLD }?.plus(frameTimeNanos)
        // Only reported once measured, so that the performance logger doesn't take the default for the display's refresh period. The median is
        // taken every 11 intervals, so a refresh rate change is only reported up to about 22 callbacks later
        val vsyncPeriodNs = if (isFramePeriodMeasured) framePeriodNs.asVsyncPeriodOrZero() else 0L
        frameRenderCoordinator.renderFrame(frameDeadline, frameTimeNanos, vsyncPeriodNs)
        Choreographer.getInstance().postFrameCallback(this)
    }
}