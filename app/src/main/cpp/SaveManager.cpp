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

#include <stdio.h>
#include <string.h>
#include <algorithm>

#include "SaveManager.h"
#include "Platform.h"

// Retry backoff after a failed flush, in worker ticks (100ms each): 1, 2, 4, ... up to the cap.
static const u32 MAX_RETRY_DELAY_TICKS = 32;
// A flush that keeps failing is logged on the first attempt and then every this many attempts.
static const u32 FLUSH_FAILURE_LOG_INTERVAL = 10;

SaveManager::SaveManager(std::string path)
{
    SecondaryBuffer = nullptr;
    SecondaryBufferLength = 0;
    SecondaryBufferLock = Platform::Mutex_Create();

    FlushLock = Platform::Mutex_Create();
    WriteBuffer = nullptr;
    WriteBufferLength = 0;

    Thread = nullptr;
    Running = false;

    Path = path;

    Buffer = nullptr;
    Length = 0;
    FlushRequested = false;

    FlushVersion = 0;
    PreviousFlushVersion = 0;
    TimeAtLastFlushRequest = 0;

    if (!path.empty())
    {
        Running = true;
        Thread = Platform::Thread_Create(std::bind(&SaveManager::run, this));
    }
}

SaveManager::~SaveManager()
{
    if (Running)
    {
        Running = false;
        Platform::Thread_Wait(Thread);
        if (!FlushSecondaryBuffer())
            Log(LogLevel::Error, "SaveManager: Final flush to %s failed, save data was not written\n", Path.c_str());
    }

    SecondaryBuffer = nullptr;
    WriteBuffer = nullptr;

    Platform::Mutex_Free(FlushLock);
    Platform::Mutex_Free(SecondaryBufferLock);
    if (Thread)
        Platform::Thread_Free(Thread);

    Buffer = nullptr;
}

std::string SaveManager::GetPath()
{
    return Path;
}

void SaveManager::SetPath(std::string path, bool reload)
{
    Path = path;

    if (reload)
    {
        FileHandle* f = Platform::OpenFile(Path, FileMode::Read);
        if (f)
        {
            if (u32 length = Platform::FileLength(f); length != Length)
            { // If the new file is a different size, we need to re-allocate the buffer.
                Length = length;
                Buffer = std::make_unique<u8[]>(Length);
            }

            FileRead(Buffer.get(), 1, Length, f);
            CloseFile(f);
        }
    }
    else
        FlushRequested = true;
}

void SaveManager::RequestFlush(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen)
{
    if (Length != savelen)
    {
        Length = savelen;
        Buffer = std::make_unique<u8[]>(Length);

        memcpy(Buffer.get(), savedata, Length);
    }
    else
    {
        if ((writeoffset+writelen) > savelen)
        {
            u32 len = savelen - writeoffset;
            memcpy(&Buffer[writeoffset], &savedata[writeoffset], len);
            len = writelen - len;
            if (len > savelen) len = savelen;
            memcpy(&Buffer[0], &savedata[0], len);
        }
        else
        {
            memcpy(&Buffer[writeoffset], &savedata[writeoffset], writelen);
        }
    }

    FlushRequested = true;
}

void SaveManager::CheckFlush()
{
    if (!FlushRequested) return;

    Platform::Mutex_Lock(SecondaryBufferLock);

    Log(LogLevel::Info, "SaveManager: Flush requested\n");

    if (SecondaryBufferLength != Length)
    {
        SecondaryBufferLength = Length;
        SecondaryBuffer = std::make_unique<u8[]>(SecondaryBufferLength);
    }

    memcpy(SecondaryBuffer.get(), Buffer.get(), Length);

    FlushRequested = false;
    FlushVersion++;
    TimeAtLastFlushRequest = time(nullptr);

    Platform::Mutex_Unlock(SecondaryBufferLock);
}

void SaveManager::SyncFromState(const u8* savedata, u32 savelen)
{
    // Get a pending in-game save onto disk now, before the state's image replaces it in Buffer. If this write fails the data stays pending in
    // SecondaryBuffer, and the writer thread retries it.
    if (Running)
    {
        CheckFlush();

        // The game may still be partway through a multi-write save if its last write is under the 2 s debounce; it's written anyway
        Platform::Mutex_Lock(SecondaryBufferLock);
        bool pending = FlushVersion != PreviousFlushVersion;
        time_t timeAtLastFlushRequest = TimeAtLastFlushRequest;
        Platform::Mutex_Unlock(SecondaryBufferLock);
        if (pending && timeAtLastFlushRequest != 0 && difftime(time(nullptr), timeAtLastFlushRequest) < 2)
            Log(LogLevel::Warn, "SaveManager: Writing a save to %s that's under 2 s old before a state load, the game may still be saving\n", Path.c_str());

        if (!FlushSecondaryBuffer())
            Log(LogLevel::Error, "SaveManager: Failed to write the pending save to %s before a state load, will retry\n", Path.c_str());
    }

    // Take the whole image, not just a range: the game's next partial write then lands on the state's data instead of mixing it with the old
    // file's blocks
    if (Length != savelen)
    {
        Length = savelen;
        Buffer = Length ? std::make_unique<u8[]>(Length) : nullptr;
    }
    if (Length)
        memcpy(Buffer.get(), savedata, Length);

    // No FlushRequested here, so nothing reaches the disk until the game saves
}

void SaveManager::run()
{
    u32 failedAttempts = 0;
    u32 retryDelayTicks = 0;
    u32 ticksUntilRetry = 0;

    for (;;)
    {
        Platform::Sleep(100000);

        if (!Running) return;

        // Skip ticks instead of sleeping longer, so that shutdown isn't delayed by the backoff.
        if (ticksUntilRetry > 0)
        {
            ticksUntilRetry--;
            continue;
        }

        // CheckFlush() (on the emulator thread) and FlushSecondaryBuffer() change it under the lock.
        Platform::Mutex_Lock(SecondaryBufferLock);
        time_t timeAtLastFlushRequest = TimeAtLastFlushRequest;
        Platform::Mutex_Unlock(SecondaryBufferLock);

        // We debounce for two seconds after last flush request to ensure that writing has finished.
        if (timeAtLastFlushRequest == 0 || difftime(time(nullptr), timeAtLastFlushRequest) < 2)
        {
            continue;
        }

        if (FlushSecondaryBuffer())
        {
            if (failedAttempts > 0)
                Log(LogLevel::Info, "SaveManager: Flush to %s succeeded after %u failed attempts\n", Path.c_str(), failedAttempts);

            failedAttempts = 0;
            retryDelayTicks = 0;
        }
        else
        {
            // The data is still pending, so it gets retried once the backoff runs out.
            failedAttempts++;
            retryDelayTicks = retryDelayTicks ? std::min(retryDelayTicks * 2, MAX_RETRY_DELAY_TICKS) : 1;
            ticksUntilRetry = retryDelayTicks;

            if (failedAttempts == 1 || (failedAttempts % FLUSH_FAILURE_LOG_INTERVAL) == 0)
                Log(LogLevel::Error, "SaveManager: Failed to write save to %s (%u attempts), will retry\n", Path.c_str(), failedAttempts);
        }
    }
}

bool SaveManager::FlushSecondaryBuffer()
{
    bool flushed = true;

    Platform::Mutex_Lock(FlushLock);
    // Only hold SecondaryBufferLock long enough to snapshot the data, since holding it during the file write would stall the emulator thread in
    // CheckFlush() on slow storage.
    Platform::Mutex_Lock(SecondaryBufferLock);

    // There's no point in re-writing the exact same data.
    if (!SecondaryBuffer || FlushVersion == PreviousFlushVersion)
    {
        Platform::Mutex_Unlock(SecondaryBufferLock);
        Platform::Mutex_Unlock(FlushLock);
        return true;
    }

    if (WriteBufferLength != SecondaryBufferLength)
    {
        WriteBufferLength = SecondaryBufferLength;
        WriteBuffer = std::make_unique<u8[]>(WriteBufferLength);
    }

    memcpy(WriteBuffer.get(), SecondaryBuffer.get(), WriteBufferLength);
    u32 version = FlushVersion;
    Platform::Mutex_Unlock(SecondaryBufferLock);

    FileHandle* f = Platform::OpenFile(Path, FileMode::Write);
    if (f)
    {
        bool written = FileWrite(WriteBuffer.get(), WriteBufferLength, 1, f) == 1;
        // Always close the file, even after a short write, since a failed close can also mean lost data.
        bool closed = CloseFile(f);
        flushed = written && closed;
        if (flushed)
            Log(LogLevel::Info, "SaveManager: Wrote %u bytes to %s\n", WriteBufferLength, Path.c_str());
    }
    else
    {
        flushed = false;
    }

    // Only mark the version we wrote as flushed, so a flush requested during the write still gets its own debounced write and a failed write's data
    // stays pending for a retry.
    if (flushed)
    {
        Platform::Mutex_Lock(SecondaryBufferLock);
        PreviousFlushVersion = version;
        if (FlushVersion == version)
            TimeAtLastFlushRequest = 0;
        Platform::Mutex_Unlock(SecondaryBufferLock);
    }
    Platform::Mutex_Unlock(FlushLock);
    return flushed;
}
