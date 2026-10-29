package me.magnum.melonds.domain.services

import android.net.Uri
import kotlinx.coroutines.flow.Flow
import me.magnum.melonds.domain.model.Cheat
import me.magnum.melonds.domain.model.ConsoleType
import me.magnum.melonds.domain.model.emulator.EmulatorEvent
import me.magnum.melonds.domain.model.emulator.FirmwareLaunchResult
import me.magnum.melonds.domain.model.emulator.LuaScriptStatus
import me.magnum.melonds.domain.model.emulator.RomLaunchResult
import me.magnum.melonds.domain.model.retroachievements.GameAchievementData
import me.magnum.melonds.domain.model.retroachievements.RAEvent
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.model.rom.config.RomLuaScript
import me.magnum.melonds.ui.emulator.rewind.model.RewindSaveState
import me.magnum.melonds.ui.emulator.rewind.model.RewindWindow

interface EmulatorManager {

    val emulatorEvents: Flow<EmulatorEvent>

    /**
     * @param luaScripts The enabled Lua scripts to run, read from the game's script folder. Empty when scripts aren't allowed
     * @param gameCode The game code from the ROM header, which names the script folder (see LuaScriptStorage). Null if it couldn't be read
     */
    suspend fun loadRom(rom: Rom, cheats: List<Cheat>, luaScripts: List<RomLuaScript>, gameCode: String?): RomLaunchResult

    /**
     * Whether any Lua script sent at launch is still running (none has been switched off after an error). Safe to call from any thread
     */
    fun areLuaScriptsRunning(): Boolean

    /**
     * The Lua scripts loaded in this session, in the order sent at launch (their position is their ID). Empty if none was loaded
     */
    fun getLuaScripts(): List<LuaScriptStatus>

    /**
     * Switches a running Lua script off for this session. The ROM config isn't changed
     */
    fun disableLuaScript(scriptId: Int)

    /**
     * Switches a stopped Lua script back on for this session, and clears its last error. The ROM config isn't changed. Returns false if there
     * is no such script or it already runs
     */
    fun enableLuaScript(scriptId: Int): Boolean

    suspend fun loadFirmware(consoleType: ConsoleType): FirmwareLaunchResult

    suspend fun updateRomEmulatorConfiguration(rom: Rom)

    suspend fun updateFirmwareEmulatorConfiguration(consoleType: ConsoleType)

    suspend fun getRewindWindow(): RewindWindow

    fun getFps(): Float

    suspend fun pauseEmulator()

    suspend fun resumeEmulator()

    suspend fun resetEmulator()

    suspend fun updateCheats(cheats: List<Cheat>)
    suspend fun setupRetroAchievements(achievementData: GameAchievementData)
    fun unloadRetroAchievementsData()

    suspend fun loadRewindState(rewindSaveState: RewindSaveState): Boolean

    suspend fun saveState(saveStateFileUri: Uri): Boolean

    suspend fun loadState(saveStateFileUri: Uri): Boolean

    suspend fun takeScreenshot(): Boolean

    fun stopEmulator()

    fun cleanEmulator()

    fun observeRetroAchievementEvents(): Flow<RAEvent>
}