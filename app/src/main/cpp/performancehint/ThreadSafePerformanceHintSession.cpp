#include "ThreadSafePerformanceHintSession.h"

ThreadSafePerformanceHintSession::ThreadSafePerformanceHintSession(std::unique_ptr<PerformanceHintManager> manager) : manager(std::move(manager))
{
}

void ThreadSafePerformanceHintSession::createSession(pid_t threadId, int64_t targetDurationNs)
{
    std::lock_guard<std::mutex> lock(sessionMutex);
    mainThreadId = threadId;
    this->targetDurationNs = targetDurationNs;
    manager->createSession(getSessionThreadIds(), targetDurationNs);
    sessionActive = true;
}

void ThreadSafePerformanceHintSession::setHelperThreads(const std::vector<pid_t>& helperThreadIds)
{
    std::lock_guard<std::mutex> lock(sessionMutex);
    if (helperThreadIds == this->helperThreadIds)
        return;

    this->helperThreadIds = helperThreadIds;
    if (!sessionActive)
        return;

    manager->destroySession();
    if (!manager->createSession(getSessionThreadIds(), targetDurationNs) && !this->helperThreadIds.empty())
    {
        // Keep the emulator thread's own session if the system won't take the helpers
        manager->createSession({ mainThreadId }, targetDurationNs);
    }
}

void ThreadSafePerformanceHintSession::reportActualWorkDuration(int64_t actualDurationNs)
{
    std::lock_guard<std::mutex> lock(sessionMutex);
    if (!sessionActive)
        return;

    manager->reportActualWorkDuration(actualDurationNs);
}

void ThreadSafePerformanceHintSession::updateTargetWorkDuration(int64_t targetDurationNs)
{
    std::lock_guard<std::mutex> lock(sessionMutex);
    // Kept for when the session is created again with other threads
    this->targetDurationNs = targetDurationNs;
    if (!sessionActive)
        return;

    manager->updateTargetWorkDuration(targetDurationNs);
}

void ThreadSafePerformanceHintSession::destroySession()
{
    std::lock_guard<std::mutex> lock(sessionMutex);
    manager->destroySession();
    sessionActive = false;
}

std::vector<pid_t> ThreadSafePerformanceHintSession::getSessionThreadIds() const
{
    std::vector<pid_t> threadIds { mainThreadId };
    threadIds.insert(threadIds.end(), helperThreadIds.begin(), helperThreadIds.end());
    return threadIds;
}
