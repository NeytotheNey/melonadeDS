package me.magnum.melonds.impl.emulator

/**
 * Event types that can be emitted by the emulator. These constants must match the values defined in AndroidMelonEventMessenger.h
 */
enum class EmulatorEventType(val event: Int) {
    /**
     * Rumble start event. Data:
     * * rumble duration in ms (`i32`)
     */
    EventRumbleStart(100),
    /**
     * Rumble stop event. No data.
     */
    EventRumbleStop(101),
    /**
     * Emulator stop event. No data.
     */
    EventEmulatorStop(102),

    /**
     * RA achievement primed. Data:
     * * achievement ID (`i64`)
     */
    EventRAAchievementPrimed(200),

    /**
     * RA achievement triggered. Data:
     * * achievement ID (`i64`)
     */
    EventRAAchievementTriggered(201),

    /**
     * RA achievement unprimed. Data:
     * * achievement ID (`i64`)
     */
    EventRAAchievementUnprimed(202),

    /**
     * RA achievement progress updated. Data:
     * * achievement ID (`i64`)
     * * current progress value (`i32`)
     * * target progress value (`i32`)
     * * formated progress string size (`i32`)
     * * formated progress string (`u8[32]`)
     */
    EventRAAchievementProgressUpdated(203),

    /**
     * RA leaderboard attempt started. Data:
     * * leaderboard ID (`i64`)
     */
    EventRALeaderboardAttemptStarted(210),

    /**
     * RA leaderboard attempt updated. Data:
     * * leaderboard ID (`i64`)
     * * formated value string size (`i32`)
     * * formated value string (`u8[32]`)
     */
    EventRALeaderboardAttemptUpdated(211),

    /**
     * RA leaderboard attempt canceled. Data:
     * * leaderboard ID (`i64`)
     */
    EventRALeaderboardAttemptCanceled(212),

    /**
     * RA leaderboard attempt completed. Data:
     * * leaderboard ID (`i64`)
     * * leaderboard value (`i32`)
     * * formated value string size (`i32`)
     * * formated value string (`u8[32]`)
     */
    EventRALeaderboardAttemptCompleted(213),

    /**
     * A Lua script was switched off after an error. Data:
     * * script ID (`i32`): its position in the list given to `MelonEmulator.setupLuaScripts()`
     * * error size (`i32`)
     * * error (`u8[]`, UTF-8, cut short to fit)
     */
    EventLuaScriptStopped(300),

    /**
     * A Lua script wants the screens swapped or not. Data:
     * * script ID (`i32`)
     * * swapped (`i32`, 0 or 1)
     */
    EventLuaSwapScreens(301),

    /**
     * A Lua script wants the on-screen controls shown or hidden. Data:
     * * script ID (`i32`)
     * * visible (`i32`, 0 or 1)
     */
    EventLuaShowControls(302),

    /**
     * A Lua script wants fast-forward on or off. Data:
     * * script ID (`i32`)
     * * enabled (`i32`, 0 or 1)
     */
    EventLuaFastForward(303),

    /**
     * Snapshots of the Lua scripts' storage are waiting to be saved (see MelonEmulator.drainLuaData). No data.
     */
    EventLuaDataSaved(304),

    /**
     * A Lua script wants the layout one of its layout options names, or the player's layout back. Data:
     * * script ID (`i32`)
     * * option key size (`i32`), 0 for the player's layout
     * * option key (`u8[]`, at most 32 bytes)
     */
    EventLuaSetLayout(305),

    /**
     * A Lua script loaded one of its savestates. No data.
     */
    EventLuaStateLoaded(306),
}
