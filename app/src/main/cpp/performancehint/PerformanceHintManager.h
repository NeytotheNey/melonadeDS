#ifndef MELONDS_ANDROID_PERFORMANCEHINTMANAGER_H
#define MELONDS_ANDROID_PERFORMANCEHINTMANAGER_H

#include <cstdint>
#include <sys/types.h>
#include <vector>

class PerformanceHintManager
{
public:
    virtual ~PerformanceHintManager() = default;
    // Returns whether the session was created
    virtual bool createSession(const std::vector<pid_t>& threadIds, int64_t targetDurationNs) = 0;
    virtual void destroySession() = 0;
    virtual void reportActualWorkDuration(int64_t actualDurationNs) = 0;
    virtual void updateTargetWorkDuration(int64_t targetDurationNs) = 0;
};

#endif
