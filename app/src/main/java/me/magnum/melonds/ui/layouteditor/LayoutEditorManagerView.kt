package me.magnum.melonds.ui.layouteditor

import android.annotation.SuppressLint
import android.content.Context
import android.util.AttributeSet
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.FrameLayout
import android.widget.SeekBar
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
import androidx.core.view.isInvisible
import androidx.core.view.isVisible
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.findViewTreeViewModelStoreOwner
import androidx.lifecycle.get
import androidx.preference.PreferenceManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.squareup.picasso.Callback
import com.squareup.picasso.Picasso
import me.magnum.melonds.R
import me.magnum.melonds.databinding.ViewLayoutEditorManagerBinding
import me.magnum.melonds.domain.model.RuntimeBackground
import me.magnum.melonds.domain.model.consoleAspectRatio
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.extensions.setBackgroundMode
import me.magnum.melonds.impl.dpToPixels
import me.magnum.melonds.ui.common.component.dialog.TextInputDialog
import me.magnum.melonds.ui.common.component.dialog.TextInputDialogState
import me.magnum.melonds.ui.layouteditor.LayoutEditorActivity.MenuOption
import me.magnum.melonds.ui.layouteditor.LayoutEditorManagerView.AspectRatioEnforcementPriority.HEIGHT
import me.magnum.melonds.ui.layouteditor.LayoutEditorManagerView.AspectRatioEnforcementPriority.WIDTH
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
import kotlin.math.min
import kotlin.math.roundToInt

private const val CONTROLS_SLIDE_ANIMATION_DURATION_MS = 100L
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

    private enum class ScreenAspectRatio(val ratio: Float?) {
        RATIO_4_3(consoleAspectRatio),
        RATIO_16_9(16 / 9f),
        UNRESTRICTED(null)
    }

    private enum class AspectRatioEnforcementPriority {
        WIDTH, HEIGHT,
    }

    private val binding: ViewLayoutEditorManagerBinding
    private val viewModel: LayoutEditorViewModel by lazy {
        val owner = findViewTreeViewModelStoreOwner() ?: error("No view-model store owner found")
        ViewModelProvider(owner).get<LayoutEditorViewModel>()
    }

    var listener: LayoutEditorManagerListener? = null
    private var areScalingControlsShown = true
    private var selectedViewMinSize = 0
    private var currentWidthScale = 0f
    private var currentHeightScale = 0f
    private var selectedViewIsScreen = false
    private var selectedScreenComponent: LayoutComponent? = null
    private var selectedAspectRatio = ScreenAspectRatio.RATIO_4_3
    private var topAspectRatio = ScreenAspectRatio.RATIO_4_3
    private var bottomAspectRatio = ScreenAspectRatio.RATIO_4_3
    private var fillingScalingControls = false
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
                        initialValue = when (currentlyShownEditablePropertyDialog) {
                            LayoutComponentEditableProperty.SIZE -> binding.seekBarSize.progress + selectedViewMinSize
                            LayoutComponentEditableProperty.WIDTH -> binding.seekBarWidth.progress + selectedViewMinSize
                            LayoutComponentEditableProperty.HEIGHT -> binding.seekBarHeight.progress + selectedViewMinSize
                            null -> 0
                        },
                        onValueChanged = {
                            when (currentlyShownEditablePropertyDialog) {
                                LayoutComponentEditableProperty.SIZE -> binding.seekBarSize.progress = it - selectedViewMinSize
                                LayoutComponentEditableProperty.WIDTH -> {
                                    binding.seekBarWidth.progress = it - selectedViewMinSize
                                    enforceAspectRatio(selectedAspectRatio, WIDTH)
                                }
                                LayoutComponentEditableProperty.HEIGHT -> {
                                    binding.seekBarHeight.progress = it - selectedViewMinSize
                                    enforceAspectRatio(selectedAspectRatio, HEIGHT)
                                }
                                null -> { /* no-op */ }
                            }
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

        ViewCompat.setOnApplyWindowInsetsListener(binding.root) { _, windowInsets ->
            val insets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            if (insets.top != topCutoutInset) {
                topCutoutInset = insets.top
                positionFloatingMenuButton()
            }
            binding.layoutScalingContainer.setPadding(
                insets.left,
                0,
                insets.right,
                insets.bottom,
            )

            WindowInsetsCompat.CONSUMED
        }

        setupFloatingMenuButton()
        binding.buttonDeleteButton.setOnClickListener {
            binding.viewLayoutEditor.deleteSelectedView()
        }
        binding.buttonCenterHorizontal.setOnClickListener {
            binding.viewLayoutEditor.centerSelectedViewHorizontally()
        }
        binding.buttonCenterVertical.setOnClickListener {
            binding.viewLayoutEditor.centerSelectedViewVertically()
        }

        binding.viewLayoutEditor.setLayoutComponentViewBuilderFactory(EditorLayoutComponentViewBuilderFactory())
        binding.viewLayoutEditor.setOnViewSelectedListener { view, widthScale, heightScale, maxWidth, maxHeight, minSize ->
            // Force the scaling controls to restart so the new view always
            // receives fresh listeners even if they were already visible.
            hideScalingControls(false)
            selectedViewIsScreen = view.component.isScreen()
            selectedScreenComponent = view.component
            selectedAspectRatio = when (view.component) {
                LayoutComponent.TOP_SCREEN -> topAspectRatio
                LayoutComponent.BOTTOM_SCREEN -> bottomAspectRatio
                else -> ScreenAspectRatio.UNRESTRICTED
            }
            showScalingControls(
                widthScale,
                heightScale,
                maxWidth,
                maxHeight,
                minSize,
                selectedViewIsScreen,
                view.baseAlpha,
                view.onTop,
            )
        }
        binding.viewLayoutEditor.setOnViewDeselectedListener {
            hideScalingControls()
        }
        binding.layoutSizeLabels.setOnClickListener {
            shownEditablePropertyDialog = LayoutComponentEditableProperty.SIZE
        }
        binding.seekBarSize.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                if (fillingScalingControls) return
                val scale = progress / seekBar.max.toFloat()
                val value = (binding.seekBarSize.max * scale + selectedViewMinSize).toInt()
                binding.textSize.text = value.toString()
                binding.viewLayoutEditor.scaleSelectedView(scale)
            }

            override fun onStartTrackingTouch(seekBar: SeekBar) {
            }

            override fun onStopTrackingTouch(seekBar: SeekBar) {
            }
        })

        binding.layoutWidthLabels.setOnClickListener {
            shownEditablePropertyDialog = LayoutComponentEditableProperty.WIDTH
        }
        binding.seekBarWidth.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                if (fillingScalingControls) return
                if (selectedViewIsScreen && fromUser && selectedAspectRatio.ratio != null) {
                    enforceAspectRatio(selectedAspectRatio, WIDTH)
                } else {
                    var widthScale = progress / binding.seekBarWidth.max.toFloat()
                    currentWidthScale = widthScale
                    binding.textWidth.text = (binding.seekBarWidth.max * currentWidthScale + selectedViewMinSize).toInt().toString()
                }
                binding.viewLayoutEditor.scaleSelectedView(currentWidthScale, currentHeightScale)
            }

            override fun onStartTrackingTouch(seekBar: SeekBar) {
            }

            override fun onStopTrackingTouch(seekBar: SeekBar) {
            }
        })

        binding.layoutHeightLabels.setOnClickListener {
            shownEditablePropertyDialog = LayoutComponentEditableProperty.HEIGHT
        }
        binding.seekBarHeight.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                if (fillingScalingControls) return
                if (selectedViewIsScreen && fromUser && selectedAspectRatio.ratio != null) {
                    enforceAspectRatio(selectedAspectRatio, HEIGHT)
                } else {
                    var heightScale = progress / binding.seekBarHeight.max.toFloat()
                    currentHeightScale = heightScale
                    binding.textHeight.text = (binding.seekBarHeight.max * currentHeightScale + selectedViewMinSize).toInt().toString()
                }
                binding.viewLayoutEditor.scaleSelectedView(currentWidthScale, currentHeightScale)
            }

            override fun onStartTrackingTouch(seekBar: SeekBar) {
            }

            override fun onStopTrackingTouch(seekBar: SeekBar) {
            }
        })

        binding.seekBarAlpha.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                if (fillingScalingControls) return
                val alpha = progress / 100f
                binding.viewLayoutEditor.setSelectedViewAlpha(alpha)
            }

            override fun onStartTrackingTouch(seekBar: SeekBar) {}
            override fun onStopTrackingTouch(seekBar: SeekBar) {}
        })

        val aspectOptions = listOf(
            resources.getString(R.string.aspect_ratio_4_3),
            resources.getString(R.string.aspect_ratio_16_9),
            resources.getString(R.string.aspect_ratio_unrestricted),
        )
        val aspectAdapter = ArrayAdapter(context, android.R.layout.simple_spinner_item, aspectOptions)
        aspectAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        binding.spinnerAspectRatio.adapter = aspectAdapter
        binding.spinnerAspectRatio.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>, view: View?, position: Int, id: Long) {
                // The spinner reports selections made from code later on (posted), so also ignore selections that don't change anything
                val newAspectRatio = ScreenAspectRatio.entries[position]
                if (!selectedViewIsScreen || fillingScalingControls || newAspectRatio == selectedAspectRatio) return
                selectedAspectRatio = newAspectRatio
                when (selectedScreenComponent) {
                    LayoutComponent.TOP_SCREEN -> topAspectRatio = selectedAspectRatio
                    LayoutComponent.BOTTOM_SCREEN -> bottomAspectRatio = selectedAspectRatio
                    else -> { }
                }
                enforceAspectRatio(selectedAspectRatio, WIDTH)
            }

            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }

        binding.checkboxAboveScreen.setOnCheckedChangeListener { _, isChecked ->
            if (fillingScalingControls) return@setOnCheckedChangeListener
            binding.viewLayoutEditor.setSelectedScreenOnTop(isChecked)
        }

        // Tinted in code because app:tint isn't applied when inflated in the external display presentation (no AppCompat inflater there)
        binding.buttonDone.compoundDrawableTintList = ContextCompat.getColorStateList(context, R.color.textLayoutEditor)
        binding.buttonDone.setOnClickListener {
            binding.viewLayoutEditor.deselectSelectedView()
            hideScalingControls()
        }

        hideScalingControls(false)
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        return if (binding.viewLayoutEditor.handleKeyDown(event)) {
            true
        } else {
            super.onKeyDown(keyCode, event)
        }
    }

    fun handleBackNavigation() {
        if (areScalingControlsShown) {
            // Same as the Done button. Deselecting also hides the scaling controls
            binding.viewLayoutEditor.deselectSelectedView()
            hideScalingControls(true)
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

    private fun showScalingControls(
        widthScale: Float,
        heightScale: Float,
        maxWidth: Int,
        maxHeight: Int,
        minSize: Int,
        isScreen: Boolean,
        alpha: Float,
        onTop: Boolean,
        animate: Boolean = true,
    ) {
        binding.layoutScalingContainer.animate().cancel()

        // Filling in the controls fires their listeners. Ignore those so that just selecting a view doesn't change (or mark as modified) the layout
        fillingScalingControls = true
        if (isScreen) {
            binding.seekBarWidth.apply {
                max = maxWidth - minSize
                progress = (widthScale * (maxWidth - minSize)).toInt()
            }
            binding.textWidth.text = ((maxWidth - minSize) * widthScale + minSize).toInt().toString()

            binding.seekBarHeight.apply {
                max = maxHeight - minSize
                progress = (heightScale * (maxHeight - minSize)).toInt()
            }
            binding.textHeight.text = ((maxHeight - minSize) * heightScale + minSize).toInt().toString()
        } else {
            binding.seekBarSize.apply {
                max = min(maxWidth - minSize, maxHeight - minSize)
                progress = (widthScale * (maxWidth - minSize)).roundToInt()
            }
            binding.textSize.text = ((maxWidth - minSize) * widthScale + minSize).toInt().toString()
        }

        binding.seekBarAlpha.progress = (alpha * 100).toInt()
        binding.checkboxAboveScreen.isChecked = onTop
        binding.spinnerAspectRatio.setSelection(selectedAspectRatio.ordinal, false)
        fillingScalingControls = false

        binding.layoutSizeLabels.isVisible = !isScreen
        binding.seekBarSize.isVisible = !isScreen
        binding.layoutWidthLabels.isVisible = isScreen
        binding.seekBarWidth.isVisible = isScreen
        binding.layoutHeightLabels.isVisible = isScreen
        binding.seekBarHeight.isVisible = isScreen
        binding.layoutAlphaLabels.isVisible = isScreen
        binding.seekBarAlpha.isVisible = isScreen
        binding.layoutAspectRatio.isVisible = isScreen
        binding.checkboxAboveScreen.isVisible = isScreen
        binding.buttonCenterHorizontal.isVisible = isScreen
        binding.buttonCenterVertical.isVisible = isScreen

        currentWidthScale = widthScale
        currentHeightScale = heightScale
        selectedViewMinSize = minSize

        if (!areScalingControlsShown) {
            if (animate) {
                binding.layoutScalingContainer.isVisible = true
                binding.layoutScalingContainer.post {
                    binding.layoutScalingContainer
                        .animate()
                        .y(binding.root.bottom.toFloat() - binding.layoutScalingContainer.height.toFloat())
                        .setDuration(CONTROLS_SLIDE_ANIMATION_DURATION_MS)
                        .withEndAction(null)
                        .start()
                }
            } else {
                binding.layoutScalingContainer.isVisible = true
                binding.layoutScalingContainer.y = binding.root.bottom.toFloat() - binding.layoutScalingContainer.height.toFloat()
            }

            areScalingControlsShown = true
        }

        binding.layoutScalingContainer.requestFocus()
    }

    private fun hideScalingControls(animate: Boolean = true) {
        if (!areScalingControlsShown) {
            return
        }

        binding.layoutScalingContainer.animate().cancel()

        if (animate) {
            binding.layoutScalingContainer.post {
                binding.layoutScalingContainer
                    .animate()
                    .y(binding.root.bottom.toFloat())
                    .setDuration(CONTROLS_SLIDE_ANIMATION_DURATION_MS)
                    .withEndAction {
                        binding.layoutScalingContainer.isInvisible = true
                    }
                    .start()
            }
        } else {
            binding.layoutScalingContainer.y = binding.layoutScalingContainer.bottom.toFloat()
            binding.layoutScalingContainer.isInvisible = true
        }

        areScalingControlsShown = false
    }

    private fun enforceAspectRatio(aspectRatio: ScreenAspectRatio, priority: AspectRatioEnforcementPriority) {
        val ratio = aspectRatio.ratio ?: return

        val displayRatio = binding.seekBarWidth.max.toFloat() / binding.seekBarHeight.max.toFloat()
        var width: Float
        var height: Float

        when (priority) {
            WIDTH -> {
                width = binding.seekBarWidth.progress.toFloat() + selectedViewMinSize
                height = width / ratio
            }
            HEIGHT -> {
                height = binding.seekBarHeight.progress.toFloat() + selectedViewMinSize
                width = height * ratio
            }
        }

        val minWidth = selectedViewMinSize * aspectRatio.ratio
        val minHeight = selectedViewMinSize.toFloat()
        val maxWidth = min(binding.seekBarWidth.max.toFloat() + selectedViewMinSize, (binding.seekBarHeight.max + selectedViewMinSize) * ratio)
        val maxHeight = min(binding.seekBarHeight.max.toFloat() + selectedViewMinSize, (binding.seekBarWidth.max + selectedViewMinSize) / ratio)

        // Enforce min size
        if (width < minWidth) {
            width = minWidth
            height = width / ratio
        }
        if (height < minHeight) {
            height = minHeight
            width = height * ratio
        }

        // Enforce max size
        if (displayRatio > ratio) {
            if (width > maxWidth) {
                width = maxWidth
                height = width / ratio
            }
        } else {
            if (height > maxHeight) {
                height = maxHeight
                width = height * ratio
            }
        }

        currentWidthScale = ((width - selectedViewMinSize) / binding.seekBarWidth.max.toFloat()).coerceIn(0f, 1f)
        binding.seekBarWidth.progress = (currentWidthScale * binding.seekBarWidth.max).toInt()
        binding.textWidth.text = (binding.seekBarWidth.max * currentWidthScale + selectedViewMinSize).toInt().toString()

        currentHeightScale = ((height - selectedViewMinSize) / binding.seekBarHeight.max.toFloat()).coerceIn(0f, 1f)
        binding.seekBarHeight.progress = (currentHeightScale * binding.seekBarHeight.max).toInt()
        binding.textHeight.text = (binding.seekBarHeight.max * currentHeightScale + selectedViewMinSize).toInt().toString()
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