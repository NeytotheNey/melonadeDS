package me.magnum.melonds.ui.layouteditor

import android.annotation.SuppressLint
import android.content.res.ColorStateList
import android.view.Gravity
import android.view.MotionEvent
import android.widget.FrameLayout
import android.widget.ImageButton
import android.widget.SeekBar
import androidx.core.content.ContextCompat
import androidx.core.content.edit
import androidx.core.graphics.Insets
import androidx.core.view.isVisible
import androidx.preference.PreferenceManager
import com.google.android.material.color.MaterialColors
import me.magnum.melonds.R
import me.magnum.melonds.databinding.ViewLayoutEditorManagerBinding
import me.magnum.melonds.domain.model.Rect
import me.magnum.melonds.domain.model.SCREEN_HEIGHT
import me.magnum.melonds.domain.model.SCREEN_WIDTH
import me.magnum.melonds.domain.model.consoleAspectRatio
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.ui.common.LayoutComponentView
import me.magnum.melonds.ui.layouteditor.model.LayoutComponentEditableProperty
import me.magnum.melonds.utils.getLayoutComponentName
import java.util.Locale
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

private const val KEY_ITEM_PANEL_EXPANDED = "layout_editor_item_panel_expanded"
private const val SIZE_STEP_PX = 16
private const val REPEAT_INITIAL_DELAY_MS = 400L
private const val REPEAT_INTERVAL_MS = 50L

/**
 * The controls for the selected item: a compact pill shown next to it, which can be expanded into a panel with every option. The last mode used
 * is remembered.
 */
class LayoutEditorItemControls(
    private val binding: ViewLayoutEditorManagerBinding,
    private val onEditPropertyValue: (LayoutComponentEditableProperty) -> Unit,
) {

    private enum class ScreenAspectRatio(val ratio: Float?) {
        RATIO_4_3(consoleAspectRatio),
        RATIO_16_9(16 / 9f),
        UNRESTRICTED(null)
    }

    private val context = binding.root.context
    private val editor = binding.viewLayoutEditor
    private val preferences = PreferenceManager.getDefaultSharedPreferences(context)
    private val margin = context.resources.getDimension(R.dimen.layout_editor_margin)
    private val marginPx = context.resources.getDimensionPixelSize(R.dimen.layout_editor_margin)
    private var isExpanded = preferences.getBoolean(KEY_ITEM_PANEL_EXPANDED, false)
    private var selectedView: LayoutComponentView? = null
    private var screenAspectRatio = ScreenAspectRatio.RATIO_4_3
    private var lastSelectedViewRect: Rect? = null
    private var controlInsets = Insets.NONE
    private var fillingControls = false
    private val scaleButtons = mapOf(1 to binding.buttonScale1x, 2 to binding.buttonScale2x, 3 to binding.buttonScale3x)

    val isShown get() = selectedView != null

    init {
        tintIcons()
        setupPill()
        setupPanel()
    }

    fun show(view: LayoutComponentView) {
        selectedView = view
        lastSelectedViewRect = null
        if (view.component.isScreen()) {
            screenAspectRatio = guessAspectRatio(view)
        }
        editor.selectedScreenAspectRatio = screenAspectRatio.ratio
        updateMode()
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
     * Keeps the controls in sync with the selected item. Called after every layout pass, so it also catches changes made from the resize handles.
     * The panel's side is only picked when it's shown, so it doesn't jump around while it's being used.
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

    fun setPropertyValue(property: LayoutComponentEditableProperty, value: Int) {
        val view = selectedView ?: return
        when (property) {
            LayoutComponentEditableProperty.SIZE, LayoutComponentEditableProperty.WIDTH -> editor.resizeSelectedView(value.toFloat(), view.getHeight().toFloat())
            LayoutComponentEditableProperty.HEIGHT -> editor.resizeSelectedView(view.getWidth().toFloat(), value.toFloat())
        }
    }

    private fun setExpanded(expanded: Boolean) {
        isExpanded = expanded
        preferences.edit { putBoolean(KEY_ITEM_PANEL_EXPANDED, expanded) }
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

        val accentColor = MaterialColors.getColor(context, androidx.appcompat.R.attr.colorPrimary, ContextCompat.getColor(context, R.color.colorPrimary))
        val onAccentColor = MaterialColors.getColor(context, com.google.android.material.R.attr.colorOnPrimary, ContextCompat.getColor(context, R.color.colorOnPrimary))
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

        binding.layoutSizeLabels.setOnClickListener { onEditPropertyValue(LayoutComponentEditableProperty.SIZE) }
        binding.layoutWidthLabels.setOnClickListener { onEditPropertyValue(LayoutComponentEditableProperty.WIDTH) }
        binding.layoutHeightLabels.setOnClickListener { onEditPropertyValue(LayoutComponentEditableProperty.HEIGHT) }

        // The Size slider and the Width slider of Free screens both set the width
        listOf(binding.seekBarSize, binding.seekBarWidth).forEach { seekBar ->
            seekBar.setOnUserProgressChanged { progress ->
                val view = selectedView ?: return@setOnUserProgressChanged
                val (widthRange, _) = editor.getSelectedViewSizeRanges()
                editor.resizeSelectedView((widthRange.first + progress).toFloat(), view.getHeight().toFloat())
            }
        }
        binding.seekBarHeight.setOnUserProgressChanged { progress ->
            val view = selectedView ?: return@setOnUserProgressChanged
            val (_, heightRange) = editor.getSelectedViewSizeRanges()
            editor.resizeSelectedView(view.getWidth().toFloat(), (heightRange.first + progress).toFloat())
        }
        binding.seekBarOpacity.setOnUserProgressChanged { progress ->
            binding.textOpacity.text = context.getString(R.string.layout_editor_opacity_value, progress)
            editor.setSelectedViewAlpha(progress / 100f)
        }
        binding.seekBarSpacing.setOnUserProgressChanged { progress ->
            binding.textSpacing.text = context.getString(R.string.layout_editor_spacing_value, progress)
            editor.setSelectedViewSpacing(progress / 100f)
        }

        binding.toggleGroupAspectRatio.addOnButtonCheckedListener { _, checkedId, isChecked ->
            val view = selectedView
            if (!isChecked || fillingControls || view == null) return@addOnButtonCheckedListener
            screenAspectRatio = when (checkedId) {
                R.id.button_aspect_ratio_16_9 -> ScreenAspectRatio.RATIO_16_9
                R.id.button_aspect_ratio_free -> ScreenAspectRatio.UNRESTRICTED
                else -> ScreenAspectRatio.RATIO_4_3
            }
            editor.selectedScreenAspectRatio = screenAspectRatio.ratio
            if (screenAspectRatio.ratio != null) {
                // Keep the width and fix the height to match the new ratio
                editor.resizeSelectedView(view.getWidth().toFloat(), view.getHeight().toFloat())
            }
            fillPanel()
        }

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
     * Fills the panel with the selected item's values. Setting the values fires the controls' listeners, so those are ignored meanwhile. Otherwise
     * just selecting an item would change (or mark as modified) the layout.
     */
    private fun fillPanel() {
        val view = selectedView ?: return
        val isScreen = view.component.isScreen()
        val isFree = isScreen && screenAspectRatio == ScreenAspectRatio.UNRESTRICTED

        fillingControls = true
        binding.textItemName.setText(getLayoutComponentName(view.component))

        binding.layoutSizeRow.isVisible = !isFree
        binding.layoutWidthRow.isVisible = isFree
        binding.layoutHeightRow.isVisible = isFree
        val (widthRange, heightRange) = editor.getSelectedViewSizeRanges()
        if (isFree) {
            binding.seekBarWidth.fill(view.getWidth(), widthRange)
            binding.textWidth.text = formatSize(view.getWidth())
            binding.seekBarHeight.fill(view.getHeight(), heightRange)
            binding.textHeight.text = formatSize(view.getHeight())
        } else {
            binding.seekBarSize.fill(view.getWidth(), widthRange)
            binding.textSize.text = formatSize(view.getWidth())
        }

        binding.toggleGroupAspectRatio.isVisible = isScreen
        binding.toggleGroupAspectRatio.check(when (screenAspectRatio) {
            ScreenAspectRatio.RATIO_4_3 -> R.id.button_aspect_ratio_4_3
            ScreenAspectRatio.RATIO_16_9 -> R.id.button_aspect_ratio_16_9
            ScreenAspectRatio.UNRESTRICTED -> R.id.button_aspect_ratio_free
        })

        binding.layoutScaleShortcuts.isVisible = isScreen
        scaleButtons.forEach { (scale, button) -> button.isVisible = fitsInEditor(getScaleSize(scale)) }

        // Only screens use their own opacity in game. Controls use the layout's opacity instead
        val opacity = (view.baseAlpha * 100).roundToInt()
        binding.layoutOpacityRow.isVisible = isScreen
        binding.seekBarOpacity.progress = opacity
        binding.textOpacity.text = context.getString(R.string.layout_editor_opacity_value, opacity)

        val spacing = (view.spacing * 100).roundToInt()
        binding.layoutSpacingRow.isVisible = view.component == LayoutComponent.DPAD || view.component == LayoutComponent.BUTTONS
        binding.seekBarSpacing.progress = spacing
        binding.textSpacing.text = context.getString(R.string.layout_editor_spacing_value, spacing)

        binding.switchAboveScreen.isVisible = isScreen
        binding.switchAboveScreen.isChecked = view.onTop
        fillingControls = false
    }

    private fun formatSize(size: Int): String {
        return String.format(Locale.getDefault(), "%d", size)
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
     * Returns the size of the screen at an integer scale of the DS resolution. The height follows the locked aspect ratio, if any.
     */
    private fun getScaleSize(scale: Int): Pair<Int, Int> {
        val width = SCREEN_WIDTH * scale
        val height = screenAspectRatio.ratio?.let { (width / it).roundToInt() } ?: (SCREEN_HEIGHT * scale)
        return width to height
    }

    /**
     * Returns the biggest size that fits in the editor with the current aspect ratio. When the ratio isn't locked, the screen's current proportions
     * are kept.
     */
    private fun getFitSize(): Pair<Int, Int> {
        val view = selectedView ?: return 0 to 0
        val ratio = screenAspectRatio.ratio ?: (view.getWidth() / view.getHeight().coerceAtLeast(1).toFloat())
        val width = min(editor.width.toFloat(), editor.height * ratio)
        return width.roundToInt() to (width / ratio).roundToInt()
    }

    private fun fitsInEditor(size: Pair<Int, Int>): Boolean {
        return size.first <= editor.width && size.second <= editor.height
    }

    private fun applyScale(size: Pair<Int, Int>) {
        editor.resizeSelectedView(size.first.toFloat(), size.second.toFloat())
    }

    /**
     * Picks the aspect ratio that matches the screen's current size, so that resizing it doesn't suddenly change its shape.
     */
    private fun guessAspectRatio(view: LayoutComponentView): ScreenAspectRatio {
        return ScreenAspectRatio.entries.firstOrNull {
            val ratio = it.ratio
            ratio != null && abs(view.getWidth() - view.getHeight() * ratio) <= 2f
        } ?: ScreenAspectRatio.UNRESTRICTED
    }

    /**
     * Places the pill just above the item if there's room, otherwise just below it, otherwise inside its top edge. It's centred on the item, but
     * moved sideways or to the next spot if it would cover the floating menu button.
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

        val above = itemTop - margin - pill.height
        val below = itemBottom + margin
        val candidateYs = listOfNotNull(
            above.takeIf { it >= minY },
            below.takeIf { it <= maxY },
            (itemTop + margin).coerceIn(minY, maxY),
        )

        val menuButton = binding.buttonFloatingMenu
        val centeredX = ((itemLeft + itemRight - pill.width) / 2f).coerceIn(minX, maxX)
        val candidateXs = listOf(
            centeredX,
            (menuButton.x - margin - pill.width).coerceIn(minX, maxX),
            (menuButton.x + menuButton.width + margin).coerceIn(minX, maxX),
        ).filter {
            // Only move it sideways while it stays next to the item
            it < itemRight && it + pill.width > itemLeft
        }

        for (x in candidateXs) {
            for (y in candidateYs) {
                val overlapsMenuButton = x < menuButton.x + menuButton.width && x + pill.width > menuButton.x &&
                        y < menuButton.y + menuButton.height && y + pill.height > menuButton.y
                if (!overlapsMenuButton) {
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
     * The floating menu button is drawn above the panel and gets touches first, so move the panel just past it when they would overlap. The
     * overlap is checked against the panel's docked position without any offset, so the result doesn't flip back and forth.
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
        val overlapsMenuButton = button.x < panelRight && button.x + button.width > panelLeft && button.y < dockedBottom && button.y + button.height > dockedTop
        var topMargin = marginPx
        var bottomMargin = marginPx
        if (overlapsMenuButton) {
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
