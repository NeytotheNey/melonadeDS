package me.magnum.melonds.ui.layouteditor

import android.annotation.SuppressLint
import android.content.Context
import android.util.AttributeSet
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.compose.foundation.text.KeyboardOptions
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
import androidx.lifecycle.findViewTreeViewModelStoreOwner
import androidx.lifecycle.get
import androidx.preference.PreferenceManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.squareup.picasso.Callback
import com.squareup.picasso.Picasso
import me.magnum.melonds.R
import me.magnum.melonds.databinding.DialogLayoutEditorGridBinding
import me.magnum.melonds.databinding.ViewLayoutEditorManagerBinding
import me.magnum.melonds.domain.model.RuntimeBackground
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.extensions.setBackgroundMode
import me.magnum.melonds.impl.dpToPixels
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
import me.magnum.melonds.utils.getLayoutComponentName
import java.util.UUID
import kotlin.math.max

private const val FLOATING_MENU_BUTTON_IDLE_ALPHA = 0.6f
private const val FLOATING_MENU_BUTTON_KEY_PREFIX = "layout_editor_menu_button"

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
    }

    private val binding: ViewLayoutEditorManagerBinding
    private val viewModel: LayoutEditorViewModel by lazy {
        val owner = findViewTreeViewModelStoreOwner() ?: error("No view-model store owner found")
        ViewModelProvider(owner).get<LayoutEditorViewModel>()
    }

    var listener: LayoutEditorManagerListener? = null
    private val itemControls: LayoutEditorItemControls
    private var topCutoutInset = 0
    private var isDraggingFloatingMenuButton = false
    private val preferences by lazy { PreferenceManager.getDefaultSharedPreferences(context) }

    private var showLayoutPropertiesDialog by mutableStateOf(initialEditorState?.isPropertiesDialogShown ?: false)
    private var showBackgroundPropertiesDialog by mutableStateOf(initialEditorState?.isBackgroundPropertiesDialogShown ?: false)
    private var shownEditablePropertyDialog by mutableStateOf<LayoutComponentEditableProperty?>(null)
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
                        onValueChanged = {
                            currentlyShownEditablePropertyDialog?.let { property -> itemControls.setPropertyValue(property, it) }
                            shownEditablePropertyDialog = null
                        },
                        onCancel = { shownEditablePropertyDialog = null },
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

        itemControls = LayoutEditorItemControls(binding) { shownEditablePropertyDialog = it }

        ViewCompat.setOnApplyWindowInsetsListener(binding.root) { _, windowInsets ->
            // The status bar is included so that the editor's own controls stay clear of it when it's shown. Only used for these controls, not the layouts
            val insets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout() or WindowInsetsCompat.Type.statusBars())
            if (insets.top != topCutoutInset) {
                topCutoutInset = insets.top
                positionFloatingMenuButton()
            }
            itemControls.setCutoutInsets(insets)

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
        // Runs after every layout pass, so the item controls follow the selected item however it was moved or resized
        viewTreeObserver.addOnGlobalLayoutListener {
            itemControls.onLayout()
        }
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
            // Same as the Done button. Deselecting also hides the item controls
            binding.viewLayoutEditor.deselectSelectedView()
        } else {
            openMenu()
        }
    }

    fun saveEditorState(): ScreenEditorState {
        return ScreenEditorState(
            isMenuShown = false,
            isPropertiesDialogShown = showLayoutPropertiesDialog,
            isBackgroundPropertiesDialogShown = showBackgroundPropertiesDialog,
        )
    }

    fun updateBackground(background: RuntimeBackground) {
        picasso.load(background.background?.uri).into(binding.imageBackground, object : Callback {
            override fun onSuccess() {
                binding.imageBackground.setBackgroundMode(background.mode)
            }

            override fun onError(e: Exception?) {
                e?.printStackTrace()
                Toast.makeText(context, R.string.layout_background_load_failed, Toast.LENGTH_LONG).show()
            }
        })
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
        button.setOnClickListener {
            openMenu()
        }
        button.setOnLongClickListener {
            // Only start dragging from a touch long press. Long presses from a gamepad or accessibility services are left to the default handling
            if (!isTouchActive) {
                return@setOnLongClickListener false
            }
            isDraggingFloatingMenuButton = true
            button.alpha = 1f
            button.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
            true
        }
        // Only handles dragging after a long press. Everything else goes through to the button so that clicks and long presses keep working
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
                            // The gesture was taken away. Put the button back where it was
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
            topCutoutInset + context.dpToPixels(8f) + button.height / 2f
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
        return "${FLOATING_MENU_BUTTON_KEY_PREFIX}${target}_$orientation"
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
            MenuOption.SAVE -> {
                if (viewModel.currentLayoutHasName()) {
                    saveLayout()
                } else {
                    showLayoutNameInputDialog { saveLayout() }
                }
            }
            MenuOption.SAVE_AND_EXIT -> {
                if (viewModel.currentLayoutHasName()) {
                    listener?.onSaveLayoutAndExit()
                } else {
                    showLayoutNameInputDialog { listener?.onSaveLayoutAndExit() }
                }
            }
            MenuOption.EXIT_WITHOUT_SAVING -> exitWithoutSaving()
        }
    }

    private fun saveLayout() {
        listener?.onSaveLayout()
        Toast.makeText(context, R.string.layout_saved, Toast.LENGTH_SHORT).show()
    }

    private fun exitWithoutSaving() {
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
     * Shows the snap grid setting. Changes are saved right away, and the editors pick them up from the preferences.
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