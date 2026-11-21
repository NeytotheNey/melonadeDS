# Coding standards

The rules coding and review agents work to, and anyone else writing code here. They cover our own code: the Kotlin app, the app's native code (`app/src/main/cpp/`), the desktop app (`desktop/src/`) and our changes to the core (`melonDS-android-lib`). Upstream code we merge isn't held to them unless a change touches it, and vendored code isn't changed at all (rule 10).

A few rules are adapted from Holzmann's "The Power of Ten" (NASA/JPL), cut down to what fits an emulator with a JIT, a garbage-collected Kotlin app and callback-driven UI libraries. What was left out, and why, is at the end.

## Rules

1. **Check what can fail.** A call that can fail (file I/O, JNI, SQLite, network, archive decoding, GL object and shader creation, Lua calls, savestate loads) has its result checked, and a failure either leaves things as they were or is reported to the user or the log. A result ignored on purpose gets a `(void)` cast or a comment saying why. In Kotlin, no `catch` that swallows an exception without handling or logging it.

2. **Bound what outside data drives.** Sizes, offsets, counts, loop lengths and recursion depth that come from ROMs, archives, save files, savestates, network replies, Lua scripts, layouts or settings files are checked against an explicit limit before use. Recursion over such data needs a depth limit.

3. **Keep the per-frame paths lean.** On the emulator thread's frame loop, the render and present path and the audio callback: no new heap allocation, file I/O or unconditional logging. Allocate up front and reuse buffers; debug logging stays behind its switch.

4. **Simple control flow, small functions.** No `goto`, `setjmp` or `longjmp`. Early returns are fine. New functions aim to fit on one screen (about 60 lines); dispatch tables and big `switch` statements over opcodes or registers are the exception.

5. **Smallest scope, clear ownership.** Declare variables where they're first used and don't reuse one for a different purpose. File-local helpers are `static` or in an anonymous namespace (C++) or `private` (Kotlin). No new globals. State shared between threads (emulator, UI or present, audio, the GL thread on Android) is atomic or behind the lock that already guards it, with a one-line comment saying which thread owns it.

6. **Assert invariants where they matter, not by quota.** Check a precondition where a wrong value would corrupt state silently. Assertions have no side effects. Anything outside data can trigger gets a recovery path (return an error, skip the item) in release builds, not a crash.

7. **No new compiler warnings.** `melonade-shared` and the desktop exe build with `-Wall -Wextra`, minus `-Wunused-parameter` and `-Wmissing-field-initializers`, which mostly fired in core headers and drowned out the rest. New or changed code adds no warnings; to tell yours from the ones already there, compare against a build of the commit you started from. Existing warnings are fixed when their file is being changed anyway, not in drive-by commits. When the compiler is wrong, rewrite the code so it's plainly valid; if that isn't possible, use the narrowest suppression with a comment.

8. **`#ifdef` only at platform seams.** Conditional compilation is for Android vs desktop in shared code and for JIT or CPU architecture in the core, not for switching features on and off (those are settings or debug switches). Prefer a function in the platform layer over an `#ifdef` in the middle of logic. Macros expand to a complete statement or expression.

9. **House style** (from the 2026-11-02 polish pass, [polish.md](polish.md)):
    - Comments keep the first sentence that says why, and drop history, restated code and plan-doc citations; the rationale goes in the commit message.
    - Native constants are `UPPER_SNAKE` in frontend files and `kCamel` only inside `scripting/`.
    - American spelling everywhere, except stored preference keys and values. Upstream names like `SoftInputBehaviour*`, `FirmwareColour*`, `favouriteColour` and the stored value `super_colours` stay British.
    - Don't undo: the four `Anchor` ternaries (they keep the float rounding); layouts mirrored by hand between native and Kotlin and checked at startup; the layout editor's fixed colors.

10. **Leave vendored code alone.** `app/src/main/cpp/lua/`, the `oboe`, `faad2` and `enet` submodules and `desktop/third_party/` aren't edited. A needed change goes in a wrapper, or in a patched copy kept beside the original as with `lua/patched/lstrlib.c`. The core (`melonDS-android-lib`) is ours to change, through its own branch and PR.

## For coding agents

Read this file before writing code. If you break a rule on purpose, say which one and why in your report, and in a code comment when the next reader would otherwise "fix" it.

## For review agents

Report a rule break only when it creates a real risk in this diff: a crash, corrupted or lost data (saves, states, settings, the library), a hang, a race between threads, or a slowdown on a per-frame path. Report that as a finding like any bug. A rule break with no such risk is a nit at most, and rules 4 and 9 are always nits. Don't report code the diff didn't change, and don't hold upstream core code to these rules unless the diff touches it. A real bug matters more than any rule here.

## Left out from The Power of Ten

No dynamic allocation after startup, no function pointers, at most one level of pointer dereference, no recursion at all and two assertions per function don't fit this project. The JIT, SDL, ImGui callbacks, virtual dispatch and the event messengers need function pointers; Kotlin allocates by design; and an assertion quota gets filler asserts, not safer code. Rule 3 keeps the part of the allocation rule that matters here, rule 2 the part of the loop and recursion rules, rule 8 the part of the preprocessor rule.
