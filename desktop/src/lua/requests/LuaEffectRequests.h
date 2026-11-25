// The tilt-shift, bloom (with its vignette and haze), depth of field, shading and sun shadows scripts ask for with melon.screen, ported from
// the app's LuaTiltShiftRequests.kt, LuaBloomRequests.kt, LuaDepthOfFieldRequests.kt, LuaShadeRequests.kt and LuaSunRequests.kt, which are
// the same class over each effect's values: the newest request among the running scripts wins, a script asking for none of the effect drops
// its request, and a stopped script's request goes away. One thread only (the one the scripts' requests arrive on)
#pragma once

#include <algorithm>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "render/PresentFrame.h"

namespace Frontend
{

// TiltShift.kt's isOff, which the desktop's TiltShift doesn't have: no blur to draw and no color boost
inline bool IsOffRequest(const TiltShift& tiltShift)
{
    return !tiltShift.HasBlur() && !tiltShift.HasColorBoost();
}
inline bool IsOffRequest(const Bloom& bloom) { return bloom.IsOff(); }
inline bool IsOffRequest(const DepthOfField& depthOfField) { return depthOfField.IsOff(); }
inline bool IsOffRequest(const Shade& shade) { return shade.IsOff(); }
inline bool IsOffRequest(const Sun& sun) { return sun.IsOff(); }

template <class T>
class LuaEffectRequests
{
public:
    // what the scripts want now, or none
    std::optional<T> Current() const
    {
        if (Requests.empty())
            return std::nullopt;
        return Requests.back().second;
    }

    // returns what the scripts want now
    std::optional<T> OnScriptRequest(int scriptId, const T& request)
    {
        if (!StoppedScripts.contains(scriptId))
        {
            // removed first, so it moves to the end as the most recent request
            Remove(scriptId);
            if (!IsOffRequest(request))
                Requests.emplace_back(scriptId, request);
        }
        return Current();
    }

    std::optional<T> OnScriptStopped(int scriptId)
    {
        StoppedScripts.insert(scriptId);
        Remove(scriptId);
        return Current();
    }

    void OnScriptStarted(int scriptId) { StoppedScripts.erase(scriptId); }

    std::optional<T> OnSessionEnded()
    {
        Requests.clear();
        StoppedScripts.clear();
        return Current();
    }

private:
    // ordered from oldest to most recent request, one per script
    std::vector<std::pair<int, T>> Requests;
    // a request still on its way from a stopped script is ignored
    std::set<int> StoppedScripts;

    void Remove(int scriptId)
    {
        Requests.erase(std::remove_if(Requests.begin(), Requests.end(), [&](const auto& entry) { return entry.first == scriptId; }),
            Requests.end());
    }
};

using LuaTiltShiftRequests = LuaEffectRequests<TiltShift>;
using LuaBloomRequests = LuaEffectRequests<Bloom>;
using LuaDepthOfFieldRequests = LuaEffectRequests<DepthOfField>;
using LuaShadeRequests = LuaEffectRequests<Shade>;
using LuaSunRequests = LuaEffectRequests<Sun>;

}
