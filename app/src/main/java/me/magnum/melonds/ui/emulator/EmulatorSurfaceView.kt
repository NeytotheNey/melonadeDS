package me.magnum.melonds.ui.emulator

import android.content.Context
import android.os.Build
import android.util.AttributeSet
import android.util.Log
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import me.magnum.melonds.domain.model.EMULATOR_TARGET_FPS
import me.magnum.melonds.domain.model.render.PresentFrameWrapper
import me.magnum.melonds.ui.emulator.model.RuntimeRendererConfiguration
import me.magnum.melonds.ui.emulator.render.EmulatorRenderer
import me.magnum.melonds.ui.emulator.render.GlContext
import me.magnum.melonds.ui.emulator.render.SurfaceRenderer

class EmulatorSurfaceView(context: Context, attrs: AttributeSet? = null) : SurfaceView(context, attrs), SurfaceHolder.Callback {

    private companion object {
        const val TAG = "EmulatorSurfaceView"
        const val PACED_FRAME_RATE = EMULATOR_TARGET_FPS.toFloat()
    }

    private val surfaceLock = Any()
    private var surfaceWidth = 0
    private var surfaceHeight = 0
    private var surface: Surface? = null
    private var renderer: EmulatorRenderer? = null
    private var surfaceRenderer: SurfaceRenderer? = null
    // Only accessed from the main thread, like the surface callbacks
    private var isPacedFrameRateEnabled = false

    init {
        holder.addCallback(this)
    }

    fun setRenderer(emulatorRenderer: EmulatorRenderer) {
        renderer = emulatorRenderer
    }

    fun updateRendererConfiguration(newRendererConfiguration: RuntimeRendererConfiguration?) {
        renderer?.updateRendererConfiguration(newRendererConfiguration)
    }

    fun setSurfaceRenderer(newRenderer: SurfaceRenderer, glContext: GlContext) {
        synchronized(surfaceLock) {
            surfaceRenderer?.stop(glContext)
            surfaceRenderer = newRenderer
        }
    }

    /**
     * While enabled (frame pacing on and not fast-forwarding), the surface asks for a refresh rate compatible with 60 FPS, so that every emulator
     * frame can stay on screen for the same number of refreshes. Must be called from the main thread.
     */
    fun setPacedFrameRateEnabled(enabled: Boolean) {
        if (enabled == isPacedFrameRateEnabled) {
            return
        }

        isPacedFrameRateEnabled = enabled
        holder.surface?.takeIf { it.isValid }?.let {
            applyFrameRate(it)
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        synchronized(surfaceLock) {
            surface = holder.surface
        }
        // A new surface has no frame rate set
        if (isPacedFrameRateEnabled) {
            applyFrameRate(holder.surface)
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        synchronized(surfaceLock) {
            surfaceWidth = width
            surfaceHeight = height
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        synchronized(surfaceLock) {
            surface = null
        }
    }

    private fun applyFrameRate(surface: Surface) {
        // 0 clears the frame rate
        val frameRate = if (isPacedFrameRateEnabled) PACED_FRAME_RATE else 0f
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                surface.setFrameRate(frameRate, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE, Surface.CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS)
            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                surface.setFrameRate(frameRate, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE)
            }
        } catch (e: Exception) {
            // Thrown if the surface was released in the meantime. The frame rate is applied again when the next surface is created
            Log.w(TAG, "Failed to set the surface frame rate", e)
        }
    }

    fun doFrame(glContext: GlContext, presentFrameWrapper: PresentFrameWrapper) {
        synchronized(surfaceLock) {
            surfaceRenderer?.doFrame(
                glContext = glContext,
                surfaceView = this,
                surface = surface,
                surfaceWidth = surfaceWidth,
                surfaceHeight = surfaceHeight,
                renderer = renderer,
                presentFrameWrapper = presentFrameWrapper,
            )
        }
    }

    fun stop(glContext: GlContext) {
        synchronized(surfaceLock) {
            surfaceRenderer?.stop(glContext)
        }
    }

    /**
     * Stops the surface and frees the renderer's GL objects, for when it won't draw again. Must be called on the render thread.
     */
    fun releaseRenderer(glContext: GlContext) {
        synchronized(surfaceLock) {
            surfaceRenderer?.stop(glContext)
            try {
                // There may be no window surface left, so use the pbuffer. Without a current context the GL objects wouldn't be freed
                glContext.useWithoutSurface()
            } catch (e: GlContext.GlContextException) {
                Log.w(TAG, "Failed to make the GL context current to release the renderer", e)
                return
            }
            renderer?.release()
        }
    }
}
