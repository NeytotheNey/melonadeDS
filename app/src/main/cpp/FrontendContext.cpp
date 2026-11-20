#include "FrontendContext.h"

namespace MelonDSAndroid
{

FrontendContext frontendContext;

void setHalvesAt1xSupported(bool supported)
{
    frontendContext.halvesAt1xSupported.store(supported, std::memory_order_relaxed);
}

bool areHalvesAt1xSupported()
{
    return frontendContext.halvesAt1xSupported.load(std::memory_order_relaxed);
}

void setOutputScaleNeed(int need)
{
    frontendContext.outputScaleNeed.store(need, std::memory_order_relaxed);
}

int getOutputScaleNeed()
{
    return frontendContext.outputScaleNeed.load(std::memory_order_relaxed);
}

void setOutputDeband(bool deband)
{
    frontendContext.outputDeband.store(deband, std::memory_order_relaxed);
}

bool getOutputDeband()
{
    return frontendContext.outputDeband.load(std::memory_order_relaxed);
}

}
