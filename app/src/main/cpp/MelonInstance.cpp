#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <chrono>
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
// Shadow2D::MaxSharpAffineScale for the "Sharper rotated 2D" setting, and Shadow2DRecorder::AffineDebug for debug.melonade.gpu2daffine
#include "GPU_Shadow2D.h"
#include "FrontendContext.h"
#include "MelonInstance.h"
#include "NDS.h"
#include "NDSCart.h"
#include "net/Net_Slirp.h"
#include "Platform.h"
#include "PlatformAndroid.h"
#include "SDCardArgsBuilder.h"
#include "debug/DebugLog.h"
#ifdef _WIN32
// MinGW has neither localtime_r nor timegm (setDateTime, syncDateTimeToDevice)
static std::tm* localtime_r(const std::time_t* time, std::tm* result)
{
    return localtime_s(result, time) == 0 ? result : nullptr;
}
#define timegm _mkgmtime
#endif
#ifdef MELONADE_DEBUG_TOOLS
#include "MelonLog.h"
#include "debug/DebugProperties.h"
#endif

using namespace std;
using namespace melonDS;
using namespace melonDS::Platform;

namespace MelonDSAndroid
{

const int REWIND_BUFFER_SIZE = 1024 * 1024 * 20; // Use 20MB per savestate
const int REWIND_SCREENSHOT_SIZE = 256 * 384 * 4;
// GPU::OutputCap: the frames a new need from the frontend has to hold for before the renderer gets it (MelonInstance::runFrame)
const int OUTPUT_NEED_STABLE_FRAMES = 30;

// MelonInstance::currentConfiguration's atomic access: the NDK's libc++ has no std::atomic<std::shared_ptr> yet, and libstdc++ deprecates
// these free functions in C++20
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
static std::shared_ptr<EmulatorConfiguration> loadConfiguration(const std::shared_ptr<EmulatorConfiguration>& configuration)
{
    return std::atomic_load(&configuration);
}

static void storeConfiguration(std::shared_ptr<EmulatorConfiguration>& configuration, const std::shared_ptr<EmulatorConfiguration>& newConfiguration)
{
    std::atomic_store(&configuration, newConfiguration);
}
#pragma GCC diagnostic pop

// The player's touch in one word (MelonInstance::userTouch): a down flag, x and y
struct PackedTouch
{
    static constexpr u32 DOWN = 1u << 31;

    bool isDown;
    u16 x;
    u16 y;

    static u32 pack(u16 x, u16 y) { return DOWN | ((u32) (x & 0x7FFF) << 16) | y; }
    static PackedTouch unpack(u32 word) { return { (word & DOWN) != 0, (u16) ((word >> 16) & 0x7FFF), (u16) (word & 0xFFFF) }; }
};

MelonInstance::MelonInstance(int instanceId, std::shared_ptr<EmulatorConfiguration> configuration, std::unique_ptr<melonDS::NDSArgs> args, std::shared_ptr<Net> net, std::unique_ptr<ScreenshotRenderer> screenshotRenderer, int consoleType) :
    instanceId(instanceId),
    consoleType(consoleType),
    net(net),
    currentConfiguration(configuration),
    screenshotRenderer(std::move(screenshotRenderer)),
    rewindManager(configuration->rewindEnabled, configuration->rewindLengthSeconds, configuration->rewindCaptureSpacingSeconds, REWIND_BUFFER_SIZE, REWIND_SCREENSHOT_SIZE)
{
    // Software renderer is always used during initialisation. Actual renderer will be set of first frame run
    currentRenderer = Renderer::Software;
    isRenderConfigurationDirty = true;
    inputMask = 0xFFF;
    frame = 0;
    luaDrawingsInScreenshots = configuration->luaDrawingsInScreenshots;
    wideViewWidth = configuration->wideViewWidth;
    wideViewMargin = configuration->wideViewMargin;
    requestedWideView = packWideView(wideViewWidth, wideViewMargin);

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
    debugOverrides = DebugProperties::apply(*nds);
#endif

    if (configuration->userInternalFirmwareAndBios)
    {
        std::filesystem::path firmwarePath = frontendContext.internalFilesDir;
        firmwarePath /= "wfcsettings.bin";
        firmwareSave = std::make_unique<SaveManager>(firmwarePath.string());
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
    // Normally done in stop(), but the scripts can't outlive the NDS, which their API (and their __gc metamethods) use
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

    // The CRC of the whole file identifies the dump, but it takes a moment for big ROMs, so it's only worked out for the debug log
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
    // The field read's tables know a few Pokemon carts by their header (and one hack by its ARM9)
    auto loadedCart = nds->NDSCartSlot.GetCart();
    gameState.setCart(loadedCart ? loadedCart->GetROM() : nullptr, loadedCart ? loadedCart->GetROMLength() : 0);

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
    if (nds->ConsoleType == 1 && cart != nullptr && cart->GetHeader().IsDSiWare() && !loadConfiguration(currentConfiguration)->showBootScreen)
    {
        auto dsi = (DSi*) nds;
        DSiSupport::SetupDSiDirectBoot(dsi);
    }
    else if (!loadConfiguration(currentConfiguration)->showBootScreen || nds->NeedsDirectBoot())
    {
        // This seems to be unused, but it's required
        std::string romName;
        nds->SetupDirectBoot(romName);
    }
    nds->Start();

    screenshotRenderer->init();
    luaEngine.start(nds, frontendContext.eventMessenger, this, &gameState);

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
        if (!loadConfiguration(currentConfiguration)->showBootScreen || nds->NeedsDirectBoot())
        {
            // This seems to be unused, but it's required
            std::string romName;
            nds->SetupDirectBoot(romName);
        }
    }

    rewindManager.Reset();
    retroAchievementsManager->Reset();
    gameState.reset();
    nds->Start();
    // The emulator thread is parked, so the scripts are reloaded before the next frame instead
    luaEngine.notifyReset();

    if (DebugLog::isEnabled()) [[unlikely]]
        DebugLog::logGameStart(*nds, "reset", hasRomCrc32, romCrc32);
}

u32 MelonInstance::runFrame(int64_t presentDueTimeNs)
{
    // A width or margin change only needs the existing GL or Compute renderer told, since updateRenderer() applies them to one that's about to be
    // set up again
    u32 requestedView = requestedWideView.load(std::memory_order_relaxed);
    int requestedWidth = (int) (requestedView & 0xFFFF);
    int requestedMargin = (int) (requestedView >> 16);
    if (requestedWidth != wideViewWidth || requestedMargin != wideViewMargin)
    {
        wideViewWidth = requestedWidth;
        wideViewMargin = requestedMargin;
        if (!isRenderConfigurationDirty && currentRenderer == Renderer::OpenGl)
        {
            auto& glRenderer = static_cast<GLRenderer&>(nds->GPU.GetRenderer3D());
            glRenderer.SetViewWidth(wideViewWidth);
            glRenderer.SetViewMargin(wideViewMargin);
        }
        else if (!isRenderConfigurationDirty && currentRenderer == Renderer::Compute)
        {
            auto& computeRenderer = static_cast<ComputeRenderer&>(nds->GPU.GetRenderer3D());
            computeRenderer.SetViewWidth(wideViewWidth);
            computeRenderer.SetViewMargin(wideViewMargin);
        }
    }

    // Cleared first, so a configuration that arrives during updateRenderer() is applied on the next frame
    if (isRenderConfigurationDirty.exchange(false))
        updateRenderer();
    // The GPU 2D shadow's mode and WideMelon's Gen 4 battle modes for this frame: after updateRenderer(), which works out what they depend on,
    // and before the renderers' SetOutputNeed, as the output cap follows them
    applyShadow2DMode();

    if (hasPendingRewindSettings.load(std::memory_order_acquire)) [[unlikely]]
        applyRewindSettings();

    // GPU::OutputCap: the screens' size only counts once it held for a while, so a fold, a rotation or a layout change resizes the frame once
    // Whether the frontend debands holds along with it (GPU::OutputCapDeband lets the cap take a ratio that isn't whole)
    int frontendOutputNeed = MelonDSAndroid::getOutputScaleNeed();
    bool frontendDeband = MelonDSAndroid::getOutputDeband();
    // Each change logs a line (debug), so a frontend that flips between two values shows up as a run of them with no "accepted"
    if (frontendOutputNeed != pendingOutputScaleNeed || frontendDeband != pendingOutputDeband)
    {
        LOG_INFO(LOG_TAG_DEBUG, "OUTCAP: frontend need %d deband %d pending (was %d deband %d, accepted %d deband %d)", frontendOutputNeed,
                 frontendDeband, pendingOutputScaleNeed, pendingOutputDeband, outputScaleNeed, outputDeband);
        pendingOutputScaleNeed = frontendOutputNeed;
        pendingOutputDeband = frontendDeband;
        pendingOutputScaleNeedFrames = 0;
    }
    else if ((pendingOutputScaleNeed != outputScaleNeed || pendingOutputDeband != outputDeband)
             && ++pendingOutputScaleNeedFrames >= OUTPUT_NEED_STABLE_FRAMES)
    {
        LOG_INFO(LOG_TAG_DEBUG, "OUTCAP: frontend need %d deband %d accepted after %d frames (was %d deband %d)", pendingOutputScaleNeed,
                 pendingOutputDeband, pendingOutputScaleNeedFrames, outputScaleNeed, outputDeband);
        outputScaleNeed = pendingOutputScaleNeed;
        outputDeband = pendingOutputDeband;
    }
    // Before the renderers' SetOutputNeed, which re-asks the scale when it changes what the cap may pick
    nds->GPU.OutputCapDeband = outputDeband;

    int screenWidth;
    // The whole frame's height: the top screen's half (192 + 2 * margin rows), the 2 row gap and the bottom screen's 192, at the frame's scale
    int frameHeight;
    // In DS pixels, with the 256 wide bottom screen centered under it between black bars when this is wider
    int topScreenWidth = 256;
    // The WideMelon camera's rows above and below the top screen's 192, in DS pixels (always 0 on Software)
    int topScreenMargin = 0;
    if (currentRenderer == Renderer::OpenGl)
    {
        auto& glRenderer = static_cast<GLRenderer &>(nds->GPU.GetRenderer3D());
        // After updateRenderer(), which sets what the cap depends on; only a change reaches the compositor, and the 3D keeps its scale
        glRenderer.SetOutputNeed(nds->GPU, outputScaleNeed);
        // The compositor's, which the frame is drawn at: below the 3D's while GPU::OutputCap caps it
        int scale = glRenderer.GetOutputScale();
        // The width the compositor draws this frame at, which can lag behind the requested one
        topScreenWidth = glRenderer.GetOutputWidth();
        // Lags like the width, along with the compositor's layout
        topScreenMargin = glRenderer.GetOutputMargin();
        screenWidth = topScreenWidth * scale;
        frameHeight = (192 + 2 * topScreenMargin + 2 + 192) * scale;
    }
    else if (currentRenderer == Renderer::Compute)
    {
        auto& computeRenderer = static_cast<ComputeRenderer&>(nds->GPU.GetRenderer3D());
        computeRenderer.SetOutputNeed(nds->GPU, outputScaleNeed);
        // The renderer's, not the setting's, as it drops to the largest scale that fits the GPU's limits, as OpenGL does, and the compositor's
        // like OpenGL's, which is below the 3D's while GPU::OutputCap caps it
        int scale = computeRenderer.GetOutputScale();
        // Same as OpenGL, the compositor's width can lag behind the requested one
        topScreenWidth = computeRenderer.GetOutputWidth();
        topScreenMargin = computeRenderer.GetOutputMargin();
        screenWidth = topScreenWidth * scale;
        frameHeight = (192 + 2 * topScreenMargin + 2 + 192) * scale;
    }
    else
    {
        screenWidth = 256;
        frameHeight = 192 + 2 + 192;
    }

    Frame* renderFrame = frameQueue.getRenderFrame();

    GpuFenceDisplay currentDisplay = getCurrentGpuFenceDisplay();
    // Delete old render fence
    if (renderFrame->renderFence)
    {
        destroyGpuFence(currentDisplay, renderFrame->renderFence);
        renderFrame->renderFence = 0;
    }

    // Ensure presentation is finished
    if (renderFrame->presentFence)
    {
        waitGpuFence(currentDisplay, renderFrame->presentFence);
    }

    // Validate frame after ensuring that the frame has finished presenting
    frameQueue.validateRenderFrame(renderFrame, screenWidth, frameHeight);
    renderFrame->topScreenWidth = topScreenWidth;
    renderFrame->topScreenMargin = topScreenMargin;
    // A depth of field, haze, 3D-only glow, shading or sun shadows (LuaEngine::wantsDepth, or the desktop's --dof) need the top half's depth,
    // which only OpenGL hands out. The scripts' last word on it is the previous frame's, so it starts a frame late
    bool isDepthWanted = currentRenderer == Renderer::OpenGl && (isFrontendDepthWanted.load(std::memory_order_relaxed) || luaEngine.wantsDepth());
    // The top half's rows of the frame's (192 + 2 * margin + 2 + 192) * scale
    int depthRows = isDepthWanted ? frameHeight / (386 + 2 * topScreenMargin) * (192 + 2 * topScreenMargin) : 0;
    frameQueue.validateDepthTexture(renderFrame, depthRows);

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
    // The compositor buffer the frame's texture is given to
    int outputBuffer = nds->GPU.FrontBuffer ? 0 : 1;
    if (isRendererAccelerated)
    {
        nds->GPU.GetRenderer3D().SetOutputTexture(outputBuffer, renderFrame->frameTexture);
        // 0 when none is wanted, which tells the renderer to keep no depth
        nds->GPU.GetRenderer3D().SetOutputDepthTexture(outputBuffer, renderFrame->depthTexture);
        // The characters' sun shadows OpenGL draws into the depth's flags, the newest script's or else the frontend's; the scripts' last word on
        // them is the previous frame's, as for the depth
        SunShadows sun = luaEngine.sunShadows();
        nds->GPU.GetRenderer3D().SetSunShadows(sun.On ? sun : frontendSun);
        // And the textures it leaves out, the same way
        const std::vector<u32>& hidden = luaEngine.hiddenTextures().empty() ? frontendHiddenTextures : luaEngine.hiddenTextures();
        nds->GPU.GetRenderer3D().SetHiddenTextures(hidden.data(), (u32) hidden.size());
        // And the ones it flags as lights
        const std::vector<u32>& lights = luaEngine.lightTextures().empty() ? frontendLightTextures : luaEngine.lightTextures();
        nds->GPU.GetRenderer3D().SetLightTextures(lights.data(), (u32) lights.size());
    }

    bool hasLuaFrameRun = false;
    if (luaEngine.isActive() || isLuaInputActive) [[unlikely]]
        hasLuaFrameRun = runLuaFrame();

    // GPU::Comp1x: the compositor only draws a screen at 1x when every surface can show it (the render thread's last word on it)
    nds->GPU.Comp1xFrontend = MelonDSAndroid::areHalvesAt1xSupported();
    u32 nLines = nds->RunFrame();
    retroAchievementsManager->FrameUpdate();
    // Returns at once for a game the field read doesn't know, or one nothing asked about
    gameState.update(nds);

    if (!isRendererAccelerated)
    {
        int frontbuf = nds->GPU.FrontBuffer;
        if (nds->GPU.Framebuffer[frontbuf][0] && nds->GPU.Framebuffer[frontbuf][1])
        {
            glBindTexture(GL_TEXTURE_2D, renderFrame->frameTexture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, nds->GPU.Framebuffer[frontbuf][0].get());
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 192 + 2, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, nds->GPU.Framebuffer[frontbuf][1].get());
            glBindTexture(GL_TEXTURE_2D, 0);
            renderFrame->halvesAt1x = 0;
            renderFrame->topWingLines = 0;
            renderFrame->hasDepth = false;
            renderFrame->depthCameraValid = false;
        }
    }
    else
    {
        // The emulator already rendered into the texture, which was set up above. What the compositor drew there, if it drew this frame (otherwise
        // the texture and its layout stay as they were)
        OutputLayout outputLayout;
        if (nds->GPU.GetRenderer3D().TakeOutputLayout(outputBuffer, outputLayout))
        {
            renderFrame->halvesAt1x = outputLayout.HalvesAt1x;
            renderFrame->scaleAt1x = outputLayout.Scale;
            renderFrame->halfScaleAt1x = outputLayout.HalfScale;
            renderFrame->topWingLines = outputLayout.TopWingLines;
            renderFrame->hasDepth = outputLayout.HasDepth && renderFrame->depthTexture != 0;
            renderFrame->depthProjection = outputLayout.Camera.Proj;
            renderFrame->depthViewport = outputLayout.Camera.Viewport;
            renderFrame->depthCameraValid = renderFrame->hasDepth && outputLayout.Camera.Valid;
        }
    }

    // on_frame_end(), which also draws the frame's Lua drawings (endFrame() leaves a stopping script's drawings out, so the overlay is empty once
    // none runs)
    if (hasLuaFrameRun) [[unlikely]]
        luaEngine.endFrame();

    // Lua drawings, before the fence so they're presented with this frame (with no script running this only clears the frame's old drawings)
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
        renderFrame->renderFence = createGpuFence(currentDisplay);
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

        // A rewind capture doesn't wait for the GPU (its screenshot is copied in a frame or so later), but a player's screenshot is waited for
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
        LOG_INFO_QUEUED(LOG_TAG_DEBUG, "REWIND frame=%d capture_ms=%.3f size=%u", frame, captureMs, nextRewindState->bufferContentSize);
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
    // No state waits for it if its save failed, and then the screenshot is dropped (resets and rewinds can't get in the way, since they only happen
    // while the emulator thread is parked, and it finishes this first)
    RewindSaveState* state = rewindManager.FindRewindSaveState(pendingRewindScreenshotFrame);
    if (screenshotRenderer->finishScreenshotReadback(state ? state->screenshot : nullptr, wait))
        pendingRewindScreenshotFrame = -1;
}

void MelonInstance::stop()
{
    luaEngine.stop();

    // The RetroAchievements manager is kept until the instance is destroyed, since other threads use it under presentationMutex, which isn't held
    // here
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
    userTouch.store(PackedTouch::pack(x, y));
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
    storeConfiguration(currentConfiguration, newConfiguration);
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

void MelonInstance::syncNdsSaveFromState(const u8* saveData, u32 saveLength)
{
    if (ndsSave)
        ndsSave->SyncFromState(saveData, saveLength);
}

void MelonInstance::syncGbaSaveFromState(const u8* saveData, u32 saveLength)
{
    if (gbaSave)
        gbaSave->SyncFromState(saveData, saveLength);
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

    gameState.reset();
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
    // Like a player's save, with a buffer that grows as needed, keeping only the part the state uses
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
    // Like a player's load, backed up and restored on failure, and run between frames on the emulator thread like a rewind save, so the renderers
    // resync as after any load (the const_cast is safe: reading never writes to the buffer)
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
    // One configuration for all of this, as the renderer it asks for decides the type of its render settings
    std::shared_ptr<EmulatorConfiguration> configuration = loadConfiguration(currentConfiguration);
    Renderer newRenderer = configuration->renderer;

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
        // New() returns nullptr if the GL setup failed and the core puts a software renderer in its place, so track that: nothing below may cast it
        // to the requested renderer, and other settings changes mustn't run the failing New() again
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

    // The internal resolution the OpenGL and compute renderers are asked for, for the GPU 2D shadow below
    int renderScale = 1;
    switch (currentRenderer)
    {
        case Renderer::Software:
        {
            // The settings are only software ones if that's the renderer that was asked for, not the fallback
            if (newRenderer == Renderer::Software)
            {
                auto softwareRenderSettings = static_cast<SoftwareRenderSettings&>(*configuration->renderSettings);
                static_cast<SoftRenderer&>(nds->GPU.GetRenderer3D()).SetThreaded(softwareRenderSettings.threadedRendering, nds->GPU);
            }
            break;
        }
        case Renderer::OpenGl:
        {
            auto glRenderSettings = static_cast<OpenGlRenderSettings&>(*configuration->renderSettings);
            auto& glRenderer = static_cast<GLRenderer&>(nds->GPU.GetRenderer3D());
            // Every time, since the renderer may be a new one at 256, and first so a new renderer's targets are only sized once
            glRenderer.SetViewWidth(wideViewWidth);
            glRenderer.SetViewMargin(wideViewMargin);
            glRenderer.SetRenderSettings(glRenderSettings.betterPolygons, glRenderSettings.scale);
            renderScale = glRenderSettings.scale;
            break;
        }
        case Renderer::Compute:
        {
            auto computeRenderSettings = static_cast<ComputeRenderSettings&>(*configuration->renderSettings);
            auto& computeRenderer = static_cast<ComputeRenderer&>(nds->GPU.GetRenderer3D());
            // Like OpenGL, every time and first, so that a new renderer's shaders are only compiled once
            computeRenderer.SetViewWidth(wideViewWidth);
            computeRenderer.SetViewMargin(wideViewMargin);
            computeRenderer.SetRenderSettings(computeRenderSettings.scale,computeRenderSettings.highResCoordinates);
            computeRenderer.SetColorDither(computeRenderSettings.colorDithering);
            renderScale = computeRenderSettings.scale;
            break;
        }
        default: __builtin_unreachable();
    }

    bool threaded2d = configuration->threaded2dRendering;
    bool hiresCapture = configuration->hiresCapture;
    int filter2d = configuration->filter2d ? 1 : 0;
    int edgeMark = configuration->edgeMarking ? 1 : 0;
    // The full fix (both of its bits), and only for the renderer that draws it: OpenGL, not the software fallback after a failed New()
    int glSeams = configuration->glSeams && currentRenderer == Renderer::OpenGl ? 3 : 0;
#ifdef MELONADE_DEBUG_OVERRIDES
    if (debugOverrides.edgeMark >= 0)
        edgeMark = debugOverrides.edgeMark;
    if (debugOverrides.glSeams >= 0)
        glSeams = debugOverrides.glSeams;
    if (debugOverrides.threaded2d >= 0)
        threaded2d = debugOverrides.threaded2d == 1;
    if (debugOverrides.hiresCapture >= 0)
        hiresCapture = debugOverrides.hiresCapture == 1;
    if (debugOverrides.filter2d >= 0)
        filter2d = debugOverrides.filter2d;
#endif
    // Runs between frames, so the 2D thread can start or stop right away
    nds->GPU.SetThreaded2D(threaded2d);
    // Only the OpenGL and compute renderers use it (the core ignores it for software), and a change drops the hi-res copies
    nds->GPU.SetHiresCapture(hiresCapture);
    // Only the compositor reads it, at the next Blit, and it does nothing for software
    nds->GPU.Filter2D = filter2d;
    // Read by the OpenGL and compute renderers at their next frame; software always marks edges
    nds->GPU.GLEdgeMark = edgeMark;
    // Read by the OpenGL renderer as each frame starts (a change draws the frame again); software and compute ignore it
    nds->GPU.GLSeams = glSeams;
    // Every renderer gets it through the polygons' depth, from the next scene the game builds (latched at the polygon RAM swap)
    nds->GPU.PreciseDepth = configuration->preciseDepth;

    // Only the OpenGL and compute renderers can draw the in-between scenes or the GPU 2D shadow, so software doesn't pay for them
    bool isGpuRenderer = currentRenderer == Renderer::OpenGl || currentRenderer == Renderer::Compute;

    // "Sharper rotated 2D": the GPU 2D shadow in mode 1, the lines with a rotated or scaled BG (GPU_Shadow2D.h). Between frames, which is where
    // a savestate load resets it too. Only from 2x, where those lines gain something, up to the scale the compositor still draws them sharp at
    // (Shadow2D::MaxSharpAffineScale; above it they'd cost the shadow for native pixels). A resolution change comes through here, so it turns on
    // and off with it
    isShadow2DOverridden = false;
    shadow2DWide = 0;
    bool isSharpAffineCapLifted = false;
#ifdef MELONADE_DEBUG_OVERRIDES
    // debug.melonade.gpu2d: DebugProperties::apply already set the mode (with its log and check) for this game, and it stays through renderer
    // and setting changes
    isShadow2DOverridden = debugOverrides.gpu2d >= 0;
    shadow2DWide = debugOverrides.gpu2dWide;
    // gpu2daffine -1 lifts the compositor's cap, so the setting runs above it too
    isSharpAffineCapLifted = debugOverrides.gpu2dAffine < 0;
#endif
    bool isShadow2DScale = renderScale >= 2 && (renderScale <= Shadow2D::MaxSharpAffineScale || isSharpAffineCapLifted);
    // Smooth 2D edges wins when both are on (an old prefs file or a backup), as the lines the shadow draws would lose its smoothing. The
    // settings screen turns one off when the other's turned on
    bool wantsShadow2D = configuration->sharperRotated2d && filter2d == 0 && isGpuRenderer && isShadow2DScale;
    shadow2DSettingMode = wantsShadow2D ? 1 : 0;
    // WideMelon's Gen 4 battles (GameState::applyWideBattle) are drawn by the shadow too, at any scale: without the setting, in mode 3, which
    // only draws the lines with a layer past the edge, and only around battles (applyShadow2DMode). Smooth 2D edges wins here as well
    canShadow2DWideBattles = filter2d == 0 && isGpuRenderer && gameState.hasWideBattles();

    bool isHalfCameraOverridden = false;
#ifdef MELONADE_DEBUG_OVERRIDES
    // DebugProperties::apply already set the mode for this game, and it stays through renderer changes
    isHalfCameraOverridden = debugOverrides.halfCamera >= 0;
#endif
    // Also between frames
    if (!isHalfCameraOverridden)
        nds->GPU.GPU3D.SetHalfCameraSmoothing(configuration->framePacingSmooth30Fps && isGpuRenderer);
}

void MelonInstance::applyShadow2DMode()
{
    // From the last frame's scene: polled for a Gen 4 game while the top screen is wide, nothing for any other game
#ifdef __ANDROID__
    bool isWideBattle = gameState.updateWideBattle(wideViewWidth > 256);
#else
    // The desktop's --no-wide-battles reads every top screen as 256 wide here, which leaves the battles at the edge
    bool isWideBattle = gameState.updateWideBattle(wideViewWidth > 256 && debugOverrides.wideBattles != 0);
#endif
    if (!isShadow2DOverridden)
    {
        // Mode 3 only around a battle, with the top screen wide: the rest of the game, and a Gen 4 game at 256, runs as without the shadow
        // (no recorder, the output cap as before). gpu2dwide rides along: in mode 3 every layer then draws past the edge in a battle, over the
        // battle's own modes (Shadow2DRecorder::Wide wins), as it does over the setting's mode 1
        int mode = shadow2DSettingMode ? shadow2DSettingMode : (canShadow2DWideBattles && isWideBattle ? 3 : 0);
        int currentMode = nds->GPU.Shadow2D ? nds->GPU.Shadow2D->Mode : 0;
        // Only on a change, as SetShadow2D resets the recorder, which leaves that frame's 2D on the CPU (a battle's first frame, which is
        // still the battle-start transition)
        if (mode != currentMode)
        {
            nds->GPU.SetShadow2D(mode, false, false, shadow2DWide);
#ifdef MELONADE_DEBUG_OVERRIDES
            if (nds->GPU.Shadow2D)
                nds->GPU.Shadow2D->AffineDebug = debugOverrides.gpu2dAffine;
#endif
        }
    }
    // After the mode, which drops them while the shadow is off; nothing outside a battle
    gameState.applyWideBattle(nds);
}

void MelonInstance::logDebugFrameState(bool isFastForward, float fps, int64_t lastFrameWorkNs)
{
    DebugLog::onFrame(*nds, isFastForward, fps, lastFrameWorkNs);
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
    // The RTC stops while emulation is paused and falls behind the device's time, so only ever move it forward: some games (like Pokemon) treat the
    // clock going backward as tampering
    if (!nds || !loadConfiguration(currentConfiguration)->syncRtcToDevice)
        return;

    int year, month, day, hour, minute, second;
    nds->RTC.GetDateTime(year, month, day, hour, minute, second);

    // Compare wall-clock readings (the local time setDateTime() writes) with timegm so DST plays no part, or the clock could still go backward during
    // the DST fall-back hour
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
    const std::time_t MIN_DRIFT_SECONDS = 5;
    if (deviceWallClock - dsWallClock >= MIN_DRIFT_SECONDS)
        setDateTime();
}

bool MelonInstance::runLuaFrame()
{
    bool hasFrameRun = false;
    if (luaEngine.isActive())
    {
        isLuaInputActive = true;

        PackedTouch touch = PackedTouch::unpack(userTouch.load());
        LuaUserInput userInput = {
            .held = (~inputMask.load() & LuaKey::BUTTONS) | (touch.isDown ? LuaKey::TOUCH : 0),
            .touchX = touch.x,
            .touchY = touch.y,
        };
        LuaInputOverride inputOverride = luaEngine.runFrame(frame, userInput);
        hasFrameRun = true;

        // A script may have been switched off during the call, and if none is left the player's input is restored below
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

    // No script left, so the UI thread applies the input again (its last changes may have been skipped while the flag was set)
    isLuaInputActive = false;
    nds->SetKeyMask(inputMask.load());
    PackedTouch touch = PackedTouch::unpack(userTouch.load());
    if (touch.isDown)
        nds->TouchScreen(touch.x, touch.y);
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