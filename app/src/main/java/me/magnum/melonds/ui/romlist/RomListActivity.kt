package me.magnum.melonds.ui.romlist

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.res.stringResource
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.google.android.material.color.DynamicColors
import dagger.hilt.android.AndroidEntryPoint
import kotlinx.coroutines.flow.collectLatest
import me.magnum.melonds.MelonDSApplication
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.ConsoleType
import me.magnum.melonds.domain.model.DownloadProgress
import me.magnum.melonds.domain.model.RomScanningStatus
import me.magnum.melonds.domain.model.appupdate.AppUpdate
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.impl.PendingSettingsRestore
import me.magnum.melonds.parcelables.RomParcelable
import me.magnum.melonds.ui.common.rom.EmulatorLaunchValidatorDelegate
import me.magnum.melonds.ui.dsiwaremanager.DSiWareManagerActivity
import me.magnum.melonds.ui.emulator.EmulatorActivity
import me.magnum.melonds.ui.romdetails.RomDetailsActivity
import me.magnum.melonds.ui.romlist.ui.DownloadProgressDialog
import me.magnum.melonds.ui.romlist.ui.NightlyUpdateDialog
import me.magnum.melonds.ui.romlist.ui.ProdUpdateAvailableDialog
import me.magnum.melonds.ui.romlist.ui.RomListScreen
import me.magnum.melonds.ui.settings.SettingsActivity
import me.magnum.melonds.ui.theme.MelonTheme

@AndroidEntryPoint
class RomListActivity : AppCompatActivity() {

    private val viewModel: RomListViewModel by viewModels()
    private val updatesViewModel: UpdatesViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        installSplashScreen()
        // installSplashScreen() switches to the post-splash theme, which overrides the dynamic colors applied when the activity was created
        DynamicColors.applyToActivityIfAvailable(this, (application as MelonDSApplication).dynamicColorsOptions)
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)

        val emulatorLauncherValidatorDelegate = EmulatorLaunchValidatorDelegate(this, object : EmulatorLaunchValidatorDelegate.Callback {
            override fun onRomValidated(rom: Rom) {
                viewModel.setRomLastPlayedNow(rom)
                val intent = EmulatorActivity.getRomEmulatorActivityIntent(this@RomListActivity, rom)
                startActivity(intent)
            }

            override fun onFirmwareValidated(consoleType: ConsoleType) {
                val intent = EmulatorActivity.getFirmwareEmulatorActivityIntent(this@RomListActivity, consoleType)
                startActivity(intent)
            }

            override fun onValidationAborted() {
                // Do nothing
            }
        })

        // Whether a settings restore was applied when the app started, read only on this screen's first creation since it can be read once
        val restoreResult = if (savedInstanceState == null) PendingSettingsRestore.consumeResult(this) else null

        setContent {
            val roms by viewModel.roms.collectAsStateWithLifecycle()
            val romScanningStatus by viewModel.romScanningStatus.collectAsStateWithLifecycle(initialValue = RomScanningStatus.NOT_SCANNING)
            val hasSearchDirectories by viewModel.hasSearchDirectories.collectAsStateWithLifecycle(initialValue = true)
            val viewMode by viewModel.viewMode.collectAsStateWithLifecycle()
            val sortingMode by viewModel.sortingMode.collectAsStateWithLifecycle()
            val sortingOrder by viewModel.sortingOrder.collectAsStateWithLifecycle()

            var currentUpdate by remember { mutableStateOf<AppUpdate?>(null) }
            var downloadProgress by remember { mutableStateOf<DownloadProgress?>(null) }
            var showInvalidDirectoryDialog by remember { mutableStateOf(false) }
            // Saveable, so the message survives a rotation, since the result can only be read once
            var restoreResultToShow by rememberSaveable { mutableStateOf(restoreResult) }

            LaunchedEffect(Unit) {
                updatesViewModel.appUpdate.collectLatest {
                    currentUpdate = it
                }
            }

            LaunchedEffect(Unit) {
                updatesViewModel.updateDownloadProgressEvent.collectLatest { progress ->
                    when (progress) {
                        is DownloadProgress.DownloadUpdate -> {
                            downloadProgress = progress
                        }
                        is DownloadProgress.DownloadComplete -> {
                            downloadProgress = null
                        }
                        is DownloadProgress.DownloadFailed -> {
                            downloadProgress = null
                            Toast.makeText(this@RomListActivity, R.string.update_download_failed, Toast.LENGTH_LONG).show()
                        }
                    }
                }
            }

            LaunchedEffect(Unit) {
                viewModel.invalidDirectoryAccessEvent.collectLatest {
                    showInvalidDirectoryDialog = true
                }
            }

            MelonTheme {
                RomListScreen(
                    roms = roms,
                    isRefreshing = romScanningStatus == RomScanningStatus.SCANNING,
                    hasSearchDirectories = hasSearchDirectories,
                    onSearchQueryChange = viewModel::setRomSearchQuery,
                    sortingMode = sortingMode,
                    sortingOrder = sortingOrder,
                    onSortChange = viewModel::setRomSorting,
                    viewMode = viewMode,
                    onViewModeChange = viewModel::setRomListViewMode,
                    onFirmwareBoot = { consoleType ->
                        emulatorLauncherValidatorDelegate.validateFirmware(consoleType)
                    },
                    onRomSelected = { rom ->
                        emulatorLauncherValidatorDelegate.validateRom(rom)
                    },
                    onRomConfigClick = { rom ->
                        val intent = Intent(this@RomListActivity, RomDetailsActivity::class.java).apply {
                            putExtra(RomDetailsActivity.KEY_ROM, RomParcelable(rom))
                        }
                        startActivity(intent)
                    },
                    onRefresh = viewModel::refreshRoms,
                    onDirectorySelected = viewModel::addRomSearchDirectory,
                    onNavigateToSettings = {
                        val intent = Intent(this@RomListActivity, SettingsActivity::class.java)
                        startActivity(intent)
                    },
                    onNavigateToDsiWareManager = {
                        val intent = Intent(this@RomListActivity, DSiWareManagerActivity::class.java)
                        startActivity(intent)
                    },
                    retrieveRomIcon = { rom ->
                        viewModel.getRomIcon(rom)
                    },
                    romMetadata = viewModel.romMetadata,
                    hasRetroAchievementsWebApiKey = viewModel::hasRetroAchievementsWebApiKey,
                )

                currentUpdate?.let { update ->
                    when (update.type) {
                        AppUpdate.Type.PRODUCTION -> {
                            ProdUpdateAvailableDialog(
                                update = update,
                                onUpdate = {
                                    currentUpdate = null
                                    downloadProgress = DownloadProgress.DownloadUpdate(0, 0)
                                    updatesViewModel.downloadUpdate(update)
                                },
                                onSkip = {
                                    updatesViewModel.skipUpdate(update)
                                    currentUpdate = null
                                },
                                onDismiss = {
                                    currentUpdate = null
                                },
                            )
                        }
                        AppUpdate.Type.NIGHTLY -> {
                            NightlyUpdateDialog(
                                onUpdate = {
                                    currentUpdate = null
                                    downloadProgress = DownloadProgress.DownloadUpdate(0, 0)
                                    updatesViewModel.downloadUpdate(update)
                                },
                                onDismiss = {
                                    updatesViewModel.skipUpdate(update)
                                    currentUpdate = null
                                },
                            )
                        }
                    }
                }

                (downloadProgress as? DownloadProgress.DownloadUpdate)?.let { progress ->
                    DownloadProgressDialog(
                        downloadProgress = progress,
                        onMoveToBackground = {
                            downloadProgress = null
                        },
                    )
                }

                if (showInvalidDirectoryDialog) {
                    AlertDialog(
                        onDismissRequest = { showInvalidDirectoryDialog = false },
                        title = { Text(stringResource(R.string.error_invalid_directory)) },
                        text = { Text(stringResource(R.string.error_invalid_directory_description)) },
                        confirmButton = {
                            TextButton(
                                onClick = { showInvalidDirectoryDialog = false },
                            ) {
                                Text(stringResource(R.string.ok).uppercase())
                            }
                        },
                    )
                }

                restoreResultToShow?.let { restoreSucceeded ->
                    AlertDialog(
                        onDismissRequest = { restoreResultToShow = null },
                        text = {
                            Text(stringResource(if (restoreSucceeded) R.string.settings_restore_success else R.string.settings_restore_failed_kept))
                        },
                        confirmButton = {
                            TextButton(
                                onClick = { restoreResultToShow = null },
                            ) {
                                Text(stringResource(R.string.ok).uppercase())
                            }
                        },
                    )
                }
            }
        }
    }
}