// The parts of the core's Platform layer the desktop frontend and tools/jitverify share: files, the log, threads and sync, from the
// headless swi-harness (melonDS-android-lib/tools/swi-harness/main.cpp). The rest is each one's own: the save hooks and stubs in
// Platform.cpp (jitverify), the desktop's in DesktopPlatform.cpp
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#ifndef _WIN32
#include <dlfcn.h>
#endif

#include "Platform.h"
#include "Frontend.h"

using namespace melonDS;

namespace Frontend
{

FILE* LogFile = nullptr;
bool VerboseLog = false;

}

namespace melonDS::Platform
{

static std::string ModeString(FileMode mode, bool exists)
{
    std::string str;
    if (mode & FileMode::Append)
        str = (mode & FileMode::Read) ? "a+" : "a";
    else if (mode & FileMode::Write)
    {
        if ((mode & FileMode::Preserve) && exists)
            str = "r+";
        else
            str = (mode & FileMode::Read) ? "w+" : "w";
    }
    else
        str = "r";

    if (!(mode & FileMode::Text))
        str += 'b';
    return str;
}

std::string GetLocalFilePath(const std::string& filename)
{
    return filename;
}

FileHandle* OpenFile(const std::string& path, FileMode mode)
{
    if ((mode & (FileMode::ReadWrite | FileMode::Append)) == FileMode::None)
        return nullptr;

    bool exists = FileExists(path);
    if ((mode & FileMode::NoCreate) && !exists && (mode & FileMode::Write))
        return nullptr;

    FILE* f = fopen(path.c_str(), ModeString(mode, exists).c_str());
    return reinterpret_cast<FileHandle*>(f);
}

FileHandle* OpenLocalFile(const std::string& path, FileMode mode)
{
    return OpenFile(GetLocalFilePath(path), mode);
}

bool FileExists(const std::string& name)
{
    FILE* f = fopen(name.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

bool LocalFileExists(const std::string& name)
{
    return FileExists(GetLocalFilePath(name));
}

bool CheckFileWritable(const std::string& filepath)
{
    FILE* f = fopen(filepath.c_str(), "ab");
    if (!f) return false;
    fclose(f);
    return true;
}

bool CheckLocalFileWritable(const std::string& filepath)
{
    return CheckFileWritable(GetLocalFilePath(filepath));
}

bool CloseFile(FileHandle* file)
{
    return fclose(reinterpret_cast<FILE*>(file)) == 0;
}

bool IsEndOfFile(FileHandle* file)
{
    return feof(reinterpret_cast<FILE*>(file)) != 0;
}

bool FileReadLine(char* str, int count, FileHandle* file)
{
    return fgets(str, count, reinterpret_cast<FILE*>(file)) != nullptr;
}

u64 FilePosition(FileHandle* file)
{
    return (u64)ftello64(reinterpret_cast<FILE*>(file));
}

bool FileSeek(FileHandle* file, s64 offset, FileSeekOrigin origin)
{
    int stdorigin = SEEK_SET;
    switch (origin)
    {
    case FileSeekOrigin::Start: stdorigin = SEEK_SET; break;
    case FileSeekOrigin::Current: stdorigin = SEEK_CUR; break;
    case FileSeekOrigin::End: stdorigin = SEEK_END; break;
    }
    return fseeko64(reinterpret_cast<FILE*>(file), offset, stdorigin) == 0;
}

void FileRewind(FileHandle* file)
{
    rewind(reinterpret_cast<FILE*>(file));
}

u64 FileRead(void* data, u64 size, u64 count, FileHandle* file)
{
    return fread(data, size, count, reinterpret_cast<FILE*>(file));
}

bool FileFlush(FileHandle* file)
{
    return fflush(reinterpret_cast<FILE*>(file)) == 0;
}

u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* file)
{
    return fwrite(data, size, count, reinterpret_cast<FILE*>(file));
}

u64 FileWriteFormatted(FileHandle* file, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = vfprintf(reinterpret_cast<FILE*>(file), fmt, args);
    va_end(args);
    return ret < 0 ? 0 : (u64)ret;
}

u64 FileLength(FileHandle* file)
{
    FILE* f = reinterpret_cast<FILE*>(file);
    s64 pos = ftello64(f);
    fseeko64(f, 0, SEEK_END);
    s64 len = ftello64(f);
    fseeko64(f, pos, SEEK_SET);
    return (u64)len;
}

void Log(LogLevel level, const char* fmt, ...)
{
    // everything goes to the log file, debug lines only to the console with --verbose
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    va_list args;
    if (Frontend::LogFile)
    {
        va_start(args, fmt);
        vfprintf(Frontend::LogFile, fmt, args);
        va_end(args);
        fflush(Frontend::LogFile);
    }
    if (level == LogLevel::Debug && !Frontend::VerboseLog)
        return;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

struct Thread
{
    std::thread Handle;
};

Thread* Thread_Create(std::function<void()> func, const char* name)
{
    Thread* thread = new Thread();
    thread->Handle = std::thread(std::move(func));
    return thread;
}

void Thread_Free(Thread* thread)
{
    if (thread->Handle.joinable()) thread->Handle.detach();
    delete thread;
}

void Thread_Wait(Thread* thread)
{
    if (thread->Handle.joinable()) thread->Handle.join();
}

struct Semaphore
{
    std::mutex Lock;
    std::condition_variable Cond;
    int Count = 0;
};

Semaphore* Semaphore_Create()
{
    return new Semaphore();
}

void Semaphore_Free(Semaphore* sema)
{
    delete sema;
}

void Semaphore_Reset(Semaphore* sema)
{
    std::lock_guard<std::mutex> lock(sema->Lock);
    sema->Count = 0;
}

void Semaphore_Wait(Semaphore* sema)
{
    std::unique_lock<std::mutex> lock(sema->Lock);
    sema->Cond.wait(lock, [sema]() { return sema->Count > 0; });
    sema->Count--;
}

bool Semaphore_TryWait(Semaphore* sema, int timeout_ms)
{
    std::unique_lock<std::mutex> lock(sema->Lock);
    if (timeout_ms == 0)
    {
        if (sema->Count <= 0) return false;
    }
    else if (!sema->Cond.wait_for(lock, std::chrono::milliseconds(timeout_ms), [sema]() { return sema->Count > 0; }))
        return false;
    sema->Count--;
    return true;
}

void Semaphore_Post(Semaphore* sema, int count)
{
    {
        std::lock_guard<std::mutex> lock(sema->Lock);
        sema->Count += count;
    }
    sema->Cond.notify_all();
}

struct Mutex
{
    std::mutex Lock;
};

Mutex* Mutex_Create()
{
    return new Mutex();
}

void Mutex_Free(Mutex* mutex)
{
    delete mutex;
}

void Mutex_Lock(Mutex* mutex)
{
    mutex->Lock.lock();
}

void Mutex_Unlock(Mutex* mutex)
{
    mutex->Lock.unlock();
}

bool Mutex_TryLock(Mutex* mutex)
{
    return mutex->Lock.try_lock();
}

void Sleep(u64 usecs)
{
    std::this_thread::sleep_for(std::chrono::microseconds(usecs));
}

u64 GetMSCount()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

u64 GetUSCount()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

#ifdef _WIN32
DynamicLibrary* DynamicLibrary_Load(const char* lib) { return nullptr; }
void DynamicLibrary_Unload(DynamicLibrary* lib) {}
void* DynamicLibrary_LoadFunction(DynamicLibrary* lib, const char* name) { return nullptr; }
#else
// tools/jitverify on Android: the JIT's fast memory gets ASharedMemory_create from libandroid.so
DynamicLibrary* DynamicLibrary_Load(const char* lib) { return reinterpret_cast<DynamicLibrary*>(dlopen(lib, RTLD_NOW | RTLD_LOCAL)); }
void DynamicLibrary_Unload(DynamicLibrary* lib) { if (lib) dlclose(lib); }
void* DynamicLibrary_LoadFunction(DynamicLibrary* lib, const char* name) { return lib ? dlsym(lib, name) : nullptr; }
#endif


}
