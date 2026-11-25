// WidescreenLaunchCameraTest.kt, case for case. WidescreenLaunch.resolve(RomModsConfig(...), null, isWideRenderer) is
// ResolveWidescreenLaunch with the same Mods values (no picked wide layout)
#include "Test.h"
#include "camera/WidescreenLaunch.h"

using namespace Frontend;

namespace
{

// RomModsConfig(wideViewWidth, wideCamera, wideCameraMaxWidth), resolved
WidescreenLaunch Resolve(std::optional<int> wideViewWidth, bool wideCamera, std::optional<int> wideCameraMaxWidth, bool isWideRenderer = true)
{
    return ResolveWidescreenLaunch(wideViewWidth, wideCamera, wideCameraMaxWidth, true, true, isWideRenderer);
}

}

TEST(WidescreenLaunchCamera, withoutTheCameraTheCoreRendersAtTheShownWidth)
{
    WidescreenLaunch launch = Resolve(342, false, 768);

    CHECK(!launch.IsCameraOn());
    CHECK_EQ(342, launch.GetRenderWidth(342, 1.f));
    CHECK_EQ(342, launch.GetRenderWidth(342, 2.f));
    CHECK_EQ(342, launch.GetMaxRenderWidth(342));
    CHECK_EQ(0, launch.GetRenderWidth(0, 1.5f));
}

TEST(WidescreenLaunchCamera, withTheCameraTheWidestRenderIsTheWidestView)
{
    WidescreenLaunch launch = Resolve(342, true, std::nullopt);

    CHECK(launch.IsCameraOn());
    CHECK_EQ(DEFAULT_WIDE_CAMERA_MAX_WIDTH, launch.GetMaxRenderWidth(342));
    CHECK_EQ(DEFAULT_WIDE_CAMERA_MAX_WIDTH, launch.GetRenderWidth(342, 512.f / 342.f));
    // never narrower than shown, and a variant that isn't wide stays at 256
    CHECK_EQ(600, launch.GetMaxRenderWidth(600));
    CHECK_EQ(600, launch.GetRenderWidth(600, 1.5f));
    CHECK_EQ(0, launch.GetRenderWidth(0, 1.5f));
}

TEST(WidescreenLaunchCamera, at1xTheCameraRendersAtTheShownWidthWithNoMargin)
{
    WidescreenLaunch launch = Resolve(342, true, 512);

    // the same as with the camera off, so the camera costs nothing until the player zooms out
    CHECK_EQ(342, launch.GetRenderWidth(342, 1.f));
    CHECK_EQ(0, launch.GetRenderMargin(342, 1.f));
    // a zoom a float error past 1 doesn't render a whole step wider
    CHECK_EQ(342, launch.GetRenderWidth(342, 1.0001f));
}

TEST(WidescreenLaunchCamera, theRenderWidthFollowsTheZoomRoundedUpToTheNextStep)
{
    WidescreenLaunch launch = Resolve(342, true, 640);

    // 342 * 1.25 = 427.5, rounded up to whole even columns
    CHECK_EQ(428, launch.GetRenderWidth(342, 1.25f));
    // between steps renders for the step above, so a pinch only resizes when it crosses one
    CHECK_EQ(428, launch.GetRenderWidth(342, 1.1f));
    CHECK_EQ(514, launch.GetRenderWidth(342, 1.5f));
    CHECK_EQ(514, launch.GetRenderWidth(342, 1.3f));
    // 342 * 1.75 = 598.5 is the step's own width; past it is capped at the widest view
    CHECK_EQ(600, launch.GetRenderWidth(342, 1.75f));
    CHECK_EQ(640, launch.GetRenderWidth(342, 1.8f));
    CHECK_EQ(640, launch.GetRenderWidth(342, 3.f));
    // each width has room for its step's window: 342 * 1.25 columns and 192 * 1.25 rows
    CHECK(launch.GetRenderWidth(342, 1.25f) >= 342 * 1.25f);
    CHECK(192 + 2 * launch.GetRenderMargin(342, 1.25f) >= 192 * 1.25f);
}

TEST(WidescreenLaunchCamera, theMarginLetsTheWidestViewFillTheScreen)
{
    WidescreenLaunch launch = Resolve(342, true, 512);
    float widest = 512.f / 342.f;

    // ceil(96 * (512 / 342 - 1)) = ceil(47.7)
    CHECK_EQ(48, launch.GetRenderMargin(342, widest));
    // 192 * 512 / 342 rows fit in 192 + 2 * 48
    CHECK(192.f * 512.f / 342.f <= 192 + 2 * launch.GetRenderMargin(342, widest));
    // the margin follows the render width: ceil(96 * 86 / 342) at 1.25
    CHECK_EQ(25, launch.GetRenderMargin(342, 1.25f));
    // no room to zoom out, or a variant that isn't wide: none
    CHECK_EQ(0, launch.GetRenderMargin(512, widest));
    CHECK_EQ(0, launch.GetRenderMargin(600, widest));
    CHECK_EQ(0, launch.GetRenderMargin(0, widest));
}

TEST(WidescreenLaunchCamera, theMarginIsWholeRowsAndCapped)
{
    // 96 * (684 - 342) / 342 is exactly 96, so it isn't rounded up to 97
    WidescreenLaunch exact = Resolve(342, true, 684);
    CHECK_EQ(96, exact.GetRenderMargin(342, 2.f));
    // 96 * (768 / 300 - 1) is about 150, over the cap
    WidescreenLaunch capped = Resolve(300, true, 768);
    CHECK_EQ(128, capped.GetRenderMargin(300, 3.f));
}

TEST(WidescreenLaunchCamera, withoutTheCameraThereIsNoMargin)
{
    WidescreenLaunch launch = Resolve(342, false, 768);

    CHECK_EQ(0, launch.GetRenderMargin(342, 2.f));
    CHECK_EQ(0, WidescreenLaunch {}.GetRenderMargin(342, 2.f));
}

TEST(WidescreenLaunchCamera, anUnusableMaxWidthFallsBackToTheDefault)
{
    CHECK_EQ(DEFAULT_WIDE_CAMERA_MAX_WIDTH, ValidWideCameraMaxWidth(1000));
    CHECK_EQ(DEFAULT_WIDE_CAMERA_MAX_WIDTH, ValidWideCameraMaxWidth(513));
    CHECK_EQ(640, ValidWideCameraMaxWidth(640));
}

TEST(WidescreenLaunchCamera, theCameraNeedsWidescreenAndAWideRenderer)
{
    CHECK(!Resolve(std::nullopt, true, std::nullopt).IsCameraOn());
    CHECK(!Resolve(342, true, std::nullopt, false).IsCameraOn());
}
