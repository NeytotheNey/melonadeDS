package me.magnum.melonds.ui.layouteditor

import android.content.Intent
import android.content.res.Configuration
import android.graphics.Color
import android.hardware.display.DisplayManager
import android.os.Bundle
import android.os.Handler
import android.view.Display
import android.widget.RelativeLayout
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.annotation.StringRes
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.content.getSystemService
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.window.layout.FoldingFeature
import androidx.window.layout.WindowInfoTracker
import com.squareup.picasso.Picasso
import dagger.hilt.android.AndroidEntryPoint
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.launch
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.Rect
import me.magnum.melonds.domain.model.input.ControllerStyle
import me.magnum.melonds.domain.model.layout.Insets
import me.magnum.melonds.domain.model.layout.ScreenFold
import me.magnum.melonds.domain.model.ui.Orientation
import me.magnum.melonds.domain.repositories.SettingsRepository
import me.magnum.melonds.extensions.setLayoutOrientation
import me.magnum.melonds.extensions.setupGameSystemBars
import me.magnum.melonds.impl.ScreenUnitsConverter
import me.magnum.melonds.impl.layout.DeviceLayoutDisplayMapper
import me.magnum.melonds.impl.layout.SecondaryDisplaySelector
import me.magnum.melonds.ui.backgrounds.BackgroundsActivity
import me.magnum.melonds.ui.layouteditor.model.LayoutTarget
import me.magnum.melonds.ui.layouteditor.model.ScreenEditorState
import java.util.UUID
import javax.inject.Inject

@AndroidEntryPoint
class LayoutEditorActivity : AppCompatActivity() {
    companion object {
        const val KEY_LAYOUT_ID = "layout_id"
    }

    enum class MenuOption(@field:StringRes val stringRes: Int) {
        ADD_COMPONENT(R.string.add_component),
        PROPERTIES(R.string.properties),
        BACKGROUNDS(R.string.background),
        GRID(R.string.layout_editor_grid),
        REVERT(R.string.revert_changes),
        RESET(R.string.reset_default),
        SAVE(R.string.save),
        SAVE_AND_EXIT(R.string.save_and_exit),
        EXIT_WITHOUT_SAVING(R.string.exit_without_saving)
    }

    @Inject
    lateinit var screenUnitsConverter: ScreenUnitsConverter
    @Inject
    lateinit var secondaryDisplaySelector: SecondaryDisplaySelector
    @Inject
    lateinit var deviceLayoutDisplayMapper: DeviceLayoutDisplayMapper
    @Inject
    lateinit var picasso: Picasso
    @Inject
    lateinit var settingsRepository: SettingsRepository

    private val viewModel: LayoutEditorViewModel by viewModels()
    private lateinit var layoutEditorManager: LayoutEditorManagerView
    private lateinit var handler: Handler
    private val displayListener = object : DisplayManager.DisplayListener {
        override fun onDisplayAdded(displayId: Int) {
            updateDisplays()
        }

        override fun onDisplayRemoved(displayId: Int) {
            updateDisplays()
        }

        override fun onDisplayChanged(displayId: Int) {
            updateDisplays()
        }
    }

    private var externalLayoutEditorPresentation: ExternalLayoutEditorPresentation? = null
    private var savedExternalEditorState: ScreenEditorState? = null
    private var controllerStyle = ControllerStyle.CLASSIC

    private val mainScreenBackgroundPickerLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        if (it.resultCode == RESULT_OK) {
            val backgroundId = it.data?.getStringExtra(BackgroundsActivity.KEY_SELECTED_BACKGROUND_ID)?.let { UUID.fromString(it) }
            viewModel.setBackgroundPropertiesBackgroundId(LayoutTarget.MAIN_SCREEN, backgroundId)
        }
    }

    private val secondaryScreenBackgroundPickerLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        if (it.resultCode == RESULT_OK) {
            val backgroundId = it.data?.getStringExtra(BackgroundsActivity.KEY_SELECTED_BACKGROUND_ID)?.let { UUID.fromString(it) }
            viewModel.setBackgroundPropertiesBackgroundId(LayoutTarget.SECONDARY_SCREEN, backgroundId)
        }
    }

    private val layoutEditorManagerListener = object : LayoutEditorManagerView.LayoutEditorManagerListener {
        override fun openBackgroundPicker(layoutTarget: LayoutTarget, selectedBackgroundId: UUID?) {
            storeLayoutChanges()
            val intent = Intent(this@LayoutEditorActivity, BackgroundsActivity::class.java).apply {
                putExtra(BackgroundsActivity.KEY_INITIAL_BACKGROUND_ID, selectedBackgroundId?.toString())
            }
            when (layoutTarget) {
                LayoutTarget.MAIN_SCREEN -> mainScreenBackgroundPickerLauncher.launch(intent)
                LayoutTarget.SECONDARY_SCREEN -> secondaryScreenBackgroundPickerLauncher.launch(intent)
            }
        }

        override fun onStoreLayoutChanges() {
            storeLayoutChanges()
        }

        override fun onSaveLayout() {
            saveLayout()
        }

        override fun onSaveLayoutAndExit() {
            saveLayout()
            finish()
        }

        override fun onExit() {
            finish()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        handler = Handler(mainLooper)

        val container = RelativeLayout(this).apply {
            setBackgroundColor(Color.BLACK)
        }
        layoutEditorManager = LayoutEditorManagerView(LayoutTarget.MAIN_SCREEN, picasso, null, this).apply {
            listener = layoutEditorManagerListener
        }
        container.addView(layoutEditorManager, RelativeLayout.LayoutParams.MATCH_PARENT, RelativeLayout.LayoutParams.MATCH_PARENT)
        setContentView(container)
        ViewCompat.setOnApplyWindowInsetsListener(container) { _, windowInsets ->
            val insets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            val uiInsets = Insets(insets.left, insets.top, insets.right, insets.bottom)
            storeLayoutChanges()
            viewModel.setUiInsets(uiInsets)

            // Do not consume insets here
            windowInsets
        }

        onBackPressedDispatcher.addCallback(object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                layoutEditorManager.handleBackNavigation()
            }
        })

        layoutEditorManager.addOnLayoutChangeListener { _, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom ->
            val oldWith = oldRight - oldLeft
            val oldHeight = oldBottom - oldTop

            val viewWidth = right - left
            val viewHeight = bottom - top

            if (viewWidth != oldWith || viewHeight != oldHeight) {
                storeLayoutChanges()
                viewModel.setCurrentUiSize(viewWidth, viewHeight)
            }
        }

        setupFullscreen()
        updateOrientation(resources.configuration)

        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                combine(viewModel.currentLayout, settingsRepository.getControllerStyle(), ::Pair).collect { (currentLayout, style) ->
                    controllerStyle = style
                    layoutEditorManager.layoutEditorView.controllerStyle = style
                    if (currentLayout == null) {
                        layoutEditorManager.layoutEditorView.destroyLayout()
                        externalLayoutEditorPresentation?.layoutEditorManager?.layoutEditorView?.destroyLayout()
                    } else {
                        handler.removeCallbacksAndMessages(null)
                        handler.post {
                            layoutEditorManager.layoutEditorView.instantiateLayout(currentLayout.layout)
                            externalLayoutEditorPresentation?.instantiateLayout(currentLayout.layout, style)
                            setLayoutOrientation(currentLayout.orientation)
                        }
                    }
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.mainScreenBackground.collect {
                    it?.let {
                        layoutEditorManager.updateBackground(it)
                    }
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.secondaryScreenBackground.collect {
                    it?.let {
                        externalLayoutEditorPresentation?.layoutEditorManager?.updateBackground(it)
                    }
                }
            }
        }
        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                WindowInfoTracker.getOrCreate(this@LayoutEditorActivity).windowLayoutInfo(this@LayoutEditorActivity).collect { info ->
                    val folds = info.displayFeatures.mapNotNull {
                        if (it is FoldingFeature) {
                            ScreenFold(
                                orientation = if (it.orientation == FoldingFeature.Orientation.HORIZONTAL) Orientation.LANDSCAPE else Orientation.PORTRAIT,
                                type = if (it.isSeparating) ScreenFold.FoldType.SEAMLESS else ScreenFold.FoldType.GAP,
                                foldBounds = Rect(it.bounds.left, it.bounds.top, it.bounds.width(), it.bounds.height())
                            )
                        } else {
                            null
                        }
                    }

                    storeLayoutChanges()
                    viewModel.setScreenFolds(folds)
                    // The folds are only on this display, so the editor on an external display doesn't get them
                    layoutEditorManager.layoutEditorView.setScreenFolds(folds)
                }
            }
        }
    }

    override fun onStart() {
        super.onStart()
        getSystemService<DisplayManager>()?.registerDisplayListener(displayListener, null)
    }

    override fun onResume() {
        super.onResume()
        updateDisplays()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        setupFullscreen()
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        storeLayoutChanges()
        updateOrientation(newConfig)
        handler.post {
            updateDisplays()
        }
    }

    private fun updateOrientation(configuration: Configuration) {
        val orientation = if (configuration.orientation == Configuration.ORIENTATION_PORTRAIT) {
            Orientation.PORTRAIT
        } else {
            Orientation.LANDSCAPE
        }
        viewModel.setCurrentSystemOrientation(orientation)
    }

    private fun updateDisplays() {
        val currentDisplay = ContextCompat.getDisplayOrDefault(this)
        val secondaryDisplay = secondaryDisplaySelector.getSecondaryDisplay(this)

        val displays = deviceLayoutDisplayMapper.mapDisplaysToLayoutDisplays(currentDisplay, secondaryDisplay)
        viewModel.setConnectedDisplays(displays)

        showExternalLayoutEditor(secondaryDisplay)
    }

    private fun showExternalLayoutEditor(secondaryDisplay: Display?) {
        if (externalLayoutEditorPresentation?.display?.displayId == secondaryDisplay?.displayId) {
            return
        }

        externalLayoutEditorPresentation?.dismiss()
        externalLayoutEditorPresentation = null

        if (secondaryDisplay != null) {
            externalLayoutEditorPresentation = ExternalLayoutEditorPresentation(picasso, this, secondaryDisplay, layoutEditorManagerListener, savedExternalEditorState).apply {
                setOnShowListener {
                    val currentConfiguration = viewModel.currentLayout.value
                    if (currentConfiguration != null) {
                        instantiateLayout(currentConfiguration.layout, controllerStyle)
                    }
                }
                show()
            }
        }
    }

    private fun storeLayoutChanges() {
        val primaryDisplayLayoutComponents = if (layoutEditorManager.layoutEditorView.isModifiedByUser()) {
            layoutEditorManager.layoutEditorView.buildCurrentLayout()
        } else {
            null
        }

        val externalPresentationLayoutView = externalLayoutEditorPresentation?.layoutEditorManager?.layoutEditorView
        val secondaryDisplayLayoutComponents = if (externalPresentationLayoutView?.isModifiedByUser() == true) {
            externalPresentationLayoutView.buildCurrentLayout()
        } else {
            null
        }
        viewModel.saveLayoutToCurrentConfiguration(primaryDisplayLayoutComponents, secondaryDisplayLayoutComponents)
    }

    private fun setupFullscreen() {
        window.setupGameSystemBars(settingsRepository.showStatusBar())
    }

    private fun saveLayout() {
        storeLayoutChanges()
        viewModel.saveCurrentLayout()
    }

    override fun onStop() {
        super.onStop()
        getSystemService<DisplayManager>()?.unregisterDisplayListener(displayListener)
        storeLayoutChanges()
        externalLayoutEditorPresentation?.let {
            savedExternalEditorState = it.saveEditorState()
            it.dismiss()
        }
        externalLayoutEditorPresentation = null
    }

    override fun onDestroy() {
        super.onDestroy()
        picasso.cancelRequest(layoutEditorManager.imageBackground)
        externalLayoutEditorPresentation?.layoutEditorManager?.imageBackground?.let {
            picasso.cancelRequest(it)
        }
    }
}