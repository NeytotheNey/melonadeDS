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
     * [speedMultiplier] is the fast-forward speed the emulator is paced at, or 0 or less if it runs unlimited; call again when it changes.
     */
    extern void setAudioFastForwardActive(bool active, float speedMultiplier = 0);
    extern bool shouldMuteAudioOutput();
    // Lock-free, for the output callback
    extern bool isAudioFastForwarding();
    // Lock-free, for the output callback: the fast-forward speed multiplier, or 0 or less if fast-forward is unlimited
    extern float getAudioFastForwardSpeed();
    // Lock-free, for the output callback: changes whenever the drift control has to start over (fast-forward toggled or its speed changed,
    // fast-forward unmuted, playback resumed)
    extern uint32_t getAudioRateGeneration();
    // Lock-free, for the output callback: changes whenever the output stream is about to (re)start, so the callback primes again
    extern uint32_t getAudioStartGeneration();
    /**
     * Emulator thread, right after each frame: runs the trim fast-forward ending asked for, now that the last fast-forward frame's audio is in the
     * ring.
     */
    extern void runPendingAudioOutputTrim(MelonInstance& instance);
    /**
     * Lock-free, for the output callback: asks the emulator thread to trim the ring to OboeCallback::TRIM_BEFORE_FRAME_KEEP_FRAMES at the start of
     * its next frame and returns the request, for [isAudioOutputTrimBeforeFrameDone].
     */
    extern uint32_t requestAudioOutputTrimBeforeFrame();
    // Lock-free, for the output callback: whether the emulator thread ran the trim [request] asked for (only the latest request is ever answered)
    extern bool isAudioOutputTrimBeforeFrameDone(uint32_t request);
    /**
     * Emulator thread, right before each frame: runs the trim the output callback asked for.
     */
    extern void runAudioOutputTrimBeforeFrame(MelonInstance& instance);
    extern void setAudioActiveInstance(std::shared_ptr<MelonInstance> instance);
    extern void cleanupAudio();
    extern void startAudio();
    extern void pauseAudio();
    /**
     * For the audio diagnostics: passes the output stream to AudioDiagnostics::onXRunCount() if there is one, without blocking (returns false while
     * another thread holds the stream).
     */
    extern bool pollAudioOutputXRuns();
}

#endif //MELONDSAUDIO_H
