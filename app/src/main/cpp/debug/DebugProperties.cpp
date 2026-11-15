#ifdef MELONADE_DEBUG_TOOLS

#include "DebugProperties.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>
#include <sys/system_properties.h>
#include "GPU.h"
#include "MelonLog.h"

using namespace melonDS;

namespace DebugProperties
{

// the app's switches (setOverrides) and what read found since, for describeActive. read from the emulator thread and the JNI thread that
// starts it, so behind a lock
struct FoundSwitch
{
    std::string name, value;
    bool fromApp;
    // the property's value when the app's replaced one that was set too, empty otherwise
    std::string overriddenProperty;
};
static std::mutex overridesMutex;
static std::vector<std::pair<std::string, std::string>> appSwitches;
static std::vector<FoundSwitch> foundSwitches;

void setOverrides(const std::string& text)
{
    std::lock_guard lock(overridesMutex);
    appSwitches.clear();
    foundSwitches.clear();

    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;

        size_t equals = line.find('=');
        if (equals == 0 || equals == std::string::npos || equals + 1 >= line.size())
            continue;
        appSwitches.emplace_back(line.substr(0, equals), line.substr(equals + 1));
    }
}

static void noteFound(const char* name, const char* value, bool fromApp, const char* overriddenProperty)
{
    for (FoundSwitch& found : foundSwitches)
    {
        if (found.name == name)
        {
            found.value = value;
            found.fromApp = fromApp;
            found.overriddenProperty = overriddenProperty;
            return;
        }
    }
    foundSwitches.push_back({ name, value, fromApp, overriddenProperty });
}

std::string describeActive()
{
    std::lock_guard lock(overridesMutex);
    std::string text;
    for (const FoundSwitch& found : foundSwitches)
    {
        if (!text.empty())
            text += ' ';
        text += found.name + "=" + found.value;
        if (!found.fromApp)
            text += "(prop)";
        else if (!found.overriddenProperty.empty())
            text += "(app, over prop " + found.overriddenProperty + ")";
    }

    std::string unused;
    for (const auto& [name, value] : appSwitches)
    {
        bool isFound = std::any_of(foundSwitches.begin(), foundSwitches.end(), [&](const FoundSwitch& found) { return found.name == name; });
        if (!isFound)
            unused += " " + name + "=" + value;
    }
    if (!unused.empty())
        text += (text.empty() ? "unread:" : " unread:") + unused;
    return text;
}

struct BoolSwitch
{
    const char* name;
    bool GPU::* target;
};

// The on/off switches, where a value of 0 or 1 replaces the core's default and anything else keeps it
static const BoolSwitch BOOL_SWITCHES[] = {
    { "hash2d", &GPU::Hash2DLogging },
    { "defercap", &GPU::DeferCapture2D },
    { "earlyflush", &GPU::EarlyFlushCapture },
    { "shadowfix", &GPU::ShadowMaskLineClear },
    { "maskdepth", &GPU::ShadowMaskDepthTest },
    { "equaldepth", &GPU::EqualDepthMargin },
    { "computedirect", &GPU::ComputeDirectDispatch },
    { "computegrow", &GPU::ComputeGrow },
    { "shadercache", &GPU::ShaderCache },
    { "computerooms", &GPU::ComputeRooms },
    { "computewarm", &GPU::ComputeWarm },
    { "computetimers", &GPU::ComputeTimers },
    { "computepeaks", &GPU::ComputePeaks },
    { "computesync", &GPU::ComputeSync },
    { "computewhy", &GPU::ComputeWhy },
    { "blitflush", &GPU::BlitFlushCapture },
    { "lateblit", &GPU::LateBlit },
    { "blitafterread", &GPU::BlitAfterReadback },
    { "bnohold", &GPU::EngineBNoHold },
    { "hash3d", &GPU::Hash3DLogging },
    { "computedepth", &GPU::ComputeDepthStats },
    { "computessbo", &GPU::ComputeSSBO },
    { "widepolys", &GPU::WidePolys },
};

bool read(const char* name, char* value)
{
    char property[PROP_NAME_MAX];
    snprintf(property, sizeof(property), "debug.melonade.%s", name);
    value[0] = '\0';
    bool isPropertySet = __system_property_get(property, value) > 0 && value[0] != '\0';

    std::lock_guard lock(overridesMutex);
    for (const auto& [appName, appValue] : appSwitches)
    {
        if (appName == name)
        {
            char propertyValue[PROP_VALUE_MAX];
            snprintf(propertyValue, sizeof(propertyValue), "%s", isPropertySet ? value : "");
            snprintf(value, PROP_VALUE_MAX, "%s", appValue.c_str());
            noteFound(name, value, true, propertyValue);
            return true;
        }
    }

    if (isPropertySet)
        noteFound(name, value, false, "");
    return isPropertySet;
}

// -1 when the property isn't 0 or 1
static int readBool(const char* name)
{
    char value[PROP_VALUE_MAX];
    if (!read(name, value))
        return -1;
    if (strcmp(value, "0") == 0)
        return 0;
    if (strcmp(value, "1") == 0)
        return 1;
    return -1;
}

Overrides apply(GPU& gpu)
{
    char value[PROP_VALUE_MAX];
    Overrides overrides;

    // on by default in debug builds, so it gets real use, and off in the core until it's proven.
    // only an accelerated renderer holds capture lines or clears the plane, so this is GL and compute
    gpu.EngineBNoHold = true;
    // the same until it's tested on a phone: OpenGL and compute (Renderer3D::SupportsBlitAfterRender)
    gpu.BlitAfterReadback = true;

    for (const BoolSwitch& boolSwitch : BOOL_SWITCHES)
    {
        int state = readBool(boolSwitch.name);
        if (state >= 0)
            gpu.*boolSwitch.target = state == 1;
    }

    if (read("shadowlog", value))
    {
        int frames = atoi(value);
        gpu.ShadowLogFrames = frames == 1 ? 300 : std::max(frames, 0);
    }

    if (read("capspin", value))
        gpu.CaptureSpinUs = std::clamp(atoi(value), 0, 20000);

    // on (2) in the core, so only the log depends on the property being set
    if (read("computepace", value))
    {
        gpu.ComputePace = std::clamp(atoi(value), 0, 2);
        gpu.ComputePaceLog = true;
    }

    if (read("computetile", value))
    {
        int tileSize = atoi(value);
        gpu.ComputeTileSize = (tileSize == 8 || tileSize == 16 || tileSize == 32) ? tileSize : 0;
    }

    // all but 32 and 256 are on in the core (223)
    if (read("computeopt", value))
        gpu.ComputeOpt = std::clamp(atoi(value), 0, 511);

    // Mali diagnostics (docs/shelved/compute-on-mali.md): computecheck N checks every Nth render
    // with polygons, 0 is off and anything else that isn't a number over 0 is 1
    if (read("computecheck", value))
    {
        int every = atoi(value);
        gpu.ComputeCheck = strcmp(value, "0") == 0 ? 0 : every > 0 ? every : 1;
    }
    if (read("computebin", value))
        gpu.ComputeBin = std::clamp(atoi(value), 0, 3);
    if (read("computecalc", value))
        gpu.ComputeCalc = std::clamp(atoi(value), 0, 1);
    // on (1) in the core
    if (read("computeflat", value))
        gpu.ComputeFlat = std::clamp(atoi(value), 0, 1);

    if (read("vertexid", value))
        gpu.GPU3D.SetVertexIdLogging(std::clamp(atoi(value), 0, 2));

    // GPU discovery (docs/plan-gpu-discovery.md)
    if (read("gltimers", value))
        gpu.GLTimers = std::clamp(atoi(value), 0, 2);
    if (read("scenestats", value))
        gpu.SceneStats = std::clamp(atoi(value), 0, 2);
    if (read("gltrans", value))
        gpu.GLTransSkip = std::clamp(atoi(value), 0, 7);
    if (read("gledgeskip", value))
        gpu.GLEdgeSkip = std::clamp(atoi(value), 0, 1);
    if (read("glclear", value))
        gpu.GLClearFast = std::clamp(atoi(value), 0, 3);
    if (read("glnodiscard", value))
        gpu.GLNoDiscard = std::clamp(atoi(value), 0, 7);
    // a measurement probe that renders wrong (T2), 0 everywhere unless set
    if (read("gltexprobe", value))
        gpu.GLTexProbe = std::clamp(atoi(value), 0, 4);
    // the decoded texel heap (T2 step 1), off by default until the phones have checked it
    if (read("gltexcache", value))
        gpu.GLTexCache = std::clamp(atoi(value), 0, 511);
    if (read("compclear", value))
        gpu.CompClear = std::clamp(atoi(value), 0, 2);
    if (read("comp1x", value))
        gpu.Comp1x = std::clamp(atoi(value), 0, 3);
    if (read("filter2drows", value))
        gpu.Filter2DRows = std::clamp(atoi(value), 0, 2);
    if (read("outcap", value))
        gpu.OutputCap = std::clamp(atoi(value), 0, 7);
    if (read("outcapneed", value))
        gpu.OutputCapNeed = std::clamp(atoi(value), 0, 16);
    if (read("outcap3d", value))
        gpu.OutputCap3D = std::clamp(atoi(value), 0, 3);

    if (read("halfcam", value))
    {
        overrides.halfCamera = std::clamp(atoi(value), 0, 5);
        gpu.GPU3D.SetHalfCamera(overrides.halfCamera);
    }

    if (read("hirescap", value))
    {
        if (strcmp(value, "0") == 0)
            overrides.hiresCapture = 0;
        else if (strcmp(value, "1") == 0)
            overrides.hiresCapture = 1;
        else if (strcmp(value, "log") == 0)
        {
            overrides.hiresCapture = 1;
            gpu.HiresCaptureDebug = 1;
        }
        else if (strcmp(value, "check") == 0)
        {
            overrides.hiresCapture = 1;
            gpu.HiresCaptureDebug = 2;
        }
    }

    if (read("filter2d", value) && value[0] >= '0' && value[0] <= '3' && value[1] == '\0')
        overrides.filter2d = value[0] - '0';
    gpu.Filter2DLogging = overrides.filter2d > 0;

    overrides.threaded2d = readBool("threaded2d");

    bool captureReads = readBool("capreads") == 1;
    gpu.SetCaptureReadLogging(captureReads);

    bool gpu2dStats = readBool("gpu2dstats") == 1;
    gpu.Set2DStatsLogging(gpu2dStats);

    gpu.GPU3D.SetWideDropLogging(readBool("widedrops") == 1);

    // the GPU 2D shadow renderer: 1 = mode 1, 2 = all; log and check add the GPU2D line, check the readback
    int gpu2dMode = 0;
    bool gpu2dLog = false, gpu2dCheck = false;
    if (read("gpu2d", value))
    {
        if (strcmp(value, "1") == 0)
            gpu2dMode = 1;
        else if (strcmp(value, "all") == 0)
            gpu2dMode = 2;
        else if (strcmp(value, "log") == 0)
        {
            gpu2dMode = 1;
            gpu2dLog = true;
        }
        else if (strcmp(value, "check") == 0)
        {
            gpu2dMode = 2;
            gpu2dLog = true;
            gpu2dCheck = true;
        }
    }
    int gpu2dWide = readBool("gpu2dwide") == 1 ? 1 : 0;
    gpu.SetShadow2D(gpu2dMode, gpu2dLog, gpu2dCheck, gpu2dWide);

    char summary[1024];
    int length = snprintf(summary, sizeof(summary), "Debug switches:");
    for (const BoolSwitch& boolSwitch : BOOL_SWITCHES)
        length += snprintf(summary + length, sizeof(summary) - length, " %s=%d", boolSwitch.name, gpu.*boolSwitch.target ? 1 : 0);
    snprintf(summary + length, sizeof(summary) - length, " shadowlog=%d capspin=%d computepace=%d computetile=%d computeopt=%d hirescap=%d hirescapdebug=%d filter2d=%d capreads=%d gpu2dstats=%d widedrops=%d threaded2d=%d vertexid=%d halfcam=%d gpu2d=%s gpu2dwide=%d gltimers=%d scenestats=%d gltrans=%d gledgeskip=%d glclear=%d glnodiscard=%d gltexprobe=%d gltexcache=%d compclear=%d comp1x=%d filter2drows=%d outcap=%d outcapneed=%d outcap3d=%d computecheck=%d computebin=%d computecalc=%d computeflat=%d",
             gpu.ShadowLogFrames, gpu.CaptureSpinUs, gpu.ComputePace, gpu.ComputeTileSize, gpu.ComputeOpt, overrides.hiresCapture, gpu.HiresCaptureDebug, overrides.filter2d, captureReads ? 1 : 0, gpu2dStats ? 1 : 0, gpu.GPU3D.GetWideDropLogging() ? 1 : 0, overrides.threaded2d, gpu.GPU3D.GetVertexIdLogging(), gpu.GPU3D.GetHalfCamera(),
             gpu2dCheck ? "check" : gpu2dLog ? "log" : gpu2dMode == 2 ? "all" : gpu2dMode == 1 ? "1" : "0", gpu2dWide, gpu.GLTimers, gpu.SceneStats, gpu.GLTransSkip, gpu.GLEdgeSkip, gpu.GLClearFast, gpu.GLNoDiscard, gpu.GLTexProbe, gpu.GLTexCache, gpu.CompClear, gpu.Comp1x, gpu.Filter2DRows, gpu.OutputCap, gpu.OutputCapNeed, gpu.OutputCap3D, gpu.ComputeCheck, gpu.ComputeBin, gpu.ComputeCalc, gpu.ComputeFlat);
    LOG_INFO(LOG_TAG_DEBUG, "%s", summary);
    // what was set, and where from: the app's Debug switches setting or setprop
    std::string active = describeActive();
    LOG_INFO(LOG_TAG_DEBUG, "Debug switches set: %s", active.empty() ? "none" : active.c_str());
    return overrides;
}

}

#endif
