package me.magnum.melonds.ui.romdetails

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.core.net.toUri
import dagger.hilt.android.AndroidEntryPoint
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.ConsoleType
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.ui.common.rom.EmulatorLaunchValidatorDelegate
import me.magnum.melonds.ui.emulator.EmulatorActivity
import me.magnum.melonds.ui.romdetails.model.LuaScriptEvent
import me.magnum.melonds.ui.romdetails.model.SaveBackupEvent
import me.magnum.melonds.ui.romdetails.ui.RomDetailsScreen
import me.magnum.melonds.ui.theme.MelonTheme

@AndroidEntryPoint
class RomDetailsActivity : AppCompatActivity() {

    companion object {
        const val KEY_ROM = "rom"
    }

    private val romDetailsViewModel by viewModels<RomDetailsViewModel>()
    private val romRetroAchievementsViewModel by viewModels<RomDetailsRetroAchievementsViewModel>()

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        val emulatorLauncherValidatorDelegate = EmulatorLaunchValidatorDelegate(this, object : EmulatorLaunchValidatorDelegate.Callback {
            override fun onRomValidated(rom: Rom) {
                val intent = EmulatorActivity.getRomEmulatorActivityIntent(this@RomDetailsActivity, rom)
                startActivity(intent)
            }

            override fun onFirmwareValidated(consoleType: ConsoleType) {
                // Do nothing (can't launch firmware fro this screen)
            }

            override fun onValidationAborted() {
                // Do nothing
            }
        })

        setContent {
            val rom by romDetailsViewModel.rom.collectAsState()
            val romMetadata by romDetailsViewModel.romMetadata.collectAsStateWithLifecycle()
            val romConfig by romDetailsViewModel.romConfigUiState.collectAsState()
            val saveBackups by romDetailsViewModel.saveBackups.collectAsState()

            val retroAchievementsUiState by romRetroAchievementsViewModel.uiState.collectAsState()

            LaunchedEffect(null) {
                romRetroAchievementsViewModel.viewAchievementEvent.collect {
                    launchViewAchievementIntent(it)
                }
            }

            LaunchedEffect(null) {
                romDetailsViewModel.luaScriptEvent.collect {
                    val message = when (it) {
                        LuaScriptEvent.FolderPickFailed -> R.string.lua_script_folder_pick_failed
                    }
                    Toast.makeText(this@RomDetailsActivity, message, Toast.LENGTH_LONG).show()
                }
            }

            LaunchedEffect(null) {
                romDetailsViewModel.saveBackupEvent.collect {
                    val message = when (it) {
                        SaveBackupEvent.RESTORED -> R.string.save_backup_restored
                        SaveBackupEvent.GAME_RUNNING -> R.string.save_backup_restore_game_running
                        SaveBackupEvent.BACKUP_FAILED -> R.string.save_backup_restore_backup_failed
                        SaveBackupEvent.FAILED -> R.string.save_backup_restore_failed
                    }
                    Toast.makeText(this@RomDetailsActivity, message, Toast.LENGTH_LONG).show()
                }
            }

            MelonTheme {
                RomDetailsScreen(
                    rom = rom,
                    romMetadata = romMetadata,
                    romConfigUiState = romConfig,
                    saveBackupsUiState = saveBackups,
                    retroAchievementsUiState = retroAchievementsUiState,
                    onNavigateBack = { onNavigateUp() },
                    onLaunchRom = {
                        emulatorLauncherValidatorDelegate.validateRom(it)
                    },
                    onRomConfigUpdate = {
                        romDetailsViewModel.onRomConfigUpdateEvent(it)
                    },
                    onModsTabShown = {
                        romDetailsViewModel.syncLuaScripts()
                    },
                    onRetroAchievementsLogin = { username, password ->
                        romRetroAchievementsViewModel.login(username, password)
                    },
                    onRetroAchievementsRetryLoad = {
                        romRetroAchievementsViewModel.retryLoadAchievements()
                    },
                    onViewAchievement = {
                        romRetroAchievementsViewModel.viewAchievement(it)
                    }
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()
        // Refreshes state the emulator, settings or the script folder may have changed in the background
        romDetailsViewModel.refreshLuaScriptsHardcoreState()
        romDetailsViewModel.reloadRom()
        romDetailsViewModel.syncLuaScripts()
    }

    private fun launchViewAchievementIntent(achievementUrl: String) {
        val intent = Intent(Intent.ACTION_VIEW).apply {
            data = achievementUrl.toUri()
        }
        startActivity(intent)
    }
}