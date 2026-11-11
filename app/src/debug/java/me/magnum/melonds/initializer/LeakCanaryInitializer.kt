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
import shark.IgnoredReferenceMatcher
import shark.ReferencePattern

/**
 * Debug-only LeakCanary setup that never dumps the heap on its own while a game is open, since a dump freezes the app for a few seconds:
 * dumps are off while an [EmulatorActivity] exists (retained objects are checked once the game closes, which also skips LeakCanary's forced
 * GC every 2 s), otherwise a visible app only dumps from the notification or after a few seconds in the background, and "Dump Heap Now" in
 * the Leaks app always works.
 */
class LeakCanaryInitializer : Initializer<Unit> {

    override fun create(context: Context) {
        LeakCanary.config = LeakCanary.config.copy(
            // A threshold that is never reached: dumps while the app is visible only happen when the notification is tapped
            retainedVisibleThreshold = Int.MAX_VALUE,
            // Android framework singleton that keeps the first text field's activity and predates our code: through its static
            // instance, and on newer Android versions also through the show-secrets setting observer it registers
            referenceMatchers = LeakCanary.config.referenceMatchers + listOf(
                IgnoredReferenceMatcher(
                    ReferencePattern.StaticFieldPattern("android.text.method.TextKeyListener", "sInstance"),
                ),
                IgnoredReferenceMatcher(
                    ReferencePattern.InstanceFieldPattern("android.text.method.TextKeyListener", "mShowSecretsUnregisterRunnable"),
                ),
            ),
        )
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(EmulatorHeapDumpBlocker())
    }

    override fun dependencies(): List<Class<out Initializer<*>>> = emptyList()

    /**
     * Disables heap dumps while an [EmulatorActivity] exists, counting them on the main thread where lifecycle callbacks run.
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
