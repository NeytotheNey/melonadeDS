/*
 * Host test for renderer/SunGuard.cpp (the sun guard band's sides and render views) that prints each failed check and exits with 1 if any
 * failed; it isn't part of the Gradle build. From app/, with any C++20 compiler (MSYS2's MinGW, say):
 *
 *   g++ -std=c++20 -O2 -Isrc/main/cpp -I../melonDS-android-lib/src src/test/cpp/SunGuardTest.cpp src/main/cpp/renderer/SunGuard.cpp -o sun-guard-test && ./sun-guard-test
 */

#include <cmath>
#include <cstdio>
#include "renderer/SunGuard.h"

using namespace MelonDSAndroid;

static int failures = 0;

#define CHECK(condition) \
    do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); failures++; } } while (0)

// Pokemon White's field camera, about: P0 2.07, P5 2.76, P11 -1 (column-major), over the native screen
static const std::array<float, 16> kWhiteProjection = { 2.07f, 0, 0, 0, 0, 2.76f, 0, 0, 0, 0, -1.02f, -1.0f, 0, 0, -32.0f, 0 };
static const std::array<float, 4> kNativeViewport = { 0, 0, 256, 192 };

static std::array<float, 3> normalized(float x, float y, float z)
{
    float length = std::sqrt(x * x + y * y + z * z);
    return { x / length, y / length, z / length };
}

static SunGuardSides sides(bool left, bool right, bool top, bool bottom)
{
    SunGuardSides result;
    result.left = left;
    result.right = right;
    result.top = top;
    result.bottom = bottom;
    return result;
}

static void testSides()
{
    // hd2d_white.lua's north-west sun, and the north-east one it had before
    auto northWest = normalized(-0.50f, 0.83f, 0.26f);
    auto northEast = normalized(0.50f, 0.83f, 0.26f);
    CHECK(pickSunGuardSides(northWest, kWhiteProjection, kNativeViewport, 256, 0) == sides(true, false, true, false));
    CHECK(pickSunGuardSides(northWest, kWhiteProjection, kNativeViewport, 342, 20) == sides(true, false, true, false));
    CHECK(pickSunGuardSides(northEast, kWhiteProjection, kNativeViewport, 256, 0) == sides(false, true, true, false));
    CHECK(pickSunGuardSides(northEast, kWhiteProjection, kNativeViewport, 342, 20) == sides(false, true, true, false));
    // Down and away: the march heads for the vanishing point, so it can leave through either side, and the bottom
    CHECK(pickSunGuardSides(normalized(0.0f, -0.6f, 0.8f), kWhiteProjection, kNativeViewport, 256, 0) == sides(true, true, false, true));
    // Less steep, the rows change sign too: all four
    CHECK(pickSunGuardSides(normalized(0.0f, -0.2f, 0.98f), kWhiteProjection, kNativeViewport, 256, 0) == sides(true, true, true, true));
    // A sun behind the camera's side (dz < 0) moves away from the vanishing point
    CHECK(pickSunGuardSides(normalized(-0.6f, 0.5f, -0.6f), kWhiteProjection, kNativeViewport, 256, 0) == sides(true, false, true, false));
    // No viewport: all four
    CHECK(pickSunGuardSides(northWest, kWhiteProjection, { 0, 0, 0, 0 }, 256, 0) == SunGuardSides {});
    // Mostly away from the camera and a little right: right of the vanishing point it leaves through the right, left of it the left
    auto slight = normalized(0.1f, 0.83f, 0.8f);
    CHECK(pickSunGuardSides(slight, kWhiteProjection, kNativeViewport, 256, 0) == sides(true, true, true, false));
}

static SunGuardView view(int width, int margin, int shiftX, int shiftY, FrameGuard guard)
{
    return { width, margin, shiftX, shiftY, guard };
}

static void testViews()
{
    // The same on all four sides: the band from before sides were picked
    CHECK(sunGuardView(256, 0, {}) == view(256, 0, 0, 0, {}));
    CHECK(sunGuardView(256, 0, { 64, 64, 64, 64 }) == view(384, 64, 0, 0, { 64, 64, 64, 64 }));
    CHECK(sunGuardView(342, 20, { 64, 64, 64, 64 }) == view(470, 84, 0, 0, { 64, 64, 64, 64 }));
    CHECK(sunGuardView(256, 0, { 33, 33, 33, 33 }) == view(322, 33, 0, 0, { 33, 33, 33, 33 }));
    CHECK(sunGuardView(700, 0, { 64, 64, 64, 64 }) == view(768, 34, 0, 0, { 34, 34, 34, 34 }));
    CHECK(sunGuardView(342, 120, { 64, 64, 64, 64 }) == view(358, 128, 0, 0, { 8, 8, 8, 8 }));
    CHECK(sunGuardView(768, 0, { 64, 64, 64, 64 }) == view(768, 0, 0, 0, {}));

    // Left and top, the design's example at 342
    CHECK(sunGuardView(342, 0, { 64, 0, 64, 0 }) == view(406, 32, 32, 32, { 64, 0, 64, 0 }));
    CHECK(sunGuardView(256, 0, { 64, 0, 64, 0 }) == view(320, 32, 32, 32, { 64, 0, 64, 0 }));
    CHECK(sunGuardView(256, 0, { 0, 64, 64, 0 }) == view(320, 32, -32, 32, { 0, 64, 64, 0 }));
    CHECK(sunGuardView(342, 20, { 0, 64, 0, 64 }) == view(406, 52, -32, -32, { 0, 64, 0, 64 }));
    // Odd guards round up to even
    CHECK(sunGuardView(256, 0, { 33, 0, 0, 0 }) == view(290, 0, 17, 0, { 34, 0, 0, 0 }));
    // Rows only on a 256 view: 2 columns on the right, for the margin
    CHECK(sunGuardView(256, 0, { 0, 0, 64, 0 }) == view(258, 32, -1, 32, { 0, 2, 64, 0 }));
    // Both sides of one axis with different guards (one lowering): each its own, rounded up
    CHECK(sunGuardView(256, 0, { 64, 32, 64, 0 }) == view(352, 32, 16, 32, { 64, 32, 64, 0 }));
    // Caps: one side takes the axis' room, two share it (even each); one row side up to twice the margin room
    CHECK(sunGuardView(700, 0, { 128, 0, 0, 0 }) == view(768, 0, 34, 0, { 68, 0, 0, 0 }));
    CHECK(sunGuardView(700, 0, { 128, 64, 0, 0 }) == view(768, 0, 0, 0, { 34, 34, 0, 0 }));
    CHECK(sunGuardView(342, 100, { 0, 0, 128, 0 }) == view(342, 128, 0, 28, { 0, 0, 56, 0 }));
    CHECK(sunGuardView(342, 100, { 0, 0, 128, 64 }) == view(342, 128, 0, 0, { 0, 0, 28, 28 }));
    CHECK(sunGuardView(256, 0, { 0, 0, 128, 0 }) == view(258, 64, -1, 64, { 0, 2, 128, 0 }));
}

static void testSteps()
{
    SunGuardView leftTop = sunGuardView(256, 0, { 64, 0, 64, 0 });
    SunGuardView rightTop = sunGuardView(256, 0, { 0, 64, 64, 0 });
    SunGuardView all = sunGuardView(256, 0, { 64, 64, 64, 64 });
    SunGuardView none = sunGuardView(256, 0, {});

    // Only growing or only shrinking: straight there
    CHECK(sunGuardStep(none, none, leftTop) == leftTop);
    CHECK(sunGuardStep(leftTop, leftTop, none) == none);
    CHECK(sunGuardStep(all, all, leftTop) == leftTop);
    CHECK(sunGuardStep(leftTop, leftTop, all) == all);
    CHECK(sunGuardStep(leftTop, leftTop, leftTop) == leftTop);
    // The left shrinks while the right grows: both first, then the right
    SunGuardView both = sunGuardStep(leftTop, leftTop, rightTop);
    CHECK(both == view(384, 32, 0, 32, { 64, 64, 64, 0 }));
    CHECK(sunGuardStep(both, both, rightTop) == rightTop);
    // The shown width shrinks while a side grows: around the new shown view
    SunGuardView wide = sunGuardView(342, 0, { 64, 0, 64, 0 });
    SunGuardView narrowRight = sunGuardView(256, 0, { 0, 64, 64, 0 });
    SunGuardView mixed = sunGuardStep(wide, wide, narrowRight);
    // Left 107 and right 64, an odd count: one more on the right
    CHECK(mixed == view(428, 32, 21, 32, { 107, 65, 64, 0 }));
    // Too big for WideMelon's widest view: straight there
    SunGuardView wideLeft = sunGuardView(640, 0, { 128, 0, 0, 0 });
    SunGuardView wideRight = sunGuardView(640, 0, { 0, 128, 0, 0 });
    CHECK(sunGuardStep(wideLeft, wideLeft, wideRight) == wideRight);
    // Geometry being built for another view than the drawn one: the larger of the two on each side. The left drawn, the right being built,
    // and the left going: the right is all that's asked for, as only the left shrinks then
    CHECK(sunGuardStep(leftTop, both, rightTop) == rightTop);
    // The left drawn, a narrower top being built, and the right growing with the left going: the larger of each side
    SunGuardView lowTop = sunGuardView(256, 0, { 64, 0, 32, 0 });
    CHECK(sunGuardStep(lowTop, leftTop, rightTop) == both);
}

static void testFrameGuards()
{
    FrameGuard leftTop { 64, 0, 64, 0 };
    CHECK(frameGuardFor(320, 32, 32, 32, &leftTop) == leftTop);
    FrameGuard all { 64, 64, 64, 64 };
    CHECK(frameGuardFor(384, 64, 0, 0, &all) == all);
    // A guard that doesn't centre the shown screen on the native one, or doesn't fit: the least that does
    CHECK(frameGuardFor(320, 32, 0, 0, &leftTop) == FrameGuard {});
    CHECK(frameGuardFor(300, 10, 0, 0, &all) == FrameGuard {});
    CHECK(frameGuardFor(342, 20, 20, -10, nullptr) == (FrameGuard { 40, 0, 0, 20 }));
    CHECK(frameGuardFor(258, 32, -1, 32, nullptr) == (FrameGuard { 0, 2, 64, 0 }));
    CHECK(frameGuardFor(256, 0, 0, 0, nullptr) == FrameGuard {});
}

int main()
{
    testSides();
    testViews();
    testSteps();
    testFrameGuards();
    if (failures == 0)
        std::printf("all passed\n");
    return failures == 0 ? 0 : 1;
}
