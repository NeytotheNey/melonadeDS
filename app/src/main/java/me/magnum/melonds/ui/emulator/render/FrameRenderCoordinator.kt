package me.magnum.melonds.ui.emulator.render

import android.os.Build
import android.os.Bundle
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
        }
    }

    fun removeSurface(surface: EmulatorSurfaceView) {
        synchronized(surfacesLock) {
            if (managedSurfaces.remove(surface)) {
                surfacesPendingRemoval.add(surface)
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

        private val frameRenderCallback = object : FrameRenderCallback {
            override fun renderFrame(isValidFrame: Boolean, frameTextureId: Int, frameTextureWidth: Int, frameTextureHeight: Int, frameSequence: Long) {
                val renderStart = System.nanoTime()

                presentFrameWrapper.apply {
                    this.isValidFrame = isValidFrame
                    this.textureId = frameTextureId
                    this.textureWidth = frameTextureWidth
                    this.textureHeight = frameTextureHeight
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
                        MSG_RENDER_FRAME -> {
                            val vsyncTimeNanos = (msg.arg1.toLong() shl 32) or (msg.arg2.toLong() and 0xFFFFFFFFL)
                            renderFrame(msg.data.getLong(MSG_RENDER_FRAME_FRAME_DEADLINE_NS), vsyncTimeNanos)
                        }
                        MSG_DESTROY_SURFACES -> destroySurfaces()
                        MSG_STOP -> stopThread()
                        MSG_RENDER_STRATEGY_SWITCH -> handleModeSwitch()
                    }
                }
            }
        }

        fun requestFrameRender(frameDeadlineNanos: Long?, vsyncTimeNanos: Long) {
            handler?.removeMessages(MSG_RENDER_FRAME)
            handler?.obtainMessage(MSG_RENDER_FRAME)?.let {
                // The vsync time is split into the message's int arguments so that no extra allocation is needed
                it.arg1 = (vsyncTimeNanos ushr 32).toInt()
                it.arg2 = vsyncTimeNanos.toInt()
                it.data = Bundle().apply {
                    putLong(MSG_RENDER_FRAME_FRAME_DEADLINE_NS, frameDeadlineNanos ?: 0L)
                }
                handler?.sendMessage(it)
            }
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
            managedSurfaces.forEach {
                it.stop(glContext)
            }
            surfacesPendingRemoval.forEach {
                it.stop(glContext)
            }
            managedSurfaces.clear()
            surfacesPendingRemoval.clear()

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

        const val MSG_RENDER_FRAME_FRAME_DEADLINE_NS = "frame-deadline"
    }
}