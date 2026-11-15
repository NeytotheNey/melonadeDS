#include <cstring>
#include <utility>
#include <android/asset_manager.h>
#include <oboe/Oboe.h>
#include "EmulatorArgsBuilder.h"
#include "MelonDS.h"
#include "MelonDSAudio.h"
#include "OboeCallback.h"
#include "MicInputOboeCallback.h"
#include "OpenGLContext.h"
#include "mic_blow.h"
#include "NDS.h"
#include "GPU.h"
#include "GPU3D.h"
#include "GBACart.h"
#include "OpenGLSupport.h"
#include "GPU3D_ComputeRooms.h"
#include "SPU.h"
#include "Platform.h"
#include "Savestate.h"
#include "MelonInstance.h"
#include "RewindManager.h"
#include "ROMManager.h"
#include "MPInterface.h"
#include "AndroidCameraHandler.h"
#include "renderer/ScreenshotRenderer.h"
#include "renderer/FrameQueue.h"
#include "retroachievements/RetroAchievementsManager.h"
#include "net/Net.h"
#include "net/Net_Slirp.h"
#include <fstream>

namespace MelonDSAndroid
{
    OpenGLContext *openGlContext;
    AndroidFileHandler* fileHandler;
    AndroidCameraHandler* cameraHandler;
    std::string internalFilesDir;
    std::shared_ptr<MelonEventMessenger> eventMessenger;
    std::shared_ptr<EmulatorConfiguration> currentConfiguration;
    std::shared_ptr<Net> net;

    std::shared_ptr<MelonInstance> instance;
    std::recursive_mutex presentationMutex;

    bool setupOpenGlContext();
    void cleanupOpenGlContext();
    void destroyInstance();

    /**
     * Used to set the emulator's initial configuration, before boot. To update the configuration during runtime, use @updateEmulatorConfiguration.
     *
     * @param emulatorConfiguration The emulator configuration during the next emulator run
     */
    void setConfiguration(EmulatorConfiguration emulatorConfiguration) {
        currentConfiguration = std::make_shared<EmulatorConfiguration>(std::move(emulatorConfiguration));
        internalFilesDir = currentConfiguration->internalFilesDir;
        // Read at the compute renderer's first compile, on the emulator thread, which this runs before
        melonDS::OpenGL::SetShaderCachePath(currentConfiguration->cacheDir.empty() ? std::string() : currentConfiguration->cacheDir + "/compute_programs.bin");
        // The same for the work buffer rooms each game grew to, read when the targets are set up
        melonDS::ComputeWorkRooms::SetPath(currentConfiguration->cacheDir.empty() ? std::string() : currentConfiguration->cacheDir + "/compute_rooms.bin");

        net = std::make_shared<Net>();
        net->SetDriver(std::make_unique<Net_Slirp>([](const u8* data, int len) {
            net->RXEnqueue(data, len);
        }));
    }

    void setup(AndroidCameraHandler* androidCameraHandler, std::shared_ptr<MelonEventMessenger> androidEventMessenger, u32* screenshotBufferPointer, int instanceId)
    {
        cameraHandler = androidCameraHandler;
        eventMessenger = androidEventMessenger;
        RetroAchievements::RetroAchievementsManager::EventMessenger = androidEventMessenger;

        // Normally cleanup() has destroyed the instance by now, but if not, stop its audio first and destroy it with the GL context current, like
        // cleanup() does
        if (instance)
        {
            cleanupAudio();
            destroyInstance();
        }

        auto instanceArgs = BuildArgsFromConfiguration(*currentConfiguration, instanceId);
        if (!instanceArgs.has_value())
        {
            // TODO: Handle this somehow?
            return;
        }
        auto newInstance = std::make_shared<MelonInstance>(
            instanceId,
            currentConfiguration,
            std::move(instanceArgs.value()),
            net,
            std::make_unique<ScreenshotRenderer>(screenshotBufferPointer),
            currentConfiguration->consoleType
        );
        {
            std::lock_guard lock(presentationMutex);
            instance = std::move(newInstance);
        }

        setupAudio(currentConfiguration->audioSettings);
        setAudioActiveInstance(instance);
    }

    void setCodeList(std::list<Cheat> cheats)
    {
        // Cheats can be updated from a coroutine that runs after the emulator was stopped
        if (instance)
            instance->loadCheats(std::move(cheats));
    }

    // The RetroAchievements calls below come from coroutines that can outlive the emulator, so they hold the lock for the whole call rather than copy
    // the pointer out, which could make this thread the last owner (the instance must be destroyed in destroyInstance(), with the GL context current)

    void setupAchievements(
        std::list<RetroAchievements::RAAchievement> achievements,
        std::list<RetroAchievements::RALeaderboard> leaderboards,
        std::optional<std::string> richPresenceScript
    )
    {
        // Also keeps it from changing the achievement list while getRuntimeAchievements() reads it on another thread
        std::lock_guard lock(presentationMutex);
        if (!instance)
            return;

        instance->setupAchievements(std::move(achievements), std::move(leaderboards), std::move(richPresenceScript));
    }

    void unloadRetroAchievementsData()
    {
        std::lock_guard lock(presentationMutex);
        if (!instance)
            return;

        instance->unloadRetroAchievementsData();
    }

    std::string getRichPresenceStatus()
    {
        std::lock_guard lock(presentationMutex);
        if (!instance)
            return "";

        return instance->getRichPresenceStatus();
    }

    std::vector<RetroAchievements::RARuntimeAchievement> getRuntimeAchievements()
    {
        std::lock_guard lock(presentationMutex);
        if (!instance)
            return { };

        return instance->getRuntimeAchievements();
    }

    /**
     * Used to update the emulator's configuration during runtime. Will only update the configurations that can actually change during runtime without causing issues,
     *
     * @param emulatorConfiguration The new emulator configuration
     */
    void updateEmulatorConfiguration(std::unique_ptr<EmulatorConfiguration> emulatorConfiguration) {
        // Settings can change after the emulator was stopped, and the next setConfiguration() replaces the configuration anyway
        if (!instance)
            return;

        std::shared_ptr<EmulatorConfiguration> sharedConfig = std::move(emulatorConfiguration);
        instance->updateConfiguration(sharedConfig);
        updateAudioSettings(sharedConfig->audioSettings);

        currentConfiguration = sharedConfig;
    }

    int loadRom(std::string romPath, std::string sramPath, RomGbaSlotConfig* gbaSlotConfig)
    {
        if (!instance->loadRom(std::move(romPath), std::move(sramPath)))
            return 2;

        if (gbaSlotConfig->type == GBA_ROM)
        {
            RomGbaSlotConfigGbaRom* gbaRomConfig = (RomGbaSlotConfigGbaRom*) gbaSlotConfig;
            if (!instance->loadGbaRom(gbaRomConfig->romPath, gbaRomConfig->savePath))
                return 1;
        }
        else if (gbaSlotConfig->type == RUMBLE_PAK)
        {
            instance->loadRumblePak();
        }
        else if (gbaSlotConfig->type == MEMORY_EXPANSION)
        {
            instance->loadGbaMemoryExpansion();
        }
        else if (gbaSlotConfig->type == MOTION_PAK_HOMEBREW)
        {
            instance->loadMotionPakHomebrew();
        }
        else if (gbaSlotConfig->type == MOTION_PAK_RETAIL)
        {
            instance->loadMotionPakRetail();
        }

        return 0;
    }

    int bootFirmware()
    {
        // TODO: Maybe validate BIOS and firmware?
        if (instance->bootFirmware())
            return ROMManager::SUCCESS;
        else
            return ROMManager::FIRMWARE_NOT_BOOTABLE;
    }

    void touchScreen(u16 x, u16 y)
    {
        if (instance)
            instance->touchScreen(x, y);
    }

    void releaseScreen()
    {
        if (instance)
            instance->releaseScreen();
    }

    void pressKey(u32 key)
    {
        if (instance)
            instance->pressKey(key);
    }

    void releaseKey(u32 key)
    {
        if (instance)
            instance->releaseKey(key);
    }

    void updateMotionData(float ax, float ay, float az, float rx, float ry, float rz)
    {
        if (instance)
            instance->updateMotionData(ax, ay, az, rx, ry, rz);
    }

    void setWideViewWidth(int width)
    {
        if (instance)
            instance->setWideViewWidth(width);
    }

    void setLuaScripts(std::vector<LuaScriptSource> scripts, std::shared_ptr<const LuaFolderContent> folderContent, bool areStateLoadsAllowed)
    {
        if (instance)
            instance->setLuaScripts(std::move(scripts), std::move(folderContent), areStateLoadsAllowed);
    }

    void disableLuaScript(int scriptId)
    {
        if (instance)
            instance->disableLuaScript(scriptId);
    }

    void enableLuaScript(int scriptId)
    {
        if (instance)
            instance->enableLuaScript(scriptId);
    }

    void restartLuaScript(int scriptId, std::vector<LuaOptionValue> options)
    {
        if (instance)
            instance->restartLuaScript(scriptId, std::move(options));
    }

    void flushLuaStorage()
    {
        if (instance)
            instance->flushLuaStorage();
    }

    void finishRewindScreenshot()
    {
        if (instance)
            instance->finishRewindScreenshot(true);
    }

    void updateLuaSticks(float leftX, float leftY, float rightX, float rightY)
    {
        if (instance)
            instance->updateLuaSticks(leftX, leftY, rightX, rightY);
    }

    void start()
    {
        startAudio();
        setupOpenGlContext();

        instance->start();
    }

    u32 loop(int64_t presentDueTimeNs)
    {
        MPInterface::Get().Process();
        runAudioOutputTrimBeforeFrame(*instance);
        u32 lines = instance->runFrame(presentDueTimeNs);
        runPendingAudioOutputTrim(*instance);
        return lines;
    }

    void logDebugFrameState(bool isFastForward, float fps, int64_t lastFrameWorkNs)
    {
        // Called by the emulator thread right after loop(), so the instance is the one that just ran
        if (instance)
            instance->logDebugFrameState(isFastForward, fps, lastFrameWorkNs);
    }

    pid_t getGpu2DThreadId()
    {
        // Same as above: the emulator thread calls it right after loop()
        return instance ? instance->getGpu2DThreadId() : 0;
    }

#ifdef MELONADE_DEBUG_TOOLS
    void takeCaptureStats(CaptureStats& stats)
    {
        // Same as above
        stats = {};
        if (instance)
            instance->takeCaptureStats(stats);
    }
#endif

    int getAudioOutputBufferFill()
    {
        return instance ? instance->getAudioOutputBufferFill() : 0;
    }

    Frame* getPresentationFrame(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline, int64_t vsyncTimeNs, int64_t dueToleranceNs, int64_t upcomingFrameDueNs)
    {
        // The caller must keep holding the lock while it uses the frame
        std::lock_guard lock(presentationMutex);
        if (!instance)
            return nullptr;

        return instance->getPresentationFrame(deadline, vsyncTimeNs, dueToleranceNs, upcomingFrameDueNs);
    }

    bool isSleeping()
    {
        std::lock_guard lock(presentationMutex);
        if (!instance)
            return false;

        return instance->isSleeping();
    }

    static std::atomic_bool halvesAt1xSupported = false;

    void setHalvesAt1xSupported(bool supported)
    {
        halvesAt1xSupported.store(supported, std::memory_order_relaxed);
    }

    bool areHalvesAt1xSupported()
    {
        return halvesAt1xSupported.load(std::memory_order_relaxed);
    }

    static std::atomic_int outputScaleNeed = 0;

    void setOutputScaleNeed(int need)
    {
        outputScaleNeed.store(need, std::memory_order_relaxed);
    }

    int getOutputScaleNeed()
    {
        return outputScaleNeed.load(std::memory_order_relaxed);
    }

    static std::atomic_bool outputDeband = false;

    void setOutputDeband(bool deband)
    {
        outputDeband.store(deband, std::memory_order_relaxed);
    }

    bool getOutputDeband()
    {
        return outputDeband.load(std::memory_order_relaxed);
    }

    void pause()
    {
        pauseAudio();
    }

    void syncDateTimeToDevice()
    {
        if (instance)
            instance->syncDateTimeToDevice();
    }

    void resume()
    {
        startAudio();
    }

    void reset()
    {
        instance->reset();
    }

    bool saveState(const char* path)
    {
        Savestate state;
        if (state.Error)
            return false;

        {
            // Runs on an IO thread, so the emulator can be stopped and the instance destroyed meanwhile
            std::lock_guard lock(presentationMutex);
            if (!instance)
                return false;

            if (!instance->saveState(&state) || state.Error)
            {
                Platform::Log(Platform::LogLevel::Error, "Failed to create savestate for \"%s\"\n", path);
                return false;
            }
        }

        // Only open the file once the state is ready: opening it truncates the slot, and a failed save must keep the previous state
        Platform::FileHandle* saveStateFile = Platform::OpenFile(path, Platform::FileMode::Write);
        if (!saveStateFile)
            return false;

        if (Platform::FileWrite(state.Buffer(), state.Length(), 1, saveStateFile) == 0)
        {
            Platform::Log(Platform::Error, "Failed to write %d-byte savestate to %s\n", state.Length(), path);
            Platform::CloseFile(saveStateFile);
            return false;
        }

        Platform::CloseFile(saveStateFile);
        return true;
    }

    bool loadState(const char* path)
    {
        auto saveStateFile = Platform::OpenFile(path, Platform::FileMode::Read);
        if (!saveStateFile)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to open state file \"%s\"\n", path);
            return false;
        }

        size_t size = Platform::FileLength(saveStateFile);

        // Allocate exactly as much memory as we need for the savestate
        std::vector<u8> buffer(size);
        if (Platform::FileRead(buffer.data(), size, 1, saveStateFile) == 0)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to read %u-byte state file \"%s\"\n", size, path);
            Platform::CloseFile(saveStateFile);
            return false;
        }
        Platform::CloseFile(saveStateFile);

        // Runs on an IO thread, so keep the lock until the load (and the backup restore if it fails) is done, all on the same instance
        std::lock_guard lock(presentationMutex);
        if (!instance)
            return false;

        Savestate state(buffer.data(), size, false);
        // The instance backs up the current state first and goes back to it if the load fails, and the scripts only hear about a load that worked
        if (!instance->loadState(&state))
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to load state file \"%s\" into emulator\n", path);
            return false;
        }
        return true;
    }

    bool loadRewindState(melonDS::RewindSaveState rewindSaveState)
    {
        // Backed up and restored on failure by the instance, like any load
        bool result = instance->loadRewindState(rewindSaveState);
        if (!result)
            Platform::Log(Platform::LogLevel::Error, "Failed to load rewind state");

        return result;
    }

    RewindWindow getRewindWindow()
    {
        // Called from a coroutine that can outlive the emulator
        std::lock_guard lock(presentationMutex);
        if (!instance)
            return { };

        return instance->getRewindWindow();
    }

    bool takeScreenshot()
    {
        if (instance)
            return instance->takeScreenshot();

        return false;
    }

    void stop()
    {
        instance->stop();
        cleanupOpenGlContext();
    }

    void cleanup()
    {
        // Stop audio first, so that the audio callback can't be holding the last reference to the instance and destroy it on its own thread
        cleanupAudio();

        destroyInstance();
        eventMessenger = nullptr;
    }

    void destroyInstance()
    {
        if (!instance)
            return;

        // Destroyed with the emulator GL context current, after waking the render thread (and only then taking the lock, which it holds while it
        // waits), so the instance's GL objects are really deleted and nothing presents a frame of it meanwhile
        instance->abortPresentationWait();
        std::lock_guard lock(presentationMutex);

        bool isContextCurrent = setupOpenGlContext();
        instance = nullptr;
        if (isContextCurrent)
            cleanupOpenGlContext();
    }

    bool setupOpenGlContext()
    {
        if (openGlContext == nullptr)
            return false;

        if (!openGlContext->Use())
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to use OpenGL context");
            return false;
        }

        return true;
    }

    void cleanupOpenGlContext()
    {
        if (openGlContext == nullptr)
            return;

        openGlContext->Release();
    }
}

