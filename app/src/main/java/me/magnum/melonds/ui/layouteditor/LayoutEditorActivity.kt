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

    // The layout shown is picked for the editor's size, insets, folds, orientation and the connected displays. While one of the editors has a
    // dialog open, those aren't updated: its keyboard makes Android show the navigation bar, which shrinks the editor, and the layout picked for
    // that size would replace the one being edited (deselecting the item a value is being entered for). They're caught up once the dialogs are
    // closed and the editor has settled
    private var shownEditorDialogs = 0
    private var isWaitingForEditorToSettle = false
    private val editorSettledRunnable = Runnable { onEditorSettled() }
    private var latestUiInsets: Insets? = null
    private var latestFolds: List<ScreenFold>? = null
    private var latestDisplays: LayoutDisplayPair? = null
    private var reportedUiSize: Point? = null
    private var reportedDisplays: LayoutDisplayPair? = null
    private var reportedUiInsets: Insets? = null
    private var reportedFolds: List<ScreenFold>? = null
    private var reportedOrientation: Orientation? = null

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
            latestUiInsets = Insets(insets.left, insets.top, insets.right, insets.bottom)
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

                    latestFolds = folds
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
     * Gives the view model what the layout shown is picked for, after storing the changes made to the layout shown so far. Only done when one of
     * them changed, since storing shows the layout again, which deselects the selected item. Held while an editor dialog is open, see
     * [shownEditorDialogs]
     */
    private fun updateLayoutVariantInputs() {
        if (shownEditorDialogs > 0 || isWaitingForEditorToSettle) return

        val uiSize = Point(layoutEditorManager.width, layoutEditorManager.height).takeIf { it.x > 0 && it.y > 0 && it != reportedUiSize }
        val uiInsets = latestUiInsets?.takeIf { it != reportedUiInsets }
        val folds = latestFolds?.takeIf { it != reportedFolds }
        val orientation = getSystemOrientation(resources.configuration).takeIf { it != reportedOrientation }
        val displays = latestDisplays?.takeIf { it != reportedDisplays }
        if (uiSize == null && uiInsets == null && folds == null && orientation == null && displays == null) return

        // Stored first, so that the changes go to the variant they were made in
        storeLayoutChanges()
        uiSize?.let {
            reportedUiSize = it
            viewModel.setCurrentUiSize(it.x, it.y)
        }
        uiInsets?.let {
            reportedUiInsets = it
            viewModel.setUiInsets(it)
        }
        folds?.let {
            reportedFolds = it
            viewModel.setScreenFolds(it)
        }
        orientation?.let {
            reportedOrientation = it
            viewModel.setCurrentSystemOrientation(it)
        }
        displays?.let {
            reportedDisplays = it
            viewModel.setConnectedDisplays(it)
        }
    }

    /**
     * Once the editor dialogs are closed, waits for the keyboard to go: the editor is back to the size of the layout shown, or the navigation bar
     * that Android shows with the keyboard is gone (the editor always hides it). If the size changed for another reason, the new layout is shown then.
     */
    private fun checkEditorSettled() {
        if (!isWaitingForEditorToSettle || shownEditorDialogs > 0) return

        val insets = ViewCompat.getRootWindowInsets(layoutEditorManager)
        val isKeyboardShown = insets != null && (insets.isVisible(WindowInsetsCompat.Type.ime()) || insets.isVisible(WindowInsetsCompat.Type.navigationBars()))
        val uiSize = Point(layoutEditorManager.width, layoutEditorManager.height)
        if (uiSize == reportedUiSize || !isKeyboardShown) {
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

        latestDisplays = deviceLayoutDisplayMapper.mapDisplaysToLayoutDisplays(currentDisplay, secondaryDisplay)
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