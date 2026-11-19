// Tests/ExitDuringWarmUpTest.cpp
// A program that starts the application and returns at once exits cleanly,
// with no thread of the framework's still loading libraries into the exit.
//
// The application starts ImageMagick's warm-up on a thread of its own at
// launch (UCImageRaster::WarmUpImageMagickInBackground). A program done
// before it - a test, a tool that never runs the main loop - used to leave it
// running into exit, where its dlopen of ImageMagick's coders could come
// after glibc had run libgomp's finaliser and start libgomp a second time:
// "goacc_register: Assertion `!dispatchers[disp->type]' failed", which
// ClipboardTargetsTest hit once in many runs. CTest runs this with
// ExitHoldAudit as LD_AUDIT, which holds the process after libgomp's
// finaliser, so such a thread aborts the exit every time; the application's
// destructor now waits for the warm-up (FinishImageMagickWarmUp), and the
// exit is clean. Where there is no ImageMagick (no warm-up, and possibly no
// libgomp) it passes for want of anything to check.
//
// Needs a display for the application to start; skips without one.
// Version: 1.0.0
// Last Modified: 2026-11-19
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "DisplayRequired.h"

#include <cstdlib>
#include <iostream>

using namespace UltraCanvas;

int main() {
    std::cerr << "=== Exit while the ImageMagick warm-up runs ===" << std::endl;
    if (!std::getenv("DISPLAY")) {
        if (DisplayTest::DisplayRequired()) {
            std::cerr << "FAIL: a display was required, and there is no DISPLAY" << std::endl;
            return 1;
        }
        std::cerr << "SKIP: no DISPLAY" << std::endl;
        return 0;
    }
    {
        UltraCanvasApplication app;
        if (!app.Initialize("ExitDuringWarmUpTest")) {
            if (DisplayTest::DisplayRequired()) {
                std::cerr << "FAIL: a display was required, and the application would not initialise" << std::endl;
                return 1;
            }
            std::cerr << "SKIP: the application would not initialise" << std::endl;
            return 0;
        }
        // No main loop: the application goes out of scope at once, while
        // the warm-up it started may still be loading ImageMagick.
    }
    // What is checked is what follows: the exit. Under ExitHoldAudit a
    // thread still loading libraries aborts it.
    std::cerr << "PASS: the application ended; the exit follows" << std::endl;
    return 0;
}
