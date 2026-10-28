package me.magnum.melonds

import android.app.Activity
import android.app.Application
import android.content.Context
import android.content.pm.ActivityInfo
import android.os.Bundle
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.app.NotificationChannelCompat
import androidx.core.app.NotificationManagerCompat
import androidx.preference.PreferenceManager
import androidx.hilt.work.HiltWorkerFactory
import androidx.work.Configuration
import com.google.android.material.color.DynamicColors
import com.google.android.material.color.DynamicColorsOptions
import dagger.hilt.android.HiltAndroidApp
import kotlinx.coroutines.DelicateCoroutinesApi
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.launch
import me.magnum.melonds.common.UriFileHandler
import me.magnum.melonds.common.uridelegates.UriHandler
import me.magnum.melonds.domain.repositories.SettingsRepository
import me.magnum.melonds.impl.PendingSettingsRestore
import me.magnum.melonds.impl.debug.DebugLogRecorder
import me.magnum.melonds.migrations.Migrator
import javax.inject.Inject

@HiltAndroidApp
class MelonDSApplication : Application(), Configuration.Provider {
    companion object {
        const val NOTIFICATION_CHANNEL_ID_BACKGROUND_TASKS = "channel_cheat_importing"

        init {
            System.loadLibrary("melonDS-android-frontend")
        }
    }

    @Inject lateinit var workerFactory: HiltWorkerFactory
    @Inject lateinit var settingsRepository: SettingsRepository
    @Inject lateinit var migrator: Migrator
    @Inject lateinit var uriHandler: UriHandler
    @Inject lateinit var debugLogRecorder: DebugLogRecorder

    private val activeActivities = mutableSetOf<Activity>()

    val dynamicColorsOptions: DynamicColorsOptions by lazy {
        DynamicColorsOptions.Builder()
            .setThemeOverlay(R.style.ThemeOverlay_MelonDS_DynamicColors)
            .setPrecondition { _, _ -> settingsRepository.isDynamicColorsEnabled() }
            .build()
    }

    override fun attachBaseContext(base: Context) {
        super.attachBaseContext(base)
        // A settings restore is applied here because this runs before anything else in the app: before the content providers (the
        // androidx.startup initializers already resolve Hilt singletons), before Hilt injects this class in onCreate(), and before the
        // migrations. Hilt isn't available yet, so it's created by hand
        PendingSettingsRestore(this, PreferenceManager.getDefaultSharedPreferences(this)).applyIfPending()
    }

    override fun onCreate() {
        super.onCreate()
        // First, so that the debug log (if it's on) covers the rest of the start-up
        debugLogRecorder.start()
        createNotificationChannels()
        applyTheme()
        applyDynamicColors()
        performMigrations()
        MelonDSAndroidInterface.setup(UriFileHandler(this, uriHandler))
    }

    private fun createNotificationChannels() {
        val defaultChannel = NotificationChannelCompat.Builder(NOTIFICATION_CHANNEL_ID_BACKGROUND_TASKS, NotificationManagerCompat.IMPORTANCE_LOW)
            .setName(getString(R.string.notification_channel_background_tasks))
            .build()

        val notificationManager = NotificationManagerCompat.from(this)
        notificationManager.createNotificationChannel(defaultChannel)
    }

    @OptIn(DelicateCoroutinesApi::class)
    private fun applyTheme() {
        GlobalScope.launch(Dispatchers.Main) {
            settingsRepository.observeTheme().collect {
                AppCompatDelegate.setDefaultNightMode(it.nightMode)
            }
        }
    }

    @OptIn(DelicateCoroutinesApi::class)
    private fun applyDynamicColors() {
        DynamicColors.applyToActivitiesIfAvailable(this, dynamicColorsOptions)

        if (!DynamicColors.isDynamicColorAvailable()) {
            return
        }

        registerActivityLifecycleCallbacks(object : ActivityLifecycleCallbacks {
            override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) {
                activeActivities.add(activity)
            }

            override fun onActivityDestroyed(activity: Activity) {
                activeActivities.remove(activity)
            }

            override fun onActivityStarted(activity: Activity) {}
            override fun onActivityResumed(activity: Activity) {}
            override fun onActivityPaused(activity: Activity) {}
            override fun onActivityStopped(activity: Activity) {}
            override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) {}
        })

        // Dynamic colours are applied when an activity is created, so recreate the existing activities when the setting changes. Like with
        // night mode changes, activities that handle UI mode changes themselves (the emulator and the layout editor) are not recreated
        GlobalScope.launch(Dispatchers.Main) {
            settingsRepository.observeDynamicColorsEnabled().distinctUntilChanged().drop(1).collect {
                activeActivities.filterNot { it.handlesUiModeChanges() }.forEach { it.recreate() }
            }
        }
    }

    private fun Activity.handlesUiModeChanges(): Boolean {
        val activityInfo = packageManager.getActivityInfo(componentName, 0)
        return activityInfo.configChanges and ActivityInfo.CONFIG_UI_MODE != 0
    }

    private fun performMigrations() {
        migrator.performMigrations()
    }

    override fun onTerminate() {
        super.onTerminate()
        MelonDSAndroidInterface.cleanup()
    }

    override val workManagerConfiguration: Configuration get() {
        return Configuration.Builder()
            .setWorkerFactory(workerFactory)
            .build()
    }
}