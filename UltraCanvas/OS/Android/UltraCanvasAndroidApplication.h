// OS/Android/UltraCanvasAndroidApplication.h
// Android platform implementation for UltraCanvas Framework.
// Runs inside android_native_app_glue's android_main thread: the framework's
// blocking Run() loop is hosted there and CollectAndProcessNativeEvents pumps
// the glue's ALooper (activity commands + input queue).
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_ANDROID_APPLICATION_H
#define ULTRACANVAS_ANDROID_APPLICATION_H

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasWindow.h"
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasCommonTypes.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// NDK types are kept out of this header where an opaque forward declaration
// suffices, so including UltraCanvasApplication.h stays cheap.
struct android_app;            // android_native_app_glue
struct AInputEvent;            // <android/input.h>
struct ANativeWindow;          // <android/native_window.h>

namespace UltraCanvas {

    class UltraCanvasAndroidWindow;

    // ===== ANDROID APPLICATION CLASS =====
    class UltraCanvasAndroidApplication : public UltraCanvasApplicationBase {
    private:
        static UltraCanvasAndroidApplication* instance;

        // The android_native_app_glue state. Stored by the android_main glue
        // (SetAndroidApp) BEFORE the application object is constructed; the
        // pointer is owned by the glue and stays valid for the activity's
        // whole native lifetime.
        static android_app* androidApp;

        // Tap → double-click synthesis (same approach as the Linux backend's
        // MouseClickInfo, keyed on event time + distance).
        int64_t lastTapTimeNs = 0;
        int lastTapX = 0;
        int lastTapY = 0;
        int64_t doubleClickTimeMs = 300;
        int doubleClickDistance = 30;   // physical px; generous for fingers

    public:
        UltraCanvasAndroidApplication();

        static UltraCanvasAndroidApplication* GetInstance() {
            return UltraCanvasAndroidApplication::instance;
        }

        // Called by the android_main glue before the app's main() runs.
        static void SetAndroidApp(android_app* app) { androidApp = app; }
        static android_app* GetAndroidApp() { return androidApp; }

        // The activity's current surface, or null between APP_CMD_TERM_WINDOW
        // and the next APP_CMD_INIT_WINDOW.
        ANativeWindow* GetNativeWindow() const;

        void SetDoubleClickTime(unsigned int milliseconds) { doubleClickTimeMs = milliseconds; }
        void SetDoubleClickDistance(int pixels) { doubleClickDistance = pixels; }

        // Run a nested pump until `isResolved()` returns true, for a modal
        // Java dialog whose answer arrives on the Java UI thread. Returns
        // false if the activity is being destroyed instead (no answer will
        // come). See the .cpp for why only activity commands are processed.
        bool PumpWhileModal(const std::function<bool()>& isResolved);

        // ===== SOFT KEYBOARD =====
        // Driven automatically by UltraCanvasCaret::onTextEditingChanged (a
        // widget claiming the caret shows the keyboard, releasing it hides
        // the keyboard on the next event-loop turn), but also public so apps
        // can force either state. JNI InputMethodManager underneath - the
        // NDK's ANativeActivity_showSoftInput is unreliable by long-standing
        // platform bug.
        void ShowSoftKeyboard();
        void HideSoftKeyboard();

        // ===== IME DELIVERY (called from the Java InputConnection) =====
        // The InputConnection runs on the Java UI thread, so its calls are
        // queued in arrival order and applied on the glue thread, where the
        // focused element can be asked whether it draws compositions. One
        // queue for all of them: a composition overtaking the commit that
        // ends it would leave a stale pre-edit on screen.
        void QueueImeCommit(const std::string& utf8);
        // The text being composed and its caret (byte offset into `utf8`);
        // "" ends the composition without committing anything.
        void QueueImeComposition(const std::string& utf8, int caretBytes);
        // Keep the composition as typed text.
        void QueueImeFinishComposition();
        void QueueImeKeyEvent(bool down, int32_t keyCode, int32_t metaState,
                              int32_t codePoint);
        void QueueImeDeleteBefore(int count);

        // Text the IME committed - an autocorrected or gesture-typed word, a
        // CJK candidate - which belongs to no single key.
        void PushCommittedText(const std::string& utf8);
        // A key press routed through the Java input view. `codePoint` is
        // KeyEvent.getUnicodeChar (0 for non-printing); pass down=false for
        // the release. Shared with the native input path.
        bool PushKeyEvent(bool down, int32_t keyCode, int32_t metaState,
                          int32_t codePoint);

        // Android has no persistent pointer cursor; both are accepted no-ops.
        bool SelectMouseCursorNative(UltraCanvasWindowBase* win, UCMouseCursor cur) override;
        bool SelectMouseCursorNative(UltraCanvasWindowBase* win, UCMouseCursor cur,
                                     const char* filename, int hotspotX, int hotspotY) override;

    protected:
        // ===== INHERITED FROM BASE APPLICATION =====
        bool InitializeNative() override;
        void ShutdownNative() override;
        void CaptureMouseNative() override;
        void ReleaseMouseNative() override;
        void CollectAndProcessNativeEvents() override;
        void WakeUpEventLoop() override;
        void InitializeWakeUp() override;
        void ShutdownWakeUp() override;
        FontStyle DetectSystemFontStyleNative() override;
        FontStyle DetectMonospacedFontStyleNative() override;
        void LoadBundledFontsNative() override;
        bool RegisterFontFileNative(const std::string& fontFilePath) override;

    private:
        // The single top-level window the activity surface backs (the first
        // registered window); popups/dialogs render in-process on top of it.
        UltraCanvasAndroidWindow* GetPrimaryWindow();

        // android_native_app_glue callbacks (userData carries `this`).
        static void HandleAppCmdThunk(android_app* app, int32_t cmd);
        static int32_t HandleInputEventThunk(android_app* app, AInputEvent* event);

        void HandleAppCmd(int32_t cmd);
        int32_t HandleInputEvent(AInputEvent* event);
        int32_t HandleMotionEvent(AInputEvent* event);
        int32_t HandleKeyEvent(AInputEvent* event);

        // Emit one finger's position as a touch event (stable pointer id).
        void PushTouchEvent(UltraCanvasAndroidWindow* win, AInputEvent* motionEvent,
                            size_t pointerIndex, UCEventType type);

        // Latched for the rest of the gesture as soon as a second finger
        // touches down: from then on the gesture is delivered as touch events
        // only, so a pinch cannot also drag whatever the first finger hit.
        // Cleared when the last finger lifts.
        bool multiTouchGesture = false;

        void PushWindowEvent(UCEventType type);

        UCKeys ConvertAndroidKeyToUCKey(int32_t keyCode);
        // US-layout printable-ASCII fallback, used only when the JNI
        // KeyCharacterMap translation is unavailable.
        char DeriveAsciiCharacter(UCKeys key, bool shift);

        // Layout-aware key -> Unicode code point via KeyCharacterMap (JNI).
        // Returns 0 for non-printing keys, dead keys, and when JNI is down.
        int32_t GetUnicodeCharacter(int32_t deviceId, int32_t keyCode,
                                    int32_t metaState);

        // Caret-driven soft keyboard: hides are deferred one event-loop turn
        // so focus moving between two text widgets doesn't flicker the IME.
        void OnTextEditingChanged(bool active);
        bool pendingImeHide = false;

        // ===== IME QUEUE (filled on the Java UI thread) =====
        struct ImeOp {
            enum class Kind { Commit, Compose, Finish, Key, DeleteBefore };
            Kind kind = Kind::Commit;
            std::string text;
            int value = 0;              // Compose: caret bytes; DeleteBefore: count
            bool down = false;          // Key
            int32_t keyCode = 0, metaState = 0, codePoint = 0;
        };
        std::mutex imeQueueMutex;
        std::vector<ImeOp> imeQueue;
        void QueueImeOp(ImeOp op);
        void DrainImeQueue();           // glue thread
        void ApplyImeOp(const ImeOp& op);

        // ===== COMPOSITION STATE (glue thread only) =====
        // The IME's current composing text. It reaches the widget only as
        // TextComposition events, and only when the focused element draws
        // compositions; any other element sees nothing until the commit.
        std::string imeComposing;
        // Whether the focused element currently shows a pre-edit that must be
        // ended with an empty TextComposition.
        bool imeCompositionShown = false;
        bool FocusedDrawsComposition();
        void PushComposition(const std::string& utf8, int caretBytes);
        void EndShownComposition();
        void CommitComposition(const std::string& utf8);
        void EraseBefore(int count);
        // A tap while composing: the composing text is kept as typed in the
        // element that has it, before the tap can move focus or the caret,
        // and the IME is restarted so it composes afresh from there.
        void FinishCompositionForTap();
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_ANDROID_APPLICATION_H
