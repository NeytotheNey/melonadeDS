package me.magnum.melonds.ui.emulator.render

import android.app.Presentation
import android.content.Context
import android.graphics.Color
import android.os.Build
import android.view.Display
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout
import androidx.core.view.isVisible
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ViewModelStoreOwner
import androidx.lifecycle.setViewTreeLifecycleOwner
import androidx.lifecycle.setViewTreeViewModelStoreOwner
import androidx.savedstate.SavedStateRegistryOwner
import androidx.savedstate.setViewTreeSavedStateRegistryOwner
import me.magnum.melonds.domain.model.RuntimeBackground
import me.magnum.melonds.domain.model.render.Bloom
import me.magnum.melonds.domain.model.render.DepthOfField
import me.magnum.melonds.domain.model.render.Shade
import me.magnum.melonds.domain.model.render.Sun
import me.magnum.melonds.domain.model.render.TiltShift
import me.magnum.melonds.ui.emulator.DSRenderer
import me.magnum.melonds.ui.emulator.EmulatorSurfaceView
import me.magnum.melonds.ui.emulator.RuntimeLayoutView
import me.magnum.melonds.ui.emulator.model.RuntimeInputLayoutConfiguration
import me.magnum.melonds.ui.emulator.model.RuntimeRendererConfiguration
import me.magnum.melonds.ui.layouteditor.model.LayoutTarget

class ExternalPresentation(
    context: Context,
    display: Display,
    private val frameRenderCoordinator: FrameRenderCoordinator,
) : Presentation(context, display) {

    val layoutView = RuntimeLayoutView(context)
    private val container = FrameLayout(context)
    private val pauseOverlay = View(context)
    private val emulatorRenderer: DSRenderer
    private val surfaceView: EmulatorSurfaceView
    private var currentBackground: RuntimeBackground? = null
    private var currentRendererConfiguration: RuntimeRendererConfiguration? = null

    init {
        window?.setFlags(
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
        )

        val layoutChangeListener = View.OnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
            updateRendererScreenAreas()
        }

        container.addOnLayoutChangeListener(layoutChangeListener)
        emulatorRenderer = DSRenderer(context).also {
            surfaceView = createSurfaceView(it)
        }
        // Touches on this display go where its own renderer draws the bottom screen
        layoutView.setFrameWidthProvider { emulatorRenderer.frameTopScreenWidth }
        layoutView.setWideWindowProvider { emulatorRenderer.frameWideWindow }
        surfaceView.updateRendererConfiguration(currentRendererConfiguration)

        container.addView(surfaceView)
        container.addView(layoutView)
        container.addView(pauseOverlay)

        pauseOverlay.apply {
            setBackgroundColor(Color.BLACK)
            alpha = 0.6f
            isVisible = false
            setOnClickListener {
                // Do nothing. Just intercept clicks
            }
        }

        frameRenderCoordinator.addSurface(surfaceView)

        (context as? LifecycleOwner)?.let { owner ->
            container.setViewTreeLifecycleOwner(owner)
        }
        (context as? ViewModelStoreOwner)?.let { owner ->
            container.setViewTreeViewModelStoreOwner(owner)
        }
        (context as? SavedStateRegistryOwner)?.let { owner ->
            container.setViewTreeSavedStateRegistryOwner(owner)
        }

        setContentView(container)
    }

    fun swapScreens() {
        layoutView.swapScreens()
        updateRendererScreenAreas()
    }

    fun updateRendererScreenAreas() {
        val touchScreenArea = layoutView.applyScreenAreasTo(emulatorRenderer)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            layoutView.systemGestureExclusionRects = touchScreenArea
        }
    }

    fun setPauseOverlayVisibility(visible: Boolean) {
        pauseOverlay.isVisible = visible
    }

    private fun createSurfaceView(renderer: EmulatorRenderer): EmulatorSurfaceView {
        return EmulatorSurfaceView(context).apply {
            setRenderer(renderer)
            isFocusable = false
            isFocusableInTouchMode = false
        }
    }

    fun updateLayout(layoutConfiguration: RuntimeInputLayoutConfiguration) {
        layoutView.instantiateLayout(layoutConfiguration, LayoutTarget.SECONDARY_SCREEN)
        updateRendererScreenAreas()
    }

    fun updateRendererConfiguration(newRendererConfiguration: RuntimeRendererConfiguration?) {
        currentRendererConfiguration = newRendererConfiguration
        surfaceView.updateRendererConfiguration(newRendererConfiguration)
    }

    fun updateTiltShift(tiltShift: TiltShift?) {
        emulatorRenderer.tiltShift = tiltShift
    }

    fun updateBloom(bloom: Bloom?) {
        emulatorRenderer.bloom = bloom
    }

    fun updateDepthOfField(depthOfField: DepthOfField?) {
        emulatorRenderer.depthOfField = depthOfField
    }

    fun updateShade(shade: Shade?) {
        emulatorRenderer.shade = shade
    }

    fun updateSun(sun: Sun?) {
        emulatorRenderer.sun = sun
    }

    override fun onStop() {
        super.onStop()
        // A presentation is only stopped when it's dismissed, and it's never shown again
        frameRenderCoordinator.removeSurface(surfaceView, releaseRenderer = true)
    }

    fun updateBackground(background: RuntimeBackground) {
        currentBackground = background
        emulatorRenderer.setBackground(background)
    }
}