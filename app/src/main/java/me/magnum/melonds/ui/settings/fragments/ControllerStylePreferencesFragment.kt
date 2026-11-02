package me.magnum.melonds.ui.settings.fragments

import android.content.Context
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.edit
import androidx.fragment.app.Fragment
import androidx.preference.PreferenceManager
import dagger.hilt.android.AndroidEntryPoint
import me.magnum.melonds.R
import me.magnum.melonds.common.PreferenceKeys
import me.magnum.melonds.common.vibration.TouchVibrator
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.Point
import me.magnum.melonds.domain.model.input.ControllerStyle
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.ui.common.componentbuilders.SingleButtonIcons
import me.magnum.melonds.ui.common.view.RaisedButtonView
import me.magnum.melonds.ui.common.view.RaisedButtons
import me.magnum.melonds.ui.emulator.input.ButtonsInputHandler
import me.magnum.melonds.ui.emulator.input.DpadInputHandler
import me.magnum.melonds.ui.emulator.input.IInputListener
import me.magnum.melonds.ui.emulator.input.SingleButtonInputHandler
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider
import me.magnum.melonds.ui.settings.settingsContentPadding
import me.magnum.melonds.ui.theme.MelonTheme
import javax.inject.Inject

@AndroidEntryPoint
class ControllerStylePreferencesFragment : Fragment(), PreferenceFragmentTitleProvider {

    @Inject lateinit var touchVibrator: TouchVibrator

    override fun getTitle() = getString(R.string.controller_style)

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return ComposeView(requireContext()).apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            setContent {
                MelonTheme {
                    ControllerStylePreferencesScreen(touchVibrator)
                }
            }
        }
    }
}

@Composable
private fun ControllerStylePreferencesScreen(touchVibrator: TouchVibrator) {
    val context = LocalContext.current
    val sharedPreferences = remember {
        PreferenceManager.getDefaultSharedPreferences(context)
    }
    var selectedStyle by remember(sharedPreferences) {
        mutableStateOf(ControllerStyle.fromPreferenceValue(sharedPreferences.getString(PreferenceKeys.INPUT_CONTROLLER_STYLE, null)))
    }

    LazyVerticalGrid(
        columns = GridCells.Adaptive(150.dp),
        modifier = Modifier.fillMaxSize().padding(settingsContentPadding()),
        contentPadding = PaddingValues(16.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        items(ControllerStyle.entries, key = { it }) { style ->
            ControllerStyleItem(
                style = style,
                name = stringResource(style.displayNameRes),
                isSelected = selectedStyle == style,
                touchVibrator = touchVibrator,
                onClick = {
                    selectedStyle = style
                    sharedPreferences.edit {
                        putString(PreferenceKeys.INPUT_CONTROLLER_STYLE, style.toPreferenceValue())
                    }
                },
            )
        }
    }
}

@get:StringRes
val ControllerStyle.displayNameRes: Int
    get() = when (this) {
        ControllerStyle.CLASSIC -> R.string.controller_style_classic
        ControllerStyle.DS_LITE -> R.string.controller_style_ds_lite
        ControllerStyle.CHARCOAL -> R.string.controller_style_charcoal
        ControllerStyle.SUPER_COLOURS -> R.string.controller_style_super_colours
        ControllerStyle.INDIGO -> R.string.controller_style_indigo
        ControllerStyle.OUTLINE -> R.string.controller_style_outline
        ControllerStyle.SOFT_TINT -> R.string.controller_style_soft_tint
        ControllerStyle.PIXEL_8 -> R.string.controller_style_pixel_8
        ControllerStyle.PIXEL_16 -> R.string.controller_style_pixel_16
    }

@Composable
private fun ControllerStyleItem(
    style: ControllerStyle,
    name: String,
    isSelected: Boolean,
    touchVibrator: TouchVibrator,
    onClick: () -> Unit,
) {
    Card(
        modifier = if (isSelected) {
            Modifier.border(
                width = 4.dp,
                color = MaterialTheme.colorScheme.primary,
                shape = MaterialTheme.shapes.medium,
            )
        } else {
            Modifier
        },
        elevation = CardDefaults.cardElevation(defaultElevation = 4.dp),
        onClick = onClick,
    ) {
        Column(
            modifier = Modifier.padding(8.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            ControllerPreview(
                modifier = Modifier.fillMaxWidth(),
                style = style,
                touchVibrator = touchVibrator.takeIf { isSelected },
            )
            Text(
                text = name,
                style = MaterialTheme.typography.bodyLarge,
            )
        }
    }
}

/**
 * A small controller made of the real button views, pressable only when [touchVibrator] is set (otherwise touches fall through to the card).
 */
@Composable
private fun ControllerPreview(
    modifier: Modifier,
    style: ControllerStyle,
    touchVibrator: TouchVibrator?,
) {
    BoxWithConstraints(
        modifier = modifier
            .aspectRatio(1.5f)
            .clip(RoundedCornerShape(12.dp))
            .background(Color(0xFF1B1D22)),
    ) {
        val margin = maxWidth * 0.04f
        val shoulderSize = maxWidth * 0.2f
        val padSize = maxWidth * 0.38f
        val smallButtonSize = maxWidth * 0.11f

        PreviewButton(Modifier.align(Alignment.TopStart).offset(margin, margin).size(shoulderSize), touchVibrator, { singleButtonHandler(Input.L, it) }) { RaisedButtons.shoulderL(it, style) }
        PreviewButton(Modifier.align(Alignment.TopEnd).offset(-margin, margin).size(shoulderSize), touchVibrator, { singleButtonHandler(Input.R, it) }) { RaisedButtons.shoulderR(it, style) }
        Row(
            modifier = Modifier.align(Alignment.TopCenter).offset(y = margin),
            horizontalArrangement = Arrangement.spacedBy(maxWidth * 0.02f),
        ) {
            PreviewButton(Modifier.size(smallButtonSize), touchVibrator, { singleButtonHandler(Input.PAUSE, it) }) { RaisedButtons.iconButton(it, style, SingleButtonIcons.icon(LayoutComponent.BUTTON_PAUSE)) }
            PreviewButton(Modifier.size(smallButtonSize), touchVibrator, { singleButtonHandler(Input.FAST_FORWARD, it) }) { RaisedButtons.iconButton(it, style, SingleButtonIcons.icon(LayoutComponent.BUTTON_FAST_FORWARD_TOGGLE)) }
        }
        PreviewButton(Modifier.align(Alignment.BottomStart).offset(margin, -margin * 1.5f).size(padSize), touchVibrator, { DpadInputHandler(NoOpInputListener, false, it, 0f) }) { RaisedButtons.dpad(it, style) }
        PreviewButton(Modifier.align(Alignment.BottomEnd).offset(-margin, -margin * 1.5f).size(padSize), touchVibrator, { ButtonsInputHandler(NoOpInputListener, false, it, 0f) }) { RaisedButtons.abxy(it, style) }
        Row(
            modifier = Modifier.align(Alignment.BottomCenter).offset(y = -margin * 1.5f),
            horizontalArrangement = Arrangement.spacedBy(maxWidth * 0.02f),
        ) {
            PreviewButton(Modifier.size(smallButtonSize), touchVibrator, { singleButtonHandler(Input.SELECT, it) }) { RaisedButtons.select(it, style) }
            PreviewButton(Modifier.size(smallButtonSize), touchVibrator, { singleButtonHandler(Input.START, it) }) { RaisedButtons.start(it, style) }
        }
    }
}

@Composable
private fun PreviewButton(
    modifier: Modifier,
    touchVibrator: TouchVibrator?,
    createInputHandler: (TouchVibrator) -> View.OnTouchListener,
    factory: (Context) -> RaisedButtonView,
) {
    AndroidView(
        modifier = modifier,
        factory = factory,
        update = { view ->
            if (touchVibrator == null) {
                view.setOnTouchListener(null)
                view.isPressed = false
                view.setPressedInputs(emptyList())
            } else {
                view.setOnTouchListener(createInputHandler(touchVibrator))
            }
        },
    )
}

private fun singleButtonHandler(input: Input, touchVibrator: TouchVibrator) = SingleButtonInputHandler(NoOpInputListener, input, false, touchVibrator)

private object NoOpInputListener : IInputListener {
    override fun onKeyPress(key: Input) {}
    override fun onKeyReleased(key: Input) {}
    override fun onTouch(point: Point) {}
}
