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
static const u32 MaxRetryDelayTicks = 32;
// A flush that keeps failing is logged on the first attempt and then every this many attempts.
static const u32 FlushFailureLogInterval = 10;

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

        // We debounce for two seconds after last flush request to ensure that writing has finished.
        if (TimeAtLastFlushRequest == 0 || difftime(time(nullptr), TimeAtLastFlushRequest) < 2)
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
            retryDelayTicks = retryDelayTicks ? std::min(retryDelayTicks * 2, MaxRetryDelayTicks) : 1;
            ticksUntilRetry = retryDelayTicks;

            if (failedAttempts == 1 || (failedAttempts % FlushFailureLogInterval) == 0)
                Log(LogLevel::Error, "SaveManager: Failed to write save to %s (%u attempts), will retry\n", Path.c_str(), failedAttempts);
        }
    }
}

bool SaveManager::FlushSecondaryBuffer(u8* dst, u32 dstLength)
{
    if (!SecondaryBuffer) return dst == nullptr;

    // When flushing to a file, there's no point in re-writing the exact same data.
    if (!dst && !NeedsFlush()) return true;
    // When flushing to memory, we don't know if dst already has any data so we only check that we CAN flush.
    if (dst && dstLength < SecondaryBufferLength) return false;

    bool flushed = true;

    Platform::Mutex_Lock(FlushLock);
    if (dst)
    {
        Platform::Mutex_Lock(SecondaryBufferLock);
        memcpy(dst, SecondaryBuffer.get(), SecondaryBufferLength);
        PreviousFlushVersion = FlushVersion;
        TimeAtLastFlushRequest = 0;
        Platform::Mutex_Unlock(SecondaryBufferLock);
    }
    else
    {
        // Only hold SecondaryBufferLock long enough to snapshot the data. Holding it during
        // the file write would stall the emulator thread in CheckFlush() on slow storage.
        Platform::Mutex_Lock(SecondaryBufferLock);
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
            // Always close the file, even after a short write. A failed close can also mean lost data.
            bool closed = CloseFile(f);
            flushed = written && closed;
            if (flushed)
                Log(LogLevel::Info, "SaveManager: Wrote %u bytes to %s\n", WriteBufferLength, Path.c_str());
        }
        else
        {
            flushed = false;
        }

        // Only mark the version we wrote as flushed. If a flush was requested during the write,
        // leave its timestamp alone so that it still gets its own debounced write. If the write
        // failed, leave everything as is so that the data stays pending and gets retried.
        if (flushed)
        {
            Platform::Mutex_Lock(SecondaryBufferLock);
            PreviousFlushVersion = version;
            if (FlushVersion == version)
                TimeAtLastFlushRequest = 0;
            Platform::Mutex_Unlock(SecondaryBufferLock);
        }
    }
    Platform::Mutex_Unlock(FlushLock);
    return flushed;
}

bool SaveManager::NeedsFlush()
{
    return FlushVersion != PreviousFlushVersion;
}
