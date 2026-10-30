package me.magnum.melonds.ui.layouts.viewmodel

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.launch
import me.magnum.melonds.domain.model.layout.WidescreenAspect
import me.magnum.melonds.domain.repositories.LayoutsRepository
import me.magnum.melonds.ui.layouts.LayoutSelectorActivity
import me.magnum.melonds.ui.layouts.model.SelectedLayout
import java.util.UUID
import javax.inject.Inject

@HiltViewModel
class LayoutSelectorViewModel @Inject constructor(
    layoutsRepository: LayoutsRepository,
    savedStateHandle: SavedStateHandle,
) : BaseLayoutsViewModel(layoutsRepository) {

    val isWideLayoutsOnly = savedStateHandle.get<Boolean>(LayoutSelectorActivity.KEY_WIDE_LAYOUTS_ONLY) == true
    // Layouts made from a wide-only picker start as wide ones, or they wouldn't show up in it
    val newLayoutWideAspect = if (isWideLayoutsOnly) {
        WidescreenAspect.fromKey(savedStateHandle.get<String>(LayoutSelectorActivity.KEY_NEW_LAYOUT_WIDE_ASPECT)) ?: WidescreenAspect.DEFAULT
    } else {
        null
    }

    private val initiallySelectedLayout = savedStateHandle.get<String?>(LayoutSelectorActivity.KEY_SELECTED_LAYOUT_ID)?.let {
        UUID.fromString(it)
    }

    private val _currentSelectedLayoutId = MutableStateFlow(SelectedLayout(initiallySelectedLayout, SelectedLayout.SelectionReason.INITIAL_SELECTION))
    override val selectedLayoutId = _currentSelectedLayoutId.asStateFlow()

    init {
        viewModelScope.launch {
            layoutsRepository.getLayouts()
                .map { layouts -> if (isWideLayoutsOnly) layouts.filter { it.isWide } else layouts }
                .onEach { layouts ->
                    val currentSelectedLayoutId = _currentSelectedLayoutId.value.layoutId
                    // If the currently selected layout cannot be found in the layouts list, it was deleted (or, for wide layouts, it isn't marked
                    // wide anymore). Fallback to the global layout (null ID), which is "Automatic" for wide layouts
                    if (!layouts.any { it.id == currentSelectedLayoutId }) {
                        applyFallbackLayout()
                    }
                }
                .collect { layouts ->
                    val layoutList = layouts.toMutableList()
                    layoutList.add(0, layoutsRepository.getGlobalLayoutPlaceholder())
                    _layouts.value = layoutList
                }
        }
    }

    override fun setSelectedLayoutId(id: UUID?) {
        _currentSelectedLayoutId.value = SelectedLayout(id, SelectedLayout.SelectionReason.SELECTED_BY_USER)
    }

    override fun applyFallbackLayout() {
        _currentSelectedLayoutId.value = SelectedLayout(null, SelectedLayout.SelectionReason.SELECTED_BY_FALLBACK)
    }
}