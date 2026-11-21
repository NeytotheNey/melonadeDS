# Plan: a SWI test ROM for FreeBIOS

Status: DS and DSi mode built and run (2026-11-02); results and the FreeBIOS fixes they led to are in the core's `freebios/UPGRADE_PLAN.md` ("SWI test ROM" and "SWI test ROM, DSi mode"). Still open: fixing what the DSi run found and wiring the TWL build into the app (tracked in `HANDOFF.md`), and the phone SoftReset log below.

## Why

FreeBIOS (our clean-room DS BIOS replacement, in the core's `freebios/`) had fixes that no game on hand ever calls: none of our ROMs calls SoftReset (SWI 0x00), HuffUnComp (0x13), RLE-to-VRAM (0x15), or IntrWait with r0 = 0. So instead of waiting for the right game, we wrote our own small DS program that calls each BIOS function with known inputs and stores what comes back, run once with the user's official BIOS and once with FreeBIOS, and diffed. That's black-box testing, so it keeps the clean room. It stays in the core as a regression test for later FreeBIOS changes.

## Clean-room rules (read first, they're strict)

- **Never inspect Nintendo's BIOS.** The official dumps (`D:\Dev\swi-harness-data\bios\`, `...\dsi\`, and the NAS share) may only be loaded so the emulator can run them. Never disassemble, hexdump or string-search them, and never use Ghidra on them.
- **No test may copy BIOS bytes into the results.** Don't point GetCRC16 at the BIOS, don't use a BIOS address as a CpuSet/CpuFastSet/decompression source, and don't dump what GetSineTable/GetPitchTable/GetVolumeTable return. Where a test needs one of those (for example "a source in the BIOS range is rejected"), store only a match flag or a hash of the destination, never the bytes. Table SWIs are compared by hash only.
- **Specs are hypotheses.** There's a GLM-written behavioral spec for the DSi BIOS. Coding subagents get only the trimmed copy, `C:\Dev\NDS BIOS & Firmware\DSi\DSi_BIOS_SWI_Behavioral_Spec.redacted.md`, never the original next to it. It's outside the repo; don't copy it in. Its claims (for example "0 ÷ 0 returns 1" or the IntrWait "leftover bits" rule) are things to test, not facts.
- **FreeBIOS only changes from evidence:** a harness diff against the official BIOS, cross-checked with GBATEK (https://problemkaputt.de/gbatek.htm and its split pages). Fixing FreeBIOS is a follow-up the user approves.

## Where things are

- **The test ROM** is in the core's `tools/swi-testrom/`: its own startup code for both CPUs, written from GBATEK, our own Python encoders for the compressed test data, and a report script. `sh tools/swi-testrom/build.sh` builds the DS and TWL ROMs reproducibly; `sh tools/swi-testrom/run.sh` runs every mode (normal, isolated hang-prone cases, SoftReset variants) with both BIOSes and writes `logs/swi/testrom/report.txt` and `report-dsi.txt` in this repo. The header of `run.sh` lists the modes and environment variables.
- **The harness** is in the core's `tools/swi-harness/`. It's a headless PC build of the core, with an SWI logger behind `MELONADE_SWI_LOG` and `swi_diff.py`. Build it with MSYS2 MinGW:
  `export PATH=/c/msys64/mingw64/bin:$PATH`, then
  `cmake -G Ninja -S tools/swi-harness -B /d/Dev/swi-harness-data/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++`
  and `cmake --build /d/Dev/swi-harness-data/build`.
  Run `swi-harness.exe <rom.nds> --bios official <dir> | --bios free --frames N --out <dir>`. Useful options: `--console dsi --dsi <dir> --nand <copy>`, `--rtc`, `--tap KEY@FRAME` and `--dump-mainram`; the full list is in `Usage()` in `main.cpp`. Inputs are in `D:\Dev\swi-harness-data\`. Each DSi run needs its own NAND copy, deleted afterward (`run.sh` handles this with one read-only copy).
- **The toolchain:** the Android NDK's clang 19 at `C:\Tools\android-sdk\ndk\28.0.13004108\toolchains\llvm\prebuilt\windows-x86_64\bin` (clang, ld.lld, llvm-objcopy). `freebios/build.sh` shows how we build bare-metal ARM images with it.

## Optional: a SoftReset log on the phone

A debug-build log line when a game calls SWI 0x00. It would show whether games that return to their title (Dragon Quest IX after a quicksave) use the BIOS reset or their own code. This is small and separate from the ROM.
