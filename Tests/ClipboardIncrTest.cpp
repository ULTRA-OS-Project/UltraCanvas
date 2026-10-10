// Tests/ClipboardIncrTest.cpp
// A copy too large for one X request travels in pieces (ICCCM INCR), both
// ways (X11).
//
// The owner answers with an INCR marker and then writes the copy piece by
// piece, each when the requestor has deleted the one before; an empty piece
// ends it. The X11 clipboard did neither half: a large picture copied in
// GIMP or a browser came back as the marker's few bytes, and a large one
// copied here was sent in one request, which an X server without
// BIG-REQUESTS refuses.
//
// The other program is a second X connection in this process, on a thread of
// its own, written from the ICCCM rather than from the clipboard's code. When
// xclip is installed the same is checked against it. Runs headless under
// Xvfb; skips when there is no display.
// Version: 1.0.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
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

using Clock = std::chrono::steady_clock;

std::vector<uint8_t> Pattern(size_t size, unsigned seed) {
    std::vector<uint8_t> data(size);
    uint32_t x = 2463534242u + seed;
    for (size_t i = 0; i < size; ++i) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        data[i] = static_cast<uint8_t>(x);
    }
    return data;
}

// Reads a whole property, deleting it - as a requestor does.
bool ReadProperty(Display* d, Window w, Atom property, std::vector<uint8_t>& out, Atom& type) {
    int format = 0;
    unsigned long items = 0, after = 0;
    unsigned char* prop = nullptr;
    if (XGetWindowProperty(d, w, property, 0, 64L * 1024 * 1024, True, AnyPropertyType, &type, &format, &items,
                           &after, &prop) != Success) {
        return false;
    }
    const size_t itemSize = format == 32 ? sizeof(long) : format == 16 ? sizeof(short) : 1;
    out.assign(prop, prop + (prop ? items * itemSize : 0));
    if (prop) XFree(prop);
    return true;
}

// Another program that owns CLIPBOARD and offers image/png: in one property,
// or in pieces of `piece` bytes - stopping after `stallAfter` pieces when set.
class Owner {
public:
    enum class Mode { Whole, Pieces };

    bool Start(std::vector<uint8_t> payload, Mode mode, size_t piece = 64 * 1024, int stallAfter = -1) {
        data = std::move(payload);
        this->mode = mode;
        this->piece = piece;
        this->stallAfter = stallAfter;
        d = XOpenDisplay(nullptr);
        if (!d) return false;
        w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 1, 1, 0, 0, 0);
        clipboard = XInternAtom(d, "CLIPBOARD", False);
        targets = XInternAtom(d, "TARGETS", False);
        png = XInternAtom(d, "image/png", False);
        incr = XInternAtom(d, "INCR", False);
        XSetSelectionOwner(d, clipboard, w, CurrentTime);
        XSync(d, False);
        if (XGetSelectionOwner(d, clipboard) != w) return false;
        worker = std::thread([this]() { Serve(); });
        return true;
    }

    void Stop() {
        running = false;
        if (worker.joinable()) worker.join();
        if (d) XCloseDisplay(d);
        d = nullptr;
    }

    ~Owner() { Stop(); }

private:
    void Serve() {
        Window requestor = 0;
        Atom property = None;
        size_t offset = 0;
        int written = 0;
        bool sending = false;
        while (running) {
            while (XPending(d) > 0) {
                XEvent event;
                XNextEvent(d, &event);
                if (event.type == SelectionRequest) {
                    const XSelectionRequestEvent& request = event.xselectionrequest;
                    XSelectionEvent reply = {};
                    reply.type = SelectionNotify;
                    reply.display = request.display;
                    reply.requestor = request.requestor;
                    reply.selection = request.selection;
                    reply.target = request.target;
                    reply.time = request.time;
                    reply.property = request.property != None ? request.property : request.target;
                    if (request.target == targets) {
                        std::vector<long> list = {static_cast<long>(targets), static_cast<long>(png)};
                        XChangeProperty(d, request.requestor, reply.property, XA_ATOM, 32, PropModeReplace,
                                        reinterpret_cast<unsigned char*>(list.data()), 2);
                    } else if (request.target == png && mode == Mode::Whole) {
                        XChangeProperty(d, request.requestor, reply.property, png, 8, PropModeReplace,
                                        data.data(), static_cast<int>(data.size()));
                    } else if (request.target == png) {
                        // ICCCM 2.7.2: listen first, then the marker with a lower bound
                        XSelectInput(d, request.requestor, PropertyChangeMask);
                        long size = static_cast<long>(data.size());
                        XChangeProperty(d, request.requestor, reply.property, incr, 32, PropModeReplace,
                                        reinterpret_cast<unsigned char*>(&size), 1);
                        requestor = request.requestor;
                        property = reply.property;
                        offset = 0;
                        written = 0;
                        sending = true;
                    } else {
                        reply.property = None;
                    }
                    XSendEvent(d, request.requestor, False, 0, reinterpret_cast<XEvent*>(&reply));
                    XFlush(d);
                } else if (event.type == PropertyNotify && sending && event.xproperty.window == requestor &&
                           event.xproperty.atom == property && event.xproperty.state == PropertyDelete) {
                    if (stallAfter >= 0 && written >= stallAfter) continue;   // stops answering
                    const size_t n = std::min(piece, data.size() - offset);
                    XChangeProperty(d, requestor, property, png, 8, PropModeReplace, data.data() + offset,
                                    static_cast<int>(n));
                    offset += n;
                    ++written;
                    if (n == 0) {
                        sending = false;
                        XSelectInput(d, requestor, NoEventMask);
                    }
                    XFlush(d);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::vector<uint8_t> data;
    Mode mode = Mode::Whole;
    size_t piece = 64 * 1024;
    int stallAfter = -1;
    Display* d = nullptr;
    Window w = 0;
    Atom clipboard = None, targets = None, png = None, incr = None;
    std::atomic<bool> running{true};
    std::thread worker;
};

// Another program pasting: asks CLIPBOARD for `target` and follows INCR.
struct Fetched {
    bool ok = false;
    bool inPieces = false;
    int pieces = 0;
    std::vector<uint8_t> data;
};

Fetched Fetch(const char* targetName, int timeoutMs = 10000) {
    Fetched result;
    Display* d = XOpenDisplay(nullptr);
    if (!d) return result;
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(d, w, PropertyChangeMask);
    const Atom clipboard = XInternAtom(d, "CLIPBOARD", False);
    const Atom target = XInternAtom(d, targetName, False);
    const Atom property = XInternAtom(d, "ULTRACANVAS_TEST_PASTE", False);
    const Atom incr = XInternAtom(d, "INCR", False);
    XConvertSelection(d, clipboard, target, property, w, CurrentTime);
    XFlush(d);
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    bool done = false;
    while (!done && Clock::now() < deadline) {
        while (!done && XPending(d) > 0) {
            XEvent event;
            XNextEvent(d, &event);
            if (event.type == SelectionNotify) {
                if (event.xselection.property == None) {
                    done = true;
                    break;
                }
                std::vector<uint8_t> bytes;
                Atom type = None;
                if (!ReadProperty(d, w, property, bytes, type)) {
                    done = true;
                    break;
                }
                if (type == incr) {
                    result.inPieces = true;   // deleting the marker asked for the first piece
                } else {
                    result.data = std::move(bytes);
                    result.ok = true;
                    done = true;
                }
            } else if (event.type == PropertyNotify && result.inPieces && event.xproperty.atom == property &&
                       event.xproperty.state == PropertyNewValue) {
                std::vector<uint8_t> bytes;
                Atom type = None;
                if (!ReadProperty(d, w, property, bytes, type) || type == None) continue;
                if (bytes.empty()) {
                    result.ok = true;
                    done = true;
                } else {
                    ++result.pieces;
                    result.data.insert(result.data.end(), bytes.begin(), bytes.end());
                }
            }
        }
        if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    XCloseDisplay(d);
    return result;
}

// The application's event loop, as CollectAndProcessNativeEvents runs it,
// until `done` or the time is up.
void PumpUntil(UltraCanvasApplication& app, const std::function<bool()>& done, int timeoutMs = 15000) {
    Display* d = app.GetDisplay();
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!done() && Clock::now() < deadline) {
        while (XPending(d) > 0) {
            XEvent event;
            XNextEvent(d, &event);
            app.ProcessXEvent(event);
        }
        pollfd connection = {ConnectionNumber(d), POLLIN, 0};
        ::poll(&connection, 1, 2);
    }
}

Fetched FetchWhilePumping(UltraCanvasApplication& app, const char* target) {
    Fetched result;
    std::atomic<bool> finished{false};
    std::thread paster([&]() {
        result = Fetch(target);
        finished = true;
    });
    PumpUntil(app, [&]() { return finished.load(); });
    paster.join();
    return result;
}

bool HaveXclip() {
    return std::system("command -v xclip >/dev/null 2>&1") == 0;
}

std::string TempDir() {
    const char* base = std::getenv("TMPDIR");
    std::string pattern = std::string(base && *base ? base : "/tmp") + "/ultracanvas-incr-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    return mkdtemp(buffer.data()) ? std::string(buffer.data()) : std::string();
}

bool WriteFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

std::vector<uint8_t> ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    std::cerr << "=== Clipboard copies in pieces (X11 INCR) ===" << std::endl;
    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");
    if (!XInitThreads()) SKIP_ALL("XInitThreads failed");

    UltraCanvasApplication app;
    if (!app.Initialize("ClipboardIncrTest")) SKIP_ALL("application would not initialise");
    UltraCanvasClipboard* clipboard = GetClipboard();
    if (!clipboard) SKIP_ALL("no clipboard");
    Display* display = app.GetDisplay();

    // ===== Reading from another program =====
    {
        const std::vector<uint8_t> big = Pattern(3 * 1024 * 1024 + 1, 1);
        Owner owner;
        if (!owner.Start(big, Owner::Mode::Pieces)) SKIP_ALL("could not own the clipboard from a second connection");

        // A window's event sent before the read must still be queued after it.
        Window probe = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
        XEvent message = {};
        message.xclient.type = ClientMessage;
        message.xclient.window = probe;
        message.xclient.message_type = XInternAtom(display, "ULTRACANVAS_TEST_PROBE", False);
        message.xclient.format = 32;
        XSendEvent(display, probe, False, NoEventMask, &message);
        XFlush(display);

        std::vector<uint8_t> image;
        std::string format;
        const bool read = clipboard->GetImage(image, format);
        TEST("a 3 MB picture sent in 64 KB pieces is read", read);
        TEST("...all of it (" + std::to_string(image.size()) + " of " + std::to_string(big.size()) + " bytes)",
             image.size() == big.size());
        TEST("...byte for byte", image == big);
        TEST("...as image/png", format == "image/png");
        XEvent probed;
        TEST("an application event that arrived during the read is still queued",
             XCheckTypedWindowEvent(display, probe, ClientMessage, &probed));
        XDestroyWindow(display, probe);

        std::vector<uint8_t> again;
        TEST("a second read of it works too", clipboard->GetImage(again, format) && again == big);
        owner.Stop();
    }
    {
        const std::vector<uint8_t> whole = Pattern(1536 * 1024, 2);
        Owner owner;
        if (owner.Start(whole, Owner::Mode::Whole)) {
            std::vector<uint8_t> image;
            std::string format;
            TEST("a 1.5 MB picture in one property is read whole",
                 clipboard->GetImage(image, format) && image == whole);
        }
        owner.Stop();
    }
    {
        Owner owner;
        if (owner.Start(Pattern(2 * 1024 * 1024, 3), Owner::Mode::Pieces, 64 * 1024, 4)) {
            std::vector<uint8_t> image;
            std::string format;
            const auto start = Clock::now();
            const bool read = clipboard->GetImage(image, format);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
            TEST("an owner that stops after four pieces gives no picture", !read && image.empty());
            TEST("...and the read gives up within seconds (" + std::to_string(ms) + " ms)", ms < 8000);
        }
        owner.Stop();
    }
    {
        const std::vector<uint8_t> next = Pattern(700 * 1024, 4);
        Owner owner;
        if (owner.Start(next, Owner::Mode::Pieces)) {
            std::vector<uint8_t> image;
            std::string format;
            TEST("after that, the next copy in pieces is read",
                 clipboard->GetImage(image, format) && image == next);
        }
        owner.Stop();
    }

    // ===== Serving to another program =====
    {
        const std::vector<uint8_t> big = Pattern(2500 * 1024, 5);
        TEST("a 2.5 MB picture is put on the clipboard", clipboard->SetImage(big, "image/png"));
        const Fetched pasted = FetchWhilePumping(app, "image/png");
        TEST("another program pastes it", pasted.ok);
        TEST("...in pieces (" + std::to_string(pasted.pieces) + ")", pasted.inPieces && pasted.pieces >= 2);
        TEST("...byte for byte", pasted.data == big);

        const Fetched twice = FetchWhilePumping(app, "image/png");
        TEST("...and again", twice.ok && twice.data == big);
    }
    {
        const std::vector<uint8_t> small = Pattern(10 * 1024, 6);
        clipboard->SetImage(small, "image/png");
        const Fetched pasted = FetchWhilePumping(app, "image/png");
        TEST("a 10 KB picture is pasted in one piece", pasted.ok && !pasted.inPieces && pasted.data == small);
    }
    {
        // Another program takes the clipboard and goes; its notice that we
        // lost it is still queued when we copy again, and is handled after.
        Owner other;
        other.Start(Pattern(16, 10), Owner::Mode::Whole);
        other.Stop();
        const std::vector<uint8_t> copied = Pattern(600 * 1024, 11);
        clipboard->SetImage(copied, "image/png");
        const Fetched pasted = FetchWhilePumping(app, "image/png");
        TEST("a late notice of losing the clipboard keeps the copy made since",
             pasted.ok && pasted.data == copied);
    }
    {
        const std::vector<uint8_t> big = Pattern(5 * 1024 * 1024, 7);
        clipboard->SetImage(big, "image/png");
        std::vector<uint8_t> image;
        std::string format;
        TEST("a 5 MB picture copied here reads back here, in pieces both ways",
             clipboard->GetImage(image, format) && image == big);
    }

    // ===== xclip, when installed =====
    if (!HaveXclip()) {
        std::cerr << "SKIP: xclip is not installed" << std::endl;
    } else {
        const std::string dir = TempDir();
        const std::vector<uint8_t> fromXclip = Pattern(3 * 1024 * 1024, 8);
        const std::string inFile = dir + "/in.png";
        const std::string outFile = dir + "/out.png";
        if (!dir.empty() && WriteFile(inFile, fromXclip)) {
            // xclip answers in pieces above about 1 MB, and stays in the
            // background until the clipboard is taken from it - with its
            // output away from ours, which a test runner reads to the end.
            const Window before = XGetSelectionOwner(display, XInternAtom(display, "CLIPBOARD", False));
            const int started = std::system(
                ("xclip -selection clipboard -t image/png -i '" + inFile + "' </dev/null >/dev/null 2>&1").c_str());
            Window owner = before;
            const auto deadline = Clock::now() + std::chrono::seconds(5);
            while (started == 0 && owner == before && Clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                owner = XGetSelectionOwner(display, XInternAtom(display, "CLIPBOARD", False));
            }
            std::vector<uint8_t> image;
            std::string format;
            TEST("a 3 MB picture copied in xclip is read here",
                 owner != before && clipboard->GetImage(image, format) && image == fromXclip);

            const std::vector<uint8_t> toXclip = Pattern(3 * 1024 * 1024 + 7, 9);
            clipboard->SetImage(toXclip, "image/png");   // xclip loses the clipboard and quits
            std::atomic<bool> finished{false};
            int pasted = -1;
            std::thread paster([&]() {
                pasted = std::system(("xclip -selection clipboard -t image/png -o > '" + outFile + "'").c_str());
                finished = true;
            });
            PumpUntil(app, [&]() { return finished.load(); });
            paster.join();
            TEST("a 3 MB picture copied here is pasted by xclip", pasted == 0 && ReadFile(outFile) == toXclip);
        }
        std::remove(inFile.c_str());
        std::remove(outFile.c_str());
        if (!dir.empty()) ::rmdir(dir.c_str());
    }

    std::cerr << "\n" << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
