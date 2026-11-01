package me.magnum.melonds.domain.model

const val SCREEN_WIDTH = 256
const val SCREEN_HEIGHT = 192
// The frame texture holds both screens stacked vertically, top screen first, with this many empty lines between them. The bottom screen starts
// at line SCREEN_HEIGHT + SCREEN_GAP. Must match the frame texture layout in MelonInstance.cpp
const val SCREEN_GAP = 2
// Height of the frame texture, in DS pixels: both screens and the gap between them. It's that many times bigger when the 3D renderer is upscaled
const val FRAME_TEXTURE_HEIGHT = SCREEN_HEIGHT * 2 + SCREEN_GAP

// Frame rate the emulator runs at without fast-forward. Must match EMULATOR_TARGET_FPS in Configuration.h
const val EMULATOR_TARGET_FPS = 60

val consoleAspectRatio: Float get() {
    return SCREEN_WIDTH.toFloat() / SCREEN_HEIGHT
}