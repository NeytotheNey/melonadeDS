// melonade-desktop-tests: runs every registered test and prints the failures; exits 1 if any failed. An argument runs only the tests whose
// suite or name contains it
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Test.h"

namespace Tests
{

namespace
{

// the test running now, and whether it failed; the harness runs one test at a time on one thread
struct Current
{
    const TestCase* Case = nullptr;
    bool Failed = false;
};

Current& Running()
{
    static Current current;
    return current;
}

std::string Join(std::initializer_list<std::string> parts)
{
    std::string text;
    for (const std::string& part : parts)
        text += (text.empty() ? "" : ", ") + part;
    return text;
}

}

std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> registry;
    return registry;
}

void Fail(const char* file, int line, const std::string& message)
{
    Current& current = Running();
    current.Failed = true;
    std::printf("FAIL %s.%s\n    %s:%d: %s\n", current.Case ? current.Case->Suite : "?", current.Case ? current.Case->Name : "?", file, line,
        message.c_str());
}

bool NearlyEqual(float expected, float actual, float delta)
{
    // Float.compare's equality: the same value, or both NaN
    if (expected == actual || (std::isnan(expected) && std::isnan(actual)))
        return true;
    return std::fabs(expected - actual) <= delta;
}

std::string Show(float value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.9g", value);
    return text;
}

std::string Show(double value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.17g", value);
    return text;
}

std::string Show(int value)
{
    return std::to_string(value);
}

std::string Show(bool value)
{
    return value ? "true" : "false";
}

std::string Show(const Frontend::IntRect& rect)
{
    return "Rect(" + Join({Show(rect.X), Show(rect.Y), Show(rect.W), Show(rect.H)}) + ")";
}

std::string Show(const Frontend::WideWindow& w)
{
    return "WideWindow(" + Join({Show(w.ShowWidth), Show(w.FrameWidth), Show(w.X), Show(w.Width), Show(w.FrameHeight), Show(w.Y), Show(w.Height)})
        + ")";
}

std::string Show(const Frontend::WideCameraRequest& r)
{
    return "Request(zoom " + Show(r.Zoom) + ", pan " + Show(r.Pan) + ", holdNative " + Show(r.HoldNative) + ", panY " + Show(r.PanY) + ")";
}

std::string Show(const Frontend::OverlayDrawing& o)
{
    return "Overlay(" + Join({Show(o.Left), Show(o.Top), Show(o.Right), Show(o.Bottom), Show(o.LeftU), Show(o.RightU), Show(o.TopV),
        Show(o.BottomV)}) + ")";
}

std::string Show(const Frontend::ScreenDrawing& s)
{
    return "ScreenDrawing(" + Join({Show(s.DrawRect), Show(s.NativeRect), Show(s.LeftU), Show(s.RightU), Show(s.TopRow), Show(s.BottomRow),
        Show(s.Overlay), Show(s.NativeRectAt1x)}) + ")";
}

std::string Show(const Frontend::ScreensDrawing& d)
{
    return "ScreensDrawing(" + Show(d.Top) + ", " + Show(d.Bottom) + ")";
}

}

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failed = 0;
    for (const Tests::TestCase& test : Tests::Registry())
    {
        if (filter && !std::strstr(test.Suite, filter) && !std::strstr(test.Name, filter))
            continue;
        Tests::Running() = {&test, false};
        test.Run();
        run++;
        if (Tests::Running().Failed)
            failed++;
    }
    std::printf("%d tests, %d passed, %d failed\n", run, run - failed, failed);
    return failed ? 1 : 0;
}
