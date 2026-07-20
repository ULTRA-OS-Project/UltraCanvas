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
    // IMPORTANT: use an opaque void* handle, NOT NSWindow*.
    // NSWindow is forward-declared differently depending on the translation unit:
    //   - Objective-C++ (.mm, __OBJC__ defined):  @class NSWindow;            -> real ObjC class
    //   - Plain C++     (.cpp):                    typedef struct objc_object  -> struct objc_object
    // Those two spellings mangle to different C++ symbol names (P8NSWindow vs
    // P11objc_object), so a method taking NativeWindowHandle would be *referenced*
    // from .mm files and *defined* in .cpp files under mismatched signatures,
    // producing "Undefined symbols" linker errors. void* mangles identically (Pv)
    // in every translation unit. Cast to/from NSWindow* with __bridge at the call sites.
    namespace UltraCanvas { using NativeWindowHandle = void*; }
#endif

#endif //ULTRACANVASDEMOAPP_ULTRACANVASNATIVEHANDLE_H
