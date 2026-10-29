#ifndef MELONDS_ANDROID_DUMMYPERFORMANCEHINTMANAGER_H
#define MELONDS_ANDROID_DUMMYPERFORMANCEHINTMANAGER_H

#include "PerformanceHintManager.h"

class DummyPerformanceHintManager : public PerformanceHintManager
{
public:
    bool createSession(const std::vector<pid_t>& threadIds, int64_t targetDurationNs) override { return true; }
    void destroySession() override { }
    void reportActualWorkDuration(int64_t actualDurationNs) override { }
    void updateTargetWorkDuration(int64_t targetDurationNs) override { }
};

#endif
