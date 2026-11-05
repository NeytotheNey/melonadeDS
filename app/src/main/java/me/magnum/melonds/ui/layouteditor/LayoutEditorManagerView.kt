package me.magnum.melonds.ui.layouteditor

import android.annotation.SuppressLint
import android.content.Context
import android.util.AttributeSet
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.ViewTreeObserver
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.core.content.ContextCompat
import androidx.core.content.edit
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.findViewTreeLifecycleOwner
import androidx.lifecycle.findViewTreeViewModelStoreOwner
import androidx.lifecycle.get
import androidx.lifecycle.lifecycleScope
import androidx.preference.PreferenceManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.squareup.picasso.Callback
import com.squareup.picasso.Picasso
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.magnum.melonds.R
import me.magnum.melonds.common.PreferenceKeys
import me.magnum.melonds.databinding.DialogLayoutEditorGridBinding
import me.magnum.melonds.databinding.ViewLayoutEditorManagerBinding
import me.magnum.melonds.domain.model.RuntimeBackground
import me.magnum.melonds.domain.model.layout.BackgroundMode
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.extensions.setBackgroundMode
import me.magnum.melonds.impl.diagnostics.SvgTimingLog
import me.magnum.melonds.ui.common.component.dialog.TextInputDialog
import me.magnum.melonds.ui.common.component.dialog.TextInputDialogState
import me.magnum.melonds.ui.layouteditor.LayoutEditorActivity.MenuOption
import me.magnum.melonds.ui.layouteditor.model.LayoutComponentEditableProperty
import me.magnum.melonds.ui.layouteditor.model.LayoutTarget
import me.magnum.melonds.ui.layouteditor.model.ScreenEditorState
import me.magnum.melonds.ui.layouteditor.ui.LayoutBackgroundDialog
import me.magnum.melonds.ui.layouteditor.ui.LayoutComponentPropertyValueDialog
import me.magnum.melonds.ui.layouteditor.ui.LayoutPropertiesDialog
import me.magnum.melonds.ui.theme.MelonTheme
import me.magnum.melonds.utils.BackgroundDecoder
import me.magnum.melonds.utils.SvgBackground
import me.magnum.melonds.utils.getLayoutComponentName
import java.util.UUID
import kotlin.math.max

private const val FLOATING_MENU_BUTTON_IDLE_ALPHA = 0.6f

class LayoutEditorManagerView(
    private val layoutTarget: LayoutTarget,
    private val picasso: Picasso,
    initialEditorState: ScreenEditorState? = null,
    context: Context,
    attrs: AttributeSet? = null
) : FrameLayout(context, attrs) {

    interface LayoutEditorManagerListener {
        fun openBackgroundPicker(layoutTarget: LayoutTarget, selectedBackgroundId: UUID?)
        fun onStoreLayoutChanges()
        fun onSaveLayout()
        fun onSaveLayoutAndExit()
        fun onExit()
        // One of the editor's own dialogs opened or closed, and onEditorSettled() must follow once the editor is back to size
        fun onEditorDialogShown()
        fun onEditorDialogHidden()
    }

    // The item the value dialog was opened for, and the ratio its size was locked to then
    private class EditablePropertyTarget(val component: LayoutComponent, val screenAspectRatio: Float?)

    // An SVG background being shown, kept parsed so it can be rasterized again at each new size of the background view
    private class ShownSvgBackground(
        val svg: SvgBackground,
        val mode: BackgroundMode,
        private val name: String?,
        private var isSvgNanos: Long,
        private var parseNanos: Long?,
    ) {
        private var lastRenderedWidth = 0
        private var lastRenderedHeight = 0

        /**
         * Logs the timing of a finished render, where the first one also reports what it took to read the file. Renders of an old size can
         * overlap newer ones.
         */
        @Synchronized
        fun logRender(width: Int, height: Int, rasterNanos: Long) {
            SvgTimingLog.log(
                source = SvgTimingLog.SOURCE_EDITOR,
                backgroundName = name,
                lastWidth = lastRenderedWidth,
                lastHeight = lastRenderedHeight,
                width = width,
                height = height,
                isSvgNanos = isSvgNanos,
                parseNanos = parseNanos,
                rasterNanos = rasterNanos,
            )
            isSvgNanos = 0L
            parseNanos = null
            lastRenderedWidth = width
            lastRenderedHeight = height
        }
    }

    // What was found out from a background's file, and how long each part took
    private class BackgroundFileLoad(val isSvg: Boolean, val svg: SvgBackground?, val isSvgNanos: Long, val parseNanos: Long)

    private val binding: ViewLayoutEditorManagerBinding
    private val viewModel: LayoutEditorViewModel by lazy {
        val owner = findViewTreeViewModelStoreOwner() ?: error("No view-model store owner found")
        ViewModelProvider(owner).get<LayoutEditorViewModel>()
    }

    var listener: LayoutEditorManagerListener? = null
    private val itemControls: LayoutEditorItemControls
    private var topControlInset = 0
    private var isDraggingFloatingMenuButton = false
    private val preferences = PreferenceManager.getDefaultSharedPreferences(context)
    // Runs after every layout pass, so the item controls follow the selected item however it was moved or resized
    private val globalLayoutListener = ViewTreeObserver.OnGlobalLayoutListener { itemControls.onLayout() }
    private var wideAspectJob: Job? = null
    // Finds out whether the background is an SVG and parses it
    private var backgroundLoadJob: Job? = null
    // The background whose load hasn't finished, which is started again on attach since detaching cancels it. Rendering an SVG isn't part of this
    private var unfinishedBackground: RuntimeBackground? = null
    private var svgRenderJob: Job? = null
    // Null while a raster background, or none, is shown
    private var shownSvgBackground: ShownSvgBackground? = null
    // Size of the background view the SVG background was last rasterized for
    private var svgRenderedSize: Pair<Int, Int>? = null

    private var showLayoutPropertiesDialog by mutableStateOf(initialEditorState?.isPropertiesDialogShown ?: false)
    private var showBackgroundPropertiesDialog by mutableStateOf(initialEditorState?.isBackgroundPropertiesDialogShown ?: false)
    private var shownEditablePropertyDialog by mutableStateOf<LayoutComponentEditableProperty?>(null)
    private var editablePropertyTarget: EditablePropertyTarget? = null
    // The editor's size when its dialogs were opened, which values entered in them are applied against until onEditorSettled()
    private var editorSizeBeforeDialogs: Pair<Int, Int>? = null
    private val nameInputDialogState = TextInputDialogState()

    val layoutEditorView get() = binding.viewLayoutEditor
    val imageBackground get() = binding.imageBackground

    init {
        val layoutInflater = LayoutInflater.from(context)
        isFocusable = true
        isFocusableInTouchMode = true
        binding = ViewLayoutEditorManagerBinding.inflate(layoutInflater)
        val composeView = ComposeView(context).apply {
            setContent {
                MelonTheme {
                    val isEditorDialogShown = showLayoutPropertiesDialog || showBackgroundPropertiesDialog || shownEditablePropertyDialog != null ||
                            nameInputDialogState.isDialogVisible
                    if (isEditorDialogShown) {
                        DisposableEffect(Unit) {
                            val editor = binding.viewLayoutEditor
                            if (editorSizeBeforeDialogs == null && editor.width > 0 && editor.height > 0) {
                                editorSizeBeforeDialogs = editor.width to editor.height
                            }
                            listener?.onEditorDialogShown()
                            onDispose {
                                val currentListener = listener
                                if (currentListener != null) {
                                    currentListener.onEditorDialogHidden()
                                } else {
                                    // Nothing would say when it has settled
                                    onEditorSettled()
                                }
                            }
                        }
                    }

                    if (showLayoutPropertiesDialog) {
                        val layoutConfiguration by viewModel.currentLayoutConfiguration.collectAsStateWithLifecycle()
                        val currentConfiguration = layoutConfiguration

                        if (currentConfiguration != null) {
                            LayoutPropertiesDialog(
                                layoutConfiguration = currentConfiguration,
                                onDismiss = { showLayoutPropertiesDialog = false },
                                onSave = { name, orientation, useCustomOpacity, opacity ->
                                    viewModel.savePropertiesToCurrentConfiguration(name, orientation, useCustomOpacity, opacity)
                                    showLayoutPropertiesDialog = false
                                }
                            )
                        }
                    }

                    if (showBackgroundPropertiesDialog) {
                        val backgroundProperties by when (layoutTarget) {
                            LayoutTarget.MAIN_SCREEN -> viewModel.mainScreenBackgroundProperties.collectAsStateWithLifecycle()
                            LayoutTarget.SECONDARY_SCREEN -> viewModel.secondaryScreenBackgroundProperties.collectAsStateWithLifecycle()
                        }
                        val currentBackgroundProperties = backgroundProperties

                        if (currentBackgroundProperties != null) {
                            LayoutBackgroundDialog(
                                backgroundId = currentBackgroundProperties.backgroundId,
                                backgroundMode = currentBackgroundProperties.backgroundMode,
                                loadBackgroundName = { backgroundId -> viewModel.getBackgroundName(backgroundId) },
                                onOpenBackgroundPicker = { listener?.openBackgroundPicker(layoutTarget, currentBackgroundProperties.backgroundId) },
                                onBackgroundModeUpdate = { viewModel.setBackgroundPropertiesBackgroundMode(layoutTarget, it) },
                                onDismiss = {
                                    viewModel.resetBackgroundProperties(layoutTarget)
                                    showBackgroundPropertiesDialog = false
                                },
                                onSave = {
                                    viewModel.saveBackgroundToCurrentConfiguration(layoutTarget)
                                    showBackgroundPropertiesDialog = false
                                }
                            )
                        }
                    }

                    val currentlyShownEditablePropertyDialog = shownEditablePropertyDialog
                    LayoutComponentPropertyValueDialog(
                        editableProperty = currentlyShownEditablePropertyDialog,
                        initialValue = currentlyShownEditablePropertyDialog?.let { itemControls.getPropertyValue(it) } ?: 0,
                        onValueChanged = { value ->
                            val target = editablePropertyTarget
                            if (currentlyShownEditablePropertyDialog != null && target != null) {
                                val editor = binding.viewLayoutEditor
                                val (editorWidth, editorHeight) = editorSizeBeforeDialogs ?: (editor.width to editor.height)
                                itemControls.setPropertyValue(target.component, currentlyShownEditablePropertyDialog, value, target.screenAspectRatio, editorWidth, editorHeight)
                            }
                            editablePropertyTarget = null
                            shownEditablePropertyDialog = null
                        },
                        onCancel = {
                            editablePropertyTarget = null
                            shownEditablePropertyDialog = null
                        },
                    )

                    TextInputDialog(
                        title = stringResource(R.string.layout_name),
                        dialogState = nameInputDialogState,
                    )
                }
            }
        }
        addView(binding.root, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
        addView(composeView, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))

        itemControls = LayoutEditorItemControls(
            binding = binding,
            onEditPropertyValue = {
                editablePropertyTarget = itemControls.selectedComponent?.let { component ->
                    EditablePropertyTarget(component, itemControls.selectedScreenAspectRatio)
                }
                shownEditablePropertyDialog = it
            },
            getWideAspect = { viewModel.currentLayout.value?.layout?.wideAspect },
            onWideAspectPicked = { wideAspect ->
                // Widening works from the stored rects, and the editor on the other display may have changed meanwhile
                listener?.onStoreLayoutChanges()
                // Reselect the top screen across the reshow the ratio change causes, disarmed if nothing is reshown so it can't fire later
                binding.viewLayoutEditor.reselectAfterNextLayout(LayoutComponent.TOP_SCREEN)
                if (!viewModel.setWideAspect(wideAspect)) {
                    binding.viewLayoutEditor.reselectAfterNextLayout(null)
                }
            },
        )

        ViewCompat.setOnApplyWindowInsetsListener(binding.root) { _, windowInsets ->
            // Includes the status bar so the editor's own controls, not the layouts, stay clear of it
            val insets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout() or WindowInsetsCompat.Type.statusBars())
            if (insets.top != topControlInset) {
                topControlInset = insets.top
                positionFloatingMenuButton()
            }
            itemControls.setControlInsets(insets)

            WindowInsetsCompat.CONSUMED
        }

        setupFloatingMenuButton()
        binding.viewLayoutEditor.setLayoutComponentViewBuilderFactory(EditorLayoutComponentViewBuilderFactory())
        binding.viewLayoutEditor.setOnViewSelectedListener { view ->
            itemControls.show(view)
        }
        binding.viewLayoutEditor.setOnViewDeselectedListener {
            itemControls.hide()
        }
        binding.imageBackground.addOnLayoutChangeListener { _, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom ->
            if (right - left != oldRight - oldLeft || bottom - top != oldBottom - oldTop) {
                renderSvgBackground()
            }
        }
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        viewTreeObserver.addOnGlobalLayoutListener(globalLayoutListener)
        // The item controls read the widescreen ratio of the variant shown, which the Wide menu, a fold or a rotation can change while an item is
        // selected
        wideAspectJob = findViewTreeLifecycleOwner()?.lifecycleScope?.launch {
            viewModel.currentLayout.map { it?.layout?.wideAspect }.distinctUntilChanged().drop(1).collect {
                itemControls.refresh()
            }
        }
        val backgroundToReload = unfinishedBackground
        if (backgroundToReload != null) {
            updateBackground(backgroundToReload)
        } else {
            // Detaching cancels a render that may not have finished; does nothing until the view has a size
            renderSvgBackground()
        }
    }

    override fun onDetachedFromWindow() {
        viewTreeObserver.removeOnGlobalLayoutListener(globalLayoutListener)
        wideAspectJob?.cancel()
        wideAspectJob = null
        backgroundLoadJob?.cancel()
        backgroundLoadJob = null
        svgRenderJob?.cancel()
        svgRenderJob = null
        // Not rendered for any size anymore, so a shown SVG background is rendered again if the view is laid out again
        svgRenderedSize = null
        super.onDetachedFromWindow()
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        return if (binding.viewLayoutEditor.handleKeyDown(event)) {
            true
        } else {
            super.onKeyDown(keyCode, event)
        }
    }

    fun handleBackNavigation() {
        if (itemControls.isShown) {
            // Same as the Done button, which also hides the item controls
            binding.viewLayoutEditor.deselectSelectedView()
        } else {
            openMenu()
        }
    }

    /**
     * Called by the host once the dialogs are closed and the editor is back to size.
     */
    fun onEditorSettled() {
        editorSizeBeforeDialogs = null
    }

    fun saveEditorState(): ScreenEditorState {
        return ScreenEditorState(
            isPropertiesDialogShown = showLayoutPropertiesDialog,
            isBackgroundPropertiesDialogShown = showBackgroundPropertiesDialog,
        )
    }

    fun updateBackground(background: RuntimeBackground) {
        // Drops whatever is still loading or rendering for the previous background, so it can't replace this one
        backgroundLoadJob?.cancel()
        backgroundLoadJob = null
        svgRenderJob?.cancel()
        svgRenderJob = null
        shownSvgBackground = null
        svgRenderedSize = null

        val uri = background.background?.uri
        if (uri == null) {
            unfinishedBackground = null
            loadRasterBackground(background)
            return
        }

        unfinishedBackground = background
        val scope = findViewTreeLifecycleOwner()?.lifecycleScope
        if (scope == null) {
            // Without a window it may just not have found its owner yet, so attaching loads it
            if (isAttachedToWindow) {
                loadRasterBackground(background)
            }
            return
        }

        // Finding out whether it's an SVG reads the file, so do it off the main thread and keep an older raster load from landing meanwhile
        picasso.cancelRequest(binding.imageBackground)
        backgroundLoadJob = scope.launch {
            val load = withContext(Dispatchers.IO) {
                val isSvgStartNanos = System.nanoTime()
                val isSvg = BackgroundDecoder.isSvg(context, uri)
                val isSvgEndNanos = System.nanoTime()
                val svg = if (isSvg) BackgroundDecoder.parseSvg(context, uri) else null
                BackgroundFileLoad(isSvg, svg, isSvgEndNanos - isSvgStartNanos, System.nanoTime() - isSvgEndNanos)
            }
            val svg = load.svg

            when {
                !load.isSvg -> loadRasterBackground(background)
                svg == null -> {
                    unfinishedBackground = null
                    binding.imageBackground.setImageDrawable(null)
                    Toast.makeText(context, R.string.layout_background_load_failed, Toast.LENGTH_LONG).show()
                }
                else -> {
                    unfinishedBackground = null
                    shownSvgBackground = ShownSvgBackground(svg, background.mode, background.background.name, load.isSvgNanos, load.parseNanos)
                    renderSvgBackground()
                }
            }
        }
    }

    /**
     * Rasterizes the SVG background at the background view's current size, unless it already was. Called again whenever the view's size
     * changes, so it stays sharp through rotations and folds.
     */
    private fun renderSvgBackground() {
        val shownBackground = shownSvgBackground ?: return
        val imageView = binding.imageBackground
        val width = imageView.width - imageView.paddingLeft - imageView.paddingRight
        val height = imageView.height - imageView.paddingTop - imageView.paddingBottom
        // Not laid out yet; the layout change listener calls this again once it is
        if (width <= 0 || height <= 0) {
            return
        }

        val size = width to height
        if (svgRenderedSize == size) {
            return
        }
        val scope = findViewTreeLifecycleOwner()?.lifecycleScope ?: return

        svgRenderedSize = size
        svgRenderJob?.cancel()
        svgRenderJob = scope.launch {
            val bitmap = withContext(Dispatchers.Default) {
                val rasterStartNanos = System.nanoTime()
                shownBackground.svg.rasterize(width, height, shownBackground.mode)?.also {
                    shownBackground.logRender(width, height, System.nanoTime() - rasterStartNanos)
                }
            }

            // A newer background or size took over while this one was rendering
            if (shownSvgBackground !== shownBackground || svgRenderedSize != size) {
                bitmap?.recycle()
                return@launch
            }

            if (bitmap == null) {
                Toast.makeText(context, R.string.layout_background_load_failed, Toast.LENGTH_LONG).show()
                return@launch
            }

            imageView.setImageBitmap(bitmap)
            imageView.setBackgroundMode(shownBackground.mode)
        }
    }

    private fun loadRasterBackground(background: RuntimeBackground) {
        picasso.load(background.background?.uri).into(binding.imageBackground, object : Callback {
            override fun onSuccess() {
                onRasterLoadFinished(background)
                binding.imageBackground.setBackgroundMode(background.mode)
            }

            override fun onError(e: Exception?) {
                onRasterLoadFinished(background)
                e?.printStackTrace()
                Toast.makeText(context, R.string.layout_background_load_failed, Toast.LENGTH_LONG).show()
            }
        })
    }

    private fun onRasterLoadFinished(background: RuntimeBackground) {
        if (unfinishedBackground === background) {
            unfinishedBackground = null
        }
    }

    private fun openButtonsMenu() {
        val instantiatedComponents = binding.viewLayoutEditor.getInstantiatedComponents()
        val componentsToShow = LayoutComponent.entries.filterNot { instantiatedComponents.contains(it) }

        val dialogBuilder = MaterialAlertDialogBuilder(context)
            .setTitle(R.string.choose_component)
            .setNegativeButton(R.string.cancel) { dialog, _ ->
                dialog.cancel()
            }

        if (componentsToShow.isNotEmpty()) {
            dialogBuilder.setItems(componentsToShow.map { resources.getString(getLayoutComponentName(it)) }.toTypedArray()) { _, which ->
                val component = componentsToShow[which]
                binding.viewLayoutEditor.addLayoutComponent(component)
            }
        } else {
            dialogBuilder.setMessage(R.string.no_more_components)
        }

        dialogBuilder.showInCurrentWindow()
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupFloatingMenuButton() {
        val button = binding.buttonFloatingMenu
        var downRawX = 0f
        var downRawY = 0f
        var downX = 0f
        var downY = 0f
        var isTouchActive = false

        // Set in code because app:tint isn't applied when inflated in the external display presentation (no AppCompat inflater there)
        button.imageTintList = ContextCompat.getColorStateList(context, R.color.textLayoutEditor)
        button.alpha = FLOATING_MENU_BUTTON_IDLE_ALPHA
        button.setOnClickListener {
            openMenu()
        }
        button.setOnLongClickListener {
            // Only start dragging from a touch long press, leaving gamepad and accessibility long presses to the default handling
            if (!isTouchActive) {
                return@setOnLongClickListener false
            }
            isDraggingFloatingMenuButton = true
            button.alpha = 1f
            button.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
            true
        }
        // Only handles dragging after a long press, so clicks and long presses still reach the button
        button.setOnTouchListener { _, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    isTouchActive = true
                    downRawX = event.rawX
                    downRawY = event.rawY
                    downX = button.x
                    downY = button.y
                    false
                }
                MotionEvent.ACTION_MOVE -> {
                    if (isDraggingFloatingMenuButton) {
                        moveFloatingMenuButton(downX + event.rawX - downRawX, downY + event.rawY - downRawY)
                        itemControls.onMenuButtonMoved()
                        true
                    } else {
                        false
                    }
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    if (isDraggingFloatingMenuButton) {
                        if (event.actionMasked == MotionEvent.ACTION_UP) {
                            saveFloatingMenuButtonPosition()
                        } else {
                            // The gesture was taken away, so put the button back where it was
                            moveFloatingMenuButton(downX, downY)
                        }
                        itemControls.onMenuButtonMoved()
                        button.alpha = FLOATING_MENU_BUTTON_IDLE_ALPHA
                    }
                    isTouchActive = false
                    isDraggingFloatingMenuButton = false
                    false
                }
                else -> false
            }
        }

        binding.root.addOnLayoutChangeListener { _, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom ->
            if (right - left != oldRight - oldLeft || bottom - top != oldBottom - oldTop) {
                positionFloatingMenuButton()
            }
        }
    }

    /**
     * Places the floating menu button at its saved position for the current orientation, or at the top center if it was never moved.
     */
    private fun positionFloatingMenuButton() {
        val button = binding.buttonFloatingMenu
        val parentWidth = binding.root.width
        val parentHeight = binding.root.height
        if (parentWidth == 0 || parentHeight == 0 || isDraggingFloatingMenuButton) {
            return
        }

        val keyPrefix = getFloatingMenuButtonKeyPrefix()
        val savedCenterX = preferences.getFloat("${keyPrefix}_x", -1f)
        val savedCenterY = preferences.getFloat("${keyPrefix}_y", -1f)

        val centerX = if (savedCenterX >= 0f) savedCenterX * parentWidth else parentWidth / 2f
        val centerY = if (savedCenterY >= 0f) {
            savedCenterY * parentHeight
        } else {
            topControlInset + resources.getDimension(R.dimen.layout_editor_margin) + button.height / 2f
        }
        moveFloatingMenuButton(centerX - button.width / 2f, centerY - button.height / 2f)
    }

    private fun moveFloatingMenuButton(x: Float, y: Float) {
        val button = binding.buttonFloatingMenu
        button.x = x.coerceIn(0f, max(0, binding.root.width - button.width).toFloat())
        button.y = y.coerceIn(0f, max(0, binding.root.height - button.height).toFloat())
    }

    private fun saveFloatingMenuButtonPosition() {
        val button = binding.buttonFloatingMenu
        val parentWidth = binding.root.width
        val parentHeight = binding.root.height
        if (parentWidth == 0 || parentHeight == 0) {
            return
        }

        val keyPrefix = getFloatingMenuButtonKeyPrefix()
        preferences.edit {
            putFloat("${keyPrefix}_x", (button.x + button.width / 2f) / parentWidth)
            putFloat("${keyPrefix}_y", (button.y + button.height / 2f) / parentHeight)
        }
    }

    private fun getFloatingMenuButtonKeyPrefix(): String {
        val orientation = if (binding.root.width > binding.root.height) "landscape" else "portrait"
        val target = if (layoutTarget == LayoutTarget.SECONDARY_SCREEN) "_external" else ""
        return "${PreferenceKeys.LAYOUT_EDITOR_MENU_BUTTON_PREFIX}${target}_$orientation"
    }

    private fun openMenu() {
        listener?.onStoreLayoutChanges()
        val values = MenuOption.entries
        val options = Array(values.size) { i -> resources.getString(values[i].stringRes) }

        MaterialAlertDialogBuilder(context)
            .setTitle(R.string.menu)
            .setItems(options) { _, which -> onMenuOptionSelected(values[which]) }
            .setNegativeButton(R.string.cancel, null)
            .showInCurrentWindow()
    }

    private fun onMenuOptionSelected(option: MenuOption) {
        when (option) {
            MenuOption.ADD_COMPONENT -> openButtonsMenu()
            MenuOption.PROPERTIES -> openPropertiesDialog()
            MenuOption.BACKGROUNDS -> openBackgroundsConfigDialog()
            MenuOption.GRID -> openGridDialog()
            MenuOption.REVERT -> viewModel.revertLayoutChanges()
            MenuOption.RESET -> viewModel.resetLayout()
            MenuOption.SAVE -> withLayoutName { saveLayout() }
            MenuOption.SAVE_AND_EXIT -> withLayoutName { listener?.onSaveLayoutAndExit() }
            MenuOption.EXIT_WITHOUT_SAVING -> exitWithoutSaving()
        }
    }

    private fun saveLayout() {
        listener?.onSaveLayout()
        Toast.makeText(context, R.string.layout_saved, Toast.LENGTH_SHORT).show()
    }

    private fun exitWithoutSaving() {
        // Stored again because the menu only blocks its own window, so the editor on the other display may have changed while it was open
        listener?.onStoreLayoutChanges()
        if (viewModel.hasUnsavedChanges()) {
            MaterialAlertDialogBuilder(context)
                .setTitle(R.string.layout_editor_discard_changes_title)
                .setMessage(R.string.layout_editor_discard_changes_message)
                .setPositiveButton(R.string.discard) { _, _ -> listener?.onExit() }
                .setNegativeButton(R.string.cancel, null)
                .showInCurrentWindow()
        } else {
            listener?.onExit()
        }
    }

    private fun openPropertiesDialog() {
        listener?.onStoreLayoutChanges()
        showLayoutPropertiesDialog = true
    }

    private fun openBackgroundsConfigDialog() {
        listener?.onStoreLayoutChanges()
        showBackgroundPropertiesDialog = true
    }

    /**
     * Shows the snap grid setting, saved straight to the preferences the editors read.
     */
    private fun openGridDialog() {
        val gridSettings = LayoutEditorGridSettings(context)
        val gridSizeButtons = mapOf(
            LayoutEditorGridSize.SMALL to R.id.button_grid_small,
            LayoutEditorGridSize.MEDIUM to R.id.button_grid_medium,
            LayoutEditorGridSize.LARGE to R.id.button_grid_large,
        )
        val dialogBuilder = MaterialAlertDialogBuilder(context)
        val dialogBinding = DialogLayoutEditorGridBinding.inflate(LayoutInflater.from(dialogBuilder.context))

        dialogBinding.switchSnapToGrid.isChecked = gridSettings.isSnapEnabled
        dialogBinding.switchSnapToGrid.setOnCheckedChangeListener { _, isChecked -> gridSettings.isSnapEnabled = isChecked }
        dialogBinding.toggleGroupGridSize.check(gridSizeButtons.getValue(gridSettings.gridSize))
        dialogBinding.toggleGroupGridSize.addOnButtonCheckedListener { _, checkedId, isChecked ->
            if (isChecked) {
                gridSizeButtons.entries.firstOrNull { it.value == checkedId }?.let { gridSettings.gridSize = it.key }
            }
        }

        dialogBuilder
            .setTitle(R.string.layout_editor_grid)
            .setView(dialogBinding.root)
            .setPositiveButton(R.string.done, null)
            .showInCurrentWindow()
    }

    /**
     * Runs [action] straight away if the layout has a name, otherwise once the user has given it one.
     */
    private fun withLayoutName(action: () -> Unit) {
        if (viewModel.currentLayoutHasName()) {
            action()
        } else {
            showLayoutNameInputDialog(action)
        }
    }

    private fun showLayoutNameInputDialog(onNameSet: () -> Unit) {
        nameInputDialogState.show(
            initialText = resources.getString(R.string.custom_layout_default_name),
            onConfirm = {
                viewModel.setCurrentLayoutName(it)
                onNameSet()
            },
            keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences, imeAction = ImeAction.Done),
        )
    }

    /**
     * Allows an [AlertDialog] to be shown on a window that is not the main application window. This is required when the layout editor is being shown on a secondary display.
     */
    private fun AlertDialog.Builder.showInCurrentWindow(): AlertDialog {
        val dialog = create()
        dialog.window?.apply {
            setType(WindowManager.LayoutParams.TYPE_APPLICATION_ATTACHED_DIALOG)
            attributes.token = this@LayoutEditorManagerView.windowToken
        }
        dialog.show()
        return dialog
    }
}