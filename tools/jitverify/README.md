# jitverify

A headless runner for JIT verification (`docs/plan-optimization.md` step 7, "Verification"). It
runs a game from boot (or a savestate) with scripted input and writes one line per frame:

    F <n> top= bot= aud= a9ts= a7ts= sys= a9= a7= ctl= main= wram= tcm= vram= save=

All hashes are XXH3-64. `top`/`bot` are the two screens, `aud` the audio samples the frame made,
`a9ts`/`a7ts`/`sys` the CPU and system timestamps, `a9`/`a7` R0-R15, CPSR and every banked
register and SPSR, `ctl` the ARM9's CP15 state (control register, DTCM/ITCM settings, protection
unit regions) with both CPUs' halt state and IME/IE/IF. The memory fields are `main` (main RAM),
`wram` (shared WRAM and ARM7 WRAM), `tcm` (ITCM and DTCM), `vram` (banks A-I, palette, OAM) and
`save` (cart save memory); `--no-mem` leaves them out. `--regs` adds the raw R0-R15 and CPSR
(`a9r=`, `a7r=`). The first line is a `#` header with what actually ran: the host, its page size,
whether the JIT and fast memory are really on, the block size and the other JIT options, and the
input script.

The reference is the same backend before and after a change (or with a switch flipped): two runs
of the same build with the same options give byte-identical files, and `compare.py` prints the
first frame that differs. The JIT against the interpreter isn't a useful reference: their timings
differ from frame 0 and their screens from about frame 214-239.

Software 3D and 2D, both on the emulator thread; no window, no audio output (the core's audio still
runs and is drained, as in the app); unthrottled. The RTC is pinned (2026-01-01 12:00, `--rtc`), the
generated firmware has the core's fixed MAC, and `--sav` is only read: save writes stay in memory,
so nothing is ever written next to the ROM. The JIT is on with fast memory by default, as the app
runs it (`--no-jit`, `--no-fastmem`, `--ldmdirect`, `--ldmfast`, `--block-size`, `--set`; see
`--help`). For every option the last one given wins, and `--fastmem` doesn't turn the JIT back on
after `--no-jit`. `--at F:reset` resets before frame F and `--at F:state` loads `--state` again,
any number of times, so stale JIT blocks after a reset or a state load get exercised. Lines are
written whole and flushed every frame (`--flush-every N` to batch them).

Known: in this core a state loaded mid-run (`--at F:state`, the desktop's too) leaves White 2
running about 2.5x slower from then on, with the interpreter as well as the JIT.

## Building

    tools/jitverify/build.sh            # Git Bash: both, or `windows` / `android`

Windows x64 (MSYS2 MinGW, x64 JIT) goes to `build-jitverify/windows-x64/jitverify.exe`, Android
arm64-v8a (NDK 28.0.13004108, API 29, static libc++, A64 JIT) to `build-jitverify/android-arm64/`,
where `jitverify.stripped` is the one to push and `jitverify` keeps the symbols. Logs:
`logs/jitverify-build-*.log`.

## Comparing

    python tools/jitverify/compare.py a.txt b.txt [--context N] [--fields top,bot,aud]

Frames are paired by number. `--fields` compares (and lists) only those fields, `--context N`
prints the frames before the difference. A header difference other than `rom=` is a warning. Exit
code 0 identical, 1 different, 2 a usage or input error (a missing file or field, no frames).

## PC

```powershell
$rom = "C:\Users\neyto\OneDrive\Documents\Games\Golden Sun Dark Dawn\Golden Sun - Dark Dawn (USA) (En,Es).nds"
$p = "--press","400:a","--press","700:a","--press","1000:start","--press","1300:a"
& .\build-jitverify\windows-x64\jitverify.exe --frames 3000 @p --out logs\jitverify\a.txt $rom
& .\build-jitverify\windows-x64\jitverify.exe --frames 3000 @p --ldmdirect 0 --out logs\jitverify\b.txt $rom
python tools\jitverify\compare.py logs\jitverify\a.txt logs\jitverify\b.txt
```

## Pixel 8 (adb, no APK)

```powershell
$adb = "C:\Tools\android-sdk\platform-tools\adb.exe"
& $adb shell mkdir -p /data/local/tmp/jitverify/out
& $adb push build-jitverify\android-arm64\jitverify.stripped /data/local/tmp/jitverify/jitverify
& $adb shell chmod +x /data/local/tmp/jitverify/jitverify
& $adb push "C:\Users\neyto\OneDrive\Documents\Games\Golden Sun Dark Dawn\Golden Sun - Dark Dawn (USA) (En,Es).nds" /data/local/tmp/jitverify/gs.nds
& $adb shell "cd /data/local/tmp/jitverify && ./jitverify --frames 3000 --press 400:a --press 700:a --press 1000:start --press 1300:a --out out/gs-a.txt gs.nds"
& $adb pull /data/local/tmp/jitverify/out/gs-a.txt logs\jitverify\
```

FreeBIOS is used by default. For the official BIOS, push `bios9.bin`, `bios7.bin` (and
`firmware.bin`) to `/data/local/tmp/jitverify/bios/` and add `--bios bios`. For a state, push the
`.ml1` too and add `--state <file>`.
