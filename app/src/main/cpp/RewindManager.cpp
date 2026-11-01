/*
    Copyright 2016-2025 Arisotura

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include "RewindManager.h"
#include <algorithm>
#include <cstring>
#include <list>

namespace melonDS
{

const int FRAMES_PER_SECOND = 60;

RewindManager::RewindManager(bool enabled, int rewindLengthSeconds, int rewindCapturingIntervalSeconds, size_t rewindBufferSize, size_t screenshotBufferSize) :
    Enabled(enabled),
    RewindLengthSeconds(rewindLengthSeconds),
    RewindCapturingIntervalSeconds(rewindCapturingIntervalSeconds),
    SavestateBufferSize(rewindBufferSize),
    ScreenshotBufferSize(screenshotBufferSize)
{
}

RewindManager::~RewindManager()
{
    for (auto state : RewindWindow)
        DeleteRewindSaveState(state);

    if (SpareState.buffer)
        DeleteRewindSaveState(SpareState);
}

void RewindManager::UpdateRewindSettings(bool enabled, int rewindLengthSeconds, int rewindCapturingIntervalSeconds)
{
    RewindLengthSeconds = rewindLengthSeconds;
    RewindCapturingIntervalSeconds = rewindCapturingIntervalSeconds;

    if (Enabled)
    {
        if (!enabled)
            Reset();
        else
            TrimRewindWindowIfRequired();
    }

    Enabled = enabled;
}

bool RewindManager::ShouldCaptureState(int currentFrame)
{
    if (!Enabled || RewindWindowSize() == 0)
    {
        return false;
    }

    return currentFrame % (RewindCapturingIntervalSeconds * FRAMES_PER_SECOND) == 0;
}

RewindSaveState* RewindManager::GetNextRewindSaveState(int currentFrame)
{
    if (RewindWindow.size() < RewindWindowSize())
    {
        // Windows is not yet full. Use the prepared savestate, or create a new one if there's none
        RewindSaveState newRewindSaveState = SpareState.buffer ? SpareState : CreateRewindSaveState(currentFrame);
        newRewindSaveState.frame = currentFrame;
        SpareState = {};
        RewindWindow.push_front(newRewindSaveState);
    }
    else
    {
        // Window is already full. Reuse the oldest savestate and move it to the front
        auto oldestState = RewindWindow.back();
        oldestState.frame = currentFrame;
        RewindWindow.pop_back();
        RewindWindow.push_front(oldestState);
    }

    return &*RewindWindow.begin();
}

void RewindManager::PrepareNextRewindSaveState()
{
    bool isWindowFull = RewindWindow.size() >= RewindWindowSize();
    if (!Enabled || isWindowFull)
    {
        // Not needed until a rewind frees up room in the window, which gives a ready state back anyway
        if (SpareState.buffer) [[unlikely]]
        {
            DeleteRewindSaveState(SpareState);
            SpareState = {};
        }
        return;
    }

    // The newest state is the last one saved
    if (!RewindWindow.empty())
        LargestSavestateSize = std::max(LargestSavestateSize, RewindWindow.front().bufferContentSize);

    if (!SpareState.buffer)
    {
        SpareState = CreateRewindSaveState(0);
        SpareStatePreparedSize = 0;
        memset(SpareState.screenshot, 0, SpareState.screenshotSize);
    }

    // Only as much as a state takes, with some room to grow (a DS game's is much smaller than the buffer, which a DSi game's nearly fills).
    // All of it before the first capture, since the size isn't known yet
    const u32 pageSize = 4096;
    const u32 growthRoom = 1024 * 1024;
    u32 preparedSize = SpareState.bufferSize;
    if (LargestSavestateSize > 0)
        preparedSize = std::min(SpareState.bufferSize, (LargestSavestateSize + growthRoom + pageSize - 1) & ~(pageSize - 1));

    if (SpareStatePreparedSize < preparedSize)
    {
        // Spread over a capture interval, so that it's ready by the next capture. A few pages a frame at the default 10 s
        u32 framesPerCapture = RewindCapturingIntervalSeconds * FRAMES_PER_SECOND;
        u32 sliceSize = (preparedSize / framesPerCapture + pageSize) & ~(pageSize - 1);
        u32 size = std::min(sliceSize, preparedSize - SpareStatePreparedSize);
        memset(SpareState.buffer + SpareStatePreparedSize, 0, size);
        SpareStatePreparedSize += size;
    }
}

RewindSaveState* RewindManager::FindRewindSaveState(int frame)
{
    for (auto& state : RewindWindow)
    {
        if (state.frame == frame)
            return &state;
    }

    return nullptr;
}

std::list<RewindSaveState> RewindManager::GetRewindWindow()
{
    return RewindWindow;
}

void RewindManager::OnRewindFromState(RewindSaveState state)
{
    auto iterator = RewindWindow.begin();
    while ((*iterator).frame > state.frame && iterator != RewindWindow.end())
    {
        RewindSaveState lastState = *iterator;
        // The one with the most of its buffer mapped is kept for the next capture. A saved state has at least what it holds mapped
        if (!SpareState.buffer || lastState.bufferContentSize > SpareStatePreparedSize)
        {
            if (SpareState.buffer)
                DeleteRewindSaveState(SpareState);

            SpareState = lastState;
            SpareStatePreparedSize = lastState.bufferContentSize;
        }
        else
        {
            DeleteRewindSaveState(lastState);
        }
        iterator++;
        RewindWindow.pop_front();
    }
}

RewindSaveState RewindManager::CreateRewindSaveState(int frame)
{
    return RewindSaveState {
        .buffer = new u8[SavestateBufferSize],
        .bufferSize = SavestateBufferSize,
        .bufferContentSize = 0,
        .screenshot = new u8[ScreenshotBufferSize],
        .screenshotSize = ScreenshotBufferSize,
        .frame = frame
    };
}

void RewindManager::TrimRewindWindowIfRequired()
{
    int windowSize = RewindWindowSize();
    while (RewindWindow.size() > windowSize)
    {
        auto lastState = RewindWindow.back();
        DeleteRewindSaveState(lastState);
        RewindWindow.pop_back();
    }
}

void RewindManager::Reset()
{
    for (auto state : RewindWindow)
    {
        DeleteRewindSaveState(state);
    }

    RewindWindow.clear();
}

void RewindManager::DeleteRewindSaveState(RewindSaveState state)
{
    delete[] state.buffer;
    delete[] state.screenshot;
}

}