package me.magnum.melonds.ui.settings.fragments

import android.content.Context
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
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
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.itemsIndexed
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
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.edit
import androidx.fragment.app.Fragment
import androidx.preference.PreferenceManager
import dagger.hilt.android.AndroidEntryPoint
import me.magnum.melonds.R
import me.magnum.melonds.common.vibration.TouchVibrator
import me.magnum.melonds.domain.model.Input
import me.magnum.melonds.domain.model.Point
import me.magnum.melonds.domain.model.input.ControllerStyle
import me.magnum.melonds.ui.common.view.RaisedButtonView
import me.magnum.melonds.ui.common.view.RaisedButtons
import me.magnum.melonds.ui.emulator.input.ButtonsInputHandler
import me.magnum.melonds.ui.emulator.input.DpadInputHandler
import me.magnum.melonds.ui.emulator.input.IInputListener
import me.magnum.melonds.ui.emulator.input.SingleButtonInputHandler
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider
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
    val resources = LocalResources.current
    val styleValues = remember {
        resources.getStringArray(R.array.controller_style)
    }
    val styleNames = remember {
        resources.getStringArray(R.array.controller_style_options)
    }
    val sharedPreferences = remember {
        PreferenceManager.getDefaultSharedPreferences(context)
    }
    var selectedStyle by remember(sharedPreferences) {
        val initialPreference = sharedPreferences.getString("input_controller_style", "classic")
        val initialStyle = ControllerStyle.entries.getOrElse(styleValues.indexOf(initialPreference)) { ControllerStyle.CLASSIC }
        mutableStateOf(initialStyle)
    }

    LazyVerticalGrid(
        columns = GridCells.Adaptive(150.dp),
        modifier = Modifier.fillMaxSize().safeDrawingPadding(),
        contentPadding = PaddingValues(16.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        itemsIndexed(ControllerStyle.entries, key = { _, style -> style }) { index, style ->
            ControllerStyleItem(
                style = style,
                name = styleNames[index],
                isSelected = selectedStyle == style,
                touchVibrator = touchVibrator,
                onClick = {
                    selectedStyle = style
                    sharedPreferences.edit {
                        putString("input_controller_style", styleValues[index])
                    }
                },
            )
        }
    }
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
 * A small controller made of the real button views. When [touchVibrator] is set the buttons can be pressed, otherwise touches fall through to the card.
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
            PreviewButton(Modifier.size(smallButtonSize), touchVibrator, { singleButtonHandler(Input.PAUSE, it) }) { RaisedButtons.iconButton(it, style, R.drawable.ic_settings) }
            PreviewButton(Modifier.size(smallButtonSize), touchVibrator, { singleButtonHandler(Input.FAST_FORWARD, it) }) { RaisedButtons.iconButton(it, style, R.drawable.ic_button_fast_forward) }
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
