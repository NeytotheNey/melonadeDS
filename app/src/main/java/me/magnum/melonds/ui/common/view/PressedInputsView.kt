package me.magnum.melonds.ui.common.view

import me.magnum.melonds.domain.model.Input

interface PressedInputsView {
    fun setPressedInputs(inputs: List<Input>)
}
