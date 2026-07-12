// Tests/FocusHistoryTest.cpp
// Unit tests for UCWeakMRUList, the MRU window-focus history that drives
// UltraCanvasApplicationBase::JumpToLastWindow(). Framework-independent:
// builds against the header only, no display connection needed.
// Version: 1.0.0
// Last Modified: 2026-07-11
// Author: UltraCanvas Framework

#include "UltraCanvasFocusHistory.h"

#include <cstdio>
#include <memory>
#include <string>

using UltraCanvas::UCWeakMRUList;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_EQ(a, b) do { \
    ++checks; \
    auto va = (a); auto vb = (b); \
    if (!(va == vb)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
    } \
} while (0)

// Stand-in for a window: named, with a visibility flag for predicate tests.
struct FakeWindow {
    std::string name;
    bool visible = true;
    explicit FakeWindow(std::string n) : name(std::move(n)) {}
};

static auto anyWindow = [](const std::shared_ptr<FakeWindow>&) { return true; };
static auto visibleWindow = [](const std::shared_ptr<FakeWindow>& w) { return w->visible; };

// The core jump-last-window scenario: two windows used alternately, the
// trigger toggles between them.
static void TestToggleBetweenTwoWindows() {
    UCWeakMRUList<FakeWindow> history;
    auto a = std::make_shared<FakeWindow>("A");
    auto b = std::make_shared<FakeWindow>("B");

    history.Touch(a);   // A focused
    history.Touch(b);   // B focused; A is now "the last window"

    auto target = history.FindMostRecent(b.get(), anyWindow);
    CHECK(target == a);

    // Jump applied: A becomes current, B becomes "the last window".
    history.Touch(a);
    target = history.FindMostRecent(a.get(), anyWindow);
    CHECK(target == b);
}

static void TestMostRecentOfSeveral() {
    UCWeakMRUList<FakeWindow> history;
    auto a = std::make_shared<FakeWindow>("A");
    auto b = std::make_shared<FakeWindow>("B");
    auto c = std::make_shared<FakeWindow>("C");

    history.Touch(a);
    history.Touch(b);
    history.Touch(c);   // order: C B A

    // From C, the last used is B, not A.
    CHECK(history.FindMostRecent(c.get(), anyWindow) == b);

    // Re-using A moves it to the front: from A the last used is now C.
    history.Touch(a);   // order: A C B
    CHECK(history.FindMostRecent(a.get(), anyWindow) == c);
}

static void TestSkipsIneligibleEntries() {
    UCWeakMRUList<FakeWindow> history;
    auto a = std::make_shared<FakeWindow>("A");
    auto b = std::make_shared<FakeWindow>("B");
    auto c = std::make_shared<FakeWindow>("C");

    history.Touch(a);
    history.Touch(b);
    history.Touch(c);   // order: C B A

    // B hidden (e.g. minimized-to-hidden or closing): jump from C lands on A.
    b->visible = false;
    CHECK(history.FindMostRecent(c.get(), visibleWindow) == a);

    // Nothing visible besides the current window -> no jump target.
    a->visible = false;
    CHECK(history.FindMostRecent(c.get(), visibleWindow) == nullptr);
}

static void TestDestroyedWindowExpires() {
    UCWeakMRUList<FakeWindow> history;
    auto a = std::make_shared<FakeWindow>("A");
    auto b = std::make_shared<FakeWindow>("B");

    history.Touch(a);
    history.Touch(b);
    CHECK_EQ(history.Size(), size_t(2));

    // Window destroyed without an explicit Remove(): the weak_ptr expires and
    // must never be returned.
    a.reset();
    CHECK(history.FindMostRecent(b.get(), anyWindow) == nullptr);
    CHECK_EQ(history.Size(), size_t(1));
}

static void TestExplicitRemove() {
    UCWeakMRUList<FakeWindow> history;
    auto a = std::make_shared<FakeWindow>("A");
    auto b = std::make_shared<FakeWindow>("B");
    auto c = std::make_shared<FakeWindow>("C");

    history.Touch(a);
    history.Touch(b);
    history.Touch(c);

    history.Remove(b.get());    // window closed
    CHECK_EQ(history.Size(), size_t(2));
    CHECK(history.FindMostRecent(c.get(), anyWindow) == a);
}

static void TestEmptyAndSingleEntry() {
    UCWeakMRUList<FakeWindow> history;
    auto a = std::make_shared<FakeWindow>("A");

    // Empty history: nothing to jump to.
    CHECK(history.FindMostRecent(nullptr, anyWindow) == nullptr);

    // Only the current window in history: still nothing to jump to.
    history.Touch(a);
    CHECK(history.FindMostRecent(a.get(), anyWindow) == nullptr);

    // But from "no current window" the single entry is a valid target.
    CHECK(history.FindMostRecent(nullptr, anyWindow) == a);
}

static void TestTouchDeduplicates() {
    UCWeakMRUList<FakeWindow> history;
    auto a = std::make_shared<FakeWindow>("A");

    history.Touch(a);
    history.Touch(a);
    history.Touch(a);
    CHECK_EQ(history.Size(), size_t(1));
}

static void TestCapDropsOldest() {
    UCWeakMRUList<FakeWindow> history;
    std::vector<std::shared_ptr<FakeWindow>> keep;

    // One more than the 32-entry cap; the oldest entry must fall off.
    for (int i = 0; i < 33; ++i) {
        auto w = std::make_shared<FakeWindow>("W" + std::to_string(i));
        keep.push_back(w);
        history.Touch(w);
    }
    CHECK_EQ(history.Size(), size_t(32));

    auto oldestOnly = [&](const std::shared_ptr<FakeWindow>& w) { return w == keep.front(); };
    CHECK(history.FindMostRecent(nullptr, oldestOnly) == nullptr);
}

int main() {
    TestToggleBetweenTwoWindows();
    TestMostRecentOfSeveral();
    TestSkipsIneligibleEntries();
    TestDestroyedWindowExpires();
    TestExplicitRemove();
    TestEmptyAndSingleEntry();
    TestTouchDeduplicates();
    TestCapDropsOldest();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
