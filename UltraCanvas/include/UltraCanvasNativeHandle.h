//
// Created by slenz on 7/15/26.
//
#pragma once
#ifndef ULTRACANVASDEMOAPP_ULTRACANVASNATIVEHANDLE_H
#define ULTRACANVASDEMOAPP_ULTRACANVASNATIVEHANDLE_H

#if defined(_WIN32) || defined(_WIN64)
    struct HWND__;   // matches windows.h's DECLARE_HANDLE(HWND)
    namespace UltraCanvas { using NativeWindowHandle = HWND__*; }      // == HWND
#elif defined(__linux__) || defined(__unix__)
    namespace UltraCanvas { using NativeWindowHandle = unsigned long; } // == XID/Window
#elif defined(__APPLE__)
    #ifdef __OBJC__
            @class NSWindow;
    #else
        typedef struct objc_object NSWindow;
    #endif
    namespace UltraCanvas { using NativeWindowHandle = NSWindow*; }
#endif

#endif //ULTRACANVASDEMOAPP_ULTRACANVASNATIVEHANDLE_H
