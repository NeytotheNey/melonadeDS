#ifndef MELONDS_OBOECALLBACK_H
#define MELONDS_OBOECALLBACK_H

#include <oboe/Oboe.h>
#include <fstream>
#include "MelonInstance.h"
#include "debug/AudioDiagnostics.h"

class OboeCallback : public oboe::AudioStreamCallback {
private:
    int _volume;
    void (*onErrorCallback)(oboe::AudioStream* stream);
    std::ostream* _recordingStream;
    float audioSampleFrac;
    AudioDiagnostics::CallbackClock diagnosticsClock;

public:
    std::weak_ptr<MelonDSAndroid::MelonInstance> activeInstance;

    OboeCallback(int volume, void (*onErrorCallback)(oboe::AudioStream* stream)) : OboeCallback(volume, onErrorCallback, nullptr) { };
    OboeCallback(int volume, void (*onErrorCallback)(oboe::AudioStream* stream), std::ostream* recordingStream);
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream, void *audioData, int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result result) override;
    
private:
    int getNumSamplesOut(int len);
};


#endif //MELONDS_OBOECALLBACK_H
