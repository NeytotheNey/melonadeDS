// A tiny test harness for melonade-desktop-tests: TEST registers a case, the CHECK macros fail it and return from it (as a JUnit assert
// ends its test), and TestMain.cpp runs them all. CHECK_NEAR is JUnit's assertEquals(float, float, delta), so the ported Kotlin tests
// keep their tolerances (a delta of 0 is an exact match)
#pragma once

#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "camera/WideCamera.h"
#include "layout/LayoutGeometry.h"

namespace Tests
{

struct TestCase
{
    const char* Suite;
    const char* Name;
    void (*Run)();
};

std::vector<TestCase>& Registry();

struct Registrar
{
    Registrar(const char* suite, const char* name, void (*run)()) { Registry().push_back({suite, name, run}); }
};

// marks the running test failed with [message]
void Fail(const char* file, int line, const std::string& message);

// JUnit's floatIsDifferent, negated
bool NearlyEqual(float expected, float actual, float delta);

std::string Show(float value);
std::string Show(double value);
std::string Show(int value);
std::string Show(bool value);
std::string Show(const Frontend::IntRect& rect);
std::string Show(const Frontend::WideWindow& window);
std::string Show(const Frontend::WideCameraRequest& request);
std::string Show(const Frontend::OverlayDrawing& overlay);
std::string Show(const Frontend::ScreenDrawing& screen);
std::string Show(const Frontend::ScreensDrawing& drawing);

template <class A, class B>
std::string Show(const std::pair<A, B>& pair)
{
    return "(" + Show(pair.first) + ", " + Show(pair.second) + ")";
}

template <class T>
std::string Show(const std::optional<T>& value)
{
    return value ? Show(*value) : std::string("none");
}

template <class T>
std::string Show(const std::vector<T>& values)
{
    std::string text = "[";
    for (size_t i = 0; i < values.size(); i++)
        text += (i ? ", " : "") + Show(values[i]);
    return text + "]";
}

// the value of [value] (Kotlin's !!): fails the running test and gives a default one when there's none
template <class T>
T SomeAt(const std::optional<T>& value, const char* file, int line)
{
    if (!value)
    {
        Fail(file, line, "expected a value, got none");
        return T {};
    }
    return *value;
}

}

#define TEST(suite, name) \
    static void suite##_##name(); \
    static ::Tests::Registrar suite##_##name##_registrar(#suite, #name, &suite##_##name); \
    static void suite##_##name()

#define CHECK(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            ::Tests::Fail(__FILE__, __LINE__, "CHECK(" #condition ")"); \
            return; \
        } \
    } while (0)

#define CHECK_EQ(expected, actual) \
    do \
    { \
        const auto& expected_ = (expected); \
        const auto& actual_ = (actual); \
        if (!(expected_ == actual_)) \
        { \
            ::Tests::Fail(__FILE__, __LINE__, "expected " + ::Tests::Show(expected_) + ", got " + ::Tests::Show(actual_)); \
            return; \
        } \
    } while (0)

#define CHECK_NEAR(expected, actual, delta) \
    do \
    { \
        float expected_ = (expected); \
        float actual_ = (actual); \
        if (!::Tests::NearlyEqual(expected_, actual_, (delta))) \
        { \
            ::Tests::Fail(__FILE__, __LINE__, "expected " + ::Tests::Show(expected_) + " +- " + ::Tests::Show((float)(delta)) + ", got " + \
                ::Tests::Show(actual_)); \
            return; \
        } \
    } while (0)

#define CHECK_NONE(value) CHECK(!(value).has_value())

// Kotlin's !! on an optional: its value, or the test fails
#define SOME(value) ::Tests::SomeAt((value), __FILE__, __LINE__)
