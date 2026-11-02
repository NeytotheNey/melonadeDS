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

#ifndef REWINDMANAGER_H
#define REWINDMANAGER_H

#include <list>
#include "types.h"

namespace melonDS
{

struct RewindSaveState {
    u8* buffer;
    u32 bufferSize;
    u32 bufferContentSize;
    u8* screenshot;
    u32 screenshotSize;
    int frame;
};

typedef struct {
    int currentFrame;
    std::list<melonDS::RewindSaveState> rewindStates;
} RewindWindow;

class RewindManager
{
public:
    RewindManager(bool enabled, int rewindLengthSeconds, int rewindCapturingIntervalSeconds, size_t rewindBufferSize, size_t screenshotBufferSize);
    ~RewindManager();
    void UpdateRewindSettings(bool enabled, int rewindLengthSeconds, int rewindCapturingIntervalSeconds);
    bool ShouldCaptureState(int currentFrame);
    RewindSaveState* GetNextRewindSaveState(int currentFrame);
    /**
     * Emulator thread, once per frame: while the window isn't full, allocates the next capture's state ahead and writes its buffer a slice per frame
     * (up to a bit more than the biggest state so far), so the capture doesn't page-fault a fresh buffer on its frame
     */
    void PrepareNextRewindSaveState();
    // The window's state for [frame], or null
    RewindSaveState* FindRewindSaveState(int frame);
    std::list<RewindSaveState> GetRewindWindow();
    void OnRewindFromState(RewindSaveState state);
    void Reset();

private:
    RewindSaveState CreateRewindSaveState(int frame);
    void TrimRewindWindowIfRequired();
    void DeleteRewindSaveState(RewindSaveState state);
    int RewindWindowSize() { return RewindLengthSeconds / RewindCapturingIntervalSeconds; };

private:
    bool Enabled;
    std::list<RewindSaveState> RewindWindow = std::list<RewindSaveState>();
    // The state that PrepareNextRewindSaveState() gets ready (its buffer is null when there's none), and how much of its buffer is written
    RewindSaveState SpareState = {};
    u32 SpareStatePreparedSize = 0;
    // The biggest state saved so far (0 before the first), which bounds how much of the next one's buffer is prepared
    u32 LargestSavestateSize = 0;
    int RewindLengthSeconds;
    int RewindCapturingIntervalSeconds;
    u32 SavestateBufferSize;
    u32 ScreenshotBufferSize;
};

}

#endif // REWINDMANAGER_H
