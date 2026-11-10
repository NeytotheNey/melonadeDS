#ifdef MELONADE_DEBUG_TOOLS

#include "DebugProperties.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sys/system_properties.h>
#include "GPU.h"
#include "MelonLog.h"

using namespace melonDS;

namespace DebugProperties
{

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
    { "computetimers", &GPU::ComputeTimers },
    { "computepeaks", &GPU::ComputePeaks },
    { "computesync", &GPU::ComputeSync },
    { "computewhy", &GPU::ComputeWhy },
    { "blitflush", &GPU::BlitFlushCapture },
    { "lateblit", &GPU::LateBlit },
    { "blitafterread", &GPU::BlitAfterReadback },
    { "bnohold", &GPU::EngineBNoHold },
};

bool read(const char* name, char* value)
{
    char property[PROP_NAME_MAX];
    snprintf(property, sizeof(property), "debug.melonade.%s", name);
    value[0] = '\0';
    return __system_property_get(property, value) > 0 && value[0] != '\0';
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

    // all but 32 are on in the core (95)
    if (read("computeopt", value))
        gpu.ComputeOpt = std::clamp(atoi(value), 0, 127);

    if (read("vertexid", value))
        gpu.GPU3D.SetVertexIdLogging(std::clamp(atoi(value), 0, 2));

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

    char summary[640];
    int length = snprintf(summary, sizeof(summary), "Debug switches:");
    for (const BoolSwitch& boolSwitch : BOOL_SWITCHES)
        length += snprintf(summary + length, sizeof(summary) - length, " %s=%d", boolSwitch.name, gpu.*boolSwitch.target ? 1 : 0);
    snprintf(summary + length, sizeof(summary) - length, " shadowlog=%d capspin=%d computepace=%d computetile=%d computeopt=%d hirescap=%d hirescapdebug=%d filter2d=%d capreads=%d gpu2dstats=%d widedrops=%d threaded2d=%d vertexid=%d halfcam=%d",
             gpu.ShadowLogFrames, gpu.CaptureSpinUs, gpu.ComputePace, gpu.ComputeTileSize, gpu.ComputeOpt, overrides.hiresCapture, gpu.HiresCaptureDebug, overrides.filter2d, captureReads ? 1 : 0, gpu2dStats ? 1 : 0, gpu.GPU3D.GetWideDropLogging() ? 1 : 0, overrides.threaded2d, gpu.GPU3D.GetVertexIdLogging(), gpu.GPU3D.GetHalfCamera());
    LOG_INFO(LOG_TAG_DEBUG, "%s", summary);
    return overrides;
}

}

#endif
