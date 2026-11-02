package me.magnum.melonds.ui.emulator.component

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Handler
import androidx.core.content.getSystemService

/**
 * Holds audio focus for the Oboe stream, which doesn't handle focus itself, so a call or another media app pauses the game.
 *
 * @param handler The handler focus changes are delivered on, which should be the main thread since [onAudioFocusLost] touches the UI.
 * @param onAudioFocusLost Called when focus is lost permanently or transiently (not for ducking, nor when focus comes back).
 */
class EmulatorAudioFocusManager(
    context: Context,
    handler: Handler,
    private val onAudioFocusLost: () -> Unit,
) {

    /**
     * Whether focus was requested but isn't held right now (lost, delayed or failed); false before the first request.
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
        // During a phone call the request is delayed instead of failing, so focus still arrives once the call ends
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
     * Requests audio focus, which is safe while it's already held.
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
