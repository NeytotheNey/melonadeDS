#include "camera/LuaWideCameraRequests.h"

#include <algorithm>

namespace Frontend
{

WideCameraRequest LuaWideCameraRequests::Request() const
{
    WideCameraRequest request;
    for (auto it = Requests.rbegin(); it != Requests.rend(); ++it)
    {
        const WideCameraRequest& r = it->second;
        if (!request.Zoom && r.Zoom)
            request.Zoom = r.Zoom;
        if (!request.Pan && r.Pan)
            request.Pan = r.Pan;
        if (!request.PanY && r.PanY)
            request.PanY = r.PanY;
        request.HoldNative = request.HoldNative || r.HoldNative;
    }
    return request;
}

void LuaWideCameraRequests::Remove(int scriptId)
{
    Requests.erase(std::remove_if(Requests.begin(), Requests.end(), [&](const auto& entry) { return entry.first == scriptId; }),
        Requests.end());
}

WideCameraRequest LuaWideCameraRequests::OnScriptRequest(int scriptId, const WideCameraRequest& request)
{
    if (!StoppedScripts.contains(scriptId))
    {
        // removed first, so it moves to the end as the most recent request
        Remove(scriptId);
        if (!request.IsOff())
            Requests.emplace_back(scriptId, request);
    }
    return Request();
}

WideCameraRequest LuaWideCameraRequests::OnScriptStopped(int scriptId)
{
    StoppedScripts.insert(scriptId);
    Remove(scriptId);
    return Request();
}

void LuaWideCameraRequests::OnScriptStarted(int scriptId)
{
    StoppedScripts.erase(scriptId);
}

WideCameraRequest LuaWideCameraRequests::OnSessionEnded()
{
    Requests.clear();
    StoppedScripts.clear();
    return Request();
}

}
