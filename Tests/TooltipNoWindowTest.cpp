// Tests/TooltipNoWindowTest.cpp
// A tooltip asked for where there is nowhere to show it.
//
// An element that is not in a window yet - built but not added, drawn into an
// offscreen context, driven by a test - passes its window, nullptr, to
// UltraCanvasTooltipManager when the pointer rests on it. That crashed: first
// in UpdateTooltipPosition (the window's size), then in starting the show
// timer, which needs the application, and a program that only draws offscreen
// has none. Asked with no window, the manager must show nothing, start no
// timer and stay ready for the next tooltip; hiding must be safe too.
// Runs without an application and without a display, on every platform.
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasTooltipManager.h"

#include <iostream>
#include <string>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

} // namespace

int main() {
    std::cout << "TooltipNoWindowTest\n";
    Check(UltraCanvasApplication::GetInstance() == nullptr, "no application runs in this test");

    UltraCanvasTooltipManager::UpdateAndShowTooltip(nullptr, "A file\nColumn: Docs", Point2Di(40, 40));
    Check(!UltraCanvasTooltipManager::IsVisible() && !UltraCanvasTooltipManager::IsPending(),
          "a plain-text tooltip with no window shows nothing and waits for nothing");

    TooltipContent content;
    content.AddPlainText("Rows and all");
    UltraCanvasTooltipManager::UpdateAndShowTooltip(nullptr, content, Point2Di(10, 10));
    Check(!UltraCanvasTooltipManager::IsVisible() && !UltraCanvasTooltipManager::IsPending(),
          "nor does a structured one");

    UltraCanvasTooltipManager::UpdateAndShowTooltipImmediately(nullptr, "At once", Point2Di(5, 5));
    Check(!UltraCanvasTooltipManager::IsVisible(), "asked to show at once, still nothing");

    UltraCanvasTooltipManager::UpdateTooltipPosition(Point2Di(80, 80));
    Check(true, "moving it with no window is harmless");

    UltraCanvasTooltipManager::HideTooltip();
    UltraCanvasTooltipManager::HideTooltipImmediately();
    Check(!UltraCanvasTooltipManager::IsVisible() && !UltraCanvasTooltipManager::IsPending(),
          "and hiding it, slowly or at once, is too");

    std::cout << "\n" << (g_failures == 0 ? "All passed" : std::to_string(g_failures) + " failed") << "\n";
    return g_failures == 0 ? 0 : 1;
}
