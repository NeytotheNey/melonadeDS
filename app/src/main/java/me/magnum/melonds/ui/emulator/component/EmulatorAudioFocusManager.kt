package me.magnum.melonds.ui.emulator.component

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Handler
import androidx.core.content.getSystemService

/**
 * Holds audio focus while the emulator is in use. The native audio goes straight to an Oboe stream, which doesn't handle audio focus by itself, so
 * without this the app is never told that something else needs the audio (e.g. a phone call, a voice assistant or another media app) and the game
 * keeps playing over it.
 *
 * @param handler The handler the focus changes are delivered on. Should be on the main thread, since [onAudioFocusLost] touches the UI
 * @param onAudioFocusLost Called when focus is lost, either permanently or transiently. Focus loss that allows ducking (e.g. navigation prompts) is
 * ignored, since the system already lowers the game's volume by itself. Getting focus back never calls anything, the game is only resumed by the user
 */
class EmulatorAudioFocusManager(
    context: Context,
    handler: Handler,
    private val onAudioFocusLost: () -> Unit,
) {

    /**
     * Whether focus was requested but isn't held right now, either because it was lost or because the request was delayed or failed. False before
     * the first request, so that an activity that is recreated while a game runs doesn't count as having lost focus. Ducking doesn't count as
     * losing focus, since the game keeps playing (quieter) in that case.
     */
    var isFocusLost = false
        private set

    private val audioManager = context.getSystemService<AudioManager>()
    private val focusRequest = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
        .setAudioAttributes(
            // Matches the native output stream (Usage::Game, with Oboe's default Music content type)
            AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build()
        )
        // During a phone call the request is delayed instead of failing, and the system grants focus once the call ends. Without this, focus would
        // never be requested again after a failed request, and the next call would go unnoticed
        .setAcceptsDelayedFocusGain(true)
        .setOnAudioFocusChangeListener({ focusChange ->
            when (focusChange) {
                AudioManager.AUDIOFOCUS_GAIN -> isFocusLost = false
                AudioManager.AUDIOFOCUS_LOSS,
                AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> {
                    isFocusLost = true
                    onAudioFocusLost()
                }
            }
        }, handler)
        .build()

    /**
     * Requests audio focus. Safe to call while focus is already held. During a phone call the request is delayed, and focus arrives through the
     * listener after the call. The game still runs in the meantime, since it's the user who chose to resume it.
     */
    fun requestFocus() {
        val result = audioManager?.requestAudioFocus(focusRequest)
        isFocusLost = result != AudioManager.AUDIOFOCUS_REQUEST_GRANTED
    }

    fun abandonFocus() {
        audioManager?.abandonAudioFocusRequest(focusRequest)
        isFocusLost = false
    }
}
