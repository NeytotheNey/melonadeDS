package me.magnum.melonds.ui.emulator.render

import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.Message
import me.magnum.melonds.MelonDSAndroidInterface
import me.magnum.melonds.MelonEmulator
import me.magnum.melonds.domain.model.render.PresentFrameWrapper
import me.magnum.melonds.domain.model.render.RenderStrategy
import me.magnum.melonds.impl.perf.DisplayPerformanceCapture
import me.magnum.melonds.ui.emulator.EmulatorSurfaceView

class FrameRenderCoordinator {

    private val glContext: GlContext
    private val frontBufferSupported = FrontBufferCapability.isFrontBufferSupported()
    @Volatile private var currentRenderStrategy: RenderStrategy = computeEffectiveRenderStrategy(RenderStrategy.BACK_BUFFER_RENDERING)

    private val frameRenderThread = FrameRenderThread()
    private val presentFrameWrapper = PresentFrameWrapper()
    private val surfacesLock = Any()
    private val managedSurfaces = mutableListOf<EmulatorSurfaceView>()
    private val surfacesPendingRemoval = mutableListOf<EmulatorSurfaceView>()
    // Surfaces whose renderer may still own GL objects, including removed ones that can be added again. Their textures, buffers and programs are
    // shared with the emulator's context, which lives as long as the process, so destroying the render context doesn't free them. They are
    // released when the surface is removed for good, or when the render thread stops
    private val surfacesToRelease = mutableSetOf<EmulatorSurfaceView>()
    private val surfacesPendingRelease = mutableListOf<EmulatorSurfaceView>()
    // Only accessed from the main thread
    private var framePacingEnabled = false
    private var fastForwardActive = false
    // Whether the surfaces currently ask for 60 FPS
    private var surfaceFrameRateVote = false

    init {
        glContext = GlContext(MelonDSAndroidInterface.getEmulatorGlContext())
        frameRenderThread.start()
    }

    fun setRenderStrategy(strategy: RenderStrategy) {
        val newRenderStrategy = computeEffectiveRenderStrategy(strategy)
        if (newRenderStrategy != currentRenderStrategy) {
            currentRenderStrategy = newRenderStrategy
            frameRenderThread.requestModeSwitch()
        }
    }

    fun addSurface(surface: EmulatorSurfaceView) {
        surface.setPacedFrameRateEnabled(surfaceFrameRateVote)
        synchronized(surfacesLock) {
            surface.setSurfaceRenderer(createRendererForStrategy(currentRenderStrategy), glContext)
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
     */
    fun renderFrame(frameDeadlineNanos: Long?, vsyncTimeNanos: Long) {
        MelonEmulator.onDisplayVsync(vsyncTimeNanos)
        frameRenderThread.requestFrameRender(frameDeadlineNanos, vsyncTimeNanos)
    }

    /**
     * Asks the system to run the emulator surfaces at a refresh rate compatible with 60 FPS while frame pacing is enabled. Must be called from the
     * main thread.
     */
    fun setFramePacingEnabled(enabled: Boolean) {
        framePacingEnabled = enabled
        updateSurfaceFrameRateVote()
    }

    /**
     * Fast-forward isn't paced, so the 60 FPS frame rate vote is dropped while it's active to let the display run at its full refresh rate. Must be
     * called from the main thread.
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

    private fun computeEffectiveRenderStrategy(strategy: RenderStrategy): RenderStrategy {
        return if (strategy == RenderStrategy.FRONT_BUFFER_RENDERING && frontBufferSupported) {
            RenderStrategy.FRONT_BUFFER_RENDERING
        } else {
            RenderStrategy.BACK_BUFFER_RENDERING
        }
    }

    private fun createRendererForStrategy(strategy: RenderStrategy): SurfaceRenderer {
        return if (strategy == RenderStrategy.FRONT_BUFFER_RENDERING && Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            SurfaceFrontRenderer()
        } else {
            SurfaceBackRenderer()
        }
    }

    private inner class FrameRenderThread : HandlerThread("FrameRenderThread") {

        private var handler: Handler? = null
        @Volatile private var running = true
        private val renderStatistics = RenderStatistics()

        // Latest frame render request. Written by the main thread and taken by this one when it handles MSG_RENDER_FRAME, so that no Bundle is
        // allocated on every vsync. Only the latest request matters, like with the pending message that each new request replaces
        private val renderRequestLock = Any()
        private var hasPendingRenderRequest = false
        private var pendingFrameDeadlineNanos = 0L
        private var pendingVsyncTimeNanos = 0L

        private val frameRenderCallback = object : FrameRenderCallback {
            override fun renderFrame(isValidFrame: Boolean, frameTextureId: Int, frameTextureWidth: Int, frameTextureHeight: Int, frameSequence: Long) {
                val renderStart = System.nanoTime()

                presentFrameWrapper.apply {
                    this.isValidFrame = isValidFrame
                    this.textureId = frameTextureId
                    this.textureWidth = frameTextureWidth
                    this.textureHeight = frameTextureHeight
                    this.frameSequence = frameSequence
                }

                managedSurfaces.forEach {
                    it.doFrame(glContext, presentFrameWrapper)
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
                    )
                }
            }
        }

        override fun onLooperPrepared() {
            handler = object : Handler(looper) {
                override fun handleMessage(msg: Message) {
                    when (msg.what) {
                        MSG_RENDER_FRAME -> renderPendingFrame()
                        MSG_DESTROY_SURFACES -> destroySurfaces()
                        MSG_STOP -> stopThread()
                        MSG_RENDER_STRATEGY_SWITCH -> handleModeSwitch()
                    }
                }
            }
        }

        fun requestFrameRender(frameDeadlineNanos: Long?, vsyncTimeNanos: Long) {
            val handler = handler ?: return
            synchronized(renderRequestLock) {
                hasPendingRenderRequest = true
                pendingFrameDeadlineNanos = frameDeadlineNanos ?: 0L
                pendingVsyncTimeNanos = vsyncTimeNanos
            }
            handler.removeMessages(MSG_RENDER_FRAME)
            handler.sendEmptyMessage(MSG_RENDER_FRAME)
        }

        fun requestSurfaceDestruction() {
            handler?.removeMessages(MSG_DESTROY_SURFACES)
            handler?.sendEmptyMessage(MSG_DESTROY_SURFACES)
        }

        fun requestModeSwitch() {
            handler?.removeMessages(MSG_RENDER_STRATEGY_SWITCH)
            handler?.sendEmptyMessage(MSG_RENDER_STRATEGY_SWITCH)
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
                MelonEmulator.presentFrame(deadline, vsyncTimeNanos, frameRenderCallback)
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

        private fun handleModeSwitch() {
            synchronized(surfacesLock) {
                managedSurfaces.forEach { surface ->
                    surface.setSurfaceRenderer(createRendererForStrategy(currentRenderStrategy), glContext)
                }
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
        const val MSG_RENDER_STRATEGY_SWITCH = 4
    }
}