// Tests/ClipboardTargetsTest.cpp
// Every format another program offers on the clipboard is seen (X11).
//
// A TARGETS list is a property of format 32, which Xlib hands back as an
// array of C longs - 8 bytes each on a 64-bit system. The X11 backend copied
// it at 4 bytes an item and then read 8-byte atoms from the copy, so
// GetAvailableFormats() and IsFormatAvailable() saw the first half of the
// list only: UltraFiler's Paste stayed off when the image or text type was in
// the second half.
//
// The program offering the formats is a second X connection in this process,
// on a thread of its own: it owns CLIPBOARD and answers TARGETS with eight
// atoms. Runs headless under Xvfb; skips when there is no display.
// Version: 1.0.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        std::cerr << "SKIP: " << reason << std::endl;                         \
        return 0;                                                             \
    } while (0)

namespace {

const std::vector<std::string> kOffered = {
    "TARGETS", "UTF8_STRING", "STRING", "text/plain",
    "text/html", "image/png", "application/x-ultracanvas-test-a", "application/x-ultracanvas-test-b"};

// Another program on the clipboard: owns CLIPBOARD and answers TARGETS.
class OtherProgram {
public:
    bool Start() {
        display = XOpenDisplay(nullptr);
        if (!display) return false;
        window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
        clipboard = XInternAtom(display, "CLIPBOARD", False);
        targets = XInternAtom(display, "TARGETS", False);
        for (const std::string& name : kOffered) atoms.push_back(XInternAtom(display, name.c_str(), False));
        XSetSelectionOwner(display, clipboard, window, CurrentTime);
        XSync(display, False);
        if (XGetSelectionOwner(display, clipboard) != window) return false;
        worker = std::thread([this]() { Serve(); });
        return true;
    }

    void Stop() {
        running = false;
        if (worker.joinable()) worker.join();
        if (display) XCloseDisplay(display);
        display = nullptr;
    }

private:
    void Serve() {
        while (running) {
            while (XPending(display) > 0) {
                XEvent event;
                XNextEvent(display, &event);
                if (event.type != SelectionRequest) continue;
                const XSelectionRequestEvent& request = event.xselectionrequest;
                XSelectionEvent reply = {};
                reply.type = SelectionNotify;
                reply.display = request.display;
                reply.requestor = request.requestor;
                reply.selection = request.selection;
                reply.target = request.target;
                reply.time = request.time;
                reply.property = None;
                if (request.target == targets && request.property != None) {
                    // Format 32 is passed to Xlib as longs, whatever their size.
                    std::vector<long> list(atoms.begin(), atoms.end());
                    XChangeProperty(display, request.requestor, request.property, XA_ATOM, 32,
                                    PropModeReplace, reinterpret_cast<unsigned char*>(list.data()),
                                    static_cast<int>(list.size()));
                    reply.property = request.property;
                }
                XSendEvent(display, request.requestor, False, 0, reinterpret_cast<XEvent*>(&reply));
                XFlush(display);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    Display* display = nullptr;
    Window window = 0;
    Atom clipboard = 0;
    Atom targets = 0;
    std::vector<Atom> atoms;
    std::atomic<bool> running{true};
    std::thread worker;
};

} // namespace

int main() {
    std::cerr << "=== Clipboard targets (X11) ===" << std::endl;
    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");
    if (!XInitThreads()) SKIP_ALL("XInitThreads failed");

    UltraCanvasApplication app;
    if (!app.Initialize("ClipboardTargetsTest")) SKIP_ALL("application would not initialise");
    UltraCanvasClipboard* clipboard = GetClipboard();
    if (!clipboard) SKIP_ALL("no clipboard");

    OtherProgram other;
    if (!other.Start()) {
        other.Stop();
        SKIP_ALL("could not own the clipboard from a second connection");
    }

    const std::vector<std::string> formats = clipboard->GetAvailableFormats();
    std::string seen;
    for (const std::string& f : formats) seen += (seen.empty() ? "" : ", ") + f;
    TEST("all eight offered formats are seen (got " + std::to_string(formats.size()) + ": " + seen + ")",
         formats.size() == kOffered.size());
    for (const std::string& name : kOffered) {
        TEST("offered: " + name, std::find(formats.begin(), formats.end(), name) != formats.end());
    }
    TEST("the last one answers IsFormatAvailable", clipboard->IsFormatAvailable("application/x-ultracanvas-test-b"));
    TEST("one not offered does not", !clipboard->IsFormatAvailable("image/jpeg"));

    other.Stop();
    std::cerr << "\n" << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
