#ifndef MELONDS_ANDROID_THREADSAFEPERFORMANCEHINTSESSION_H
#define MELONDS_ANDROID_THREADSAFEPERFORMANCEHINTSESSION_H

#include <memory>
#include <mutex>
#include <vector>
#include "PerformanceHintManager.h"

class ThreadSafePerformanceHintSession
{
public:
    explicit ThreadSafePerformanceHintSession(std::unique_ptr<PerformanceHintManager> manager);
    void createSession(pid_t threadId, int64_t targetDurationNs);
    /**
     * Sets the threads that work for the session's thread (the core's 2D render thread, say) so they get the same boost, by creating the session
     * again, since changing a session's threads needs API 34
     */
    void setHelperThreads(const std::vector<pid_t>& helperThreadIds);
    void reportActualWorkDuration(int64_t actualDurationNs);
    void updateTargetWorkDuration(int64_t targetDurationNs);
    void destroySession();

private:
    std::vector<pid_t> getSessionThreadIds() const;

    std::unique_ptr<PerformanceHintManager> manager;
    std::mutex sessionMutex;
    bool sessionActive = false;
    pid_t mainThreadId = 0;
    std::vector<pid_t> helperThreadIds;
    int64_t targetDurationNs = 0;
};

#endif
