- **Text sent from C++ to Android's Java side is real UTF-8 now.** Dialog
  titles, messages, button labels and default input, clipboard text copied
  out, file-picker MIME types and suggested file names were converted with
  JNI's `NewStringUTF`. That takes JNI's *modified* UTF-8, so:
  - an emoji or any other character outside the Basic Multilingual Plane
    reached Java as garbage;
  - malformed UTF-8 (a truncated file name, say) aborted the whole app when
    CheckJNI was on, which it is in every debuggable APK.

  The new `AndroidJni::ToJString` converts UTF-8 to UTF-16 itself and turns
  malformed bytes into U+FFFD. It is the reverse of the `ToStdString` fix in
  the same release.
