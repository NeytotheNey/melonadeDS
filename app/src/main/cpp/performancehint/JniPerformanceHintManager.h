#ifndef MELONDS_ANDROID_JNIPERFORMANCEHINTMANAGER_H
#define MELONDS_ANDROID_JNIPERFORMANCEHINTMANAGER_H

#include <jni.h>
#include "PerformanceHintManager.h"
#include "../JniEnvHandler.h"

class JniPerformanceHintManager : public PerformanceHintManager
{
public:
    explicit JniPerformanceHintManager(JniEnvHandler* jniEnvHandler);
    bool createSession(const std::vector<pid_t>& threadIds, int64_t targetDurationNs) override;
    void destroySession() override;
    void reportActualWorkDuration(int64_t actualDurationNs) override;
    void updateTargetWorkDuration(int64_t targetDurationNs) override;

private:
    JniEnvHandler* jniEnvHandler;
    jobject hintSession;
    jmethodID reportMethod;
    jmethodID updateTargetMethod;
    jmethodID closeMethod;
};

#endif
