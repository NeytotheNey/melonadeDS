#include "PgoDump.h"

#ifdef MELONADE_PGO_GENERATE

#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <unistd.h>
#include "MelonDS.h"
#include "MelonLog.h"

extern "C" {
// The profile runtime's interface (clang's profile/instr_prof_interface.h), linked in by -fprofile-generate
void __llvm_profile_set_filename(const char* name);
int __llvm_profile_write_file(void);
void __llvm_profile_reset_counters(void);
}

namespace
{
    std::mutex dumpMutex;
    std::string dumpPrefix;
    int dumpIndex = 0;

    std::string currentPath;

    /**
     * Points the runtime at the next unused file. The runtime copies the name (compiler-rt's parseAndSetFilename with copy on; the header's "not
     * copied" note is out of date), and currentPath keeps it alive until the next call anyway.
     */
    void setNextFilename()
    {
        currentPath = dumpPrefix + std::to_string(dumpIndex) + ".profraw";
        __llvm_profile_set_filename(currentPath.c_str());
    }

    /**
     * Files are <dir>/pgo/<pid>-<process start time>-<dump index>.profraw, so a later session that reuses the pid doesn't overwrite them.
     */
    void init(const std::string& filesDir)
    {
        std::lock_guard<std::mutex> lock(dumpMutex);
        if (!dumpPrefix.empty())
            return;

        std::filesystem::path dir = std::filesystem::path(filesDir) / "pgo";
        std::error_code error;
        std::filesystem::create_directories(dir, error);
        if (error)
            LOG_WARN(LOG_TAG_DEBUG, "PGO: couldn't create %s: %s", dir.c_str(), error.message().c_str());

        dumpPrefix = (dir / (std::to_string(getpid()) + "-" + std::to_string((long long) time(nullptr)) + "-")).string();
        setNextFilename();
        LOG_INFO(LOG_TAG_DEBUG, "PGO: instrumented build, profiles go to %s*.profraw", dumpPrefix.c_str());
    }

    /**
     * Writes the counters since the last dump to the current file, then resets them and moves on to the next file (which is also where the
     * runtime's own atexit dump would go, if the process ever exits normally). Each dump resets the counters, so llvm-profdata merge adds the files
     * up without counting anything twice.
     */
    void dump(const char* reason)
    {
        std::lock_guard<std::mutex> lock(dumpMutex);
        if (dumpPrefix.empty())
            return;

        std::string path = currentPath;
        // The runtime appends ("ab"), so a retry after a partial write would add to a broken file; start it fresh
        unlink(path.c_str());
        if (__llvm_profile_write_file() != 0)
        {
            // The counters are kept, so the next dump retries the same file with everything since the last good one
            LOG_WARN(LOG_TAG_DEBUG, "PGO: profile dump on %s to %s failed", reason, path.c_str());
            return;
        }

        __llvm_profile_reset_counters();
        dumpIndex++;
        setNextFilename();
        LOG_INFO(LOG_TAG_DEBUG, "PGO: dumped profile on %s to %s", reason, path.c_str());
    }
}

void PgoDump::onSetup(JNIEnv* env, jobject emulatorConfiguration)
{
    std::string dumpDir = MelonDSAndroid::frontendContext.internalFilesDir;
    jclass configurationClass = env->GetObjectClass(emulatorConfiguration);
    jfieldID pgoDirField = env->GetFieldID(configurationClass, "pgoDumpDirectory", "Ljava/lang/String;");
    if (!pgoDirField)
    {
        // A NoSuchFieldError is pending (the Kotlin field was renamed or stripped); clear it and keep the internal dir
        env->ExceptionClear();
        LOG_WARN(LOG_TAG_DEBUG, "PGO: EmulatorConfiguration has no pgoDumpDirectory, using the internal files dir");
    }
    else if (jstring pgoDir = (jstring) env->GetObjectField(emulatorConfiguration, pgoDirField))
    {
        const char* chars = env->GetStringUTFChars(pgoDir, nullptr);
        dumpDir = chars;
        env->ReleaseStringUTFChars(pgoDir, chars);
        env->DeleteLocalRef(pgoDir);
    }
    env->DeleteLocalRef(configurationClass);
    init(dumpDir);
}

void PgoDump::onPause()
{
    dump("pause");
}

void PgoDump::onStop()
{
    dump("stop");
}

#else

void PgoDump::onSetup(JNIEnv*, jobject)
{
}

void PgoDump::onPause()
{
}

void PgoDump::onStop()
{
}

#endif
