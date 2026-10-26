package me.magnum.melonds.ui.settings.preferences

import android.content.Context
import android.util.AttributeSet
import android.view.KeyEvent
import android.view.View
import android.widget.TextView
import androidx.preference.PreferenceViewHolder
import androidx.preference.SeekBarPreference
import com.google.android.material.slider.Slider
import me.magnum.melonds.R

/**
 * A [SeekBarPreference] drawn with an M3 [Slider]. It keeps the attributes, value handling and persistence of
 * [SeekBarPreference], so fragments can keep treating it as one. The current summary is shown next to the title. When
 * there is no summary, the current value is shown instead.
 *
 * Every change (touch on the slider, D-pad on the row, or setting [value] in code) goes through [setValue], which
 * notifies the preference and binds the row again, so the slider and the value text always show the stored value.
 */
class SliderPreference(context: Context, attrs: AttributeSet?) : SeekBarPreference(context, attrs) {

    private var isTrackingTouch = false

    init {
        layoutResource = R.layout.preference_slider
    }

    override fun onBindViewHolder(holder: PreferenceViewHolder) {
        super.onBindViewHolder(holder)

        val slider = holder.findViewById(R.id.slider) as Slider
        val summaryView = holder.findViewById(android.R.id.summary) as TextView?

        // SeekBarPreference sends D-pad keys on the row to its own (hidden) SeekBar and saves the value without
        // notifying, which would leave the slider out of date. Handle the keys here instead
        holder.itemView.setOnKeyListener { view, keyCode, event -> onRowKey(view, keyCode, event) }
        // The row is the only focus stop, like the platform SeekBarPreference. Left/right on the row moves the slider
        slider.isFocusable = false

        // Clear listeners first since the view can be bound again while the user is dragging the slider
        slider.clearOnChangeListeners()
        slider.clearOnSliderTouchListeners()

        val sliderMax = if (max > min) max else min + 1
        slider.valueFrom = min.toFloat()
        slider.valueTo = sliderMax.toFloat()
        slider.value = value.coerceIn(min, sliderMax).toFloat()
        slider.isEnabled = isEnabled

        if (summary.isNullOrEmpty()) {
            summaryView?.text = value.toString()
            summaryView?.visibility = View.VISIBLE
        }

        slider.addOnChangeListener { _, newValue, fromUser ->
            if (summary.isNullOrEmpty()) {
                summaryView?.text = newValue.toInt().toString()
            }
            if (fromUser && (updatesContinuously || !isTrackingTouch)) {
                commitValue(newValue.toInt())
            }
        }
        slider.addOnSliderTouchListener(object : Slider.OnSliderTouchListener {
            override fun onStartTrackingTouch(slider: Slider) {
                isTrackingTouch = true
            }

            override fun onStopTrackingTouch(slider: Slider) {
                isTrackingTouch = false
                val newValue = slider.value.toInt()
                if (newValue != value) {
                    commitValue(newValue)
                }
                // Move the slider back if the change listener rejected the new value
                if (slider.value.toInt() != value) {
                    slider.value = value.toFloat()
                }
            }
        })
    }

    private fun onRowKey(view: View, keyCode: Int, event: KeyEvent): Boolean {
        if (keyCode != KeyEvent.KEYCODE_DPAD_LEFT && keyCode != KeyEvent.KEYCODE_DPAD_RIGHT) {
            return false
        }
        if (!isAdjustable || !isEnabled) {
            return false
        }
        if (event.action != KeyEvent.ACTION_DOWN) {
            // Consume the key up too, so it doesn't move the focus
            return true
        }

        val isRtl = view.layoutDirection == View.LAYOUT_DIRECTION_RTL
        val increases = (keyCode == KeyEvent.KEYCODE_DPAD_RIGHT) != isRtl
        val step = seekBarIncrement.coerceAtLeast(1)
        val newValue = (if (increases) value + step else value - step).coerceIn(min, max)
        commitValue(newValue)
        return true
    }

    private fun commitValue(newValue: Int) {
        if (newValue != value && callChangeListener(newValue)) {
            value = newValue
        }
    }
}
