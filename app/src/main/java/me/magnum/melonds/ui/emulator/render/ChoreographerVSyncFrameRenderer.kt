package me.magnum.melonds.ui.emulator.render

import android.os.Build
import android.view.Choreographer
import androidx.annotation.RequiresApi
import me.magnum.melonds.ui.emulator.render.ChoreographerFrameRenderer.Companion.asVsyncPeriodOrZero

@RequiresApi(Build.VERSION_CODES.TIRAMISU)
class ChoreographerVSyncFrameRenderer(
    private val frameRenderCoordinator: FrameRenderCoordinator,
) : ChoreographerFrameRenderer, Choreographer.VsyncCallback {

    override fun startRendering() {
        Choreographer.getInstance().postVsyncCallback(this)
    }

    override fun stopRendering() {
        Choreographer.getInstance().removeVsyncCallback(this)
    }

    override fun onVsync(data: Choreographer.FrameData) {
        val frameDelta = data.preferredFrameTimeline.deadlineNanos - data.frameTimeNanos
        val frameDeadline = if (frameDelta > ChoreographerFrameRenderer.DEADLINE_FRAME_TIME_THRESHOLD) {
            data.preferredFrameTimeline.deadlineNanos
        } else {
            null
        }

        frameRenderCoordinator.renderFrame(frameDeadline, data.frameTimeNanos, getVsyncPeriodNanos(data))
        Choreographer.getInstance().postVsyncCallback(this)
    }

    /**
     * The system's vsync period for this app, from the spacing of the frame timelines (one vsync apart), or 0 if it can't be told. It follows a
     * refresh rate change from the first vsync at the new rate, and late callbacks don't affect it.
     */
    private fun getVsyncPeriodNanos(data: Choreographer.FrameData): Long {
        // The array is reused by Choreographer, so this doesn't allocate
        val timelines = data.frameTimelines
        if (timelines.size < 2) {
            return 0L
        }
        return (timelines[1].expectedPresentationTimeNanos - timelines[0].expectedPresentationTimeNanos).asVsyncPeriodOrZero()
    }
}
