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
    { "blitflush", &GPU::BlitFlushCapture },
    { "lateblit", &GPU::LateBlit },
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

    overrides.threaded2d = readBool("threaded2d");

    char summary[512];
    int length = snprintf(summary, sizeof(summary), "Debug switches:");
    for (const BoolSwitch& boolSwitch : BOOL_SWITCHES)
        length += snprintf(summary + length, sizeof(summary) - length, " %s=%d", boolSwitch.name, gpu.*boolSwitch.target ? 1 : 0);
    snprintf(summary + length, sizeof(summary) - length, " shadowlog=%d capspin=%d hirescap=%d hirescapdebug=%d threaded2d=%d",
             gpu.ShadowLogFrames, gpu.CaptureSpinUs, overrides.hiresCapture, gpu.HiresCaptureDebug, overrides.threaded2d);
    LOG_INFO(LOG_TAG_DEBUG, "%s", summary);
    return overrides;
}

}

#endif
