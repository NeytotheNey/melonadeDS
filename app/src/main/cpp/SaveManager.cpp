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

#include "SaveManager.h"
#include "Platform.h"

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
        FlushSecondaryBuffer();
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
    for (;;)
    {
        Platform::Sleep(100000);

        if (!Running) return;

        // We debounce for two seconds after last flush request to ensure that writing has finished.
        if (TimeAtLastFlushRequest == 0 || difftime(time(nullptr), TimeAtLastFlushRequest) < 2)
        {
            continue;
        }

        FlushSecondaryBuffer();
    }
}

void SaveManager::FlushSecondaryBuffer(u8* dst, u32 dstLength)
{
    if (!SecondaryBuffer) return;

    // When flushing to a file, there's no point in re-writing the exact same data.
    if (!dst && !NeedsFlush()) return;
    // When flushing to memory, we don't know if dst already has any data so we only check that we CAN flush.
    if (dst && dstLength < SecondaryBufferLength) return;

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
            FileWrite(WriteBuffer.get(), WriteBufferLength, 1, f);
            Log(LogLevel::Info, "SaveManager: Wrote %u bytes to %s\n", WriteBufferLength, Path.c_str());
            CloseFile(f);
        }

        // Only mark the version we wrote as flushed. If a flush was requested during the write,
        // leave its timestamp alone so that it still gets its own debounced write.
        Platform::Mutex_Lock(SecondaryBufferLock);
        PreviousFlushVersion = version;
        if (FlushVersion == version)
            TimeAtLastFlushRequest = 0;
        Platform::Mutex_Unlock(SecondaryBufferLock);
    }
    Platform::Mutex_Unlock(FlushLock);
}

bool SaveManager::NeedsFlush()
{
    return FlushVersion != PreviousFlushVersion;
}
