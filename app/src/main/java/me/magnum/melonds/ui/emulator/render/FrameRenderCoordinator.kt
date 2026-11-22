package me.magnum.melonds.ui.emulator.render

import android.os.Handler
import android.os.HandlerThread
import android.os.Message
import android.util.Log
import me.magnum.melonds.MelonDSAndroidInterface
import me.magnum.melonds.MelonEmulator
import me.magnum.melonds.domain.model.render.PresentFrameWrapper
import me.magnum.melonds.impl.diagnostics.DisplayPerformanceCapture
import me.magnum.melonds.impl.diagnostics.LogTags
import me.magnum.melonds.ui.emulator.EmulatorSurfaceView
import me.magnum.melonds.ui.emulator.camera.WideCameraController

class FrameRenderCoordinator {

    private val glContext: GlContext

    private val frameRenderThread = FrameRenderThread()
    private val presentFrameWrapper = PresentFrameWrapper()
    private val surfacesLock = Any()
    private val managedSurfaces = mutableListOf<EmulatorSurfaceView>()
    private val surfacesPendingRemoval = mutableListOf<EmulatorSurfaceView>()
    // Surfaces whose renderer may still own GL objects, which are shared with the emulator's context and so outlive the render context
    private val surfacesToRelease = mutableSetOf<EmulatorSurfaceView>()
    private val surfacesPendingRelease = mutableListOf<EmulatorSurfaceView>()
    // Only accessed from the main thread
    private var framePacingEnabled = false
    private var fastForwardActive = false
    // Whether the surfaces currently ask for 60 FPS
    private var surfaceFrameRateVote = false
    // The WideMelon camera, moved on the render thread for each presented frame (PresentFrameWrapper.wideWindow)
    @Volatile
    var wideCameraController: WideCameraController? = null

    init {
        glContext = GlContext(MelonDSAndroidInterface.getEmulatorGlContext())
        frameRenderThread.start()
    }

    fun addSurface(surface: EmulatorSurfaceView) {
        surface.setPacedFrameRateEnabled(surfaceFrameRateVote)
        synchronized(surfacesLock) {
            surface.setSurfaceRenderer(SurfaceBackRenderer(), glContext)
            managedSurfaces.add(surface)
            surfacesToRelease.add(surface)
        }
    }

    /**
     * @param releaseRenderer Whether to also free the GL objects of the surface's renderer, for surfaces that won't be added again
     */
    fun removeSurface(surface: EmulatorSurfaceView, releaseRenderer: Boolean = false) {
        synchronized(surfacesLock) {
            val wasManaged = managedSurfaces.remove(surface)
            if (wasManaged) {
                surfacesPendingRemoval.add(surface)
            }
            val mustRelease = releaseRenderer && surfacesToRelease.remove(surface)
            if (mustRelease) {
                surfacesPendingRelease.add(surface)
            }
            if (wasManaged || mustRelease) {
                frameRenderThread.requestSurfaceDestruction()
            }
        }
    }

    /**
     * Called from the Choreographer callback (main thread) on every vsync.
     *
     * @param vsyncTimeNanos The vsync's timestamp (Choreographer's frame time, CLOCK_MONOTONIC)
     * @param vsyncPeriodNanos The vsync period, or 0 if unknown
     */
    fun renderFrame(frameDeadlineNanos: Long?, vsyncTimeNanos: Long, vsyncPeriodNanos: Long) {
        MelonEmulator.onDisplayVsync(vsyncTimeNanos)
        frameRenderThread.requestFrameRender(frameDeadlineNanos, vsyncTimeNanos, vsyncPeriodNanos)
    }

    /**
     * Asks the system to run the emulator surfaces at a refresh rate compatible with 60 FPS while frame pacing is enabled; main thread only.
     */
    fun setFramePacingEnabled(enabled: Boolean) {
        framePacingEnabled = enabled
        updateSurfaceFrameRateVote()
    }

    /**
     * Drops the 60 FPS frame rate vote while fast-forward is active, since it isn't paced; main thread only.
     */
    fun setFastForwardActive(active: Boolean) {
        fastForwardActive = active
        updateSurfaceFrameRateVote()
    }

    private fun updateSurfaceFrameRateVote() {
        val vote = framePacingEnabled && !fastForwardActive
        if (vote == surfaceFrameRateVote) {
            return
        }

        surfaceFrameRateVote = vote
        val surfaces = synchronized(surfacesLock) { managedSurfaces.toList() }
        surfaces.forEach {
            it.setPacedFrameRateEnabled(vote)
        }
    }

    fun stop() {
        frameRenderThread.requestStop()
        frameRenderThread.quitSafely()
        frameRenderThread.join()
    }

    private inner class FrameRenderThread : HandlerThread("FrameRenderThread") {

        private var handler: Handler? = null
        @Volatile private var running = true
        private val renderStatistics = RenderStatistics()

        // Latest frame render request, written by the main thread and taken by this one on MSG_RENDER_FRAME so no Bundle is allocated per vsync
        private val renderRequestLock = Any()
        private var hasPendingRenderRequest = false
        private var pendingFrameDeadlineNanos = 0L
        private var pendingVsyncTimeNanos = 0L
        private var pendingVsyncPeriodNanos = 0L
        // Vsync period of the request being rendered, for the display samples
        private var renderingVsyncPeriodNanos = 0L
        // The output cap (GPU::OutputCap, on by default): the need last passed to MelonEmulator.setOutputScaleNeed(), -1 so the first frame
        // passes its own over whatever an earlier session left there
        private var lastOutputScaleNeed = -1
        // The output cap: whether any surface debanded last frame, as last passed to MelonEmulator.setOutputDeband(); null so the first frame
        // passes its own
        private var lastOutputDeband: Boolean? = null
        // The output cap: each managed surface's need this frame and the ones last logged, in managedSurfaces' order, so a change in
        // any of them (a second surface showing up, say) logs a line without allocating every frame
        private var surfaceOutputScaleNeeds = IntArray(4)
        private var loggedSurfaceOutputScaleNeeds = IntArray(0)

        private val frameRenderCallback = object : FrameRenderCallback {
            override fun renderFrame(
                isValidFrame: Boolean,
                frameTextureId: Int,
                frameTextureWidth: Int,
                frameTextureHeight: Int,
                topScreenWidth: Int,
                topScreenMargin: Int,
                overlayTextureId: Int,
                overlayScreens: Int,
                halvesAt1x: Int,
                scaleAt1x: Int,
                halfScaleAt1x: Int,
                topWingLines: Int,
                depthTextureId: Int,
                isDepthCameraValid: Boolean,
                frameSequence: Long,
            ): Boolean {
                val renderStart = System.nanoTime()

                presentFrameWrapper.apply {
                    this.isValidFrame = isValidFrame
                    this.textureId = frameTextureId
                    this.textureWidth = frameTextureWidth
                    this.textureHeight = frameTextureHeight
                    this.topScreenWidth = topScreenWidth
                    this.topScreenMargin = topScreenMargin
                    this.overlayTextureId = overlayTextureId
                    this.overlayScreens = overlayScreens
                    this.halvesAt1x = halvesAt1x
                    this.scaleAt1x = scaleAt1x
                    this.halfScaleAt1x = halfScaleAt1x
                    this.depthTextureId = depthTextureId
                    // depthCamera itself was just filled by presentFrame()
                    this.isDepthCameraValid = isDepthCameraValid
                    this.frameSequence = frameSequence
                    // Moved once per presented frame, here, so every surface shows the same window
                    this.wideWindow = if (isValidFrame) wideCameraController?.onFrame(renderStart, topScreenWidth, topScreenMargin, topWingLines) else null
                }

                // debug.melonade.comp1x: the emulator only draws screens at 1x while every surface can show them. The output cap (on by
                // default): the frame's scale is capped by the biggest any surface shows a screen at
                var canDrawHalvesAt1x = true
                var outputScaleNeed = 0
                var outputDeband = false
                if (surfaceOutputScaleNeeds.size < managedSurfaces.size) {
                    surfaceOutputScaleNeeds = IntArray(managedSurfaces.size)
                }
                managedSurfaces.forEachIndexed { index, surface ->
                    surface.doFrame(glContext, presentFrameWrapper)
                    canDrawHalvesAt1x = canDrawHalvesAt1x && surface.canDrawHalvesAt1x()
                    val surfaceNeed = surface.outputScaleNeed()
                    surfaceOutputScaleNeeds[index] = surfaceNeed
                    outputScaleNeed = maxOf(outputScaleNeed, surfaceNeed)
                    outputDeband = outputDeband || surface.debandsOutput()
                }
                logSurfaceOutputScaleNeeds(managedSurfaces.size, outputScaleNeed)
                if (outputScaleNeed != lastOutputScaleNeed) {
                    lastOutputScaleNeed = outputScaleNeed
                    MelonEmulator.setOutputScaleNeed(outputScaleNeed)
                }
                // Deband lets the cap take a scale that doesn't divide the internal one (GPU::OutputCapDeband); every surface shows the same
                // frame, so any one debanding counts
                if (outputDeband != lastOutputDeband) {
                    Log.i(LogTags.DEBUG, "OUTCAP: frontend sends deband $outputDeband (was $lastOutputDeband)")
                    lastOutputDeband = outputDeband
                    MelonEmulator.setOutputDeband(outputDeband)
                }

                val renderDuration = System.nanoTime() - renderStart
                renderStatistics.trackRenderEvent(renderDuration)

                if (DisplayPerformanceCapture.isEnabled) {
                    val stateFlags = MelonEmulator.getPerformanceEmulationStateFlags()
                    DisplayPerformanceCapture.record(
                        timestampNs = renderStart,
                        renderDurationNs = renderDuration,
                        isValidFrame = isValidFrame,
                        frameSequence = frameSequence,
                        isEmulationPaused = (stateFlags and MelonEmulator.PERF_STATE_FLAG_PAUSED) != 0,
                        isDsSleeping = (stateFlags and MelonEmulator.PERF_STATE_FLAG_DS_SLEEP) != 0,
                        isFastForward = (stateFlags and MelonEmulator.PERF_STATE_FLAG_FAST_FORWARD) != 0,
                        refreshPeriodNs = renderingVsyncPeriodNanos,
                    )
                }

                return canDrawHalvesAt1x
            }
        }

        /**
         * The output cap: logs the [count] surfaces' needs and the [sent] one, their max, when any of them changed since the last line.
         */
        private fun logSurfaceOutputScaleNeeds(count: Int, sent: Int) {
            val logged = loggedSurfaceOutputScaleNeeds
            var changed = logged.size != count
            var index = 0
            while (!changed && index < count) {
                changed = logged[index] != surfaceOutputScaleNeeds[index]
                index++
            }
            if (!changed) {
                return
            }

            loggedSurfaceOutputScaleNeeds = surfaceOutputScaleNeeds.copyOf(count)
            Log.i(LogTags.DEBUG, "OUTCAP: frontend sends need $sent, max over $count surfaces: ${loggedSurfaceOutputScaleNeeds.joinToString()}")
        }

        override fun onLooperPrepared() {
            handler = object : Handler(looper) {
                override fun handleMessage(msg: Message) {
                    when (msg.what) {
                        MSG_RENDER_FRAME -> renderPendingFrame()
                        MSG_DESTROY_SURFACES -> destroySurfaces()
                        MSG_STOP -> stopThread()
                    }
                }
            }
        }

        fun requestFrameRender(frameDeadlineNanos: Long?, vsyncTimeNanos: Long, vsyncPeriodNanos: Long) {
            val handler = handler ?: return
            synchronized(renderRequestLock) {
                hasPendingRenderRequest = true
                pendingFrameDeadlineNanos = frameDeadlineNanos ?: 0L
                pendingVsyncTimeNanos = vsyncTimeNanos
                pendingVsyncPeriodNanos = vsyncPeriodNanos
            }
            handler.removeMessages(MSG_RENDER_FRAME)
            handler.sendEmptyMessage(MSG_RENDER_FRAME)
        }

        fun requestSurfaceDestruction() {
            handler?.removeMessages(MSG_DESTROY_SURFACES)
            handler?.sendEmptyMessage(MSG_DESTROY_SURFACES)
        }

        fun requestStop() {
            running = false
            handler?.sendEmptyMessage(MSG_STOP)
        }

        private fun renderPendingFrame() {
            val frameDeadlineNanos: Long
            val vsyncTimeNanos: Long
            synchronized(renderRequestLock) {
                // Already taken by an earlier message that was handled after the request was made
                if (!hasPendingRenderRequest) {
                    return
                }
                hasPendingRenderRequest = false
                frameDeadlineNanos = pendingFrameDeadlineNanos
                vsyncTimeNanos = pendingVsyncTimeNanos
                renderingVsyncPeriodNanos = pendingVsyncPeriodNanos
            }
            renderFrame(frameDeadlineNanos, vsyncTimeNanos)
        }

        private fun renderFrame(frameDeadlineNanos: Long, vsyncTimeNanos: Long) {
            if (!running)
                return

            synchronized(surfacesLock) {
                val deadline = if (frameDeadlineNanos > 0) {
                    // Use 2 times the average render duration to be safe. A large margin is required because only the CPU time is being measured and the GPU is performing
                    // additional work, which is not being measured. As a future improvement, GPU time can be taken into account as well to obtain a more accurate deadline
                    frameDeadlineNanos - (renderStatistics.getMeanRenderDurationNs() * 2f).toLong()
                } else {
                    0L
                }
                MelonEmulator.presentFrame(deadline, vsyncTimeNanos, presentFrameWrapper.depthCamera, frameRenderCallback)
            }
        }

        private fun destroySurfaces() {
            synchronized(surfacesLock) {
                surfacesPendingRemoval.forEach {
                    it.stop(glContext)
                }
                surfacesPendingRemoval.clear()
                surfacesPendingRelease.forEach {
                    it.releaseRenderer(glContext)
                }
                surfacesPendingRelease.clear()
            }
        }

        private fun stopThread() {
            synchronized(surfacesLock) {
                managedSurfaces.forEach {
                    it.stop(glContext)
                }
                surfacesPendingRemoval.forEach {
                    it.stop(glContext)
                }
                // The renderers are torn down for good, so free their GL objects while the render context still exists
                (surfacesToRelease + surfacesPendingRelease).forEach {
                    it.releaseRenderer(glContext)
                }
                managedSurfaces.clear()
                surfacesPendingRemoval.clear()
                surfacesToRelease.clear()
                surfacesPendingRelease.clear()
            }

            glContext.release()
            glContext.destroy()
        }
    }

    private class RenderStatistics {
        private var meanRenderDurationNs = 0L
        private var collectedSamples = 0

        fun trackRenderEvent(durationNs: Long) {
            if (collectedSamples < 60) {
                collectedSamples++
            }

            meanRenderDurationNs = (meanRenderDurationNs * (collectedSamples - 1) + durationNs) / collectedSamples
        }

        fun getMeanRenderDurationNs(): Long {
            return meanRenderDurationNs
        }
    }

    private companion object {
        const val MSG_RENDER_FRAME = 1
        const val MSG_DESTROY_SURFACES = 2
        const val MSG_STOP = 3
    }
}