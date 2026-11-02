package me.magnum.melonds.domain.model

const val SCREEN_WIDTH = 256
const val SCREEN_HEIGHT = 192
// Empty lines between the stacked screens in the frame texture; must match MelonInstance.cpp
const val SCREEN_GAP = 2
// Frame texture height in DS pixels before upscaling
const val FRAME_TEXTURE_HEIGHT = SCREEN_HEIGHT * 2 + SCREEN_GAP

// Base emulator frame rate; must match EMULATOR_TARGET_FPS in Configuration.h
const val EMULATOR_TARGET_FPS = 60

// The even widths of the top screen's 3D view that the core takes; must match the range in Configuration.h
const val MIN_WIDE_VIEW_WIDTH = SCREEN_WIDTH
const val MAX_WIDE_VIEW_WIDTH = 768

val consoleAspectRatio: Float get() {
    return SCREEN_WIDTH.toFloat() / SCREEN_HEIGHT
}