package me.magnum.melonds.extensions

import android.view.Window
import android.view.WindowManager
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

inline val Window.insetsControllerCompat: WindowInsetsControllerCompat?
    get() = WindowCompat.getInsetsController(this, decorView)

/**
 * Hides the navigation bar, and the status bar too unless [showStatusBar] is set. Used by the emulator and the layout editor so that both show the same
 * screen area. The status bar is hidden by the theme (windowFullscreen), so that flag has to be cleared to show it. A shown status bar is drawn over the
 * content instead of pushing it down, since the layouts are sized for the whole window. Before API 35 the window would also grow into the display
 * cutout once the status bar is visible, so the cutout is kept out of the window to keep its size the same either way. From API 35 the window always
 * covers the cutout and the cutout mode is ignored.
 */
@Suppress("DEPRECATION")
fun Window.setupGameSystemBars(showStatusBar: Boolean) {
    // This runs on every focus change and each of these relayouts the window, so only switch when the setting has changed. The fullscreen flag tells
    // which state the window is in
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
