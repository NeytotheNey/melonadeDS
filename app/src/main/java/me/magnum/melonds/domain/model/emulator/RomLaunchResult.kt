package me.magnum.melonds.domain.model.emulator

import me.magnum.melonds.MelonEmulator

sealed class RomLaunchResult {
    data object LaunchFailedRomNotFound : RomLaunchResult()
    data object LaunchFailedRomNotSupported : RomLaunchResult()
    data class LaunchFailedSramProblem(val reason: Exception) : RomLaunchResult()
    // The ROM opens but its save folder can't be read right (a folder Android hid for a while), so the game isn't started
    data object LaunchFailedRomFolderUnreadable : RomLaunchResult()
    data class LaunchFailed(val reason: MelonEmulator.LoadResult) : RomLaunchResult()
    data class LaunchSuccessful(val isGbaLoadSuccessful: Boolean) : RomLaunchResult()
}