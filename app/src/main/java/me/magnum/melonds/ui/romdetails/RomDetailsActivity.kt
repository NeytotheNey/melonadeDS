package me.magnum.melonds.ui.romdetails

import android.content.Intent
import android.net.Uri
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
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.res.stringResource
import androidx.core.net.toUri
import dagger.hilt.android.AndroidEntryPoint
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.ConsoleType
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.ui.common.rom.EmulatorLaunchValidatorDelegate
import me.magnum.melonds.ui.emulator.EmulatorActivity
import me.magnum.melonds.ui.romdetails.model.LuaScriptEvent
import me.magnum.melonds.ui.romdetails.model.RomConfigUpdateEvent
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
            val romConfig by romDetailsViewModel.romConfigUiState.collectAsState()

            val retroAchievementsUiState by romRetroAchievementsViewModel.uiState.collectAsState()

            LaunchedEffect(null) {
                romRetroAchievementsViewModel.viewAchievementEvent.collect {
                    launchViewAchievementIntent(it)
                }
            }

            // Kept as two saveable values, so the question survives rotation
            var luaScriptToReplaceUri by rememberSaveable { mutableStateOf<Uri?>(null) }
            var luaScriptToReplaceName by rememberSaveable { mutableStateOf<String?>(null) }
            LaunchedEffect(null) {
                romDetailsViewModel.luaScriptEvent.collect {
                    val message = when (it) {
                        is LuaScriptEvent.ConfirmReplace -> {
                            luaScriptToReplaceUri = it.uri
                            luaScriptToReplaceName = it.fileName
                            null
                        }
                        LuaScriptEvent.NotLuaScript -> R.string.lua_script_not_lua_file
                        LuaScriptEvent.TooLarge -> R.string.lua_script_too_large
                        LuaScriptEvent.AddFailed -> R.string.lua_script_add_failed
                        LuaScriptEvent.DeleteFailed -> R.string.lua_script_delete_failed
                    }
                    if (message != null) {
                        Toast.makeText(this@RomDetailsActivity, message, Toast.LENGTH_LONG).show()
                    }
                }
            }

            MelonTheme {
                RomDetailsScreen(
                    rom = rom,
                    romConfigUiState = romConfig,
                    retroAchievementsUiState = retroAchievementsUiState,
                    onNavigateBack = { onNavigateUp() },
                    onLaunchRom = {
                        emulatorLauncherValidatorDelegate.validateRom(it)
                    },
                    onRomConfigUpdate = {
                        romDetailsViewModel.onRomConfigUpdateEvent(it)
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

                val replaceUri = luaScriptToReplaceUri
                val replaceName = luaScriptToReplaceName
                if (replaceUri != null && replaceName != null) {
                    val dismiss = {
                        luaScriptToReplaceUri = null
                        luaScriptToReplaceName = null
                    }
                    AlertDialog(
                        onDismissRequest = dismiss,
                        text = { Text(stringResource(R.string.lua_script_replace_confirmation, replaceName)) },
                        confirmButton = {
                            TextButton(
                                onClick = {
                                    dismiss()
                                    romDetailsViewModel.onRomConfigUpdateEvent(RomConfigUpdateEvent.LuaScriptAdd(replaceUri, replace = true))
                                },
                            ) {
                                Text(stringResource(R.string.lua_script_replace).uppercase())
                            }
                        },
                        dismissButton = {
                            TextButton(onClick = dismiss) {
                                Text(stringResource(R.string.cancel).uppercase())
                            }
                        },
                    )
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        // The hardcore setting may have changed while this screen was in the background
        romDetailsViewModel.refreshLuaScriptsHardcoreState()
    }

    private fun launchViewAchievementIntent(achievementUrl: String) {
        val intent = Intent(Intent.ACTION_VIEW).apply {
            data = achievementUrl.toUri()
        }
        startActivity(intent)
    }
}