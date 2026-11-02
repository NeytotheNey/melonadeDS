package me.magnum.melonds.utils

import android.os.Build

/**
 * Whether the emulator's JIT can be used on this device, which is only the case on 64-bit devices.
 */
fun supportsJit(): Boolean {
    return Build.SUPPORTED_64_BIT_ABIS.isNotEmpty()
}
