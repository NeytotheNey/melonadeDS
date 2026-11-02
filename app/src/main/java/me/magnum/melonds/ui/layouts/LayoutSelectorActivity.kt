package me.magnum.melonds.ui.layouts

import android.content.Intent
import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import dagger.hilt.android.AndroidEntryPoint
import kotlinx.coroutines.launch
import me.magnum.melonds.R
import me.magnum.melonds.ui.layouts.model.SelectedLayout
import me.magnum.melonds.ui.layouts.ui.LayoutsScreen
import me.magnum.melonds.ui.layouts.viewmodel.LayoutSelectorViewModel
import me.magnum.melonds.ui.theme.MelonTheme

@AndroidEntryPoint
class LayoutSelectorActivity : AppCompatActivity() {
    companion object {
        const val KEY_SELECTED_LAYOUT_ID = "selected_layout_id"
        // Lists only the layouts marked as widescreen, with "Automatic" (no layout) at the top, for a game's wide layout
        const val KEY_WIDE_LAYOUTS_ONLY = "wide_layouts_only"
        // With KEY_WIDE_LAYOUTS_ONLY: the ratio (WidescreenAspect.key, 16:9 if missing) a layout made from the picker starts with
        const val KEY_NEW_LAYOUT_WIDE_ASPECT = "new_layout_wide_aspect"
    }

    private val viewModel by viewModels<LayoutSelectorViewModel>()

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        setContent {
            MelonTheme {
                LayoutsScreen(
                    viewModel = viewModel,
                    onNavigateBack = ::finish,
                    noLayoutName = if (viewModel.isWideLayoutsOnly) stringResource(R.string.wide_layout_automatic) else null,
                    newLayoutWideAspect = viewModel.newLayoutWideAspect,
                )
            }
        }

        lifecycleScope.launch {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.selectedLayoutId.collect {
                    val intent = Intent().apply {
                        putExtra(KEY_SELECTED_LAYOUT_ID, it.layoutId?.toString())
                    }
                    setResult(RESULT_OK, intent)

                    if (it.reason == SelectedLayout.SelectionReason.SELECTED_BY_USER) {
                        finish()
                    }
                }
            }
        }
    }
}