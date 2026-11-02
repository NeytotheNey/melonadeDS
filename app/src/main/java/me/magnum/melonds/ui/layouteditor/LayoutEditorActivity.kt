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
import me.magnum.melonds.domain.model.Point
import me.magnum.melonds.domain.model.Rect
import me.magnum.melonds.domain.model.layout.Insets
import me.magnum.melonds.domain.model.layout.LayoutDisplayPair
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
        private const val EDITOR_SETTLE_TIMEOUT_MS = 1000L

        const val KEY_LAYOUT_ID = "layout_id"
        // For a new layout: makes it a wide layout at this ratio (WidescreenAspect.key), with its top screen widened
        const val KEY_NEW_LAYOUT_WIDE_ASPECT = "new_layout_wide_aspect"
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

        override fun onEditorDialogShown() {
            shownEditorDialogs++
            isWaitingForEditorToSettle = false
            layoutEditorManager.removeCallbacks(editorSettledRunnable)
        }

        override fun onEditorDialogHidden() {
            if (shownEditorDialogs == 0) return
            shownEditorDialogs--
            if (shownEditorDialogs > 0) return

            isWaitingForEditorToSettle = true
            // In case the editor doesn't get back to its size, because something else changed it meanwhile
            layoutEditorManager.postDelayed(editorSettledRunnable, EDITOR_SETTLE_TIMEOUT_MS)
            checkEditorSettled()
        }
    }

    // Layout-variant inputs are held back while an editor dialog is open, so the keyboard can't swap the layout being edited
    private var shownEditorDialogs = 0
    private var isWaitingForEditorToSettle = false
    private val editorSettledRunnable = Runnable { onEditorSettled() }
    // The insets, folds and displays as last received, while the size and orientation are read when needed
    private var latestInputs = LayoutVariantInputs()
    private var reportedInputs = LayoutVariantInputs()

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
            latestInputs = latestInputs.copy(uiInsets = Insets(insets.left, insets.top, insets.right, insets.bottom))
            updateLayoutVariantInputs()
            checkEditorSettled()

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
                updateLayoutVariantInputs()
                checkEditorSettled()
            }
        }

        setupFullscreen()
        updateLayoutVariantInputs()

        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                combine(viewModel.currentLayout, settingsRepository.getControllerStyle(), ::Pair).collect { (currentLayout, style) ->
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

                    latestInputs = latestInputs.copy(folds = folds)
                    updateLayoutVariantInputs()
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
        updateLayoutVariantInputs()
        handler.post {
            updateDisplays()
        }
    }

    private fun getSystemOrientation(configuration: Configuration): Orientation {
        return if (configuration.orientation == Configuration.ORIENTATION_PORTRAIT) {
            Orientation.PORTRAIT
        } else {
            Orientation.LANDSCAPE
        }
    }

    /**
     * Pushes changed variant inputs to the view model after storing the shown layout, only when one changed since storing deselects the item.
     */
    private fun updateLayoutVariantInputs() {
        if (shownEditorDialogs > 0 || isWaitingForEditorToSettle) return

        val uiSize = Point(layoutEditorManager.width, layoutEditorManager.height).takeIf { it.x > 0 && it.y > 0 }
        val latest = latestInputs.copy(uiSize = uiSize, orientation = getSystemOrientation(resources.configuration))
        val changed = latest.changedSince(reportedInputs)
        if (changed == LayoutVariantInputs()) return

        // Stored first, so that the changes go to the variant they were made in
        storeLayoutChanges()
        reportedInputs = reportedInputs.updatedWith(changed)
        changed.uiSize?.let { viewModel.setCurrentUiSize(it.x, it.y) }
        changed.uiInsets?.let { viewModel.setUiInsets(it) }
        changed.folds?.let { viewModel.setScreenFolds(it) }
        changed.orientation?.let { viewModel.setCurrentSystemOrientation(it) }
        changed.displays?.let { viewModel.setConnectedDisplays(it) }
    }

    /**
     * After the dialogs close, waits until the keyboard is gone or the editor is back to size.
     */
    private fun checkEditorSettled() {
        if (!isWaitingForEditorToSettle || shownEditorDialogs > 0) return

        val insets = ViewCompat.getRootWindowInsets(layoutEditorManager)
        val isKeyboardShown = insets != null && (insets.isVisible(WindowInsetsCompat.Type.ime()) || insets.isVisible(WindowInsetsCompat.Type.navigationBars()))
        val uiSize = Point(layoutEditorManager.width, layoutEditorManager.height)
        if (uiSize == reportedInputs.uiSize || !isKeyboardShown) {
            // Posted so that the views are laid out at their new size first
            layoutEditorManager.removeCallbacks(editorSettledRunnable)
            layoutEditorManager.post(editorSettledRunnable)
        }
    }

    private fun onEditorSettled() {
        isWaitingForEditorToSettle = false
        layoutEditorManager.removeCallbacks(editorSettledRunnable)
        layoutEditorManager.onEditorSettled()
        externalLayoutEditorPresentation?.layoutEditorManager?.onEditorSettled()
        updateLayoutVariantInputs()
    }

    private fun updateDisplays() {
        val currentDisplay = ContextCompat.getDisplayOrDefault(this)
        val secondaryDisplay = secondaryDisplaySelector.getSecondaryDisplay(this)

        latestInputs = latestInputs.copy(displays = deviceLayoutDisplayMapper.mapDisplaysToLayoutDisplays(currentDisplay, secondaryDisplay))
        updateLayoutVariantInputs()

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
                        instantiateLayout(currentConfiguration.layout, layoutEditorManager.layoutEditorView.controllerStyle)
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

/**
 * What the layout editor's shown layout variant is picked for, with null for not known yet (or unchanged, in [changedSince]'s result).
 */
private data class LayoutVariantInputs(
    val uiSize: Point? = null,
    val uiInsets: Insets? = null,
    val folds: List<ScreenFold>? = null,
    val orientation: Orientation? = null,
    val displays: LayoutDisplayPair? = null,
) {
    /**
     * Returns the known inputs that differ from [reported], with the rest null.
     */
    fun changedSince(reported: LayoutVariantInputs) = LayoutVariantInputs(
        uiSize = uiSize?.takeIf { it != reported.uiSize },
        uiInsets = uiInsets?.takeIf { it != reported.uiInsets },
        folds = folds?.takeIf { it != reported.folds },
        orientation = orientation?.takeIf { it != reported.orientation },
        displays = displays?.takeIf { it != reported.displays },
    )

    /**
     * Returns these inputs with the known ones of [changed] put in.
     */
    fun updatedWith(changed: LayoutVariantInputs) = LayoutVariantInputs(
        uiSize = changed.uiSize ?: uiSize,
        uiInsets = changed.uiInsets ?: uiInsets,
        folds = changed.folds ?: folds,
        orientation = changed.orientation ?: orientation,
        displays = changed.displays ?: displays,
    )
}
