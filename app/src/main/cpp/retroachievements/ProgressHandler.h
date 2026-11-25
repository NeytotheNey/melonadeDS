#ifndef PROGRESSHANDLER_H
#define PROGRESSHANDLER_H

#include <cstddef>
#include <cstdint>

namespace MelonDSAndroid
{
namespace RetroAchievements
{

/**
 * Another owner of the achievements' progress (the desktop's rc_client), which the RCHV savestate section holds in place of the
 * RetroAchievementsManager's rc_runtime once it's set (RetroAchievementsManager::SetProgressHandler): same section, same format
 * (rc_client's progress is rc_runtime's). Called on the emulator thread with the manager's lock held
 */
class ProgressHandler
{
public:
    virtual ~ProgressHandler() = default;
    // the bytes SerializeProgress() writes; 0 when there's nothing to save (no game loaded), and the manager's own empty progress is saved
    virtual size_t ProgressSize() = 0;
    virtual bool SerializeProgress(uint8_t* buffer, size_t size) = 0;
    // false when [buffer] can't be taken (the manager resets the progress then); a null or empty one is a reset
    virtual bool DeserializeProgress(const uint8_t* buffer, size_t size) = 0;
    virtual void ResetProgress() = 0;
};

}
}

#endif //PROGRESSHANDLER_H
