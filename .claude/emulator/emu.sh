#!/bin/bash
# Drives the melonade_fold Android emulator for testing melonadeDS; `emu.sh help` lists the commands
set -u
export MSYS_NO_PATHCONV=1
# Windows-style paths: path conversion is off (adb needs device paths left alone), so native tools need C:/ forms
HERE="$(cygpath -m "$(cd "$(dirname "$0")" && pwd)")"
REPO="$(cygpath -m "$(cd "$(dirname "$0")/../.." && pwd)")"
SDK="$(cygpath -m "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-C:/Tools/android-sdk}}")"
# Own variable rather than JAVA_HOME, which may point at an older JDK the build rejects
JDK="$(cygpath -u "${EMU_JAVA_HOME:-C:/Program Files/Java/jdk-25}")"
ADB="$SDK/platform-tools/adb.exe"
PORT=5580
A=("$ADB" -s "emulator-$PORT")
OUT="$REPO/logs/emulator"
APK="$REPO/app/build/intermediates/apk/gitHubProd/debug/app-gitHub-prod-debug.apk"
PKG=me.magnum.melonds.dev
PY=(py -I -X utf8 "$HERE/emu_ui.py")
mkdir -p "$OUT/shots"

booted() { [ "$("${A[@]}" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ]; }
folded() { "${A[@]}" shell cmd device_state state 2>/dev/null | grep -q "name='CLOSED'"; }

case "${1:-help}" in
  start)
    if booted; then echo "already running"; exit 0; fi
    nohup "$SDK/emulator/emulator.exe" -avd melonade_fold -no-window -no-audio -no-snapshot-save -no-boot-anim -port $PORT \
      > "$OUT/emulator.log" 2>&1 < /dev/null &
    disown
    for i in $(seq 1 60); do booted && { echo "booted after ~$((i * 3))s"; exit 0; }; sleep 3; done
    echo "boot timed out, see $OUT/emulator.log"; exit 1 ;;
  stop)
    "${A[@]}" emu kill ;;
  build)
    # Overwrites the shared debug APK with an x86_64 one (the phone needs an arm64 rebuild after), path conversion back on for gradlew
    cd "$REPO" && env -u MSYS_NO_PATHCONV JAVA_HOME="$JDK" bash ./gradlew assembleGitHubProdDebug \
      -Pandroid.injected.build.abi=x86_64 -Pandroid.injected.testOnly=false --console=plain > "$REPO/logs/build-x86_64.log" 2>&1
    rc=$?; tail -3 "$REPO/logs/build-x86_64.log"; exit $rc ;;
  install)
    unzip -l "$APK" 2>/dev/null | grep -q "lib/x86_64/libmelonDS-android-frontend.so" \
      || { echo "APK has no x86_64 core; run: emu.sh build"; exit 1; }
    "${A[@]}" install -r "$APK" | tail -1 ;;
  launch)
    # Brings the app to the front like a launcher tap (0x10200000 = NEW_TASK | RESET_TASK_IF_NEEDED), back into a running game if
    # there is one (use restart for the ROM list), naming the component because debug builds also have LeakCanary's launcher icon
    "${A[@]}" shell am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -f 0x10200000 \
      -n $PKG/me.magnum.melonds.ui.romlist.RomListActivity > /dev/null 2>&1; sleep 3
    "${A[@]}" shell dumpsys activity activities | grep -m1 topResumedActivity; exit 0 ;;
  restart)
    "${A[@]}" shell am force-stop $PKG; "$0" launch ;;
  press)
    # press <button> [ms]: on-screen DS button in the DEFAULT layout
    b=$(echo "${2:?button}" | tr 'A-Z' 'a-z')
    if folded; then
      case $b in a) xy="1016 2192";; b) xy="909 2296";; x) xy="909 2083";; y) xy="803 2192";;
        l) xy="35 1826";; r) xy="1040 1826";; up) xy="170 2080";; down) xy="170 2290";;
        left) xy="65 2186";; right) xy="275 2186";; select) xy="487 2308";; start) xy="592 2308";;
        *) echo "unknown button $b"; exit 1;; esac
    else
      case $b in a) xy="2011 1979";; b) xy="1905 2084";; x) xy="1905 1872";; y) xy="1798 1979";;
        l) xy="37 1197";; r) xy="2036 1197";; up) xy="170 1866";; down) xy="170 2078";;
        left) xy="64 1974";; right) xy="276 1974";; select) xy="985 2097";; start) xy="1090 2097";;
        *) echo "unknown button $b"; exit 1;; esac
    fi
    set -- $xy "${3:-150}"; "${A[@]}" shell input swipe $1 $2 $1 $2 $3; echo "pressed $b at $1,$2" ;;
  scroll)
    # scroll [down|up]: swipe through the middle of the active panel
    read -r w h <<< "$("${A[@]}" shell wm size | awk -F'[ x]' '/Physical/{print $3, $4}' | tr -d '\r')"
    x=$((w / 2)); top=$((h * 3 / 10)); bottom=$((h * 7 / 10))
    if [ "${2:-down}" = "up" ]; then "${A[@]}" shell input swipe $x $top $x $bottom 300
    else "${A[@]}" shell input swipe $x $bottom $x $top 300; fi ;;
  state)
    "${A[@]}" shell cmd device_state state | grep -i committed
    "${A[@]}" shell wm size
    "${A[@]}" shell dumpsys activity activities | grep -m1 topResumedActivity ;;
  fold)   "${A[@]}" emu fold;   sleep 3; "$0" state ;;
  unfold) "${A[@]}" emu unfold; sleep 3; "$0" state ;;
  shot)
    # shot <name> [--full]: screenshot of whichever panel is active, saved half size unless --full
    name=${2:?name}; idx=1; folded && idx=2
    id=$("${A[@]}" shell dumpsys SurfaceFlinger --display-id | sed -n "${idx}p" | awk '{print $2}')
    full="$OUT/shots/$name.png"
    "${A[@]}" exec-out screencap -p -d "$id" > "$full"
    if [ "${3:-}" = "--full" ]; then echo "$full (full size)"; else
      "${PY[@]}" half "$full" "$OUT/shots/${name}_half.png" 2; echo "$OUT/shots/${name}_half.png"; fi ;;
  ui)
    # ui [filter]: labelled and clickable nodes with tap centres, from the accessibility tree (no screenshot)
    ok=
    for try in 1 2 3; do
      "${A[@]}" shell rm -f /sdcard/ui.xml
      "${A[@]}" shell uiautomator dump /sdcard/ui.xml 2>&1 | grep -q "dumped to" && { ok=1; break; }
      sleep 1
    done
    [ -n "$ok" ] || { echo "uiautomator dump failed 3 times (screen still animating?), take a screenshot instead"; exit 1; }
    "${A[@]}" pull /sdcard/ui.xml "$OUT/ui.xml" > /dev/null 2>&1 && "${PY[@]}" nodes "$OUT/ui.xml" "${2:-}" ;;
  tap-text)
    # tap-text <text> [n]: tap the n-th (default 1st) node whose label contains <text>
    "$0" ui "${2:?text}" > "$OUT/ui_match.txt" || { cat "$OUT/ui_match.txt"; exit 1; }
    xy=$(sed -n "${3:-1}p" "$OUT/ui_match.txt" | sed -E 's/^\(([0-9]+),([0-9]+)\).*/\1 \2/')
    [ -n "$xy" ] || { echo "no match #${3:-1}"; exit 1; }
    "${A[@]}" shell input tap $xy; echo "tapped $xy: $(sed -n "${3:-1}p" "$OUT/ui_match.txt")" ;;
  tap)   "${A[@]}" shell input tap "${2:?x}" "${3:?y}" ;;
  swipe) "${A[@]}" shell input swipe "${2:?x1}" "${3:?y1}" "${4:?x2}" "${5:?y2}" "${6:-300}" ;;
  hold)  "${A[@]}" shell input swipe "${2:?x}" "${3:?y}" "$2" "$3" "${4:-800}" ;;
  key)   "${A[@]}" shell input keyevent "${2:?KEYCODE_...}" ;;
  back)  "${A[@]}" shell input keyevent KEYCODE_BACK ;;
  text)  "${A[@]}" shell input text "${2:?text}" ;;
  crash)
    # crash: recent crashes and fatal errors from this app
    "${A[@]}" logcat -d -b crash | tail -40
    "${A[@]}" logcat -d '*:E' | grep -iE "melonds|FATAL|AndroidRuntime" | tail -20 ;;
  log)   "${A[@]}" logcat -d -s melonade:* melonade-core:* melonade-cpu:* melonade-audio:* melonade-lua:* melonade-gl:* melonade-debug:* | tail -"${2:-30}" ;;
  logclear) "${A[@]}" logcat -c ;;
  adb)   shift; "${A[@]}" "$@" ;;
  *)
    sed -n '2p' "$0"
    echo "commands: start stop build install launch restart state fold unfold shot <name> [--full] ui [filter] scroll [down|up]"
    echo "          press <a|b|x|y|l|r|up|down|left|right|select|start> [ms]"
    echo "          tap-text <text> [n] tap <x> <y> swipe <x1> <y1> <x2> <y2> [ms] hold <x> <y> [ms] key <KEYCODE> back"
    echo "          text <string> crash log [n] logclear adb <args...>" ;;
esac
