// What scripts ask of the WideMelon camera with melon.wide, ported from the app's LuaWideCameraRequests.kt, taken a part at a time: the zoom
// and each axis of the pan come from the script that changed its requests most recently among those asking for one, and 1x is held while
// any script holds it. A script asking for nothing drops its request, and a stopped script's requests go away. One thread only (the one
// the scripts' requests arrive on)
#pragma once

#include <set>
#include <utility>
#include <vector>

#include "camera/WideCamera.h"

namespace Frontend
{

class LuaWideCameraRequests
{
public:
    // what the scripts ask for now, an empty request for nothing
    WideCameraRequest Request() const;

    // returns what the scripts ask for now
    WideCameraRequest OnScriptRequest(int scriptId, const WideCameraRequest& request);
    WideCameraRequest OnScriptStopped(int scriptId);
    void OnScriptStarted(int scriptId);
    WideCameraRequest OnSessionEnded();

private:
    // ordered from oldest to most recent request, one per script
    std::vector<std::pair<int, WideCameraRequest>> Requests;
    // a request still on its way from a stopped script is ignored
    std::set<int> StoppedScripts;

    void Remove(int scriptId);
};

}
