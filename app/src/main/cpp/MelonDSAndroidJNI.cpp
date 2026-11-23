#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <jni.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <pthread.h>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <MelonDS.h>
#include <MelonDSAudio.h>
#include <RomGbaSlotConfig.h>
#include <android/asset_manager_jni.h>
#include "UriFileHandler.h"
#include "JniEnvHandler.h"
#include "AndroidMelonEventMessenger.h"
#include "MelonDSAndroidInterface.h"
#include "MelonDSAndroidConfiguration.h"
#include "MelonDSAndroidCameraHandler.h"
#include "RetroAchievementsMapper.h"
#include "performancehint/ThreadSafePerformanceHintSession.h"
#include "performancehint/PerformanceHintManagerFactory.h"
#include "perf/FrameSampleRing.h"
#include "debug/AudioDiagnostics.h"
#include "debug/DebugLog.h"
#include "renderer/FramePacing.h"
#include "MelonLog.h"
#include "MonotonicClock.h"

#include "Platform.h"
#ifdef MELONADE_DEBUG_TOOLS
#include <cerrno>
#include <cstring>
#include <sched.h>
#include <sys/system_properties.h>
#include "debug/DebugProperties.h"
#endif
#include "perf/PgoDump.h"

enum GbaSlotType {
    NONE = 0,
    GBA_ROM = 1,
    RUMBLE_PAK = 2,
    MEMORY_EXPANSION = 3,
    MOTION_PAK_HOMEBREW = 4,
    MOTION_PAK_RETAIL = 5,
};

void* emulate(void*);
MelonDSAndroid::RomGbaSlotConfig* buildGbaSlotConfig(GbaSlotType slotType, const char* romPath, const char* savePath);

pthread_t emuThread;
pthread_mutex_t emuThreadMutex;
pthread_cond_t emuThreadCond;

// The emulation state is written from the UI side and read by the emulator and render threads (the perf logger doesn't take emuThreadMutex), so it's
// atomic; paused is still only changed under emuThreadMutex for the pause wait, and stop only accessed under it while the emulator thread runs
std::atomic_bool started = false;
bool stop;
std::atomic_bool paused = false;
std::atomic_bool isThreadReallyPaused = false;
int observedFrames = 0;
float fps = 0;
std::atomic_int targetFps = 0;
std::atomic<float> fastForwardSpeedMultiplier = 0.0f;
std::atomic_bool limitFps = true;
std::atomic_bool isFastForwardEnabled = false;

jobject globalCameraManager;
MelonDSAndroidCameraHandler* androidCameraHandler;

static const int64_t FRAME_DURATION_60FPS_NS = 1000000000LL / EMULATOR_TARGET_FPS;
static const int64_t FRAME_DURATION_1000FPS_NS = 1000000; // 1ms. Used as frame time when fast-forward is enabled
ThreadSafePerformanceHintSession* performanceHintSession = nullptr;

// Performance logging: one sample per frame, with room for over a second at 16x between drains; the field order and flags must match
// MelonEmulator.PERF_FRAME_* (the count is checked at startup)
static constexpr std::size_t PERF_FRAME_SAMPLE_FIELDS = 10;
static const int64_t PERF_FRAME_FLAG_FAST_FORWARD = 1;
static const int64_t PERF_FRAME_FLAG_AFTER_PAUSE = 2;
// The frame's start was locked to the display's vsync grid (locked frames also store their refresh divisor N in the flags)
static const int64_t PERF_FRAME_FLAG_PACING_LOCKED = 4;
// The audio buffer was full after the frame, so some of its samples were dropped
static const int64_t PERF_FRAME_FLAG_AUDIO_BUFFER_FULL = 8;
static const int PERF_FRAME_PACING_DIVISOR_SHIFT = 8;
// Returned by getPerformanceEmulationStateFlags(); must match MelonEmulator.PERF_STATE_FLAG_*
static const jint PERF_STATE_FLAG_PAUSED = 1;
static const jint PERF_STATE_FLAG_DS_SLEEP = 2;
static const jint PERF_STATE_FLAG_FAST_FORWARD = 4;
static FrameSampleRing<16384, PERF_FRAME_SAMPLE_FIELDS> perfFrameSamples;
static std::atomic_bool isPerfCaptureEnabled = false;

// Frame pacing: vsync timestamps are published by the UI thread and used by the emulator thread to schedule frames and by the render thread to pick
// which frame to present
static DisplayVsyncTracker displayVsyncTracker;
static std::atomic_bool isFramePacingEnabled = false;
static std::atomic_bool isFramePacingBufferEnabled = false;
// Due vsync of the frame the emulator thread is about to run (or is running), or 0 if it isn't paced, so the render thread knows whether to wait for
// a frame that hasn't been pushed yet
static std::atomic<int64_t> upcomingFrameDueNs = 0;
// Paced frames are due a fixed offset after a vsync, and this fraction of a refresh (half) of tolerance absorbs that offset and any phase error
static const int64_t PRESENT_DUE_TOLERANCE_REFRESH_DENOMINATOR = 2;
// PresentFrameWrapper.depthCamera's layout (PresentFrameWrapper.kt's DEPTH_CAMERA_*): the projection's 16 floats, then the viewport's 4
static const jsize DEPTH_CAMERA_PROJECTION = 0;
static const jsize DEPTH_CAMERA_VIEWPORT = 16;
static const jsize DEPTH_CAMERA_SIZE = 20;

/**
 * FrameRenderCallback.renderFrame(), looked up on the first call (always from Java, so FindClass() uses the app's class loader) through a global
 * reference that's never released, which keeps the method ID valid.
 */
static jmethodID getRenderFrameMethodId(JNIEnv* env)
{
    static const jmethodID renderFrameMethodId = [env]() {
        jclass localCallbackClass = env->FindClass("me/magnum/melonds/ui/emulator/render/FrameRenderCallback");
        jclass callbackClass = (jclass) env->NewGlobalRef(localCallbackClass);
        env->DeleteLocalRef(localCallbackClass);
        return env->GetMethodID(callbackClass, "renderFrame", "(ZIIIIIIIIIIIIIIIIZFJ)Z");
    }();
    return renderFrameMethodId;
}

extern "C"
{
JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_setupEmulator(JNIEnv* env, jobject thiz, jobject emulatorConfiguration, jobject cameraManager, jobject screenshotBuffer)
{
    // Before anything logs from the emulator, render or audio threads
    DebugLog::startLogQueue();

    MelonDSAndroid::EmulatorConfiguration finalEmulatorConfiguration = MelonDSAndroidConfiguration::buildEmulatorConfiguration(env, emulatorConfiguration);
    fastForwardSpeedMultiplier = finalEmulatorConfiguration.fastForwardSpeedMultiplier;
    isFramePacingEnabled = finalEmulatorConfiguration.framePacingVsync;
    isFramePacingBufferEnabled = finalEmulatorConfiguration.framePacingBuffer;

    globalCameraManager = env->NewGlobalRef(cameraManager);

    auto androidEventMessenger = std::make_shared<AndroidMelonEventMessenger>();
    androidCameraHandler = new MelonDSAndroidCameraHandler(jniEnvHandler, globalCameraManager);
    u32* screenshotBufferPointer = (u32*) env->GetDirectBufferAddress(screenshotBuffer);

    MelonDSAndroid::setConfiguration(std::move(finalEmulatorConfiguration));
    // Empty unless this is a PGO generate build; called in every build so this function matches the profile (perf/PgoDump.h)
    PgoDump::onSetup(env, emulatorConfiguration);
    MelonDSAndroid::setup(androidCameraHandler, std::move(androidEventMessenger), screenshotBufferPointer, 0);
    paused = false;
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_setupCheats(JNIEnv* env, jobject thiz, jobjectArray cheats)
{
    jsize cheatCount = env->GetArrayLength(cheats);
    if (cheatCount < 1) {
        MelonDSAndroid::setCodeList(std::list<MelonDSAndroid::Cheat>());
        return;
    }

    jclass cheatClass = env->GetObjectClass(env->GetObjectArrayElement(cheats, 0));
    jfieldID codeField = env->GetFieldID(cheatClass, "code", "Ljava/lang/String;");

    std::list<MelonDSAndroid::Cheat> internalCheats;

    for (int i = 0; i < cheatCount; ++i) {
        jobject cheat = env->GetObjectArrayElement(cheats, i);
        jstring code = (jstring) env->GetObjectField(cheat, codeField);
        const char* codeStringPtr = env->GetStringUTFChars(code, JNI_FALSE);
        std::string codeString = codeStringPtr;
        // Since each part of a cheat code has 8 characters (4 bytes), we can add 1 to the length (to ensure that each part has a matching space separator) and divide by 9
        // (part length + space separator) to calculate the total number of parts in the cheat
        size_t codeLength = (codeString.size() + 1) / 9;

        bool isBad = false;
        std::size_t start = 0;
        std::size_t end = 0;

        MelonDSAndroid::Cheat internalCheat;
        internalCheat.code.reserve(codeLength);

        // Split code string into sections separated by a space
        while ((end = codeString.find(' ', start)) != std::string::npos) {
            if (end != start) {
                char* endPointer;
                std::string sectionString = codeString.substr(start, end - start);
                // Each code section must be 4 bytes (8 hex characters)
                if (sectionString.size() != 8) {
                    isBad = true;
                    break;
                }

                unsigned long section = strtoul(sectionString.c_str(), &endPointer, 16);
                if (*endPointer == 0) {
                    internalCheat.code.push_back((u32) section);
                } else {
                    isBad = true;
                    break;
                }
            }
            start = end + 1;
        }

        if (!isBad && end != start) {
            char* endPointer;
            std::string sectionString = codeString.substr(start, end - start);
            if (sectionString.size() != 8) {
                isBad = true;
            } else {
                unsigned long section = strtoul(sectionString.c_str(), &endPointer, 16);
                internalCheat.code.push_back((u32) section);
            }
        }

        env->ReleaseStringUTFChars(code, codeStringPtr);

        if (isBad) {
            continue;
        }

        internalCheats.push_back(internalCheat);
    }

    MelonDSAndroid::setCodeList(internalCheats);
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_setupAchievements(JNIEnv* env, jobject thiz, jobjectArray achievements, jobjectArray leaderboards, jstring richPresenceScript)
{
    std::list<MelonDSAndroid::RetroAchievements::RAAchievement> internalAchievements;
    std::list<MelonDSAndroid::RetroAchievements::RALeaderboard> internalLeaderboards;
    mapAchievementsFromJava(env, achievements, internalAchievements);
    mapLeaderboardsFromJava(env, leaderboards, internalLeaderboards);

    std::optional<std::string> richPresence = std::nullopt;

    if (richPresenceScript != nullptr)
    {
        jboolean isStringCopy;
        const char* richPresenceString = env->GetStringUTFChars(richPresenceScript, &isStringCopy);
        richPresence = richPresenceString;

        if (isStringCopy)
            env->ReleaseStringUTFChars(richPresenceScript, richPresenceString);
    }

    MelonDSAndroid::setupAchievements(internalAchievements, internalLeaderboards, richPresence);
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_unloadRetroAchievementsData(JNIEnv* env, jobject thiz)
{
    MelonDSAndroid::unloadRetroAchievementsData();
}

JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_MelonEmulator_getRichPresenceStatus(JNIEnv* env, jobject thiz)
{
    std::string richPresenceString = MelonDSAndroid::getRichPresenceStatus();
    if (richPresenceString.empty())
        return nullptr;
    else
        return env->NewStringUTF(richPresenceString.c_str());
}

JNIEXPORT jobjectArray JNICALL
Java_me_magnum_melonds_MelonEmulator_getRuntimeAchievements(JNIEnv* env, jobject thiz)
{
    jclass simpleRuntimeAchievementClass = env->FindClass("me/magnum/melonds/domain/model/retroachievements/RASimpleRuntimeAchievement");
    jmethodID simpleRuntimeAchievementConstructor = env->GetMethodID(simpleRuntimeAchievementClass, "<init>", "(JII)V");

    auto runtimeAchievements = MelonDSAndroid::getRuntimeAchievements();

    jobjectArray achievements = env->NewObjectArray(runtimeAchievements.size(), simpleRuntimeAchievementClass, nullptr);

    int index = 0;
    for (const auto &item: runtimeAchievements)
    {
        jobject simpleRuntimeAchievement = env->NewObject(simpleRuntimeAchievementClass, simpleRuntimeAchievementConstructor, item.id, (jint) item.value, (jint) item.target);
        env->SetObjectArrayElement(achievements, index++, simpleRuntimeAchievement);
    }

    return achievements;
}

JNIEXPORT jint JNICALL
Java_me_magnum_melonds_MelonEmulator_loadRomInternal(JNIEnv* env, jobject thiz, jstring romPath, jstring sramPath, jint gbaSlotType, jstring gbaRomPath, jstring gbaSramPath)
{
    jboolean isCopy = JNI_FALSE;
    const char* rom = romPath == nullptr ? nullptr : env->GetStringUTFChars(romPath, &isCopy);
    const char* sram = sramPath == nullptr ? nullptr : env->GetStringUTFChars(sramPath, &isCopy);
    const char* gbaRom = gbaRomPath == nullptr ? nullptr : env->GetStringUTFChars(gbaRomPath, &isCopy);
    const char* gbaSram = gbaSramPath == nullptr ? nullptr : env->GetStringUTFChars(gbaSramPath, &isCopy);

    MelonDSAndroid::RomGbaSlotConfig* gbaSlotConfig = buildGbaSlotConfig((GbaSlotType) gbaSlotType, gbaRom, gbaSram);
    int result = MelonDSAndroid::loadRom(rom, sram, gbaSlotConfig);
    delete gbaSlotConfig;

    if (isCopy == JNI_TRUE) {
        if (romPath) env->ReleaseStringUTFChars(romPath, rom);
        if (sramPath) env->ReleaseStringUTFChars(sramPath, sram);
        if (gbaRomPath) env->ReleaseStringUTFChars(gbaRomPath, gbaRom);
        if (gbaSramPath) env->ReleaseStringUTFChars(gbaSramPath, gbaSram);
    }

    return result;
}

JNIEXPORT jint JNICALL
Java_me_magnum_melonds_MelonEmulator_bootFirmwareInternal(JNIEnv* env, jobject thiz) {
    return MelonDSAndroid::bootFirmware();
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_startEmulation(JNIEnv* env, jobject thiz)
{
    stop = false;
    isThreadReallyPaused = false;
    limitFps = true;
    targetFps = EMULATOR_TARGET_FPS;
    isFastForwardEnabled = false;
    MelonDSAndroid::setAudioFastForwardActive(false);

    pthread_mutex_init(&emuThreadMutex, NULL);
    pthread_cond_init(&emuThreadCond, NULL);
    pthread_create(&emuThread, NULL, emulate, NULL);
    pthread_setname_np(emuThread, "EmulatorThread");

    started = true;
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_onDisplayVsync(JNIEnv* env, jobject thiz, jlong vsyncTimeNs)
{
    displayVsyncTracker.onVsync((int64_t) vsyncTimeNs);
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_presentFrame(JNIEnv* env, jobject thiz, jlong deadlineNs, jlong vsyncTimeNs, jfloatArray depthCamera, jobject renderFrameCallback)
{
    jmethodID renderFrameMethodId = getRenderFrameMethodId(env);

    std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadlineTime;
    if (deadlineNs > 0)
    {
        std::chrono::nanoseconds deadline(deadlineNs);
        deadlineTime = std::make_optional(std::chrono::time_point<std::chrono::steady_clock>(deadline));
    }
    else
    {
        deadlineTime = std::nullopt;
    }

    int64_t dueToleranceNs = displayVsyncTracker.getRefreshPeriodNs() / PRESENT_DUE_TOLERANCE_REFRESH_DENOMINATOR;
    int64_t emulatingFrameDueNs = upcomingFrameDueNs.load(std::memory_order_relaxed);
    // Keeps the instance (and so the frame, its texture and its fences) from being destroyed by another thread until the frame is presented
    std::lock_guard presentationLock(MelonDSAndroid::presentationMutex);
    Frame* presentationFrame = MelonDSAndroid::getPresentationFrame(deadlineTime, (int64_t) vsyncTimeNs, dueToleranceNs, emulatingFrameDueNs);
    EGLDisplay currentDisplay = eglGetCurrentDisplay();

    if (presentationFrame != nullptr && presentationFrame->presentFence)
    {
        eglDestroySyncKHR(currentDisplay, presentationFrame->presentFence);
        presentationFrame->presentFence = 0;
    }

    if (presentationFrame != nullptr)
    {
        eglWaitSyncKHR(currentDisplay, presentationFrame->renderFence, 0);
        // Bit 0: the top screen has Lua drawings, bit 1: the bottom screen has
        jint overlayScreens = (presentationFrame->overlayRows[0].isEmpty() ? 0 : 1) | (presentationFrame->overlayRows[1].isEmpty() ? 0 : 2);
        // Only a depth the compositor wrote this frame, since the texture outlives a switch to a draw that doesn't
        jint depthTexture = presentationFrame->hasDepth ? (jint) presentationFrame->depthTexture : 0;
        // Its camera into the caller's array (PresentFrameWrapper.depthCamera: DEPTH_CAMERA_PROJECTION, DEPTH_CAMERA_VIEWPORT) whenever
        // there's depth, valid or not, as the depth of field's view distances take the projection either way; the array keeps whatever it
        // held otherwise
        bool hasDepthCamera = depthTexture != 0 && depthCamera != nullptr && env->GetArrayLength(depthCamera) >= DEPTH_CAMERA_SIZE;
        jboolean depthCameraValid = hasDepthCamera && presentationFrame->depthCameraValid;
        jfloat depthFocus = depthTexture != 0 ? presentationFrame->depthFocus : NO_DEPTH_FOCUS;
        if (hasDepthCamera)
        {
            env->SetFloatArrayRegion(depthCamera, DEPTH_CAMERA_PROJECTION, 16, presentationFrame->depthProjection.data());
            env->SetFloatArrayRegion(depthCamera, DEPTH_CAMERA_VIEWPORT, 4, presentationFrame->depthViewport.data());
        }
        // GPU::Comp1x: the screens at 1x in the texture (Frame::halvesAt1x); the callback says whether every surface can draw such a frame
        jboolean halvesAt1xSupported = env->CallBooleanMethod(renderFrameCallback, renderFrameMethodId, true, (jint) presentationFrame->frameTexture, (jint) presentationFrame->width, (jint) presentationFrame->height, (jint) presentationFrame->topScreenWidth, (jint) presentationFrame->topScreenMargin, (jint) presentationFrame->topScreenGuard.left, (jint) presentationFrame->topScreenGuard.right, (jint) presentationFrame->topScreenGuard.top, (jint) presentationFrame->topScreenGuard.bottom, (jint) presentationFrame->overlayTexture, overlayScreens, (jint) presentationFrame->halvesAt1x, (jint) presentationFrame->scaleAt1x, (jint) presentationFrame->halfScaleAt1x, (jint) presentationFrame->topWingLines, depthTexture, depthCameraValid, depthFocus, (jlong) presentationFrame->sequence);
        MelonDSAndroid::setHalvesAt1xSupported(halvesAt1xSupported);
        EGLSyncKHR presentFence = eglCreateSyncKHR(currentDisplay, EGL_SYNC_FENCE_KHR, nullptr);
        presentationFrame->presentFence = presentFence;
    }
    else
    {
        jboolean halvesAt1xSupported = env->CallBooleanMethod(renderFrameCallback, renderFrameMethodId, false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, JNI_FALSE, (jfloat) NO_DEPTH_FOCUS, (jlong) 0);
        MelonDSAndroid::setHalvesAt1xSupported(halvesAt1xSupported);
    }
}

JNIEXPORT jfloat JNICALL
Java_me_magnum_melonds_MelonEmulator_getFPS(JNIEnv* env, jobject thiz)
{
    return fps;
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_setPerformanceCaptureEnabled(JNIEnv* env, jobject thiz, jboolean enabled)
{
    if (enabled)
    {
        // Drop samples left over from a previous capture, from the same thread that drains them
        perfFrameSamples.discardAll();
    }
    AudioDiagnostics::setPerfCaptureEnabled(enabled);
    isPerfCaptureEnabled = enabled;
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_setDebugLogEnabled(JNIEnv* env, jobject thiz, jboolean enabled)
{
    DebugLog::setEnabled(enabled);
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_setDebugSwitches(JNIEnv* env, jobject thiz, jstring switches)
{
#ifdef MELONADE_DEBUG_TOOLS
    // the app's Debug switches setting, already split into "name=value" lines (DebugSwitches.kt)
    std::string text;
    if (switches != nullptr)
    {
        const char* chars = env->GetStringUTFChars(switches, nullptr);
        if (chars != nullptr)
        {
            text = chars;
            env->ReleaseStringUTFChars(switches, chars);
        }
    }
    DebugProperties::setOverrides(text);
#endif
}


JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_MelonEmulator_getActiveDebugSwitches(JNIEnv* env, jobject thiz)
{
#ifdef MELONADE_DEBUG_TOOLS
    return env->NewStringUTF(DebugProperties::describeActive().c_str());
#else
    return env->NewStringUTF("");
#endif
}

JNIEXPORT jint JNICALL
Java_me_magnum_melonds_MelonEmulator_drainPerformanceFrameSamples(JNIEnv* env, jobject thiz, jlongArray buffer)
{
    const std::size_t fields = decltype(perfFrameSamples)::FIELDS;
    std::size_t maxSamples = (std::size_t) env->GetArrayLength(buffer) / fields;

    std::size_t count = perfFrameSamples.drain(maxSamples, [&](const int64_t* source, std::size_t sampleOffset, std::size_t sampleCount) {
        env->SetLongArrayRegion(buffer, (jsize) (sampleOffset * fields), (jsize) (sampleCount * fields), (const jlong*) source);
    });
    return (jint) count;
}

JNIEXPORT jlong JNICALL
Java_me_magnum_melonds_MelonEmulator_getDroppedPerformanceFrameSampleCount(JNIEnv* env, jobject thiz)
{
    return (jlong) perfFrameSamples.getDroppedSampleCount();
}

JNIEXPORT jint JNICALL
Java_me_magnum_melonds_MelonEmulator_getPerformanceFrameSampleFieldCount(JNIEnv* env, jobject thiz)
{
    return (jint) PERF_FRAME_SAMPLE_FIELDS;
}

JNIEXPORT jint JNICALL
Java_me_magnum_melonds_MelonEmulator_getAudioDiagnosticsTotalCount(JNIEnv* env, jobject thiz)
{
    return (jint) AudioDiagnostics::TOTAL_COUNT;
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_getAudioDiagnosticsTotals(JNIEnv* env, jobject thiz, jlongArray totals)
{
    int64_t values[AudioDiagnostics::TOTAL_COUNT];
    AudioDiagnostics::getTotals(values);
    jsize count = std::min(env->GetArrayLength(totals), (jsize) AudioDiagnostics::TOTAL_COUNT);
    env->SetLongArrayRegion(totals, 0, count, (const jlong*) values);
}

JNIEXPORT jstring JNICALL
Java_me_magnum_melonds_MelonEmulator_getAudioStreamInfo(JNIEnv* env, jobject thiz)
{
    char info[AudioDiagnostics::MAX_STREAM_INFO_LENGTH];
    if (AudioDiagnostics::getStreamInfo(info, sizeof(info)) == 0)
        return nullptr;
    return env->NewStringUTF(info);
}

JNIEXPORT jint JNICALL
Java_me_magnum_melonds_MelonEmulator_drainAudioStreamEvents(JNIEnv* env, jobject thiz, jlongArray timestamps, jobjectArray names, jobjectArray details)
{
    AudioDiagnostics::StreamEvent events[AudioDiagnostics::MAX_PENDING_EVENTS];
    std::size_t maxEvents = std::min({ (std::size_t) env->GetArrayLength(timestamps), (std::size_t) env->GetArrayLength(names), (std::size_t) env->GetArrayLength(details), AudioDiagnostics::MAX_PENDING_EVENTS });
    std::size_t count = AudioDiagnostics::takeEvents(events, maxEvents);
    for (std::size_t i = 0; i < count; i++)
    {
        jlong timestamp = events[i].timestampNs;
        env->SetLongArrayRegion(timestamps, (jsize) i, 1, &timestamp);
        jstring name = env->NewStringUTF(events[i].name);
        jstring detail = env->NewStringUTF(events[i].detail);
        env->SetObjectArrayElement(names, (jsize) i, name);
        env->SetObjectArrayElement(details, (jsize) i, detail);
        env->DeleteLocalRef(name);
        env->DeleteLocalRef(detail);
    }
    return (jint) count;
}

JNIEXPORT jint JNICALL
Java_me_magnum_melonds_MelonEmulator_getPerformanceEmulationStateFlags(JNIEnv* env, jobject thiz)
{
    jint flags = 0;
    if (started && paused)
        flags |= PERF_STATE_FLAG_PAUSED;
    if (MelonDSAndroid::isSleeping())
        flags |= PERF_STATE_FLAG_DS_SLEEP;
    if (isFastForwardEnabled)
        flags |= PERF_STATE_FLAG_FAST_FORWARD;
    return flags;
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_pauseEmulation(JNIEnv* env, jobject thiz)
{
    if (started) {
        pthread_mutex_lock(&emuThreadMutex);
    }

    if (!stop) {
        paused = true;
    }

    if (started) {
        pthread_mutex_unlock(&emuThreadMutex);
    }

    MelonDSAndroid::pause();
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_resumeEmulation(JNIEnv* env, jobject thiz)
{
    if (started) {
        pthread_mutex_lock(&emuThreadMutex);
    }

    if (!stop) {
        // Update the RTC before the emulator thread is released, which is only safe while it's parked in its pause wait (it can't leave while we hold
        // the mutex)
        if (started && paused && isThreadReallyPaused) {
            MelonDSAndroid::syncDateTimeToDevice();
        }

        paused = false;
        if (started) {
            pthread_cond_broadcast(&emuThreadCond);
        }
    }

    if (started) {
        pthread_mutex_unlock(&emuThreadMutex);
    }

    MelonDSAndroid::resume();
}

/**
 * Pauses the emulator and waits for its thread to park between frames, so a player's save or load can't overlap a frame's Lua state save or load;
 * returns false if the emulator isn't running or is stopping, and sets wasPaused to whether it was already paused.
 */
static bool pauseAndWaitForEmulatorThread(JNIEnv* env, jobject thiz, bool& wasPaused)
{
    if (!started)
        return false;

    pthread_mutex_lock(&emuThreadMutex);
    if (stop) {
        pthread_mutex_unlock(&emuThreadMutex);
        return false;
    }
    wasPaused = paused;
    pthread_mutex_unlock(&emuThreadMutex);

    if (!wasPaused)
        Java_me_magnum_melonds_MelonEmulator_pauseEmulation(env, thiz);

    // A stop lets the thread end without parking
    while (!isThreadReallyPaused && started)
        std::this_thread::yield();

    return isThreadReallyPaused;
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_resetEmulation(JNIEnv* env, jobject thiz) {
    bool wasPaused = false;
    if (!pauseAndWaitForEmulatorThread(env, thiz, wasPaused))
        return;

    MelonDSAndroid::reset();
    // A reset always resumes
    Java_me_magnum_melonds_MelonEmulator_resumeEmulation(env, thiz);
}

JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_MelonEmulator_saveStateInternal(JNIEnv* env, jobject thiz, jstring path)
{
    bool wasPaused = false;
    if (!pauseAndWaitForEmulatorThread(env, thiz, wasPaused))
        return JNI_FALSE;

    const char* saveStatePath = env->GetStringUTFChars(path, nullptr);
    bool result = MelonDSAndroid::saveState(saveStatePath);
    env->ReleaseStringUTFChars(path, saveStatePath);

    if (!wasPaused)
        Java_me_magnum_melonds_MelonEmulator_resumeEmulation(env, thiz);

    return result;
}

JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_MelonEmulator_loadStateInternal(JNIEnv* env, jobject thiz, jstring path)
{
    bool wasPaused = false;
    if (!pauseAndWaitForEmulatorThread(env, thiz, wasPaused))
        return JNI_FALSE;

    const char* saveStatePath = env->GetStringUTFChars(path, nullptr);
    bool result = MelonDSAndroid::loadState(saveStatePath);
    env->ReleaseStringUTFChars(path, saveStatePath);

    if (!wasPaused)
        Java_me_magnum_melonds_MelonEmulator_resumeEmulation(env, thiz);

    return result;
}

JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_MelonEmulator_loadRewindState(JNIEnv* env, jobject thiz, jobject rewindSaveState) {
    bool result = true;

    // Ignored while the emulation is stopping
    bool wasPaused = false;
    if (pauseAndWaitForEmulatorThread(env, thiz, wasPaused)) {
        jclass rewindSaveStateClass = env->FindClass("me/magnum/melonds/ui/emulator/rewind/model/RewindSaveState");
        jfieldID bufferField = env->GetFieldID(rewindSaveStateClass, "buffer", "Ljava/nio/ByteBuffer;");
        jfieldID bufferContentSizeField = env->GetFieldID(rewindSaveStateClass, "bufferContentSize", "J");
        jfieldID screenshotBufferField = env->GetFieldID(rewindSaveStateClass, "screenshotBuffer", "Ljava/nio/ByteBuffer;");
        jfieldID frameField = env->GetFieldID(rewindSaveStateClass, "frame", "I");
        jobject buffer = env->GetObjectField(rewindSaveState, bufferField);
        jlong bufferContentSize = env->GetLongField(rewindSaveState, bufferContentSizeField);
        jobject screenshotBuffer = env->GetObjectField(rewindSaveState, screenshotBufferField);
        jint frame = (int) env->GetIntField(rewindSaveState, frameField);

        melonDS::RewindSaveState state = melonDS::RewindSaveState {
            .buffer = (u8*) env->GetDirectBufferAddress(buffer),
            .bufferSize = (u32) env->GetDirectBufferCapacity(buffer),
            .bufferContentSize = (u32) bufferContentSize,
            .screenshot = (u8*) env->GetDirectBufferAddress(screenshotBuffer),
            .screenshotSize = (u32) env->GetDirectBufferCapacity(screenshotBuffer),
            .frame = frame
        };

        result = MelonDSAndroid::loadRewindState(state);

        // Resume emulation if it was running
        if (!wasPaused) {
            Java_me_magnum_melonds_MelonEmulator_resumeEmulation(env, thiz);
        }
    }

    return result;
}

JNIEXPORT jobject JNICALL
Java_me_magnum_melonds_MelonEmulator_getRewindWindow(JNIEnv* env, jobject thiz) {
    // The window is read while paused, so if the pause was just asked for, wait for the emulator thread to park (after its frame and the last
    // capture's screenshot) until a resume or a stop
    while (started && paused && !isThreadReallyPaused)
        std::this_thread::yield();

    auto currentRewindWindow = MelonDSAndroid::getRewindWindow();

    jclass rewindSaveStateClass = env->FindClass("me/magnum/melonds/ui/emulator/rewind/model/RewindSaveState");
    jmethodID rewindSaveStateConstructor = env->GetMethodID(rewindSaveStateClass, "<init>", "(Ljava/nio/ByteBuffer;JLjava/nio/ByteBuffer;I)V");

    jclass listClass = env->FindClass("java/util/ArrayList");
    jmethodID listConstructor = env->GetMethodID(listClass, "<init>", "()V");
    jmethodID listAddMethod = env->GetMethodID(listClass, "add", "(ILjava/lang/Object;)V");
    jobject rewindStateList = env->NewObject(listClass, listConstructor);

    int index = 0;
    for (auto state : currentRewindWindow.rewindStates) {
        jobject stateBuffer = env->NewDirectByteBuffer(state.buffer, state.bufferSize);
        jobject stateScreenshot = env->NewDirectByteBuffer(state.screenshot, state.screenshotSize);
        jobject rewindSaveState = env->NewObject(rewindSaveStateClass, rewindSaveStateConstructor, stateBuffer, (jlong) state.bufferContentSize, stateScreenshot, state.frame);
        env->CallVoidMethod(rewindStateList, listAddMethod, index++, rewindSaveState);
    }

    jclass rewindWindowClass = env->FindClass("me/magnum/melonds/ui/emulator/rewind/model/RewindWindow");
    jmethodID rewindWindowConstructor = env->GetMethodID(rewindWindowClass, "<init>", "(ILjava/util/ArrayList;)V");
    jobject rewindWindow = env->NewObject(rewindWindowClass, rewindWindowConstructor, currentRewindWindow.currentFrame, rewindStateList);
    return rewindWindow;
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_stopEmulation(JNIEnv* env, jobject thiz)
{
    if (started)
    {
        pthread_mutex_lock(&emuThreadMutex);
        stop = true;
        paused = false;
        started = false;
        pthread_cond_broadcast(&emuThreadCond);
        pthread_mutex_unlock(&emuThreadMutex);

        pthread_join(emuThread, NULL);
        pthread_mutex_destroy(&emuThreadMutex);
        pthread_cond_destroy(&emuThreadCond);
    }

    MelonDSAndroid::cleanup();
    // After the emulator thread has ended. Empty unless this is a PGO generate build (perf/PgoDump.h)
    PgoDump::onStop();

    env->DeleteGlobalRef(globalCameraManager);

    globalCameraManager = nullptr;

    delete androidCameraHandler;

    // The session's last lines go out before the activity finishes
    LogQueue::flush();
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_onScreenTouch(JNIEnv* env, jobject thiz, jint x, jint y)
{
    MelonDSAndroid::touchScreen(x, y);
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_onScreenRelease(JNIEnv* env, jobject thiz)
{
    MelonDSAndroid::releaseScreen();
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_onKeyPress(JNIEnv* env, jobject thiz, jint key)
{
    MelonDSAndroid::pressKey(key);
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_onKeyRelease(JNIEnv* env, jobject thiz, jint key)
{
    MelonDSAndroid::releaseKey(key);
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_updateMotionData(JNIEnv* env, jobject thiz, jfloat ax, jfloat ay, jfloat az, jfloat rx, jfloat ry, jfloat rz)
{
    MelonDSAndroid::updateMotionData(ax, ay, az, rx, ry, rz);
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_setWideView(JNIEnv* env, jobject thiz, jint width, jint margin)
{
    MelonDSAndroid::setWideView(width, margin);
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_setOutputScaleNeed(JNIEnv* env, jobject thiz, jint need)
{
    MelonDSAndroid::setOutputScaleNeed(need);
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_setOutputDeband(JNIEnv* env, jobject thiz, jboolean deband)
{
    MelonDSAndroid::setOutputDeband(deband);
}

JNIEXPORT jboolean JNICALL
Java_me_magnum_melonds_MelonEmulator_takeScreenshot(JNIEnv* env, jobject thiz)
{
    return MelonDSAndroid::takeScreenshot();
}

/**
 * Sets the frame limiter, the audio side and the performance hint target for fast-forward being [enabled] at [speedMultiplier] (0 or less means
 * unlimited), changing the pacing before the audio side hears about it, since that side trims once more after the frame now running.
 */
static void applyFastForwardPacing(bool enabled, float speedMultiplier)
{
    if (enabled) {
        limitFps = speedMultiplier > 0;
        targetFps = (int) (EMULATOR_TARGET_FPS * speedMultiplier);
    } else {
        limitFps = true;
        targetFps = EMULATOR_TARGET_FPS;
    }
    MelonDSAndroid::setAudioFastForwardActive(enabled, speedMultiplier);

    if (performanceHintSession != nullptr) {
        if (!enabled) {
            performanceHintSession->updateTargetWorkDuration(FRAME_DURATION_60FPS_NS);
        } else if (speedMultiplier > 0) {
            auto frameDurationNs = static_cast<int64_t>(FRAME_DURATION_60FPS_NS / speedMultiplier);
            performanceHintSession->updateTargetWorkDuration(frameDurationNs);
        } else {
            performanceHintSession->updateTargetWorkDuration(FRAME_DURATION_1000FPS_NS);
        }
    }
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_setFastForwardEnabled(JNIEnv* env, jobject thiz, jboolean enabled)
{
    isFastForwardEnabled = enabled;
    applyFastForwardPacing(enabled, fastForwardSpeedMultiplier);
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_setMicrophoneEnabled(JNIEnv* env, jobject thiz, jboolean enabled)
{
    if (enabled)
        MelonDSAndroid::userEnableMic();
    else
        MelonDSAndroid::userDisableMic();
}

JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_updateEmulatorConfiguration(JNIEnv* env, jobject thiz, jobject emulatorConfiguration)
{
    MelonDSAndroid::EmulatorConfiguration newConfiguration = MelonDSAndroidConfiguration::buildEmulatorConfiguration(env, emulatorConfiguration);

    float speedMultiplier = newConfiguration.fastForwardSpeedMultiplier;
    fastForwardSpeedMultiplier = speedMultiplier;
    isFramePacingEnabled = newConfiguration.framePacingVsync;
    isFramePacingBufferEnabled = newConfiguration.framePacingBuffer;

    MelonDSAndroid::updateEmulatorConfiguration(std::make_unique<MelonDSAndroid::EmulatorConfiguration>(std::move(newConfiguration)));

    if (isFastForwardEnabled)
        applyFastForwardPacing(true, speedMultiplier);
}
}

MelonDSAndroid::RomGbaSlotConfig* buildGbaSlotConfig(GbaSlotType slotType, const char* romPath, const char* savePath)
{
    if (slotType == GbaSlotType::GBA_ROM && romPath != nullptr)
    {
        MelonDSAndroid::RomGbaSlotConfigGbaRom* gbaSlotConfigGbaRom = new MelonDSAndroid::RomGbaSlotConfigGbaRom {
            .romPath = std::string(romPath),
            .savePath = savePath ? std::string(savePath) : "",
        };
        return (MelonDSAndroid::RomGbaSlotConfig*) gbaSlotConfigGbaRom;
    }
    else if (slotType == GbaSlotType::RUMBLE_PAK)
    {
        return (MelonDSAndroid::RomGbaSlotConfig*) new MelonDSAndroid::RomGbaSlotRumblePak;
    }
    else if (slotType == GbaSlotType::MEMORY_EXPANSION)
    {
        return (MelonDSAndroid::RomGbaSlotConfig*) new MelonDSAndroid::RomGbaSlotConfigMemoryExpansion;
    }
    else if (slotType == GbaSlotType::MOTION_PAK_HOMEBREW)
    {
        return (MelonDSAndroid::RomGbaSlotConfig*) new MelonDSAndroid::RomGbaSlotMotionPakHomebrew;
    }
    else if (slotType == GbaSlotType::MOTION_PAK_RETAIL)
    {
        return (MelonDSAndroid::RomGbaSlotConfig*) new MelonDSAndroid::RomGbaSlotMotionPakRetail;
    }
    else
    {
        return (MelonDSAndroid::RomGbaSlotConfig*) new MelonDSAndroid::RomGbaSlotConfigNone;
    }
}

static double getCurrentMillis() {
    return monotonicNanos() / 1000000.0;
}

// Frame pacing state of the emulator thread, carried from one frame to the next
struct FramePacingState
{
    VsyncFramePacer framePacer;
    // Presentation due time (0 when the next frame isn't paced) and refresh divisor of the next frame
    int64_t nextFrameDueNs = 0;
    int nextFrameDivisor = 0;
    // Whether the next frame's start is locked to the vsync grid (overloaded frames have a due time but aren't locked)
    bool isNextFrameLocked = false;

    /**
     * The next frame isn't paced: it starts right away and is due at [dueNs] (0 to present it as soon as it's ready).
     */
    void reset(int64_t dueNs)
    {
        nextFrameDueNs = dueNs;
        isNextFrameLocked = false;
        nextFrameDivisor = 0;
        upcomingFrameDueNs.store(dueNs, std::memory_order_relaxed);
    }
};

/**
 * Used when the next frame falls back to the timer: the first frame after pacing stops is kept behind a buffered frame that may still be queued, and
 * later ones are presented right away.
 */
static void unlockPacingFromRunning(FramePacingState& pacing)
{
    pacing.reset(pacing.framePacer.unlockFromRunning());
}

/**
 * Locks the next frame's start to the vsync grid and sleeps until it (moving [currentTick] and setting [sleepDuration] in ms), given the [workNs] the
 * last frame took; returns false when frames can't be locked to the display, so the caller uses the timer.
 */
static bool schedulePacedFrame(FramePacingState& pacing, int64_t workNs, double& currentTick, double& sleepDuration)
{
    int64_t lastVsyncNs = displayVsyncTracker.getLastVsyncNs();
    int64_t refreshPeriodNs = displayVsyncTracker.getRefreshPeriodNs();
    int64_t currentNs = monotonicNanos();
    int pacingDivisor = VsyncFramePacer::getRefreshDivisor(currentNs, lastVsyncNs, refreshPeriodNs);
    if (pacingDivisor <= 0)
        return false;

    // Without the buffer a locked frame is due on the first vsync after one frame period, and the buffer adds one more
    bool isBufferEnabled = isFramePacingBufferEnabled.load(std::memory_order_relaxed);
    VsyncFramePacer::FrameSchedule schedule = pacing.framePacer.scheduleNextFrame(currentNs, lastVsyncNs, refreshPeriodNs, pacingDivisor, workNs, isBufferEnabled);
    pacing.nextFrameDueNs = schedule.dueNs;
    pacing.isNextFrameLocked = schedule.isLocked;
    pacing.nextFrameDivisor = pacingDivisor;
    // Published before sleeping so that the render thread knows whether this frame can still be due on the vsync it's presenting
    upcomingFrameDueNs.store(pacing.nextFrameDueNs, std::memory_order_relaxed);

    int64_t nextFrameStartNs = schedule.startNs;
    if (nextFrameStartNs > currentNs)
    {
        timespec wakeUpTime = {
            .tv_sec = (time_t) (nextFrameStartNs / 1000000000LL),
            .tv_nsec = (long) (nextFrameStartNs % 1000000000LL),
        };
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wakeUpTime, nullptr);
        double timeAfterSleep = getCurrentMillis();
        sleepDuration = timeAfterSleep - currentTick;
        currentTick = timeAfterSleep;
    }

    return true;
}

/**
 * Timer frame limiter for unpaced frames: sleeps off the rest of the time the [nLines] that just ran should take ([delay] is the time since the
 * previous frame ended, all in ms), carrying the error over, and returns how long it slept, with [currentTick] moved past the sleep.
 */
static double runTimerLimiter(double& frameLimitError, u32 nLines, double delay, double& currentTick)
{
    double sleepDuration = 0;
    double frameTimeStep = (double) nLines / ((float) targetFps.load(std::memory_order_relaxed) * 263.0) * 1000.0;
    if (frameTimeStep < 1)
        frameTimeStep = 1;

    frameLimitError += frameTimeStep - delay;
    if (frameLimitError < -frameTimeStep)
        frameLimitError = -frameTimeStep;
    if (frameLimitError > frameTimeStep)
        frameLimitError = frameTimeStep;

    if (round(frameLimitError) > 0.0)
    {
        timespec sleepTime = {
            .tv_sec = 0,
            .tv_nsec = (long) (frameLimitError * 1000000),
        };
        clock_nanosleep(CLOCK_MONOTONIC, 0, &sleepTime, nullptr);
        double timeAfterSleep = getCurrentMillis();
        sleepDuration = timeAfterSleep - currentTick;
        frameLimitError -= timeAfterSleep - currentTick;
        currentTick = timeAfterSleep;
    }

    return sleepDuration;
}

/**
 * Records the frame that just ran for the performance logger, with [frameBeginTick], [frameEndTick] and [sleepDuration] in ms, and [isFrameLocked]
 * and [frameDivisor] its vsync lock and refresh divisor.
 */
static void pushPerfSample(double frameBeginTick, double frameEndTick, int64_t workNs, double sleepDuration, u32 nLines, bool isFirstFrameAfterPause, bool isFrameLocked, int frameDivisor, const AudioDiagnostics::FrameSample& audioSample)
{
    int64_t flags = (isFastForwardEnabled.load(std::memory_order_relaxed) ? PERF_FRAME_FLAG_FAST_FORWARD : 0) | (isFirstFrameAfterPause ? PERF_FRAME_FLAG_AFTER_PAUSE : 0);
    if (isFrameLocked)
        flags |= PERF_FRAME_FLAG_PACING_LOCKED | ((int64_t) frameDivisor << PERF_FRAME_PACING_DIVISOR_SHIFT);
    if (audioSample.isBufferFull)
        flags |= PERF_FRAME_FLAG_AUDIO_BUFFER_FULL;

    const int64_t sample[PERF_FRAME_SAMPLE_FIELDS] = {
        (int64_t) (frameEndTick * 1000000.0),
        workNs,
        (int64_t) (sleepDuration * 1000000.0),
        (int64_t) ((frameEndTick - frameBeginTick) * 1000000.0),
        (int64_t) nLines,
        flags,
        audioSample.fill,
        audioSample.silenceFrames,
        audioSample.skewPpm,
        audioSample.achievedSpeedMilli,
    };
    perfFrameSamples.push(sample);
}

#ifdef MELONADE_DEBUG_TOOLS
/**
 * Debug builds only: pins [tid] to the CPUs listed in the debug.melonade.[property] switch (see DebugProperties.h), setting only the affinity and not
 * the ADPF hint session, and returns whether it did.
 */
static bool debugPinThreadToCpus(const char* property, pid_t tid, const char* threadName)
{
    char value[PROP_VALUE_MAX];
    if (!DebugProperties::read(property, value))
        return false;

    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    unsigned long long mask = 0;
    const char* cursor = value;
    for (;;)
    {
        char* end;
        long cpu = strtol(cursor, &end, 10);
        if (end == cursor || cpu < 0 || cpu >= CPU_SETSIZE || (*end != ',' && *end != '\0'))
        {
            LOG_WARN(LOG_TAG_DEBUG, "%s=\"%s\" isn't a list of CPU ids, %s not pinned", property, value, threadName);
            return false;
        }
        CPU_SET(cpu, &cpus);
        if (cpu < 64)
            mask |= 1ull << cpu;
        if (*end == '\0')
            break;
        cursor = end + 1;
    }

    if (sched_setaffinity(tid, sizeof(cpus), &cpus) != 0)
    {
        LOG_WARN(LOG_TAG_DEBUG, "couldn't pin %s (tid %d) to CPUs %s (mask 0x%llx): %s", threadName, tid, value, mask, strerror(errno));
        return false;
    }
    LOG_INFO(LOG_TAG_DEBUG, "pinned %s (tid %d) to CPUs %s (mask 0x%llx)", threadName, tid, value, mask);
    return true;
}

/**
 * Lets a thread run on every CPU again, undoing the mask it inherited from a pinned emulator thread.
 */
static void debugUnpinThread(pid_t tid, const char* threadName)
{
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    long cpuCount = sysconf(_SC_NPROCESSORS_CONF);
    for (long cpu = 0; cpu < cpuCount && cpu < CPU_SETSIZE; cpu++)
        CPU_SET(cpu, &cpus);

    if (sched_setaffinity(tid, sizeof(cpus), &cpus) != 0)
        LOG_WARN(LOG_TAG_DEBUG, "couldn't reset %s (tid %d) to all %ld CPUs: %s", threadName, tid, cpuCount, strerror(errno));
    else
        LOG_INFO(LOG_TAG_DEBUG, "reset %s (tid %d) to all %ld CPUs (it inherited the emulator thread's mask)", threadName, tid, cpuCount);
}
#endif

void* emulate(void*)
{
#ifdef MELONADE_DEBUG_TOOLS
    // Before the core's threads exist, so they inherit the mask (see debugPinThreadToCpus)
    bool emulatorThreadPinned = debugPinThreadToCpus("cpusemu", gettid(), "EmulatorThread");
#endif

    double startTick = getCurrentMillis();
    double lastTick = startTick;
    double lastMeasureFpsTick = startTick;
    double frameLimitError = 0.0;
    bool isFirstFrameAfterPause = true;

    FramePacingState pacing;
    upcomingFrameDueNs.store(0, std::memory_order_relaxed);

    MelonDSAndroid::start();

    auto manager = PerformanceHintManagerFactory::create(jniEnvHandler);
    performanceHintSession = new ThreadSafePerformanceHintSession(std::move(manager));
    performanceHintSession->createSession(gettid(), FRAME_DURATION_60FPS_NS);
#ifdef MELONADE_DEBUG_TOOLS
    // The compute renderer's capture readback stats, logged as a CAPWAIT line every 300 frames that had any
    MelonDSAndroid::CaptureStats captureStatsSum;
    int captureStatsFrames = 0;
#endif
    // The core's 2D render thread does part of each frame's work, so it joins the session while threaded 2D is on
    pid_t sessionGpu2DThreadId = 0;

    for (;;)
    {
        pthread_mutex_lock(&emuThreadMutex);
        if (paused && !stop) {
            // Changed Lua storage is snapshotted (the app may be killed while paused) and the last rewind capture's screenshot copied in, outside the
            // mutex so a resume doesn't wait for them
            pthread_mutex_unlock(&emuThreadMutex);
            MelonDSAndroid::flushLuaStorage();
            MelonDSAndroid::finishRewindScreenshot();
            // Here rather than in pauseEmulation(), so the core's counters are written between frames. Empty unless this is a PGO generate build
            // (perf/PgoDump.h)
            PgoDump::onPause();
            pthread_mutex_lock(&emuThreadMutex);
        }
        if (paused) {
            AudioDiagnostics::onEmulatorPaused();
            isThreadReallyPaused = true;
            while (paused && !stop)
                pthread_cond_wait(&emuThreadCond, &emuThreadMutex);

            frameLimitError = 0;
            lastTick = getCurrentMillis();
            isThreadReallyPaused = false;
            isFirstFrameAfterPause = true;

            // The first frame after a pause starts right away, off the vsync grid, and pacing re-syncs after it
            pacing.framePacer.unlock();
            pacing.reset(0);
        }

        if (stop) {
            pthread_mutex_unlock(&emuThreadMutex);
            break;
        }

        pthread_mutex_unlock(&emuThreadMutex);

        double frameBeginTick = lastTick;
        double sleepDuration = 0;
        int64_t frameDueNs = pacing.nextFrameDueNs;
        int frameDivisor = pacing.nextFrameDivisor;
        bool isFrameLocked = pacing.isNextFrameLocked;
        auto frameStart = std::chrono::steady_clock::now();

        u32 nLines = MelonDSAndroid::loop(frameDueNs);

        int64_t workNs = std::chrono::nanoseconds(std::chrono::steady_clock::now() - frameStart).count();
        // The audio buffer's high point, before the frame's sleep drains it
        bool isAudioDiagnosticsEnabled = AudioDiagnostics::isEnabled();
        int audioFillAfterFrame = isAudioDiagnosticsEnabled ? MelonDSAndroid::getAudioOutputBufferFill() : 0;
        if (performanceHintSession != nullptr)
        {
            // The 2D thread starts or stops at the start of a frame (when the setting changes), and its ID is known once it runs
            pid_t gpu2DThreadId = MelonDSAndroid::getGpu2DThreadId();
            if (gpu2DThreadId != sessionGpu2DThreadId) [[unlikely]]
            {
#ifdef MELONADE_DEBUG_TOOLS
                // A new 2D thread (re-created when the setting changes) is pinned again, or without cpus2d reset to every CPU rather than keeping the
                // cpusemu mask it inherited
                if (gpu2DThreadId != 0 && !debugPinThreadToCpus("cpus2d", gpu2DThreadId, "GPU2DThread") && emulatorThreadPinned)
                    debugUnpinThread(gpu2DThreadId, "GPU2DThread");
#endif
                sessionGpu2DThreadId = gpu2DThreadId;
                performanceHintSession->setHelperThreads(gpu2DThreadId ? std::vector<pid_t> { gpu2DThreadId } : std::vector<pid_t> {});
            }

            performanceHintSession->reportActualWorkDuration(workNs);
        }

#ifdef MELONADE_DEBUG_TOOLS
        {
            MelonDSAndroid::CaptureStats captureStats;
            MelonDSAndroid::takeCaptureStats(captureStats);
            captureStatsSum.maps += captureStats.maps;
            captureStatsSum.waitNs += captureStats.waitNs;
            captureStatsSum.spinNs += captureStats.spinNs;
            captureStatsSum.spanNs += captureStats.spanNs;
            if (++captureStatsFrames == 300)
            {
                if (captureStatsSum.maps > 0)
                    LOG_INFO_QUEUED(LOG_TAG_DEBUG, "CAPWAIT frames=%d maps=%lld wait_ms_f=%.3f spin_ms_f=%.3f span_ms_map=%.3f",
                                        captureStatsFrames, (long long) captureStatsSum.maps, captureStatsSum.waitNs / 1e6 / captureStatsFrames,
                                        captureStatsSum.spinNs / 1e6 / captureStatsFrames, captureStatsSum.spanNs / 1e6 / captureStatsSum.maps);
                captureStatsSum = {};
                captureStatsFrames = 0;
            }
        }
#endif

        double currentTick = getCurrentMillis();
        double delay = currentTick - lastTick;

        // Lock the next frame's start to the display's vsync grid if possible, otherwise fall back to the timer
        bool isFpsLimited = limitFps.load(std::memory_order_relaxed);
        bool isNextFramePaced = isFpsLimited && !isFastForwardEnabled.load(std::memory_order_relaxed) && isFramePacingEnabled.load(std::memory_order_relaxed)
                && schedulePacedFrame(pacing, workNs, currentTick, sleepDuration);

        if (isNextFramePaced)
        {
            // Start the timer limiter from scratch if pacing stops
            frameLimitError = 0;
            lastTick = currentTick;
        }
        else if (isFpsLimited)
        {
            unlockPacingFromRunning(pacing);
            sleepDuration = runTimerLimiter(frameLimitError, nLines, delay, currentTick);
            lastTick = currentTick;
        }
        else
        {
            unlockPacingFromRunning(pacing);
            frameLimitError = 0;
            lastTick = getCurrentMillis();
        }

        // The speed paced fast-forward really reaches, for the audio diagnostics
        AudioDiagnostics::onEmulatorFrameEnd(lastTick - frameBeginTick, isFpsLimited && isFastForwardEnabled.load(std::memory_order_relaxed),
                fastForwardSpeedMultiplier.load(std::memory_order_relaxed));

        AudioDiagnostics::FrameSample audioSample;
        if (isAudioDiagnosticsEnabled) [[unlikely]]
            audioSample = AudioDiagnostics::onFrame(audioFillAfterFrame, MelonDSAndroid::getAudioOutputBufferFill());

        if (isPerfCaptureEnabled.load(std::memory_order_relaxed)) [[unlikely]]
            pushPerfSample(frameBeginTick, lastTick, workNs, sleepDuration, nLines, isFirstFrameAfterPause, isFrameLocked, frameDivisor, audioSample);
        isFirstFrameAfterPause = false;

        if (DebugLog::isEnabled()) [[unlikely]]
            MelonDSAndroid::logDebugFrameState(isFastForwardEnabled.load(std::memory_order_relaxed), fps, workNs);

        observedFrames++;
        if (observedFrames >= 30) {
            fps = (observedFrames * 1000.0) / (lastTick - lastMeasureFpsTick);
            lastMeasureFpsTick = lastTick;
            observedFrames = 0;
        }
    }

    if (performanceHintSession != nullptr) {
        performanceHintSession->destroySession();

        delete performanceHintSession;
        performanceHintSession = nullptr;
    }

    MelonDSAndroid::stop();
    pthread_exit(NULL);
}