// OS/MacOS/UltraCanvasMacOSWindow.h
// macOS window implementation with Cocoa/Cairo support
// Version: 2.4.0 - Input method composition: the content view is an
//   NSTextInputClient, and an element that draws compositions gets
//   TextComposition events (InterpretKeyDown, the Ime* callbacks)
// Version: 2.3.0 - GetContentView() for the VoiceOver (NSAccessibility) bridge
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_MACOS_WINDOW_H
#define ULTRACANVAS_MACOS_WINDOW_H

// ===== CORE INCLUDES =====
// NOTE: Do NOT include UltraCanvasWindow.h here - it creates a circular dependency.
// UltraCanvasWindow.h includes this header after defining UltraCanvasWindowBase.
#include "../../libspecific/Cairo/RenderContextCairo.h"

// ===== MACOS PLATFORM INCLUDES =====
#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
    #import <QuartzCore/QuartzCore.h>
#else
// Forward declarations for C++ only files
typedef struct objc_object NSWindow;
typedef struct objc_object NSView;
typedef struct objc_object NSEvent;
typedef struct CGContext* CGContextRef;
#endif

// ===== CAIRO INCLUDES =====
#include <cairo/cairo.h>
#include <cairo/cairo-quartz.h>

// ===== STANDARD INCLUDES =====
#include <memory>
#include <string>
#include <mutex>

namespace UltraCanvas {

// Forward declarations
    class UltraCanvasMacOSApplication;

// ===== MACOS WINDOW CLASS =====
    class UltraCanvasMacOSWindow : public UltraCanvasWindowBase {
    friend UltraCanvasMacOSApplication;
    private:
        // ===== COCOA WINDOW SYSTEM =====
        NSWindow* nsWindow;
        NSView* contentView;
        void* windowDelegate;  // NSWindowDelegate*

        bool pendingShow = false;

        // ===== INPUT METHOD COMPOSITION =====
        // The text an input method is composing (NSTextInputClient marked
        // text), in UTF-8, with its length and selection in UTF-16 units as
        // AppKit counts them. Empty when nothing is being composed.
        std::string imeMarkedText;
        unsigned long imeMarkedLength16 = 0;
        unsigned long imeSelectionLocation16 = 0;
        unsigned long imeSelectionLength16 = 0;
        // The key event being interpreted by the input method (NSEvent*,
        // unretained, only set during InterpretKeyDown); null otherwise.
        void* imeKeyEvent = nullptr;
        // Whether anything came back for that key (text, a composition, a
        // command); a key the input method swallowed without either is
        // delivered plain.
        bool imeKeyDelivered = false;

        void PushImeComposition(const std::string& text, int cursorBytes);
        void PushImeCommit(const std::string& text, bool fromKey);

        // ===== THREAD SAFETY =====
        mutable std::mutex cairoMutex;

        // ===== INTERNAL METHODS =====
        bool CreateNSWindow();
        bool CreateNativeCairoSurface();
        void DestroyNativeCairoSurface();

    protected:
        bool CreateNative() override;
        void DestroyNative() override;
        void DoResizeNative() override;
        // Query the live per-window backing scale ([nsWindow backingScaleFactor]).
        float QueryNativeDeviceScale() const override;
        // Rebuild the offscreen Cairo surface at the current backing scale.
        bool RecreateNativeSurface() override;

    public:
        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasMacOSWindow();

        // ===== INHERITED FROM BASE WINDOW =====
        void Show() override;
        void Hide() override;
        void SetWindowTitle(const std::string& title) override;
        void SetWindowIcon(const std::string& iconPath) override;
        void SetWindowSize(int width, int height) override;
        void SetWindowPosition(int x, int y) override;
        void SetResizable(bool resizable) override;
        void Minimize() override;
        void Maximize() override;
        void Restore() override;
        void SetFullscreen(bool fullscreen) override;
        void InvalidateWindowNative() override;
        NativeWindowHandle GetNativeHandle() const override;
        void GetScreenSize(int& width, int& height) const override;
        void GetScreenBounds(int& x, int& y, int& width, int& height) const override;
        // macOS window geometry is in logical points (Cocoa applies the backing
        // scale itself), so the native window size equals the logical size.
        void GetNativeWindowSize(int& w, int& h) const override { w = config_.width; h = config_.height; }
        NSWindow* GetNSWindowHandle() const;
        void RaiseAndFocus() override;

        void Focus();  // Not virtual in base class

        // ===== GETTERS =====
        NSWindow* GetNSWindow() const { return nsWindow; }
        // The flipped NSView the window draws into: window coordinates are its
        // coordinates (points). The accessibility bridge places elements in it.
        NSView* GetContentView() const { return contentView; }

        // ===== INPUT METHOD COMPOSITION =====
        // Hands a key press to the input method (interpretKeyEvents:) when
        // the focused element draws compositions (DrawsTextComposition()) or
        // a composition is in progress; the key then comes back as typed text
        // (KeyDown with `text`), as a TextComposition, or as the plain
        // KeyDown it would have been. False when the key is left to the
        // ordinary path, unchanged: another element, or Command / Control held.
        bool InterpretKeyDown(NSEvent* event);
        // Ends a composition in progress, keeping its text as typed (a click
        // elsewhere, the window losing the keyboard).
        void CommitComposition();
        // The content view's NSTextInputClient methods land here.
        void ImeInsertText(const std::string& text);
        void ImeSetMarkedText(const std::string& text, unsigned long length16,
                              unsigned long selectionLocation16, unsigned long selectionLength16,
                              int cursorBytes);
        void ImeUnmarkText();
        void ImeDoCommand();
        bool ImeHasMarkedText() const { return imeMarkedLength16 > 0; }
        unsigned long ImeMarkedLength16() const { return imeMarkedLength16; }
        unsigned long ImeSelectionLocation16() const { return imeSelectionLocation16; }
        unsigned long ImeSelectionLength16() const { return imeSelectionLength16; }
        // Where the candidate window goes: the focused element's caret, in
        // window coordinates (its bounds when it has no text interface).
        Rect2Df ImeCaretRectInWindow();

        // ===== WINDOW DELEGATE CALLBACKS =====
        void OnWindowWillClose();
        void OnWindowDidResize();
        void OnWindowDidMove();
        void OnWindowDidBecomeKey();
        void OnWindowDidResignKey();
        void OnWindowDidMiniaturize();
        void OnWindowDidDeminiaturize();
        // Fired when the window's backing scale changes (e.g. dragged onto a
        // different display). Recreates the Cairo surface at the new pixel
        // size so rendering stays crisp on the new screen.
        void OnWindowDidChangeBackingProperties();
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_MACOS_WINDOW_H