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
    extern void setAudioFastForwardActive(bool active);
    extern bool shouldMuteAudioOutput();
    // Lock-free, for the output callback
    extern bool isAudioFastForwarding();
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
