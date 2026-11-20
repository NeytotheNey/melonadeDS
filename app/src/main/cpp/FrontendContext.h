#ifndef FRONTENDCONTEXT_H
#define FRONTENDCONTEXT_H

#include <atomic>
#include <memory>
#include <string>
#include "MelonEventMessenger.h"

namespace MelonDSAndroid
{

/**
 * What the shared native code (MelonInstance and the code it runs) needs from the frontend it runs in, the Android app or the desktop. Each
 * frontend fills it in before it makes an instance: the app in MelonDS.cpp (setConfiguration() and setup()), the desktop in its main.cpp.
 */
struct FrontendContext
{
    // The app's internal files directory, for the internal firmware's save (wfcsettings.bin) and the internal BIOS files
    std::string internalFilesDir;
    // Where the emulator's events go (rumble, RetroAchievements, Lua requests, stops)
    std::shared_ptr<MelonEventMessenger> eventMessenger;
    // See setHalvesAt1xSupported(), setOutputScaleNeed() and setOutputDeband()
    std::atomic_bool halvesAt1xSupported = false;
    std::atomic_int outputScaleNeed = 0;
    std::atomic_bool outputDeband = false;
};

extern FrontendContext frontendContext;

/**
 * GPU::Comp1x: whether every surface presenting frames can draw a screen the compositor drew at 1x (Frame::halvesAt1x), as the render thread
 * last found after presenting; read by the emulator thread before each frame (GPU::Comp1xFrontend). Off until a presentation says so
 */
void setHalvesAt1xSupported(bool supported);
bool areHalvesAt1xSupported();
/**
 * GPU::OutputCap: how many times the native size the screens are shown at, at most, over every surface presenting frames (0 for unknown),
 * as the render thread last worked it out (FrameRenderCoordinator); read by the emulator thread before each frame (MelonInstance::runFrame)
 */
void setOutputScaleNeed(int need);
int getOutputScaleNeed();
/**
 * GPU::OutputCapDeband: whether any surface presenting frames debands them (its renderer configuration's deband), as the render thread
 * last saw it (FrameRenderCoordinator); read by the emulator thread before each frame along with the need (MelonInstance::runFrame)
 */
void setOutputDeband(bool deband);
bool getOutputDeband();

}

#endif //FRONTENDCONTEXT_H
