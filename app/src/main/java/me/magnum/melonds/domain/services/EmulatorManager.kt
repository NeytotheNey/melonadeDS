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
     * @param luaScripts The enabled Lua scripts to run, read from [luaScriptFolder]. Empty when scripts aren't allowed
     * @param luaScriptFolder The game's linked script folder. Null if none is linked
     */
    suspend fun loadRom(rom: Rom, cheats: List<Cheat>, luaScripts: List<RomLuaScript>, luaScriptFolder: Uri?): RomLaunchResult

    /**
     * Whether any Lua script sent at launch is still running (none has been switched off after an error). Safe to call from any thread
     */
    fun areLuaScriptsRunning(): Boolean

    /**
     * The Lua scripts of this session (their position is their ID): the ones sent at launch in that order, then the ones that couldn't be
     * read, which are off with the reason as their error. Empty if none was switched on
     */
    fun getLuaScripts(): List<LuaScriptStatus>

    /**
     * Switches a running Lua script off for this session. The ROM config isn't changed
     */
    fun disableLuaScript(scriptId: Int)

    /**
     * Switches a stopped Lua script back on for this session, and clears its last error. The ROM config isn't changed. Returns false if there
     * is no such script, it couldn't be read at launch, or it already runs
     */
    fun enableLuaScript(scriptId: Int): Boolean

    /**
     * Restarts a Lua script with new option values (by key, as text), or starts it if it was off. Values are checked against the script's
     * declarations, and bad ones fall back to the default. The ROM config isn't changed here. Returns false if there is no such script or it
     * couldn't be read at launch
     */
    fun restartLuaScript(scriptId: Int, optionValues: Map<String, String>): Boolean

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