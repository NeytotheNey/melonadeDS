#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <chrono>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <filesystem>
#include <GLES3/gl3.h>
#include "Args.h"
#include "CRC32.h"
#include "GPU3D_Compute.h"
#include "Configuration.h"
#include "DSi.h"
#include "DSiSupport.h"
#include "DSi_I2C.h"
#include "GPU3D_OpenGL.h"
#include "MelonDS.h"
#include "MelonInstance.h"
#include "NDS.h"
#include "NDSCart.h"
#include "net/Net_Slirp.h"
#include "Platform.h"
#include "PlatformAndroid.h"
#include "SDCardArgsBuilder.h"
#include "debug/DebugLog.h"
#ifdef MELONADE_DEBUG_TOOLS
#include "MelonLog.h"
#include "debug/DebugProperties.h"
#endif

using namespace std;
using namespace melonDS;
using namespace melonDS::Platform;

namespace MelonDSAndroid
{

const int kRewindBufferSize = 1024 * 1024 * 20; // Use 20MB per savestate
const int kRewindScreenshotSize = 256 * 384 * 4;
const u32 kUserTouchDown = 1u << 31;

MelonInstance::MelonInstance(int instanceId, std::shared_ptr<EmulatorConfiguration> configuration, std::unique_ptr<melonDS::NDSArgs> args, std::shared_ptr<Net> net, std::unique_ptr<ScreenshotRenderer> screenshotRenderer, int consoleType) :
    instanceId(instanceId),
    currentConfiguration(configuration),
    net(net),
    screenshotRenderer(std::move(screenshotRenderer)),
    consoleType(consoleType),
    rewindManager(configuration->rewindEnabled, configuration->rewindLengthSeconds, configuration->rewindCaptureSpacingSeconds, kRewindBufferSize, kRewindScreenshotSize)
{
    // Software renderer is always used during initialisation. Actual renderer will be set of first frame run
    currentRenderer = Renderer::Software;
    isRenderConfigurationDirty = true;
    inputMask = 0xFFF;
    frame = 0;
    luaDrawingsInScreenshots = configuration->luaDrawingsInScreenshots;
    wideViewWidth = configuration->wideViewWidth;
    requestedWideViewWidth = wideViewWidth;

    net->RegisterInstance(instanceId);

    if (consoleType == 1)
    {
        melonDS::DSiArgs &dsiArgs = static_cast<melonDS::DSiArgs &>(*args);
        nds = new DSi(std::move(dsiArgs), this);
    }
    else
    {
        nds = new NDS(std::move(*args), this);
    }

#ifdef MELONADE_DEBUG_TOOLS
    debugOverrides = DebugProperties::apply(nds->GPU);
#endif

    if (configuration->userInternalFirmwareAndBios)
    {
        std::filesystem::path firmwarePath = MelonDSAndroid::internalFilesDir;
        firmwarePath /= "wfcsettings.bin";
        firmwareSave = std::make_unique<SaveManager>(firmwarePath);
    }
    else
    {
        std::string firmwarePathString;
        if (consoleType == 1)
            firmwarePathString = configuration->dsiFirmwarePath;
        else
            firmwarePathString = configuration->dsFirmwarePath;

        firmwareSave = std::make_unique<SaveManager>(firmwarePathString);
    }

    // All instances have a RetroAchievements manager, but only the first instance will actually load achievements
    retroAchievementsManager = std::make_unique<RetroAchievements::RetroAchievementsManager>(nds);

    nds->Reset();
    setBatteryLevels();
    setDateTime();
}

MelonInstance::~MelonInstance()
{
    // Normally done in stop(). The scripts can't outlive the NDS, which their API (and their __gc metamethods) use
    luaEngine.stop();
    frameQueue.clear();
    net->UnregisterInstance(instanceId);
    delete nds;
}

bool MelonInstance::loadRom(std::string romPath, std::string sramPath)
{
    unique_ptr<u8[]> romData;
    unique_ptr<u8[]> sramData;
    u32 romFileLength = 0;
    u32 sramFileLength = 0;

    // ROM file loading
    Platform::FileHandle* romFile = Platform::OpenFile(romPath, FileMode::Read);
    if (!romFile)
        return false;

    u64 length = Platform::FileLength(romFile);
    if (length > 0x40000000)
    {
        Platform::CloseFile(romFile);
        return false;
    }

    romFileLength = (u32) length;
    Platform::FileRewind(romFile);
    romData = make_unique<u8[]>(romFileLength);
    size_t nread = Platform::FileRead(romData.get(), (size_t) romFileLength, 1, romFile);
    Platform::CloseFile(romFile);
    if (nread != 1)
    {
        return false;
    }

    // SRAM file loading
    FileHandle* sramFile = Platform::OpenFile(sramPath, FileMode::Read);
    if (!sramFile)
    {
        return false;
    }
    else if (!Platform::CheckFileWritable(sramPath))
    {
        return false;
    }

    sramFileLength = (u32) Platform::FileLength(sramFile);

    FileRewind(sramFile);
    sramData = std::make_unique<u8[]>(sramFileLength);
    FileRead(sramData.get(), sramFileLength, 1, sramFile);
    CloseFile(sramFile);

    // The CRC of the whole file identifies the dump. It takes a moment for big ROMs, so it's only worked out for the debug log
    hasRomCrc32 = DebugLog::isEnabled();
    romCrc32 = hasRomCrc32 ? CRC32(romData.get(), (int) romFileLength) : 0;

    NDSCart::NDSCartArgs cartargs{
        // No DLDI SD card: the app never turns one on (MelonDSAndroidConfiguration)
        .SDCard = std::nullopt,
        .SRAM = std::move(sramData),
        .SRAMLength = sramFileLength,
    };

    auto cart = NDSCart::ParseROM(std::move(romData), romFileLength, this, std::move(cartargs));
    if (!cart)
    {
        return false;
    }

    nds->SetNDSCart(std::move(cart));
    ndsSave = std::make_unique<SaveManager>(sramPath);

    return true;
}

bool MelonInstance::loadGbaRom(std::string romPath, std::string sramPath)
{
    unique_ptr<u8[]> romData;
    unique_ptr<u8[]> sramData = nullptr;
    u32 romFileLength = 0;
    u32 sramFileLength = 0;

    // ROM file loading
    Platform::FileHandle* romFile = Platform::OpenFile(romPath, FileMode::Read);
    if (!romFile)
        return false;

    u64 length = Platform::FileLength(romFile);
    if (length > 0x40000000)
    {
        Platform::CloseFile(romFile);
        return false;
    }

    romFileLength = length;
    Platform::FileRewind(romFile);
    romData = make_unique<u8[]>(romFileLength);
    size_t nread = Platform::FileRead(romData.get(), (size_t) romFileLength, 1, romFile);
    Platform::CloseFile(romFile);
    if (nread != 1)
    {
        return false;
    }

    FileHandle* saveFile = Platform::OpenFile(sramPath, FileMode::Read);
    if (!saveFile)
    {
        return false;
    }
    else if (!Platform::CheckFileWritable(sramPath))
    {
        return false;
    }

    sramFileLength = (u32) FileLength(saveFile);

    if (sramFileLength > 0)
    {
        FileRewind(saveFile);
        sramData = std::make_unique<u8[]>(sramFileLength);
        FileRead(sramData.get(), sramFileLength, 1, saveFile);
    }
    CloseFile(saveFile);

    auto cart = GBACart::ParseROM(std::move(romData), romFileLength, std::move(sramData), sramFileLength, this);
    if (!cart)
    {
        return false;
    }

    nds->SetGBACart(std::move(cart));
    gbaSave = std::make_unique<SaveManager>(sramPath);

    return true;
}

void MelonInstance::loadRumblePak()
{
    auto rumblePakCart = GBACart::LoadAddon(GBAAddon_RumblePak, this);
    nds->SetGBACart(std::move(rumblePakCart));
}

void MelonInstance::loadGbaMemoryExpansion()
{
    auto memoryExpansionCart = GBACart::LoadAddon(GBAAddon_RAMExpansion, this);
    nds->SetGBACart(std::move(memoryExpansionCart));
}

void MelonInstance::loadMotionPakHomebrew()
{
    auto motionPakCart = GBACart::LoadAddon(GBAAddon_MotionPakHomebrew, this);
    nds->SetGBACart(std::move(motionPakCart));
}

void MelonInstance::loadMotionPakRetail()
{
    auto motionPakCart = GBACart::LoadAddon(GBAAddon_MotionPakRetail, this);
    nds->SetGBACart(std::move(motionPakCart));
}

bool MelonInstance::bootFirmware()
{
    if (nds->NeedsDirectBoot())
        return false;

    return true;
}

void MelonInstance::start()
{
    auto cart = nds->NDSCartSlot.GetCart();
    if (nds->ConsoleType == 1 && cart != nullptr && cart->GetHeader().IsDSiWare() && !currentConfiguration->showBootScreen)
    {
        auto dsi = (DSi*) nds;
        DSiSupport::SetupDSiDirectBoot(dsi);
    }
    else if (!currentConfiguration->showBootScreen || nds->NeedsDirectBoot())
    {
        // This seems to be unused, but it's required
        std::string romName;
        nds->SetupDirectBoot(romName);
    }
    nds->Start();

    screenshotRenderer->init();
    luaEngine.start(nds, MelonDSAndroid::eventMessenger, this);

    if (DebugLog::isEnabled()) [[unlikely]]
        DebugLog::logGameStart(*nds, "start", hasRomCrc32, romCrc32);
}

void MelonInstance::reset()
{
    nds->Reset();
    setBatteryLevels();
    setDateTime();

    // If there is a cart inserted, check if direct boot is required
    if (nds->GetNDSCart())
    {
        if (!currentConfiguration->showBootScreen || nds->NeedsDirectBoot())
        {
            // This seems to be unused, but it's required
            std::string romName;
            nds->SetupDirectBoot(romName);
        }
    }

    rewindManager.Reset();
    retroAchievementsManager->Reset();
    nds->Start();
    // The emulator thread is parked, so the scripts are reloaded before the next frame instead
    luaEngine.notifyReset();

    if (DebugLog::isEnabled()) [[unlikely]]
        DebugLog::logGameStart(*nds, "reset", hasRomCrc32, romCrc32);
}

u32 MelonInstance::runFrame(int64_t presentDueTimeNs)
{
    // A width change only needs the existing GL or Compute renderer told. If the renderer is about to be set up again, updateRenderer()
    // applies it
    int requestedWidth = requestedWideViewWidth.load(std::memory_order_relaxed);
    if (requestedWidth != wideViewWidth)
    {
        wideViewWidth = requestedWidth;
        if (!isRenderConfigurationDirty && currentRenderer == Renderer::OpenGl)
            static_cast<GLRenderer&>(nds->GPU.GetRenderer3D()).SetViewWidth(wideViewWidth);
        else if (!isRenderConfigurationDirty && currentRenderer == Renderer::Compute)
            static_cast<ComputeRenderer&>(nds->GPU.GetRenderer3D()).SetViewWidth(wideViewWidth);
    }

    if (isRenderConfigurationDirty)
    {
        updateRenderer();
        isRenderConfigurationDirty = false;
    }

    if (hasPendingRewindSettings.load(std::memory_order_acquire)) [[unlikely]]
        applyRewindSettings();

    int screenWidth;
    int screenHeight;
    // In DS pixels. The bottom screen is always 256 wide, centred under it with black bars beside it when this is wider
    int topScreenWidth = 256;
    if (currentRenderer == Renderer::OpenGl)
    {
        auto& glRenderer = static_cast<GLRenderer &>(nds->GPU.GetRenderer3D());
        int scale = glRenderer.GetScaleFactor();
        // The width the compositor draws this frame at, which can lag behind the requested one
        topScreenWidth = glRenderer.GetOutputWidth();
        screenWidth = topScreenWidth * scale;
        screenHeight = (192 + 1) * scale;
    }
    else if (currentRenderer == Renderer::Compute)
    {
        auto computeRenderSettings = static_cast<ComputeRenderSettings&>(*currentConfiguration->renderSettings);
        int scale = computeRenderSettings.scale;
        // Same as OpenGL, the compositor's width can lag behind the requested one
        topScreenWidth = static_cast<ComputeRenderer&>(nds->GPU.GetRenderer3D()).GetOutputWidth();
        screenWidth = topScreenWidth * scale;
        screenHeight = (192 + 1) * scale;
    }
    else
    {
        screenWidth = 256;
        screenHeight = 192 + 1;
    }

    Frame* renderFrame = frameQueue.getRenderFrame();

    EGLDisplay currentDisplay = eglGetCurrentDisplay();
    // Delete old render fence
    if (renderFrame->renderFence)
    {
        eglDestroySyncKHR(currentDisplay, renderFrame->renderFence);
        renderFrame->renderFence = 0;
    }

    // Ensure presentation is finished
    if (renderFrame->presentFence)
    {
        eglWaitSyncKHR(currentDisplay, renderFrame->presentFence, 0);
    }

    // Validate frame after ensuring that the frame has finished presenting
    frameQueue.validateRenderFrame(renderFrame, screenWidth, screenHeight * 2);
    renderFrame->topScreenWidth = topScreenWidth;

    [[unlikely]] if (nds->GPU.GetRenderer3D().NeedsShaderCompile())
    {
        // Compile all required shaders at once
        do
        {
            int currentShader;
            int shadersCount;
            nds->GPU.GetRenderer3D().ShaderCompileStep(currentShader, shadersCount);
        }
        while (nds->GPU.GetRenderer3D().NeedsShaderCompile());
    }

    bool isRendererAccelerated = nds->GPU.GetRenderer3D().Accelerated;
    if (isRendererAccelerated)
    {
        int backBuffer = nds->GPU.FrontBuffer ? 0 : 1;
        nds->GPU.GetRenderer3D().SetOutputTexture(backBuffer, renderFrame->frameTexture);
    }

    bool hasLuaFrameRun = false;
    if (luaEngine.isActive() || isLuaInputActive) [[unlikely]]
        hasLuaFrameRun = runLuaFrame();

    u32 nLines = nds->RunFrame();
    retroAchievementsManager->FrameUpdate();

    if (!isRendererAccelerated)
    {
        int frontbuf = nds->GPU.FrontBuffer;
        if (nds->GPU.Framebuffer[frontbuf][0] && nds->GPU.Framebuffer[frontbuf][1])
        {
            glBindTexture(GL_TEXTURE_2D, renderFrame->frameTexture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, nds->GPU.Framebuffer[frontbuf][0].get());
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 192 + 2, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, nds->GPU.Framebuffer[frontbuf][1].get());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
    else
    {
        // Do nothing. Emulator already renders into the texture, which was set-up above
    }

    // on_frame_end(), which also draws the frame's Lua drawings. Scripts only stop during runLuaFrame() and endFrame(), and endFrame() leaves
    // their drawings out, so the overlay is empty once none runs
    if (hasLuaFrameRun) [[unlikely]]
        luaEngine.endFrame();

    // Lua drawings, before the fence so that they're presented with this frame. While no script runs, this only clears the frame's old
    // drawings, if it has any
    const LuaOverlay& luaOverlay = luaEngine.getOverlay();
    if (luaOverlay.hasDrawings() || renderFrame->overlayTexture) [[unlikely]]
    {
        TextureRows overlayRows[2] = {
            { luaOverlay.getRowStart(LuaOverlay::Top), luaOverlay.getRowEnd(LuaOverlay::Top) },
            { luaOverlay.getRowStart(LuaOverlay::Bottom), luaOverlay.getRowEnd(LuaOverlay::Bottom) },
        };
        frameQueue.updateOverlay(renderFrame, luaOverlay.getPixels(), LuaOverlay::kWidth, LuaOverlay::kHeight, overlayRows);
    }

    bool isSleeping = nds->CPUStop & CPUStop_Sleep;
    sleeping.store(isSleeping, std::memory_order_relaxed);
    if (!isSleeping) [[likely]]
    {
        renderFrame->renderFence = eglCreateSyncKHR(currentDisplay, EGL_SYNC_FENCE_KHR, nullptr);
        glFlush();
        frameQueue.pushRenderedFrame(renderFrame, presentDueTimeNs);
    }
    else
    {
        frameQueue.discardRenderedFrame(renderFrame);
    }

    if (ndsSave)
        ndsSave->CheckFlush();

    if (gbaSave)
        gbaSave->CheckFlush();

    if (firmwareSave)
        firmwareSave->CheckFlush();

    frame++;
    // The last capture's screenshot, once the GPU has it
    if (screenshotRenderer->isScreenshotReadbackPending()) [[unlikely]]
        finishRewindScreenshot(false);
    rewindManager.PrepareNextRewindSaveState();

    bool needsRewindCapture = rewindManager.ShouldCaptureState(frame);
    bool needsScreenshot = screenshotRenderer->isScreenshotPending();

#ifdef MELONADE_DEBUG_TOOLS
    auto captureStart = std::chrono::steady_clock::now();
#endif
    if (needsRewindCapture || needsScreenshot) [[unlikely]]
    {
        // The overlay only has drawings while a script runs
        const LuaOverlay* screenshotOverlay = luaDrawingsInScreenshots.load(std::memory_order_relaxed) && luaOverlay.hasDrawings() ? &luaOverlay : nullptr;
        // The last capture's screenshot came in long ago, unless the GPU is very far behind
        if (needsRewindCapture)
            finishRewindScreenshot(true);

        // A rewind capture doesn't wait for the GPU: its screenshot is copied in a frame or so later. A player's screenshot is waited for
        if (needsScreenshot)
            screenshotRenderer->renderScreenshot(&nds->GPU, currentRenderer, renderFrame, screenshotOverlay);
        else
            screenshotRenderer->startScreenshotReadback(&nds->GPU, currentRenderer, renderFrame, screenshotOverlay);
    }

    if (needsRewindCapture)
    {
        auto nextRewindState = rewindManager.GetNextRewindSaveState(frame);
        saveRewindState(nextRewindState);
#ifdef MELONADE_DEBUG_TOOLS
        // Mostly the core's savestate copy (~20 MB in DSi mode)
        double captureMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - captureStart).count();
        LOG_INFO(LOG_TAG_DEBUG, "REWIND frame=%d capture_ms=%.3f size=%u", frame, captureMs, nextRewindState->bufferContentSize);
#endif
    }

    return nLines;
}

void MelonInstance::applyRewindSettings()
{
    std::optional<RewindSettings> settings;
    {
        std::lock_guard lock(rewindSettingsMutex);
        settings = pendingRewindSettings;
        pendingRewindSettings.reset();
        hasPendingRewindSettings.store(false, std::memory_order_relaxed);
    }

    if (!settings)
        return;

    // Turning rewind off or shortening the window frees states, so the last capture's screenshot is put in its state first
    finishRewindScreenshot(true);
    rewindManager.UpdateRewindSettings(settings->enabled, settings->lengthSeconds, settings->captureSpacingSeconds);
}

void MelonInstance::finishRewindScreenshot(bool wait)
{
    // No state waits for it if its save failed, and then the screenshot is dropped. Resets and rewinds only happen while the emulator thread
    // is parked, and it finishes this before it parks
    RewindSaveState* state = rewindManager.FindRewindSaveState(pendingRewindScreenshotFrame);
    if (screenshotRenderer->finishScreenshotReadback(state ? state->screenshot : nullptr, wait))
        pendingRewindScreenshotFrame = -1;
}

void MelonInstance::stop()
{
    luaEngine.stop();

    // The RetroAchievements manager is kept until the instance is destroyed. Savestates and RetroAchievements calls on other threads use it
    // under presentationMutex, which the emulator thread doesn't hold here, so freeing it now could free it under them. The instance is
    // always destroyed right after stopping (it can't be started again), and the manager's teardown doesn't need the emulator thread
    screenshotRenderer->cleanup();
}

void MelonInstance::updateMotionData(float ax, float ay, float az, float rx, float ry, float rz)
{
    motionData[MotionQueryType::MotionAccelerationX].store(ax, std::memory_order_relaxed);
    motionData[MotionQueryType::MotionAccelerationY].store(ay, std::memory_order_relaxed);
    motionData[MotionQueryType::MotionAccelerationZ].store(az, std::memory_order_relaxed);
    motionData[MotionQueryType::MotionRotationX].store(rx, std::memory_order_relaxed);
    motionData[MotionQueryType::MotionRotationY].store(ry, std::memory_order_relaxed);
    motionData[MotionQueryType::MotionRotationZ].store(rz, std::memory_order_relaxed);
}

float MelonInstance::getMotionData(MotionQueryType type)
{
    if (type < MotionQueryType::MotionAccelerationX || type > MotionQueryType::MotionRotationZ)
        return 0.0f;

    return motionData[type].load(std::memory_order_relaxed);
}

void MelonInstance::touchScreen(u16 x, u16 y)
{
    userTouch.store(kUserTouchDown | ((u32) (x & 0x7FFF) << 16) | y);
    // While a script runs, the emulator thread applies the touch before each frame instead
    if (!isLuaInputActive)
        nds->TouchScreen(x, y);
}

void MelonInstance::releaseScreen()
{
    userTouch.store(0);
    if (!isLuaInputActive)
        nds->ReleaseScreen();
}

void MelonInstance::pressKey(u32 key)
{
    // Special handling for Lid input
    if (key == 16 + 7)
    {
        nds->SetLidClosed(true);
    }
    else
    {
        u32 newMask = inputMask.fetch_and(~(1u << key)) & ~(1u << key);
        if (!isLuaInputActive)
            nds->SetKeyMask(newMask);
    }
}

void MelonInstance::releaseKey(u32 key)
{
    // Special handling for Lid input
    if (key == 16 + 7)
    {
        nds->SetLidClosed(false);
    }
    else
    {
        u32 newMask = inputMask.fetch_or(1u << key) | (1u << key);
        if (!isLuaInputActive)
            nds->SetKeyMask(newMask);
    }
}

int MelonInstance::readAudioOutput(s16* buffer, int length)
{
    return nds->SPU.ReadOutput(buffer, length);
}

void MelonInstance::setAudioOutputSkew(double skew)
{
    nds->SPU.SetOutputSkew(skew);
}

int MelonInstance::trimAudioOutput(int thresholdFrames, int keepFrames)
{
    return nds->SPU.TrimOutputIfAbove(thresholdFrames, keepFrames);
}

int MelonInstance::getAudioOutputBufferFill()
{
    return nds->SPU.GetOutputSize();
}

bool MelonInstance::takeScreenshot()
{
    return screenshotRenderer->takeScreenshot();
}

void MelonInstance::loadCheats(std::list<Cheat> cheats)
{
    std::vector<ARCode> codeList;

    for (auto cheat : cheats)
    {
        ARCode arCode {
            .Enabled = true,
            .Code = cheat.code,
        };
        codeList.push_back(arCode);
    }

    nds->AREngine.Cheats = codeList;
}

int MelonInstance::sendNetPacket(u8* data, int length)
{
    return net->SendPacket(data, length, instanceId);
}

int MelonInstance::receiveNetPacket(u8* data)
{
    return net->RecvPacket(data, instanceId);
}

Frame* MelonInstance::getPresentationFrame(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline, int64_t vsyncTimeNs, int64_t dueToleranceNs, int64_t upcomingFrameDueNs)
{
    return frameQueue.getPresentFrame(deadline, vsyncTimeNs, dueToleranceNs, upcomingFrameDueNs);
}

void MelonInstance::updateConfiguration(std::shared_ptr<EmulatorConfiguration> newConfiguration)
{
    if (nds)
    {
        nds->SPU.SetInterpolation(static_cast<AudioInterpolation>(newConfiguration->audioSettings.audioInterpolation));
        nds->SPU.SetDegrade10Bit(static_cast<AudioBitDepth>(newConfiguration->audioSettings.audioBitrate));
    }

    {
        std::lock_guard lock(rewindSettingsMutex);
        pendingRewindSettings = RewindSettings { newConfiguration->rewindEnabled != 0, newConfiguration->rewindLengthSeconds, newConfiguration->rewindCaptureSpacingSeconds };
        hasPendingRewindSettings.store(true, std::memory_order_release);
    }

    luaDrawingsInScreenshots.store(newConfiguration->luaDrawingsInScreenshots, std::memory_order_relaxed);
    currentConfiguration = newConfiguration;
    isRenderConfigurationDirty = true;
}

void MelonInstance::requestNdsSaveWrite(const u8* saveData, u32 saveLength, u32 writeOffset, u32 writeLength)
{
    if (ndsSave)
        ndsSave->RequestFlush(saveData, saveLength, writeOffset, writeLength);
}

void MelonInstance::requestGbaSaveWrite(const u8* saveData, u32 saveLength, u32 writeOffset, u32 writeLength)
{
    if (gbaSave)
        gbaSave->RequestFlush(saveData, saveLength, writeOffset, writeLength);
}

void MelonInstance::requestFirmwareSaveWrite(const u8* saveData, u32 saveLength, u32 writeOffset, u32 writeLength)
{
    if (firmwareSave)
        firmwareSave->RequestFlush(saveData, saveLength, writeOffset, writeLength);
}

bool MelonInstance::saveState(Savestate* state)
{
    if (!retroAchievementsManager->DoSavestate(state))
        return false;

    return nds->DoSavestate(state);
}

bool MelonInstance::loadState(Savestate* state)
{
    if (!loadStateWithBackup(state))
        return false;

    // Loads happen on other threads, so the scripts hear about it before the next frame
    luaEngine.notifyStateLoaded();
    return true;
}

bool MelonInstance::restoreState(Savestate* state)
{
    if (!retroAchievementsManager->DoSavestate(state))
        return false;

    if (!nds->DoSavestate(state))
        return false;

    setBatteryLevels();
    setDateTime();
    return true;
}

bool MelonInstance::loadStateWithBackup(Savestate* state)
{
    // A bad header (wrong magic, version or length, as in a truncated file) is found when the state is opened, before anything is loaded
    if (state->Error)
        return false;

    std::unique_ptr<Savestate> backup = std::make_unique<Savestate>(Savestate::DEFAULT_SIZE);
    if (backup->Error)
    {
        Platform::Log(Platform::LogLevel::Error, "Failed to allocate memory for state backup\n");
        return false;
    }

    if (!saveState(backup.get()) || backup->Error)
    {
        Platform::Log(Platform::LogLevel::Error, "Failed to back up state, aborting load\n");
        return false;
    }

    // The core doesn't stop at the first error, so a load that fails partway leaves the emulator half-loaded
    if (!restoreState(state) || state->Error)
    {
        // The backup was saved (and finished), so it's read back through a state that loads from its buffer
        Savestate backupState(backup->Buffer(), backup->Length(), false);
        if (backupState.Error || !restoreState(&backupState) || backupState.Error)
            Platform::Log(Platform::LogLevel::Error, "Failed to load backup state\n");
        else
            Platform::Log(Platform::LogLevel::Info, "Backup state loaded\n");

        return false;
    }

    return true;
}

bool MelonInstance::saveLuaState(std::vector<u8>& data)
{
    // Like a player's save. The buffer grows as needed, and only the part the state uses is kept
    Savestate state;
    if (state.Error)
        return false;

    if (!saveState(&state) || state.Error)
    {
        Platform::Log(Platform::LogLevel::Error, "Failed to create a savestate for a Lua script\n");
        return false;
    }

    const u8* buffer = static_cast<const u8*>(state.Buffer());
    data.assign(buffer, buffer + state.Length());
    return true;
}

bool MelonInstance::loadLuaState(const std::vector<u8>& data)
{
    // Like a player's load: it goes back to the backup if it fails. It runs on the emulator thread between frames, the same place as a
    // rewind save, so the renderers resync the way they do after any load (the core resets the VRAM caches and restarts the software
    // renderer's thread in DoSavestate)
    // Reading never writes to the buffer
    Savestate state(const_cast<u8*>(data.data()), (u32) data.size(), false);
    if (!loadStateWithBackup(&state))
    {
        Platform::Log(Platform::LogLevel::Error, "Failed to load a Lua script's state\n");
        return false;
    }

    // The scripts are told by LuaEngine, which asked for the load
    return true;
}

RewindWindow MelonInstance::getRewindWindow()
{
    return RewindWindow {
        .currentFrame = frame,
        .rewindStates = rewindManager.GetRewindWindow(),
    };
}

bool MelonInstance::loadRewindState(RewindSaveState rewindSaveState)
{
    Savestate* savestate = new Savestate(rewindSaveState.buffer, rewindSaveState.bufferContentSize, false);
    if (savestate->Error)
    {
        delete savestate;
        return false;
    }

    bool result = loadState(savestate);
    if (result)
    {
        frame = rewindSaveState.frame;
        rewindManager.OnRewindFromState(rewindSaveState);
    }

    delete savestate;

    return result;
}

void MelonInstance::setupAchievements(
    std::list<RetroAchievements::RAAchievement> achievements,
    std::list<RetroAchievements::RALeaderboard> leaderboards,
    std::optional<std::string> richPresenceScript
)
{
    if (instanceId == 0)
    {
        retroAchievementsManager->LoadAchievements(achievements);
        retroAchievementsManager->LoadLeaderboards(leaderboards);
        if (richPresenceScript)
            retroAchievementsManager->SetupRichPresence(*richPresenceScript);
    }
}

void MelonInstance::unloadRetroAchievementsData()
{
    retroAchievementsManager->UnloadEverything();
}

std::string MelonInstance::getRichPresenceStatus()
{
    if (instanceId == 0 && retroAchievementsManager)
        return retroAchievementsManager->GetRichPresenceStatus();
    else
        return "";
}

std::vector<RetroAchievements::RARuntimeAchievement> MelonInstance::getRuntimeAchievements()
{
    if (instanceId == 0 && retroAchievementsManager)
        return retroAchievementsManager->GetRuntimeAchievements();
    else
        return { };
}

void MelonInstance::updateRenderer()
{
    Renderer newRenderer = currentConfiguration->renderer;

    // A renderer that failed is only tried again once something else was asked for in between
    if (failedRenderer && *failedRenderer != newRenderer)
        failedRenderer.reset();

    if (newRenderer != currentRenderer && !failedRenderer)
    {
        std::unique_ptr<Renderer3D> renderer;
        switch (newRenderer)
        {
            case Renderer::Software:
                renderer = std::make_unique<SoftRenderer>();
                break;
            case Renderer::OpenGl:
                renderer = GLRenderer::New();
                break;
            case Renderer::Compute:
                renderer = ComputeRenderer::New();
                break;
            default: __builtin_unreachable();
        }
        // New() returns nullptr if the GL setup failed, and the core puts a software renderer in its place. Track that, so nothing below
        // casts it to the renderer that was asked for, and remember it so other settings changes don't run the failing New() again
        if (!renderer && newRenderer != Renderer::Software)
        {
            Platform::Log(Platform::LogLevel::Error, "Failed to create the %s renderer, using the software renderer\n", newRenderer == Renderer::OpenGl ? "OpenGL" : "Compute");
            currentRenderer = Renderer::Software;
            failedRenderer = newRenderer;
        }
        else
        {
            currentRenderer = newRenderer;
        }
        nds->GPU.SetRenderer3D(std::move(renderer));
    }

    switch (currentRenderer)
    {
        case Renderer::Software:
        {
            // The settings are only software ones if that's the renderer that was asked for, not the fallback
            if (newRenderer == Renderer::Software)
            {
                auto softwareRenderSettings = static_cast<SoftwareRenderSettings&>(*currentConfiguration->renderSettings);
                static_cast<SoftRenderer&>(nds->GPU.GetRenderer3D()).SetThreaded(softwareRenderSettings.threadedRendering, nds->GPU);
            }
            break;
        }
        case Renderer::OpenGl:
        {
            auto glRenderSettings = static_cast<OpenGlRenderSettings&>(*currentConfiguration->renderSettings);
            auto& glRenderer = static_cast<GLRenderer&>(nds->GPU.GetRenderer3D());
            // Every time, since the renderer may be a new one at 256. Set first so that a new renderer's targets are only sized once
            glRenderer.SetViewWidth(wideViewWidth);
            glRenderer.SetRenderSettings(glRenderSettings.betterPolygons, glRenderSettings.scale);
            break;
        }
        case Renderer::Compute:
        {
            auto computeRenderSettings = static_cast<ComputeRenderSettings&>(*currentConfiguration->renderSettings);
            auto& computeRenderer = static_cast<ComputeRenderer&>(nds->GPU.GetRenderer3D());
            // Like OpenGL, every time and first, so that a new renderer's shaders are only compiled once
            computeRenderer.SetViewWidth(wideViewWidth);
            computeRenderer.SetRenderSettings(computeRenderSettings.scale,computeRenderSettings.highResCoordinates);
            break;
        }
        default: __builtin_unreachable();
    }

    bool threaded2d = currentConfiguration->threaded2dRendering;
    bool hiresCapture = currentConfiguration->hiresCapture;
#ifdef MELONADE_DEBUG_TOOLS
    if (debugOverrides.threaded2d >= 0)
        threaded2d = debugOverrides.threaded2d == 1;
    if (debugOverrides.hiresCapture >= 0)
        hiresCapture = debugOverrides.hiresCapture == 1;
#endif
    // Runs between frames, so the 2D thread can start or stop right away
    nds->GPU.SetThreaded2D(threaded2d);
    // Only the OpenGL and compute renderers use it (the core ignores it for software). A change drops the hi-res copies
    nds->GPU.SetHiresCapture(hiresCapture);
}

pid_t MelonInstance::getGpu2DThreadId()
{
    Platform::Thread* thread = nds->GPU.Get2DThread();
    return thread ? Platform::GetThreadTid(thread) : 0;
}

#ifdef MELONADE_DEBUG_TOOLS
void MelonInstance::takeCaptureStats(CaptureStats& stats)
{
    melonDS::Renderer3D& renderer = nds->GPU.GPU3D.GetCurrentRenderer();
    stats.maps = renderer.CaptureMapCount;
    stats.waitNs = renderer.CaptureMapWaitNs;
    stats.spinNs = renderer.CaptureSpinNs;
    stats.spanNs = renderer.CaptureSpanNs;
    renderer.CaptureMapCount = 0;
    renderer.CaptureMapWaitNs = 0;
    renderer.CaptureSpinNs = 0;
    renderer.CaptureSpanNs = 0;
}
#endif

void MelonInstance::setBatteryLevels()
{
    if (consoleType == 1)
    {
        auto dsi = static_cast<DSi*>(nds);
        dsi->I2C.GetBPTWL()->SetBatteryLevel(DSi_BPTWL::batteryLevel_Full);
        dsi->I2C.GetBPTWL()->SetBatteryCharging(false);
    }
    else
    {
        nds->SPI.GetPowerMan()->SetBatteryLevelOkay(true);
    }
}

void MelonInstance::setDateTime()
{
    std::time_t t = std::time(0);
    std::tm now = {};
    if (!localtime_r(&t, &now))
        return;

    nds->RTC.SetDateTime(now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);
}

void MelonInstance::syncDateTimeToDevice()
{
    // The RTC is driven by the emulator's scheduler, so it stops while emulation is paused and falls behind the device's time. Only ever move
    // it forward: some games (like Pokemon) treat the clock going backward as tampering
    if (!nds || !currentConfiguration->syncRtcToDevice)
        return;

    int year, month, day, hour, minute, second;
    nds->RTC.GetDateTime(year, month, day, hour, minute, second);

    // Compare wall-clock readings (the local time setDateTime() writes) with timegm so that DST plays no part. Otherwise the clock could
    // still go backward as the game sees it during the DST fall-back hour
    std::tm dsTime = {};
    dsTime.tm_year = year - 1900;
    dsTime.tm_mon = month - 1;
    dsTime.tm_mday = day;
    dsTime.tm_hour = hour;
    dsTime.tm_min = minute;
    dsTime.tm_sec = second;
    std::time_t dsWallClock = timegm(&dsTime);

    std::time_t t = std::time(0);
    std::tm deviceTime = {};
    if (!localtime_r(&t, &deviceTime))
        return;
    std::time_t deviceWallClock = timegm(&deviceTime);

    if (dsWallClock == (std::time_t) -1 || deviceWallClock == (std::time_t) -1)
        return;

    // Ignore small differences so that a quick pause doesn't touch the clock
    const std::time_t kMinDriftSeconds = 5;
    if (deviceWallClock - dsWallClock >= kMinDriftSeconds)
        setDateTime();
}

bool MelonInstance::runLuaFrame()
{
    bool hasFrameRun = false;
    if (luaEngine.isActive())
    {
        isLuaInputActive = true;

        u32 touch = userTouch.load();
        LuaUserInput userInput = {
            .held = (~inputMask.load() & LuaKey::BUTTONS) | ((touch & kUserTouchDown) ? LuaKey::TOUCH : 0),
            .touchX = (u16) ((touch >> 16) & 0x7FFF),
            .touchY = (u16) (touch & 0xFFFF),
        };
        LuaInputOverride inputOverride = luaEngine.runFrame(frame, userInput);
        hasFrameRun = true;

        // A script may have been switched off during the call. If none is left, the player's input is restored below
        if (luaEngine.isActive())
        {
            u32 held = (userInput.held & ~inputOverride.release) | inputOverride.press;
            nds->SetKeyMask(~held & LuaKey::BUTTONS);
            if (held & LuaKey::TOUCH)
            {
                if (inputOverride.hasTouchPosition)
                    nds->TouchScreen(inputOverride.touchX, inputOverride.touchY);
                else
                    nds->TouchScreen(userInput.touchX, userInput.touchY);
            }
            else
            {
                nds->ReleaseScreen();
            }
            return true;
        }
    }

    // No script left, so the UI thread applies the input again. Its last changes may have been skipped while the flag was set
    isLuaInputActive = false;
    nds->SetKeyMask(inputMask.load());
    u32 touch = userTouch.load();
    if (touch & kUserTouchDown)
        nds->TouchScreen((touch >> 16) & 0x7FFF, touch & 0xFFFF);
    else
        nds->ReleaseScreen();
    return hasFrameRun;
}

void MelonInstance::saveRewindState(RewindSaveState* rewindSaveState)
{
    Savestate* savestate = new Savestate(rewindSaveState->buffer, rewindSaveState->bufferSize, true);
    if (saveState(savestate))
    {
        rewindSaveState->bufferContentSize = savestate->Length();
        // The GL renderers' screenshot is still on its way (see finishRewindScreenshot())
        if (screenshotRenderer->isScreenshotReadbackPending())
            pendingRewindScreenshotFrame = rewindSaveState->frame;
        else
            memcpy(rewindSaveState->screenshot, screenshotRenderer->getScreenshot(), rewindSaveState->screenshotSize);
    }

    delete savestate;
}

}