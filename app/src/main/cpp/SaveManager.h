/*
    Copyright 2016-2022 melonDS team

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

#ifndef SAVEMANAGER_H
#define SAVEMANAGER_H

#include <atomic>
#include <memory>
#include <string>
#include <unistd.h>
#include <time.h>

#include "types.h"
#include "Platform.h"

using namespace melonDS;
using namespace melonDS::Platform;

class SaveManager
{
public:
    SaveManager(std::string path);
    ~SaveManager();

    std::string GetPath();
    void SetPath(std::string path, bool reload);

    void RequestFlush(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen);
    void CheckFlush();

    // Takes the save image from a loaded savestate as the in-memory copy without writing it to disk; the game's next save writes it out. A pending
    // in-game save is written first so the load can't drop it.
    void SyncFromState(const u8* savedata, u32 savelen);

    // Returns false if the data could not be flushed (a failed file flush leaves it pending to be retried), true if there was nothing to flush.
    bool FlushSecondaryBuffer();

private:

    void run();

    std::string Path;

    std::atomic_bool Running;

    std::unique_ptr<u8[]> Buffer;
    u32 Length;
    bool FlushRequested;

    Platform::Thread* Thread;
    Platform::Mutex* SecondaryBufferLock;
    std::unique_ptr<u8[]> SecondaryBuffer;
    u32 SecondaryBufferLength;

    // Serializes FlushSecondaryBuffer() calls, kept apart from SecondaryBufferLock so CheckFlush() never has to wait for file I/O.
    Platform::Mutex* FlushLock;
    // Snapshot of SecondaryBuffer written to the file outside of SecondaryBufferLock, only touched while holding FlushLock.
    std::unique_ptr<u8[]> WriteBuffer;
    u32 WriteBufferLength;

    time_t TimeAtLastFlushRequest;

    // We keep versions in case the user closes the application before
    // a flush cycle is finished.
    u32 PreviousFlushVersion;
    u32 FlushVersion;
};

#endif // SAVEMANAGER_H
