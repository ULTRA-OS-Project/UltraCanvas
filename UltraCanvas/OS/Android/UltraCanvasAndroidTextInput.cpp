// OS/Android/UltraCanvasAndroidTextInput.cpp
// JNI entry points for the Java InputConnection (full IME support).
//
// Without an InputConnection the soft keyboard can only send bare key events,
// which loses everything an IME does on top: autocorrect, the suggestion bar,
// gesture typing, and every language where a word is composed from several
// keystrokes (CJK) or picked from candidates. UltraCanvasActivity's input view
// supplies one; this file receives what it produces.
//
// The text being composed is forwarded too. An element that draws
// compositions itself (DrawsTextComposition) receives it as TextComposition
// events and shows it inline at its caret, as the Linux and Windows backends
// deliver it; any other element sees only the committed result. These run on
// the Java UI thread, so each call is queued and applied on the glue thread
// (UltraCanvasAndroidApplication::DrainImeQueue), in the order the IME made
// them.
//
// Version: 1.1.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

// The public headers first: they define the base classes and then pull in the
// OS headers through their platform alias chain (same pattern as Linux).
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasAndroidApplication.h"
#include "UltraCanvasAndroidJni.h"

#include <jni.h>

extern "C" {

// Text the IME committed (a word, a CJK candidate, an emoji, a paste). It
// replaces whatever was being composed.
JNIEXPORT void JNICALL
Java_org_ultraos_ultracanvas_UltraCanvasActivity_nativeOnCommitText(
        JNIEnv* env, jclass, jstring text) {
    using namespace UltraCanvas;
    auto* app = UltraCanvasAndroidApplication::GetInstance();
    if (!app || !text) return;
    app->QueueImeCommit(AndroidJni::ToStdString(env, text));
}

// The text being composed (setComposingText), with its caret as a byte offset
// into the UTF-8 the Java side measured. "" ends the composition.
JNIEXPORT void JNICALL
Java_org_ultraos_ultracanvas_UltraCanvasActivity_nativeOnComposingText(
        JNIEnv* env, jclass, jstring text, jint caretBytes) {
    using namespace UltraCanvas;
    auto* app = UltraCanvasAndroidApplication::GetInstance();
    if (!app) return;
    app->QueueImeComposition(AndroidJni::ToStdString(env, text),
                             static_cast<int>(caretBytes));
}

// finishComposingText: the composing text stays, as typed.
JNIEXPORT void JNICALL
Java_org_ultraos_ultracanvas_UltraCanvasActivity_nativeOnFinishComposingText(
        JNIEnv*, jclass) {
    using namespace UltraCanvas;
    auto* app = UltraCanvasAndroidApplication::GetInstance();
    if (!app) return;
    app->QueueImeFinishComposition();
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
    app->QueueImeKeyEvent(down == JNI_TRUE, keyCode, metaState, codePoint);
}

// The IME asked to delete text around the cursor - what a soft keyboard's
// backspace does instead of sending a key event. Replayed as that many
// Backspace presses, since text widgets already implement backspace.
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
    app->QueueImeDeleteBefore(before > 64 ? 64 : static_cast<int>(before));
}

} // extern "C"
