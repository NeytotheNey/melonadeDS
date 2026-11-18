#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <memory>
#include <string>
#include "renderer/Renderer.h"

// Frame rate the emulator runs at without fast-forward; must match EMULATOR_TARGET_FPS in NDS.kt
constexpr int EMULATOR_TARGET_FPS = 60;

namespace MelonDSAndroid
{

struct RenderSettings
{
};

struct SoftwareRenderSettings : public RenderSettings
{
    bool threadedRendering;
};

struct OpenGlRenderSettings : public RenderSettings
{
    bool betterPolygons;
    int scale;
};

struct ComputeRenderSettings : public RenderSettings
{
    int scale;
    bool highResCoordinates;
    // Dithers polygon color gradients to 6 bits instead of dropping the low bits (ComputeRenderer::SetColorDither)
    bool colorDithering;
};

// Must match AudioLatency.latencyValue in AudioLatency.kt
enum class AudioLatency
{
    Low = 0,
    Medium = 1,
    High = 2,
};

struct AudioSettings
{
    bool soundEnabled;
    bool muteFastForwardAudio;
    int volume;
    int audioInterpolation;
    int audioBitrate;
    AudioLatency audioLatency;
    int micSource;
};

struct SdCardSettings
{
    bool enabled;
    char* imagePath;
    int imageSize;
    bool readOnly;
    bool folderSync;
    char* folderPath;
};

typedef struct
{
    char username[11];
    int language;
    int birthdayMonth;
    int birthdayDay;
    int favouriteColour;
    char message[27];
    bool randomizeMacAddress;
    char macAddress[18];
} FirmwareConfiguration;

typedef struct
{
    bool userInternalFirmwareAndBios;
    char* dsBios7Path;
    char* dsBios9Path;
    char* dsFirmwarePath;
    char* dsiBios7Path;
    char* dsiBios9Path;
    char* dsiFirmwarePath;
    char* dsiNandPath;
    char* internalFilesDir;
    // The app's cache dir, for files Android may delete (the compute program cache)
    std::string cacheDir;
    float fastForwardSpeedMultiplier;
    bool framePacingVsync;
    bool framePacingBuffer;
    // OpenGL and compute only: in-between 3D scenes in games that run at 30 FPS (GPU3D_HalfCamera.h), applied in MelonInstance::updateRenderer()
    bool framePacingSmooth30Fps;
    bool showBootScreen;
    bool useJit;
    bool syncRtcToDevice;
    int consoleType;
    AudioSettings audioSettings;
    int rewindEnabled;
    int rewindCaptureSpacingSeconds;
    int rewindLengthSeconds;
    FirmwareConfiguration firmwareConfiguration;
    std::unique_ptr<RenderSettings> renderSettings;
    // Draws the 2D layers on their own thread, for every renderer, so it's not in renderSettings
    bool threaded2dRendering;
    // OpenGL and compute: VRAM display shows display capture redone at the internal resolution (GPU::HiresCapture)
    bool hiresCapture;
    // OpenGL and compute, at internal resolution 2x and up: the compositor smooths 2D edges (GPU::Filter2D)
    bool filter2d;
    // OpenGL and compute, at internal resolution 2x to 6x: rotated and scaled 2D layers drawn at that resolution, the GPU 2D shadow in mode 1
    // (GPU::SetShadow2D, GPU_Shadow2D.h), applied in MelonInstance::updateRenderer()
    bool sharperRotated2d;
    // OpenGL and compute: draw the games' edge marking (GPU::GLEdgeMark 1, or 0 when off). Software always draws it
    bool edgeMarking;
    // Puts the Lua scripts' drawings into savestate and rewind pictures
    bool luaDrawingsInScreenshots;
    // WideMelon: width of the top screen's 3D view (even, 256..768; anything else, 0 when it's off, means 256), on the OpenGL and compute renderers
    // only
    int wideViewWidth;
    // WideMelon camera, stage 2: 3D rows rendered above and below the top screen's 192 (0..128, 0 when the camera is off), with wideViewWidth
    int wideViewMargin;
    SdCardSettings dsiSdCardSettings;
    SdCardSettings dldiSdCardSettings;
    Renderer renderer;
} EmulatorConfiguration;

}

#endif
