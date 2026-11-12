// Tests/DialogStackingTest.cpp
// A dialog stays above its own window, not above every application's.
//
// DialogConfig switched alwaysOnTop on for every dialog. Windows always made
// that WS_EX_TOPMOST, and once X11 and macOS honoured the flag too
// (UltraCanvasWindow.h 2.8.0) every dialog - UltraFiler's Proxy settings,
// UltraMail's Account settings, every message box - was _NET_WM_STATE_ABOVE:
// on top of every other program's windows, and could not be sent behind
// them. The settings windows, built from a plain WindowConfig, behaved.
// A dialog is kept above its parent by WM_TRANSIENT_FOR instead, which
// ShowModal sets. Without the fix the "not above" checks fail.
//
// The config checks need no display; the window checks read the X11
// properties under Xvfb and skip without a DISPLAY (or on another backend).
// Version: 1.0.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasWindow.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
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

namespace {

// The atoms a window's _NET_WM_STATE (or another ATOM[] property) holds.
std::vector<Atom> ReadAtoms(Display* display, Window window, const char* property) {
    std::vector<Atom> atoms;
    Atom actualType = 0;
    int actualFormat = 0;
    unsigned long count = 0, bytesAfter = 0;
    unsigned char* data = nullptr;
    Atom name = XInternAtom(display, property, False);
    if (XGetWindowProperty(display, window, name, 0, 64, False, XA_ATOM, &actualType, &actualFormat,
                           &count, &bytesAfter, &data) == 0 &&
        data) {
        if (actualType == XA_ATOM && actualFormat == 32) {
            const auto* values = reinterpret_cast<const Atom*>(data);
            atoms.assign(values, values + count);
        }
        XFree(data);
    }
    return atoms;
}

bool Holds(const std::vector<Atom>& atoms, Display* display, const char* atomName) {
    const Atom wanted = XInternAtom(display, atomName, False);
    for (Atom a : atoms)
        if (a == wanted) return true;
    return false;
}

Window TransientFor(Display* display, Window window) {
    Window parent = 0;
    if (!XGetTransientForHint(display, window, &parent)) return 0;
    return parent;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Dialog Stacking Suite"                  << std::endl;
    std::cerr << "========================================" << std::endl;

    std::cerr << "\n--- The configs ---" << std::endl;
    TEST("A DialogConfig is not alwaysOnTop", !DialogConfig().alwaysOnTop);
    TEST("nor an InputDialogConfig", !InputDialogConfig().alwaysOnTop);
    TEST("nor a FileDialogConfig", !FileDialogConfig().alwaysOnTop);
    TEST("A DialogConfig is still a modal dialog",
         DialogConfig().modal && DialogConfig().type == WindowType::Dialog);
    TEST("A plain WindowConfig is not alwaysOnTop either", !WindowConfig().alwaysOnTop);

    UltraCanvasApplication app;
    Display* display = nullptr;
    if (std::getenv("DISPLAY") && app.Initialize("DialogStackingTest")) display = app.GetDisplay();

    if (!display) {
        std::cerr << "SKIP: the window checks (no X11 display)" << std::endl;
    } else {
        UltraCanvasDialogManager::SetUseNativeDialogs(false);

        WindowConfig mainConfig;
        mainConfig.title = "DialogStackingTest main";
        mainConfig.width = 400;
        mainConfig.height = 300;
        auto mainWindow = CreateWindow(mainConfig);
        mainWindow->Show();
        const auto mainX = static_cast<Window>(mainWindow->GetNativeHandle());

        std::cerr << "\n--- A dialog shown over its window ---" << std::endl;
        {
            // As UltraFiler's Proxy settings and UltraMail's Account settings
            // are made: a custom dialog, its parent given only to ShowModal.
            DialogConfig config;
            config.title = "Proxy settings";
            config.width = 320;
            config.height = 160;
            config.dialogType = DialogType::Custom;
            config.buttons = DialogButtons::NoButtons;
            auto dialog = UltraCanvasDialogManager::CreateDialog(config);
            dialog->AddChild(CreateContainer("stackingColumn", 0, 0, 0, 0));
            UltraCanvasDialogManager::ShowDialog(dialog, nullptr, mainWindow.get());
            XSync(display, False);

            const auto dialogX = static_cast<Window>(dialog->GetNativeHandle());
            TEST("The dialog has a window", dialogX != 0);
            const auto state = ReadAtoms(display, dialogX, "_NET_WM_STATE");
            TEST("It is not kept above every window (_NET_WM_STATE_ABOVE)",
                 !Holds(state, display, "_NET_WM_STATE_ABOVE"));
            TEST("It is transient for its own window (WM_TRANSIENT_FOR)",
                 mainX != 0 && TransientFor(display, dialogX) == mainX);
            TEST("It is typed a dialog (_NET_WM_WINDOW_TYPE_DIALOG)",
                 Holds(ReadAtoms(display, dialogX, "_NET_WM_WINDOW_TYPE"), display,
                       "_NET_WM_WINDOW_TYPE_DIALOG"));
            UltraCanvasDialogManager::CloseAllDialogs();
        }

        std::cerr << "\n--- A message box with no parent named ---" << std::endl;
        {
            // ShowModal falls back to the application's window.
            DialogConfig config;
            config.title = "Question";
            config.message = "Replace it?";
            config.dialogType = DialogType::Question;
            config.buttons = DialogButtons::YesNo;
            auto dialog = UltraCanvasDialogManager::CreateDialog(config);
            UltraCanvasDialogManager::ShowDialog(dialog, nullptr, nullptr);
            XSync(display, False);

            const auto dialogX = static_cast<Window>(dialog->GetNativeHandle());
            TEST("It is not kept above every window either",
                 dialogX != 0 && !Holds(ReadAtoms(display, dialogX, "_NET_WM_STATE"), display,
                                        "_NET_WM_STATE_ABOVE"));
            TEST("and is transient for the application's window",
                 mainX != 0 && TransientFor(display, dialogX) == mainX);
            UltraCanvasDialogManager::CloseAllDialogs();
        }

        std::cerr << "\n--- A window that asks to be above everything ---" << std::endl;
        {
            // The check above can see the state: a recording bar still gets it.
            WindowConfig barConfig;
            barConfig.title = "DialogStackingTest bar";
            barConfig.width = 200;
            barConfig.height = 40;
            barConfig.alwaysOnTop = true;
            auto bar = CreateWindow(barConfig);
            XSync(display, False);
            const auto barX = static_cast<Window>(bar->GetNativeHandle());
            TEST("An alwaysOnTop window is _NET_WM_STATE_ABOVE",
                 barX != 0 && Holds(ReadAtoms(display, barX, "_NET_WM_STATE"), display,
                                    "_NET_WM_STATE_ABOVE"));
            bar->Close();
        }

        mainWindow->Close();
    }

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
