#ifndef PGODUMP_H
#define PGODUMP_H

#include <jni.h>

// Profile dump hooks for -PnativePgo=generate builds (tools/pgo/README.md). Android kills the app's process rather than letting it exit, so the
// profile runtime's atexit dump never runs; the frontend writes the counters itself when the emulation pauses or stops.
//
// Every build calls these, and they're out of line (PgoDump.cpp, noinline) with empty bodies unless MELONADE_PGO_GENERATE. That way the callers
// (setupEmulator, stopEmulation and the emulator thread's pause path) compile to the same control flow in generate and use builds, so they match
// the profile instead of being dropped as mismatched
namespace PgoDump
{
    /**
     * Once the configuration is set: picks the dump directory, the configuration's pgoDumpDirectory (the external files dir, which adb reads
     * without run-as) or the internal files dir without one.
     */
    [[gnu::noinline]] void onSetup(JNIEnv* env, jobject emulatorConfiguration);

    /**
     * On the emulator thread when it pauses, between frames.
     */
    [[gnu::noinline]] void onPause();

    /**
     * After the emulator thread has ended.
     */
    [[gnu::noinline]] void onStop();
}

#endif //PGODUMP_H
