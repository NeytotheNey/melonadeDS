package me.magnum.melonds.domain.model.rom.config

import android.net.Uri
import me.magnum.melonds.domain.model.MAX_WIDE_VIEW_WIDTH
import me.magnum.melonds.domain.model.MIN_WIDE_VIEW_WIDTH
import java.util.UUID

data class RomModsConfig(
    // Width of the top screen's 3D view in pixels, or null when widescreen is off
    val wideViewWidth: Int? = null,
    // Tree URI of the folder the game's scripts are read from, or null if none is linked
    val luaScriptFolder: Uri? = null,
    // The scripts found in that folder, kept in step with it by LuaScriptFolderSync
    val luaScripts: List<RomLuaScript> = emptyList(),
    // Wide layout used while widescreen is on, or null for automatic
    val wideLayoutId: UUID? = null,
    // The WideMelon camera (pan and zoom-out) while widescreen is on
    val wideCamera: Boolean = false,
    // How wide the camera can zoom out to in DS pixels, which the core renders at, or null for DEFAULT_WIDE_CAMERA_MAX_WIDTH
    val wideCameraMaxWidth: Int? = null,
    // Whether the camera eases back to the middle while the player moves
    val wideCameraReturnToCenter: Boolean = true,
    // The zoom (1 = native) last picked with the Camera Zoom hotkey, which the camera starts at, or null for 1
    val wideCameraZoom: Float? = null,
    // Whether a text box pulls the camera in to 1x, in the Pokemon games the native field read knows
    val wideCameraZoomOnText: Boolean = true,
) {
    // wideCameraZoom if it's a usable zoom (the camera clamps it to the game's range), otherwise 1
    val validWideCameraZoom: Float get() = wideCameraZoom?.takeIf { it.isFinite() && it >= 1f } ?: 1f

    // wideViewWidth if the core takes it, otherwise null (off)
    val validWideViewWidth: Int? get() = wideViewWidth?.takeIf { it in MIN_WIDE_VIEW_WIDTH..MAX_WIDE_VIEW_WIDTH && it % 2 == 0 }

    // wideCameraMaxWidth if the core takes it, otherwise the default
    val validWideCameraMaxWidth: Int
        get() = wideCameraMaxWidth?.takeIf { it in MIN_WIDE_VIEW_WIDTH..MAX_WIDE_VIEW_WIDTH && it % 2 == 0 } ?: DEFAULT_WIDE_CAMERA_MAX_WIDTH

    companion object {
        // The Mods tab's choices for the camera's widest view (WidescreenAspect's two widest, then 2x, 2.5x and 3x the DS's width)
        val WIDE_CAMERA_MAX_WIDTHS = listOf(426, 448, 512, 640, MAX_WIDE_VIEW_WIDTH)
        const val DEFAULT_WIDE_CAMERA_MAX_WIDTH = 512
    }
}
