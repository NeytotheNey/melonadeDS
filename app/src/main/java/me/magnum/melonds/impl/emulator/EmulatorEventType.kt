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
     * Snapshots of the Lua scripts' storage are waiting to be saved (see MelonEmulator.drainLuaData), with no data.
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
     * A Lua script loaded one of its savestates, with no data.
     */
    EventLuaStateLoaded(306),

    /**
     * A Lua script wants the tilt-shift blur changed. Data:
     * * script ID (`i32`)
     * * top band's strength (`f32`), 0 to 1
     * * bottom band's strength (`f32`), 0 to 1
     * * sharp band's center (`f32`), a fraction of the screen's height from the top
     * * sharp band's height (`f32`), a fraction of the screen's height
     * * blur radius (`f32`), in DS pixels
     * * saturation multiplier (`f32`), 0 to 2
     * * contrast multiplier (`f32`), 0 to 2
     */
    EventLuaTiltShift(307),

    /**
     * A Lua script's melon.wide requests changed. Data:
     * * script ID (`i32`)
     * * zoom (`f32`), 1 to 3, or negative for the player's
     * * whether there's a pan (`i32`, 0 or 1)
     * * pan (`f32`), the window's offset from center in DS pixels
     * * whether 1x is held (`i32`, 0 or 1)
     * * whether there's a vertical pan (`i32`, 0 or 1)
     * * vertical pan (`f32`), the window's offset down from center in DS pixels
     */
    EventLuaWideCamera(308),

    /**
     * A Lua script wants its bloom or vignette changed. Data:
     * * script ID (`i32`)
     * * bloom strength (`f32`), 0 to 2, 0 for no bloom
     * * bloom threshold (`f32`), a luma from 0 to 1
     * * bloom radius (`f32`), 0 to 8 DS pixels
     * * vignette strength (`f32`), 0 to 1, 0 for no vignette
     * * vignette size (`f32`), where it starts as a fraction of the half-diagonal, 0 to 1
     */
    EventLuaBloom(309),

    /**
     * A Lua script wants its depth of field changed. Data:
     * * script ID (`i32`)
     * * strength (`f32`), 0 to 1, 0 for none
     * * radius (`f32`), 0 to 12 DS pixels
     * * focus x (`f32`), -256 to 512 DS pixels of the top screen's 256 x 192 view
     * * focus y (`f32`), -192 to 384 DS pixels
     * * focus depth (`f32`), 0 to 1, or -1 for the depth at the focus point
     * * range (`f32`), 0 to 1 of the renderer's depth, kept sharp around the focus
     * * falloff (`f32`), 0 to 1 of the renderer's depth, from the range's edge to the full blur
     */
    EventLuaDepthOfField(310),

    /**
     * A Lua script wants its shading changed. Data:
     * * script ID (`i32`)
     * * strength (`f32`), 0 to 1, 0 for none
     * * radius (`f32`), 1 to 16 DS pixels
     * * range (`f32`), 0 to 1 of the renderer's depth, how much farther than its neighborhood a pixel is when it's fully shaded
     * * color (`i32`), 0xRRGGBB, the shadow's tint
     */
    EventLuaShade(311),
}
