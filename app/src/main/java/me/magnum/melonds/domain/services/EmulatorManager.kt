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
     * @param luaScripts The enabled Lua scripts to run, empty when scripts aren't allowed
     * @param luaScriptFolder The game's linked script folder, or null if none is linked
     * @param areLuaStateLoadsAllowed Whether the scripts may load savestates (false in RetroAchievements hardcore)
     * @param wideViewWidth The width the game starts at (WidescreenLaunch.viewWidth), or 0 for the normal width
     */
    suspend fun loadRom(
        rom: Rom,
        cheats: List<Cheat>,
        luaScripts: List<RomLuaScript>,
        luaScriptFolder: Uri?,
        areLuaStateLoadsAllowed: Boolean,
        wideViewWidth: Int,
    ): RomLaunchResult

    /**
     * Whether any Lua script sent at launch is still running; safe to call from any thread.
     */
    fun areLuaScriptsRunning(): Boolean

    /**
     * The Lua scripts of this session by ID: the ones sent at launch, then the ones that couldn't be read.
     */
    fun getLuaScripts(): List<LuaScriptStatus>

    /**
     * The Lua script with this ID, or null if the session has none
     */
    fun getLuaScript(scriptId: Int): LuaScriptStatus?

    /**
     * Switches a running Lua script off for this session without changing the ROM config.
     */
    fun disableLuaScript(scriptId: Int)

    /**
     * Switches a stopped Lua script back on for this session; returns false if it doesn't exist, couldn't be read or already runs.
     */
    fun enableLuaScript(scriptId: Int): Boolean

    /**
     * Restarts (or starts) a script with new option values; returns false if it doesn't exist or couldn't be read.
     */
    fun restartLuaScript(scriptId: Int, optionValues: Map<String, String>): Boolean

    suspend fun loadFirmware(consoleType: ConsoleType): FirmwareLaunchResult

    suspend fun updateRomEmulatorConfiguration(rom: Rom)

    /**
     * Changes the running game's widescreen width (0 for the normal width), when the layout shown asks for another one
     */
    fun setWideViewWidth(width: Int)

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