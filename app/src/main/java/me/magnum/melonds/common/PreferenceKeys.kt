package me.magnum.melonds.common

/**
 * Keys of the preferences melonadeDS added on top of upstream melonDS (whose keys stay literals); each must match the android:key in the
 * screen named next to it (PreferenceKeysTest checks) and never changes, since keys are saved on devices and in settings backups.
 */
object PreferenceKeys {
    // pref_video.xml
    const val VIDEO_THREADED_2D = "video_threaded_2d"
    const val VIDEO_HIRES_CAPTURE = "video_hires_capture"
    const val VIDEO_COLOR_DITHERING = "video_color_dithering"
    const val VIDEO_DS_SCREEN_COLORS = "video_ds_screen_colors"
    const val VIDEO_SCREEN_SHADOW = "video_screen_shadow"
    const val VIDEO_LUA_DRAWINGS_IN_STATE_PICTURES = "video_lua_drawings_in_state_pictures"
    const val SHOW_STATUS_BAR = "show_status_bar"
    const val FRAME_PACING_VSYNC = "frame_pacing_vsync"
    const val FRAME_PACING_LOCK_60HZ = "frame_pacing_lock_60hz"
    const val FRAME_PACING_BUFFER = "frame_pacing_buffer"

    // pref_input.xml
    const val INPUT_CONTROLLER_STYLE = "input_controller_style"

    // pref_audio.xml
    const val MUTE_FAST_FORWARD_AUDIO = "mute_fast_forward_audio"

    // pref_performance.xml
    const val ENABLE_PERF_LOGGING = "enable_perf_logging"
    const val PERF_LOG_DIR = "perf_log_dir"
    const val ENABLE_DEBUG_LOG = "enable_debug_log"

    // pref_system.xml
    const val SYNC_RTC_TO_DEVICE = "sync_rtc_to_device"

    // pref_appearance.xml
    const val DYNAMIC_COLORS = "dynamic_colors"

    // Section headers, which only exist in the preference screens and hold no value
    const val CATEGORY_DISPLAY_3D_RENDERING = "category_display_3d_rendering" // pref_video.xml
    const val CATEGORY_CONTROLS_VIBRATION = "category_controls_vibration" // pref_input.xml
    const val CATEGORY_PERFORMANCE_DEVICE = "category_performance_device" // pref_performance.xml

    // Timestamp (ms) of the newest process exit already looked at by ExitReasonReporter, with no preference screen
    const val DEBUG_LAST_REPORTED_EXIT_MS = "debug_last_reported_exit_ms"

    // Layout editor state, with no preference screen
    const val LAYOUT_EDITOR_SNAP_TO_GRID = "layout_editor_snap_to_grid"
    const val LAYOUT_EDITOR_GRID_SIZE = "layout_editor_grid_size"
    const val LAYOUT_EDITOR_ITEM_PANEL_EXPANDED = "layout_editor_item_panel_expanded"
    // Followed by the button's target and the orientation
    const val LAYOUT_EDITOR_MENU_BUTTON_PREFIX = "layout_editor_menu_button"
}
