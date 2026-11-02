package me.magnum.melonds.extensions

import android.view.Window
import android.view.WindowManager
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

inline val Window.insetsControllerCompat: WindowInsetsControllerCompat?
    get() = WindowCompat.getInsetsController(this, decorView)

/**
 * Hides the navigation bar and, unless [showStatusBar] is set, the status bar, keeping the window the same size either way (a shown status bar
 * overlays the content and, before API 35, the cutout stays outside the window).
 */
@Suppress("DEPRECATION")
fun Window.setupGameSystemBars(showStatusBar: Boolean) {
    // This runs on every focus change and each switch relayouts the window, so only switch when the fullscreen flag doesn't match the setting
    val isFullscreen = (attributes.flags and WindowManager.LayoutParams.FLAG_FULLSCREEN) != 0
    if (showStatusBar && isFullscreen) {
        clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
        WindowCompat.setDecorFitsSystemWindows(this, false)
        attributes = attributes.apply { layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER }
    } else if (!showStatusBar && !isFullscreen) {
        addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
        WindowCompat.setDecorFitsSystemWindows(this, true)
        attributes = attributes.apply { layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT }
    }

    insetsControllerCompat?.let {
        if (showStatusBar) {
            it.show(WindowInsetsCompat.Type.statusBars())
        } else {
            it.hide(WindowInsetsCompat.Type.statusBars())
        }
        it.hide(WindowInsetsCompat.Type.navigationBars())
        it.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }
}
