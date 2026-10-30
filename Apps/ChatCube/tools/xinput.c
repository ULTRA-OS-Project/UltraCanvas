// Apps/ChatCube/tools/xinput.c
// Clicks, typing and key presses on the current DISPLAY through the XTest
// extension, for screenshot.sh: a ChatCube under Xvfb is driven like a
// person would, with no hooks in the app.
//
//   xinput click X Y        press and release the left button at X, Y
//   xinput type "text"      type ASCII text (a capital or a shifted symbol
//                           holds Shift)
//   xinput key KeySym       one key by its X name: Return, Tab, Escape
//   xinput sleep MS         wait
//
// The verbs follow one another on one command line, in order.
// Build: cc -O1 -o xinput xinput.c -lX11 -lXtst (screenshot.sh does it).
//
// Version: 0.1.0
// Last Modified: 2026-10-28
// Author: UltraCanvas Framework / ULTRA OS
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Display* display;

static void PressKey(KeySym keysym, int shift) {
    KeyCode code = XKeysymToKeycode(display, keysym);
    KeyCode shiftCode = XKeysymToKeycode(display, XK_Shift_L);
    if (!code) {
        fprintf(stderr, "xinput: no key for %s\n", XKeysymToString(keysym));
        return;
    }
    if (shift) XTestFakeKeyEvent(display, shiftCode, True, 0);
    XTestFakeKeyEvent(display, code, True, 0);
    XTestFakeKeyEvent(display, code, False, 0);
    if (shift) XTestFakeKeyEvent(display, shiftCode, False, 0);
    XFlush(display);
    usleep(12000);
}

int main(int argc, char** argv) {
    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "xinput: no display\n");
        return 1;
    }
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "click") && i + 2 < argc) {
            XTestFakeMotionEvent(display, -1, atoi(argv[i + 1]), atoi(argv[i + 2]), 0);
            XFlush(display);
            usleep(60000);
            XTestFakeButtonEvent(display, 1, True, 0);
            XFlush(display);
            usleep(40000);
            XTestFakeButtonEvent(display, 1, False, 0);
            XFlush(display);
            usleep(120000);
            i += 2;
        } else if (!strcmp(argv[i], "type") && i + 1 < argc) {
            for (const char* p = argv[++i]; *p; ++p) {
                const unsigned char c = (unsigned char)*p;
                KeySym keysym;
                int shift = 0;
                if (c >= 'A' && c <= 'Z') {
                    keysym = c;
                    shift = 1;
                } else if (c == ' ') {
                    keysym = XK_space;
                } else {
                    keysym = c;
                    shift = strchr("~!@#$%^&*()_+{}|:\"<>?", c) != NULL;
                }
                PressKey(keysym, shift);
            }
        } else if (!strcmp(argv[i], "key") && i + 1 < argc) {
            PressKey(XStringToKeysym(argv[++i]), 0);
        } else if (!strcmp(argv[i], "sleep") && i + 1 < argc) {
            usleep(1000 * atoi(argv[++i]));
        } else {
            fprintf(stderr, "xinput: bad argument %s\n", argv[i]);
            return 2;
        }
    }
    XCloseDisplay(display);
    return 0;
}
