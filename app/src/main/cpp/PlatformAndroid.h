#ifndef PLATFORMANDROID_H
#define PLATFORMANDROID_H

#include <string>
#include <sys/types.h>
#include "Platform.h"

namespace melonDS
{
namespace Platform
{

melonDS::Platform::FileHandle* OpenInternalFile(const std::string path, FileMode mode);

/**
 * The kernel thread ID of a thread made with Thread_Create, or 0 if it hasn't started yet.
 */
pid_t GetThreadTid(Thread* thread);

}
}

#endif
