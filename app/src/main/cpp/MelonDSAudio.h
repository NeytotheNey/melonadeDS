#ifndef MELONDSAUDIO_H
#define MELONDSAUDIO_H

#include "Configuration.h"
#include "MelonInstance.h"
#include "types.h"

namespace MelonDSAndroid
{
    extern void userEnableMic();
    extern void userDisableMic();
    extern void enableMic();
    extern void disableMic();
    extern int readMic(melonDS::s16* data, int maxlength);

    extern void setupAudio(AudioSettings audioSettings);
    extern void updateAudioSettings(AudioSettings audioSettings);
    /**
     * [speedMultiplier] is the fast-forward speed the emulator is paced at, or 0 or less if it runs unlimited. Call again when it changes.
     */
    extern void setAudioFastForwardActive(bool active, float speedMultiplier = 0);
    extern bool shouldMuteAudioOutput();
    // Lock-free, for the output callback
    extern bool isAudioFastForwarding();
    // Lock-free, for the output callback. The fast-forward speed multiplier, or 0 or less if fast-forward is unlimited
    extern float getAudioFastForwardSpeed();
    // Lock-free, for the output callback. Changes whenever the drift control has to start over: fast-forward turned on or off or its speed
    // changed, fast-forward unmuted, playback resumed
    extern uint32_t getAudioRateGeneration();
    // Lock-free, for the output callback. Changes whenever the output stream is about to (re)start, so the callback primes again
    extern uint32_t getAudioStartGeneration();
    /**
     * Emulator thread, right after each frame. Runs the trim fast-forward ending asked for, now that the last fast-forward frame's audio is in
     * the ring. Costs one relaxed load otherwise.
     */
    extern void runPendingAudioOutputTrim(MelonInstance& instance);
    /**
     * Lock-free, for the output callback. Asks the emulator thread to trim the ring to OboeCallback::TRIM_BEFORE_FRAME_KEEP_FRAMES at the
     * start of its next frame. Returns the request, for [isAudioOutputTrimBeforeFrameDone].
     */
    extern uint32_t requestAudioOutputTrimBeforeFrame();
    // Lock-free, for the output callback. Whether the emulator thread ran the trim [request] asked for. Only the latest request is ever answered
    extern bool isAudioOutputTrimBeforeFrameDone(uint32_t request);
    /**
     * Emulator thread, right before each frame. Runs the trim the output callback asked for. Costs two relaxed loads otherwise.
     */
    extern void runAudioOutputTrimBeforeFrame(MelonInstance& instance);
    extern void setAudioActiveInstance(std::shared_ptr<MelonInstance> instance);
    extern void cleanupAudio();
    extern void startAudio();
    extern void pauseAudio();
    /**
     * For the audio diagnostics. Passes the output stream to AudioDiagnostics::onXRunCount() if there is one. Never blocks: returns false
     * without doing anything while another thread holds the stream (opening, closing or starting it).
     */
    extern bool pollAudioOutputXRuns();
}

#endif //MELONDSAUDIO_H
