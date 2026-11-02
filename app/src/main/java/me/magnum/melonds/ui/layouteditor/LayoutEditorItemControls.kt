package me.magnum.melonds.ui.layouteditor

import android.annotation.SuppressLint
import android.content.res.ColorStateList
import android.view.Gravity
import android.view.Menu
import android.view.MotionEvent
import android.view.View
import android.widget.FrameLayout
import android.widget.ImageButton
import android.widget.PopupMenu
import android.widget.SeekBar
import androidx.core.content.ContextCompat
import androidx.core.content.edit
import androidx.core.graphics.Insets
import androidx.core.view.doOnLayout
import androidx.core.view.isVisible
import androidx.preference.PreferenceManager
import com.google.android.material.color.MaterialColors
import me.magnum.melonds.R
import me.magnum.melonds.common.PreferenceKeys
import me.magnum.melonds.databinding.ViewLayoutEditorManagerBinding
import me.magnum.melonds.domain.model.Rect
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.SCREEN_WIDTH
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.domain.model.layout.WidescreenAspect
import me.magnum.melonds.ui.common.LayoutComponentView
import me.magnum.melonds.ui.layouteditor.LayoutEditorView.ScreenAspectRatio
import me.magnum.melonds.ui.layouteditor.model.LayoutComponentEditableProperty
import me.magnum.melonds.utils.getLayoutComponentName
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

private const val SIZE_STEP_PX = 16
private const val REPEAT_INITIAL_DELAY_MS = 400L
private const val REPEAT_INTERVAL_MS = 50L
// Menu id of the Wide chip's Off item, since the ratios use their ordinal
private const val WIDE_ASPECT_MENU_OFF = -1

/**
 * Controls for the selected item: a pill beside it or an expandable full panel, remembering the last mode.
 */
class LayoutEditorItemControls(
    private val binding: ViewLayoutEditorManagerBinding,
    private val onEditPropertyValue: (LayoutComponentEditableProperty) -> Unit,
    // The widescreen ratio of the variant shown (UILayout.wideAspect), or null for a normal one
    private val getWideAspect: () -> WidescreenAspect?,
    // Sets the widescreen ratio of the variant shown (null for off), from the top screen's Wide menu
    private val onWideAspectPicked: (WidescreenAspect?) -> Unit,
) {

    private val context = binding.root.context
    private val editor = binding.viewLayoutEditor
    private val preferences = PreferenceManager.getDefaultSharedPreferences(context)
    private val marginPx = context.resources.getDimensionPixelSize(R.dimen.layout_editor_margin)
    private var isExpanded = preferences.getBoolean(PreferenceKeys.LAYOUT_EDITOR_ITEM_PANEL_EXPANDED, false)
    private var selectedView: LayoutComponentView? = null
    // Whether the top screen, the only item with a Wide chip, is selected
    private var isTopScreen = false
    private var lastSelectedViewRect: Rect? = null
    private var controlInsets = Insets.NONE
    private var fillingControls = false
    private val scaleButtons = mapOf(1 to binding.buttonScale1x, 2 to binding.buttonScale2x, 3 to binding.buttonScale3x)

    val isShown get() = selectedView != null
    val selectedComponent get() = selectedView?.component

    init {
        tintIcons()
        setupPill()
        setupPanel()
    }

    fun show(view: LayoutComponentView) {
        selectedView = view
        lastSelectedViewRect = null
        isTopScreen = view.component == LayoutComponent.TOP_SCREEN
        editor.selectedScreenWideAspect = if (isTopScreen) getWideAspect() else null
        if (view.component.isScreen()) {
            editor.selectedScreenAspect = guessAspectRatio(view)
        }
        updateMode()
    }

    /**
     * Refills the controls after the variant's widescreen ratio changed, which doesn't always reshow the layout.
     */
    fun refresh() {
        selectedView?.let { show(it) }
    }

    fun hide() {
        selectedView = null
        binding.layoutItemPill.isVisible = false
        binding.layoutItemPanel.isVisible = false
    }

    /**
     * Sets the insets the pill and the panel must stay clear of: the display cutout and the status bar.
     */
    fun setControlInsets(insets: Insets) {
        controlInsets = insets
        binding.layoutItemPanelContainer.setPadding(insets.left, insets.top, insets.right, insets.bottom)
    }

    /**
     * Repositions and refills the controls after a layout pass.
     */
    fun onLayout() {
        val view = selectedView ?: return
        reposition(view)

        val rect = view.getRect()
        if (rect != lastSelectedViewRect) {
            lastSelectedViewRect = rect
            if (isExpanded) {
                fillPanel()
            }
        }
    }

    /**
     * Moving the floating menu button doesn't cause a layout pass, so it must tell the controls to get out of its way.
     */
    fun onMenuButtonMoved() {
        selectedView?.let { reposition(it) }
    }

    private fun reposition(view: LayoutComponentView) {
        if (isExpanded) {
            keepPanelClearOfMenuButton()
        } else {
            positionPill(view)
        }
    }

    fun getPropertyValue(property: LayoutComponentEditableProperty): Int {
        val view = selectedView ?: return 0
        return when (property) {
            LayoutComponentEditableProperty.SIZE, LayoutComponentEditableProperty.WIDTH -> view.getWidth()
            LayoutComponentEditableProperty.HEIGHT -> view.getHeight()
        }
    }

    /**
     * The ratio the selected screen is locked to, or null when free or a control is selected.
     */
    val selectedScreenAspectRatio: Float? get() = if (selectedView?.component?.isScreen() == true) lockedRatio else null

    /**
     * Applies a value from the value dialog to [component], reselecting it if the layout was reshown meanwhile.
     */
    fun setPropertyValue(component: LayoutComponent, property: LayoutComponentEditableProperty, value: Int, ratioWhenOpened: Float?, editorWidth: Int, editorHeight: Int) {
        val view = editor.getLayoutComponentView(component) ?: return
        // A just-reshown view has no size until it's laid out, and selecting it before that would take its anchor and ratio from an empty rect
        view.view.doOnLayout {
            if (editor.getLayoutComponentView(component) !== view) return@doOnLayout
            // While it's still selected, its controls have the latest ratio
            val wasSelected = selectedView === view
            val ratio = if (wasSelected) lockedRatio else ratioWhenOpened
            editor.reselectIfDeselectedByLayout(component)
            if (!wasSelected && selectedView === view && view.component.isScreen()) {
                // Put back the ratio it had, since selecting it guessed one from its shape, which can't tell Free from a 4:3 shape
                editor.selectedScreenAspect = getScreenAspectRatio(ratio)
                if (isExpanded) {
                    fillPanel()
                }
            }
            when (property) {
                LayoutComponentEditableProperty.SIZE, LayoutComponentEditableProperty.WIDTH -> {
                    editor.resizeView(view, value.toFloat(), view.getHeight().toFloat(), ratio, editorWidth, editorHeight)
                }
                LayoutComponentEditableProperty.HEIGHT -> {
                    editor.resizeView(view, view.getWidth().toFloat(), value.toFloat(), ratio, editorWidth, editorHeight)
                }
            }
        }
    }

    private fun setExpanded(expanded: Boolean) {
        isExpanded = expanded
        preferences.edit { putBoolean(PreferenceKeys.LAYOUT_EDITOR_ITEM_PANEL_EXPANDED, expanded) }
        updateMode()
    }

    private fun updateMode() {
        val view = selectedView ?: return
        binding.layoutItemPill.isVisible = !isExpanded
        binding.layoutItemPanel.isVisible = isExpanded
        if (isExpanded) {
            fillPanel()
            dockPanel(view)
        }
    }

    private fun tintIcons() {
        // Tinted in code because app:tint isn't applied when inflated in the external display presentation (no AppCompat inflater there)
        val iconTint = ContextCompat.getColorStateList(context, R.color.textLayoutEditor)
        listOf(
            binding.buttonPillSmaller,
            binding.buttonPillBigger,
            binding.buttonPillCenterHorizontal,
            binding.buttonPillDelete,
            binding.buttonPillExpand,
            binding.buttonPanelCollapse,
            binding.buttonPanelCenterHorizontal,
            binding.buttonPanelCenterVertical,
            binding.buttonMoveLeft,
            binding.buttonMoveUp,
            binding.buttonMoveDown,
            binding.buttonMoveRight,
            binding.buttonPanelDelete,
        ).forEach { it.imageTintList = iconTint }
        binding.switchAboveScreen.compoundDrawableTintList = iconTint

        val accentColor = MaterialColors.getColor(context, androidx.appcompat.R.attr.colorPrimary, ContextCompat.getColor(context, R.color.m3Primary))
        val onAccentColor = MaterialColors.getColor(context, com.google.android.material.R.attr.colorOnPrimary, ContextCompat.getColor(context, R.color.m3OnPrimary))
        listOf(binding.buttonPillDone, binding.buttonPanelDone).forEach {
            it.backgroundTintList = ColorStateList.valueOf(accentColor)
            it.imageTintList = ColorStateList.valueOf(onAccentColor)
        }
    }

    private fun setupPill() {
        binding.buttonPillSmaller.setOnRepeatingClickListener { editor.resizeSelectedViewBy(-SIZE_STEP_PX) }
        binding.buttonPillBigger.setOnRepeatingClickListener { editor.resizeSelectedViewBy(SIZE_STEP_PX) }
        binding.buttonPillCenterHorizontal.setOnClickListener { editor.centerSelectedViewHorizontally() }
        binding.buttonPillDelete.setOnClickListener { editor.deleteSelectedView() }
        binding.buttonPillExpand.setOnClickListener { setExpanded(true) }
        binding.buttonPillDone.setOnClickListener { editor.deselectSelectedView() }
    }

    private fun setupPanel() {
        binding.buttonPanelCollapse.setOnClickListener { setExpanded(false) }
        binding.buttonPanelDone.setOnClickListener { editor.deselectSelectedView() }

        binding.layoutSizeRow.textLabel.setText(R.string.label_size)
        binding.layoutWidthRow.textLabel.setText(R.string.label_width)
        binding.layoutHeightRow.textLabel.setText(R.string.label_height)
        binding.layoutOpacityRow.textLabel.setText(R.string.layout_editor_opacity)
        binding.layoutSpacingRow.textLabel.setText(R.string.layout_editor_spacing)
        binding.layoutOpacityRow.seekBar.max = 100
        binding.layoutSpacingRow.seekBar.max = 100

        binding.layoutSizeRow.layoutLabels.setOnClickListener { onEditPropertyValue(LayoutComponentEditableProperty.SIZE) }
        binding.layoutWidthRow.layoutLabels.setOnClickListener { onEditPropertyValue(LayoutComponentEditableProperty.WIDTH) }
        binding.layoutHeightRow.layoutLabels.setOnClickListener { onEditPropertyValue(LayoutComponentEditableProperty.HEIGHT) }

        // The Size slider and the Width slider of Free screens both set the width
        listOf(binding.layoutSizeRow.seekBar, binding.layoutWidthRow.seekBar).forEach { seekBar ->
            seekBar.setOnUserProgressChanged { progress ->
                val view = selectedView ?: return@setOnUserProgressChanged
                val (widthRange, _) = editor.getSelectedViewSizeRanges()
                editor.resizeSelectedView((widthRange.first + progress).toFloat(), view.getHeight().toFloat())
            }
        }
        binding.layoutHeightRow.seekBar.setOnUserProgressChanged { progress ->
            val view = selectedView ?: return@setOnUserProgressChanged
            val (_, heightRange) = editor.getSelectedViewSizeRanges()
            editor.resizeSelectedView(view.getWidth().toFloat(), (heightRange.first + progress).toFloat())
        }
        binding.layoutOpacityRow.seekBar.setOnUserProgressChanged { progress ->
            binding.layoutOpacityRow.textValue.text = context.getString(R.string.percent_value, progress)
            editor.setSelectedViewAlpha(progress / 100f)
        }
        binding.layoutSpacingRow.seekBar.setOnUserProgressChanged { progress ->
            binding.layoutSpacingRow.textValue.text = context.getString(R.string.percent_value, progress)
            editor.setSelectedViewSpacing(progress / 100f)
        }

        binding.toggleGroupAspectRatio.addOnButtonCheckedListener { _, checkedId, isChecked ->
            val view = selectedView
            if (!isChecked || fillingControls || view == null) return@addOnButtonCheckedListener
            if (checkedId == R.id.button_aspect_ratio_wide) {
                // The Wide chip opens its menu instead and the lock follows the pick there, so put the current choice back until then
                binding.toggleGroupAspectRatio.post { fillPanel() }
                return@addOnButtonCheckedListener
            }
            editor.selectedScreenAspect = when (checkedId) {
                R.id.button_aspect_ratio_16_9 -> ScreenAspectRatio.RATIO_16_9
                R.id.button_aspect_ratio_free -> ScreenAspectRatio.UNRESTRICTED
                else -> ScreenAspectRatio.RATIO_4_3
            }
            if (lockedRatio != null) {
                // Keep the width and fix the height to match the new ratio
                editor.resizeSelectedView(view.getWidth().toFloat(), view.getHeight().toFloat())
            }
            fillPanel()
        }
        // Called after the toggle group's own handling, whether or not the chip was already checked
        binding.buttonAspectRatioWide.setOnClickListener { showWideAspectMenu() }

        scaleButtons.forEach { (scale, button) ->
            button.text = context.getString(R.string.layout_editor_scale_multiplier, scale)
            button.setOnClickListener { applyScale(getScaleSize(scale)) }
        }
        binding.buttonScaleFit.setOnClickListener { applyScale(getFitSize()) }

        binding.switchAboveScreen.setOnCheckedChangeListener { _, isChecked ->
            if (fillingControls) return@setOnCheckedChangeListener
            editor.setSelectedScreenOnTop(isChecked)
        }

        binding.buttonPanelCenterHorizontal.setOnClickListener { editor.centerSelectedViewHorizontally() }
        binding.buttonPanelCenterVertical.setOnClickListener { editor.centerSelectedViewVertically() }
        binding.buttonMoveLeft.setOnRepeatingClickListener { editor.moveSelectedView(-1, 0) }
        binding.buttonMoveUp.setOnRepeatingClickListener { editor.moveSelectedView(0, -1) }
        binding.buttonMoveDown.setOnRepeatingClickListener { editor.moveSelectedView(0, 1) }
        binding.buttonMoveRight.setOnRepeatingClickListener { editor.moveSelectedView(1, 0) }
        binding.buttonPanelDelete.setOnClickListener { editor.deleteSelectedView() }
    }

    /**
     * Fills the panel from the selected item, with the controls' listeners muted meanwhile.
     */
    private fun fillPanel() {
        val view = selectedView ?: return
        val isScreen = view.component.isScreen()
        val isFree = isScreen && editor.selectedScreenAspect == ScreenAspectRatio.UNRESTRICTED

        fillingControls = true
        binding.textItemName.setText(getLayoutComponentName(view.component))

        binding.layoutSizeRow.root.isVisible = !isFree
        binding.layoutWidthRow.root.isVisible = isFree
        binding.layoutHeightRow.root.isVisible = isFree
        val (widthRange, heightRange) = editor.getSelectedViewSizeRanges()
        if (isFree) {
            binding.layoutWidthRow.seekBar.fill(view.getWidth(), widthRange)
            binding.layoutWidthRow.textValue.text = view.getWidth().toString()
            binding.layoutHeightRow.seekBar.fill(view.getHeight(), heightRange)
            binding.layoutHeightRow.textValue.text = view.getHeight().toString()
        } else {
            binding.layoutSizeRow.seekBar.fill(view.getWidth(), widthRange)
            binding.layoutSizeRow.textValue.text = view.getWidth().toString()
        }

        binding.toggleGroupAspectRatio.isVisible = isScreen
        // In a wide variant the top screen has the variant's ratio instead of plain 16:9, which would only stretch it
        val currentWideAspect = editor.selectedScreenWideAspect
        binding.buttonAspectRatioWide.isVisible = isTopScreen
        binding.buttonAspectRatio169.isVisible = currentWideAspect == null
        binding.buttonAspectRatioWide.text = if (currentWideAspect != null) {
            context.getString(R.string.layout_editor_aspect_ratio_wide, getWideAspectName(currentWideAspect))
        } else {
            context.getString(R.string.layout_editor_aspect_ratio_wide_off)
        }
        binding.toggleGroupAspectRatio.check(when (editor.selectedScreenAspect) {
            ScreenAspectRatio.RATIO_4_3 -> R.id.button_aspect_ratio_4_3
            ScreenAspectRatio.RATIO_16_9 -> R.id.button_aspect_ratio_16_9
            ScreenAspectRatio.WIDE -> R.id.button_aspect_ratio_wide
            ScreenAspectRatio.UNRESTRICTED -> R.id.button_aspect_ratio_free
        })

        binding.layoutScaleShortcuts.isVisible = isScreen
        scaleButtons.forEach { (scale, button) -> button.isVisible = fitsInEditor(getScaleSize(scale)) }

        // Only screens use their own opacity in game, controls the layout's
        val opacity = (view.baseAlpha * 100).roundToInt()
        binding.layoutOpacityRow.root.isVisible = isScreen
        binding.layoutOpacityRow.seekBar.progress = opacity
        binding.layoutOpacityRow.textValue.text = context.getString(R.string.percent_value, opacity)

        val spacing = (view.spacing * 100).roundToInt()
        binding.layoutSpacingRow.root.isVisible = view.component == LayoutComponent.DPAD || view.component == LayoutComponent.BUTTONS
        binding.layoutSpacingRow.seekBar.progress = spacing
        binding.layoutSpacingRow.textValue.text = context.getString(R.string.percent_value, spacing)

        binding.switchAboveScreen.isVisible = isScreen
        binding.switchAboveScreen.isChecked = view.onTop
        fillingControls = false
    }

    private fun SeekBar.fill(value: Int, range: IntRange) {
        max = range.last - range.first
        progress = value - range.first
    }

    private fun SeekBar.setOnUserProgressChanged(onChanged: (Int) -> Unit) {
        setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                if (fromUser && !fillingControls) {
                    onChanged(progress)
                }
            }

            override fun onStartTrackingTouch(seekBar: SeekBar) {}
            override fun onStopTrackingTouch(seekBar: SeekBar) {}
        })
    }

    /**
     * The ratio the selected screen's size is locked to, or null if it's free
     */
    private val lockedRatio: Float? get() = editor.selectedScreenAspectRatio

    /**
     * Returns the screen size at an integer DS scale, where a wide top screen keeps the DS height instead.
     */
    private fun getScaleSize(scale: Int): Size {
        val ratio = lockedRatio
        if (editor.selectedScreenAspect == ScreenAspectRatio.WIDE && ratio != null) {
            val height = SCREEN_HEIGHT * scale
            return Size((height * ratio).roundToInt(), height)
        }

        val width = SCREEN_WIDTH * scale
        val height = ratio?.let { (width / it).roundToInt() } ?: (SCREEN_HEIGHT * scale)
        return Size(width, height)
    }

    /**
     * Returns the biggest size that fits the editor at the locked ratio, or at the current proportions.
     */
    private fun getFitSize(): Size {
        val view = selectedView ?: return Size(0, 0)
        val ratio = lockedRatio ?: (view.getWidth() / view.getHeight().coerceAtLeast(1).toFloat())
        val width = min(editor.width.toFloat(), editor.height * ratio)
        return Size(width.roundToInt(), (width / ratio).roundToInt())
    }

    private fun fitsInEditor(size: Size): Boolean {
        return size.width <= editor.width && size.height <= editor.height
    }

    private fun applyScale(size: Size) {
        editor.resizeSelectedView(size.width.toFloat(), size.height.toFloat())
    }

    /**
     * The Wide chip's menu: the ratios, plus Off in a wide variant.
     */
    private fun showWideAspectMenu() {
        val currentWideAspect = editor.selectedScreenWideAspect
        // android.widget.PopupMenu, since the external display presentation has no AppCompat theme
        val menu = PopupMenu(context, binding.buttonAspectRatioWide)
        // Item ids start at 1, since 0 is Menu.NONE
        WidescreenAspect.entries.forEach {
            menu.menu.add(Menu.NONE, it.ordinal + 1, Menu.NONE, getWideAspectName(it))
        }
        if (currentWideAspect != null) {
            menu.menu.add(Menu.NONE, WIDE_ASPECT_MENU_OFF, Menu.NONE, R.string.layout_widescreen_off)
        }
        menu.menu.setGroupCheckable(Menu.NONE, true, true)
        currentWideAspect?.let { menu.menu.findItem(it.ordinal + 1)?.isChecked = true }
        menu.setOnMenuItemClickListener { item ->
            val pickedAspect = WidescreenAspect.entries.getOrNull(item.itemId - 1)
            // The controls are refreshed when the ratio changes (see refresh), or the top screen is selected again with the layout
            onWideAspectPicked(pickedAspect)
            true
        }
        menu.show()
    }

    private fun getWideAspectName(aspect: WidescreenAspect): String {
        return context.resources.getStringArray(R.array.widescreen_aspect_options).getOrElse(aspect.ordinal) { aspect.key }
    }

    /**
     * Returns the choice whose locked ratio is [ratio] (null for Free), the reverse of [lockedRatio].
     */
    private fun getScreenAspectRatio(ratio: Float?): ScreenAspectRatio {
        val currentWideAspect = editor.selectedScreenWideAspect
        return when {
            ratio == null -> ScreenAspectRatio.UNRESTRICTED
            currentWideAspect != null && ratio == currentWideAspect.aspectRatio -> ScreenAspectRatio.WIDE
            currentWideAspect == null && ratio == ScreenAspectRatio.RATIO_16_9.ratio -> ScreenAspectRatio.RATIO_16_9
            else -> ScreenAspectRatio.RATIO_4_3
        }
    }

    /**
     * Picks the aspect ratio that matches the screen's current size, so that resizing it doesn't suddenly change its shape.
     */
    private fun guessAspectRatio(view: LayoutComponentView): ScreenAspectRatio {
        // A wide variant's top screen has no plain 16:9, so a 16:9 shape that isn't the variant's ratio counts as Free
        val currentWideAspect = editor.selectedScreenWideAspect
        val candidates = if (currentWideAspect != null) {
            listOf(ScreenAspectRatio.WIDE to currentWideAspect.aspectRatio, ScreenAspectRatio.RATIO_4_3 to ScreenAspectRatio.RATIO_4_3.ratio)
        } else {
            listOf(ScreenAspectRatio.RATIO_4_3 to ScreenAspectRatio.RATIO_4_3.ratio, ScreenAspectRatio.RATIO_16_9 to ScreenAspectRatio.RATIO_16_9.ratio)
        }
        return candidates.firstOrNull { (_, ratio) ->
            ratio != null && abs(view.getWidth() - view.getHeight() * ratio) <= 2f
        }?.first ?: ScreenAspectRatio.UNRESTRICTED
    }

    /**
     * Places the pill above, below or inside the item, moved aside to avoid the floating menu button.
     */
    private fun positionPill(view: LayoutComponentView) {
        val pill = binding.layoutItemPill
        val root = binding.root
        if (pill.width == 0 || root.width == 0) return

        val minX = controlInsets.left.toFloat()
        val maxX = max(minX, (root.width - controlInsets.right - pill.width).toFloat())
        val minY = controlInsets.top.toFloat()
        val maxY = max(minY, (root.height - controlInsets.bottom - pill.height).toFloat())
        val itemLeft = editor.left + view.view.x
        val itemTop = editor.top + view.view.y
        val itemRight = itemLeft + view.view.width
        val itemBottom = itemTop + view.view.height

        val above = itemTop - marginPx - pill.height
        val below = itemBottom + marginPx
        val candidateYs = listOfNotNull(
            above.takeIf { it >= minY },
            below.takeIf { it <= maxY },
            (itemTop + marginPx).coerceIn(minY, maxY),
        )

        val menuButton = binding.buttonFloatingMenu
        val centeredX = ((itemLeft + itemRight - pill.width) / 2f).coerceIn(minX, maxX)
        val candidateXs = listOf(
            centeredX,
            (menuButton.x - marginPx - pill.width).coerceIn(minX, maxX),
            (menuButton.x + menuButton.width + marginPx).coerceIn(minX, maxX),
        ).filter {
            // Only move it sideways while it stays next to the item
            it < itemRight && it + pill.width > itemLeft
        }

        for (x in candidateXs) {
            for (y in candidateYs) {
                if (!menuButton.overlaps(x, y, x + pill.width, y + pill.height)) {
                    pill.x = x
                    pill.y = y
                    return
                }
            }
        }
        pill.x = centeredX
        pill.y = candidateYs.first()
    }

    /**
     * Docks the panel on the half of the editor away from the selected item.
     */
    private fun dockPanel(view: LayoutComponentView) {
        val panel = binding.layoutItemPanel
        val itemCenterY = view.view.y + view.view.height / 2f
        val verticalGravity = if (itemCenterY > editor.height / 2f) Gravity.TOP else Gravity.BOTTOM
        val params = panel.layoutParams as FrameLayout.LayoutParams
        if (params.gravity != (verticalGravity or Gravity.CENTER_HORIZONTAL)) {
            params.gravity = verticalGravity or Gravity.CENTER_HORIZONTAL
            panel.layoutParams = params
        }
        keepPanelClearOfMenuButton()
    }

    /**
     * Pushes the panel past the floating menu button, measured from its unoffset docked position so it doesn't flip back and forth.
     */
    private fun keepPanelClearOfMenuButton() {
        val panel = binding.layoutItemPanel
        val container = binding.layoutItemPanelContainer
        if (panel.height == 0 || container.height == 0) return

        val params = panel.layoutParams as FrameLayout.LayoutParams
        val isDockedTop = (params.gravity and Gravity.VERTICAL_GRAVITY_MASK) == Gravity.TOP
        val panelLeft = container.left + panel.left.toFloat()
        val panelRight = panelLeft + panel.width
        // The offset changes how tall the panel can be, so work out the height it would have without one
        val innerHeight = container.height - container.paddingTop - container.paddingBottom
        val dockedHeight = min(panel.getChildAt(0).height + panel.paddingTop + panel.paddingBottom, (innerHeight - 2 * marginPx) / 2)
        val dockedTop = if (isDockedTop) {
            container.top + container.paddingTop + marginPx.toFloat()
        } else {
            container.bottom - container.paddingBottom - marginPx - dockedHeight.toFloat()
        }
        val dockedBottom = dockedTop + dockedHeight

        val button = binding.buttonFloatingMenu
        var topMargin = marginPx
        var bottomMargin = marginPx
        if (button.overlaps(panelLeft, dockedTop, panelRight, dockedBottom)) {
            if (isDockedTop) {
                topMargin = (button.y + button.height + marginPx - container.top - container.paddingTop).roundToInt()
            } else {
                bottomMargin = (container.bottom - container.paddingBottom - button.y + marginPx).roundToInt()
            }
        }

        if (params.topMargin != topMargin || params.bottomMargin != bottomMargin) {
            params.topMargin = topMargin
            params.bottomMargin = bottomMargin
            panel.layoutParams = params
        }
    }

    /**
     * Whether this view, at its current position, overlaps the given area of its parent.
     */
    private fun View.overlaps(left: Float, top: Float, right: Float, bottom: Float): Boolean {
        return x < right && x + width > left && y < bottom && y + height > top
    }

    /**
     * Runs [action] on click, and repeatedly while the button is held down.
     */
    @SuppressLint("ClickableViewAccessibility")
    private fun ImageButton.setOnRepeatingClickListener(action: () -> Unit) {
        val repeatAction = object : Runnable {
            override fun run() {
                if (!this@setOnRepeatingClickListener.isShown) return
                action()
                postDelayed(this, REPEAT_INTERVAL_MS)
            }
        }
        // Clicks still come from keyboards, gamepads and accessibility services
        setOnClickListener { action() }
        setOnTouchListener { button, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    button.drawableHotspotChanged(event.x, event.y)
                    button.isPressed = true
                    action()
                    button.postDelayed(repeatAction, REPEAT_INITIAL_DELAY_MS)
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    button.isPressed = false
                    button.removeCallbacks(repeatAction)
                }
            }
            true
        }
    }
}
