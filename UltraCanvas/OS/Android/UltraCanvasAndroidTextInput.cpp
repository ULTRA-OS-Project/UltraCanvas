// OS/Android/UltraCanvasAndroidTextInput.cpp
// JNI entry points for the Java InputConnection (full IME support).
//
// Without an InputConnection the soft keyboard can only send bare key events,
// which loses everything an IME does on top: autocorrect, the suggestion bar,
// gesture typing, and every language where a word is composed from several
// keystrokes (CJK) or picked from candidates. UltraCanvasActivity's input view
// supplies one; this file receives what it produces.
//
// Composition itself stays inside the IME's own buffer and is NOT forwarded:
// only committed text arrives here. That matches the framework's existing
// cross-platform text model - the Linux backend likewise takes only the
// committed result out of Xutf8LookupString, and no core widget can render an
// inline preedit region. Showing composing text inline would be a new
// cross-platform capability (core model + rendering + every backend), not an
// Android detail.
//
// Version: 1.0.0
// Last Modified: 2026-08-23
// Author: UltraCanvas Framework

// The public headers first: they define the base classes and then pull in the
// OS headers through their platform alias chain (same pattern as Linux).
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasAndroidApplication.h"
#include "UltraCanvasAndroidJni.h"

#include <android/keycodes.h>
#include <jni.h>

extern "C" {

// Text the IME committed (a word, a CJK candidate, an emoji, a paste).
JNIEXPORT void JNICALL
Java_org_ultraos_ultracanvas_UltraCanvasActivity_nativeOnCommitText(
        JNIEnv* env, jclass, jstring text) {
    using namespace UltraCanvas;
    auto* app = UltraCanvasAndroidApplication::GetInstance();
    if (!app || !text) return;
    app->PushCommittedText(AndroidJni::ToStdString(env, text));
}

// A key press the input view saw, or one the IME synthesised through
// InputConnection.sendKeyEvent. `codePoint` is KeyEvent.getUnicodeChar(),
// already layout-resolved by the framework, so no KeyCharacterMap lookup is
// needed on this path.
JNIEXPORT void JNICALL
Java_org_ultraos_ultracanvas_UltraCanvasActivity_nativeOnJavaKeyEvent(
        JNIEnv*, jclass, jboolean down, jint keyCode, jint metaState,
        jint codePoint) {
    using namespace UltraCanvas;
    auto* app = UltraCanvasAndroidApplication::GetInstance();
    if (!app) return;
    app->PushKeyEvent(down == JNI_TRUE, keyCode, metaState, codePoint);
}

// The IME asked to delete text around the cursor - what a soft keyboard's
// backspace does instead of sending a key event. The framework has no
// "delete N characters" event, and text widgets already implement backspace,
// so this is replayed as that many Backspace presses.
//
// Only `before` is honoured: `after` (forward delete) has no soft-keyboard
// equivalent, and replaying it as Delete presses would move a caret the IME
// believes it left in place.
JNIEXPORT void JNICALL
Java_org_ultraos_ultracanvas_UltraCanvasActivity_nativeOnDeleteSurroundingText(
        JNIEnv*, jclass, jint before, jint after) {
    using namespace UltraCanvas;
    (void)after;
    auto* app = UltraCanvasAndroidApplication::GetInstance();
    if (!app) return;

    // A pathological count would flood the queue; a real backspace is 1.
    const int count = before > 64 ? 64 : static_cast<int>(before);
    for (int i = 0; i < count; ++i) {
        app->PushKeyEvent(true, AKEYCODE_DEL, 0, 0);
        app->PushKeyEvent(false, AKEYCODE_DEL, 0, 0);
    }
}

} // extern "C"
