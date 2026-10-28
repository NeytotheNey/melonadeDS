package me.magnum.melonds.initializer

import android.app.Activity
import android.app.Application
import android.content.Context
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import androidx.startup.Initializer
import leakcanary.LeakCanary
import me.magnum.melonds.ui.emulator.EmulatorActivity

/**
 * Debug builds only, like LeakCanary itself. A heap dump freezes the app for a few seconds, so LeakCanary is set up to never dump the heap on
 * its own while a game is open:
 * - While an [EmulatorActivity] exists (in the foreground or not), heap dumps are disabled. Destroyed objects are still watched, but retained ones
 *   are only checked once the game is closed. That also skips the GC LeakCanary forces every 2 s while it waits on retained objects
 * - Otherwise, the heap isn't dumped automatically while the app is visible. The retained objects notification can be tapped to dump it, and it's
 *   dumped automatically once the app has been in the background for a few seconds
 *
 * "Dump Heap Now" in the Leaks app (Heap Dumps tab) always works, game or not.
 */
class LeakCanaryInitializer : Initializer<Unit> {

    override fun create(context: Context) {
        // A threshold that is never reached: dumps while the app is visible only happen when the notification is tapped
        LeakCanary.config = LeakCanary.config.copy(retainedVisibleThreshold = Int.MAX_VALUE)
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(EmulatorHeapDumpBlocker())
    }

    override fun dependencies(): List<Class<out Initializer<*>>> = emptyList()

    /**
     * Disables heap dumps while an [EmulatorActivity] exists. Lifecycle callbacks run on the main thread.
     */
    private class EmulatorHeapDumpBlocker : Application.ActivityLifecycleCallbacks {

        private companion object {
            // Covers the gap between the old and the new activity when the emulator screen is recreated
            const val ENABLE_DELAY_MS = 5000L
        }

        private val handler = Handler(Looper.getMainLooper())
        private val enableHeapDumps = Runnable { setHeapDumpsEnabled(true) }
        private var emulatorActivityCount = 0

        override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) {
            if (activity is EmulatorActivity) {
                emulatorActivityCount++
                handler.removeCallbacks(enableHeapDumps)
                setHeapDumpsEnabled(false)
            }
        }

        override fun onActivityDestroyed(activity: Activity) {
            if (activity is EmulatorActivity) {
                emulatorActivityCount--
                if (emulatorActivityCount == 0) {
                    handler.postDelayed(enableHeapDumps, ENABLE_DELAY_MS)
                }
            }
        }

        override fun onActivityStarted(activity: Activity) {}
        override fun onActivityResumed(activity: Activity) {}
        override fun onActivityPaused(activity: Activity) {}
        override fun onActivityStopped(activity: Activity) {}
        override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) {}

        private fun setHeapDumpsEnabled(enabled: Boolean) {
            if (LeakCanary.config.dumpHeap != enabled) {
                LeakCanary.config = LeakCanary.config.copy(dumpHeap = enabled)
            }
        }
    }
}
