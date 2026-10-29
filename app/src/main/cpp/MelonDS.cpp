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

        // Normally there is no instance left at this point, since cleanup() destroys it. If there is one, stop its audio first and destroy it
        // with the GL context current, like cleanup() does
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

    // The RetroAchievements calls below come from coroutines that can outlive the emulator, so the instance may already be gone (or be
    // destroyed by another thread meanwhile). Hold the lock for the whole call instead of copying the pointer out: a copy could make this
    // thread the last owner, and the instance must be destroyed in destroyInstance(), with the GL context current

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
        // Settings can change after the emulator was stopped. The next setConfiguration() replaces the configuration anyway
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

    void setLuaScripts(std::vector<LuaScriptSource> scripts, std::shared_ptr<const LuaFolderContent> folderContent)
    {
        if (instance)
            instance->setLuaScripts(std::move(scripts), std::move(folderContent));
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
        return instance->runFrame(presentDueTimeNs);
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

        std::unique_ptr<Savestate> backup = std::make_unique<Savestate>(Savestate::DEFAULT_SIZE);
        if (backup->Error)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to allocate memory for state backup\n");
            return false;
        }

        // Runs on an IO thread, so the emulator can be stopped and the instance destroyed meanwhile. Keep the lock until the backup is
        // restored, so that the backup and the load happen on the same instance
        std::lock_guard lock(presentationMutex);
        if (!instance)
            return false;

        if (!instance->saveState(backup.get()) || backup->Error)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to back up state, aborting load (from \"%s\")\n", path);
            return false;
        }

        std::unique_ptr<Savestate> state = std::make_unique<Savestate>(buffer.data(), size, false);

        if (!instance->loadState(state.get()) || state->Error)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to load state file \"%s\" into emulator\n", path);
            // Restore backup
            if (!instance->loadState(backup.get()) || backup->Error)
                Platform::Log(Platform::LogLevel::Error, "Failed to load backup state\n", path);
            else
                Platform::Log(Platform::LogLevel::Info, "Backup state loaded\n", path);

            return false;
        }
        return true;
    }

    bool loadRewindState(melonDS::RewindSaveState rewindSaveState)
    {
        std::unique_ptr<Savestate> backup = std::make_unique<Savestate>(Savestate::DEFAULT_SIZE);
        if (backup->Error)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to allocate memory for state backup");
            return false;
        }

        if (!instance->saveState(backup.get()) || backup->Error)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to back up state, aborting rewind state load");
            return false;
        }

        bool result = instance->loadRewindState(rewindSaveState);
        if (!result)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to load rewind state");
            // Restore backup
            if (!instance->loadState(backup.get()) || backup->Error)
                Platform::Log(Platform::LogLevel::Error, "Failed to load backup state");
            else
                Platform::Log(Platform::LogLevel::Info, "Backup state loaded");
        }

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

        // The instance owns GL objects (frame textures and fences, the 3D renderer, the compositor), which its destructor deletes. It runs on the
        // thread that stops the emulator (usually the main thread), which has no GL context current, so every glDelete* call would be a no-op and
        // the objects would leak. The emulator thread has already released the emulator context in stop() (or never started), so make it current
        // here while the instance is destroyed.
        //
        // The render thread may be presenting one of the instance's frames (and using its fences), or waiting for one. Wake it up, then wait
        // for it to finish before destroying the frame queue. The lock is taken after waking it, because the render thread holds it while it
        // waits. The render thread never keeps a reference to the instance: if it destroyed it, the glDelete* calls would run in the render
        // context, where FBO and VAO names belong to the frontend's renderers
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

