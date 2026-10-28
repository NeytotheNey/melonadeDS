package me.magnum.melonds.ui.common.componentbuilders

import android.content.Context
import android.view.View
import androidx.core.content.ContextCompat
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.input.ControllerStyle
import me.magnum.melonds.ui.common.LayoutComponentViewBuilder

class EditorBackgroundLayoutComponentViewBuilder(private val baseBuilder: LayoutComponentViewBuilder) : LayoutComponentViewBuilder() {
    override fun build(context: Context, style: ControllerStyle): View {
        return baseBuilder.build(context, style).apply {
            background = ContextCompat.getDrawable(context, R.drawable.background_uiview_selector)
        }
    }

    override fun applySpacing(view: View, spacing: Float) {
        baseBuilder.applySpacing(view, spacing)
    }

    override fun getAspectRatio() = baseBuilder.getAspectRatio()
}