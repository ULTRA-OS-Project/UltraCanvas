// OS/Android/java/org/ultraos/ultracanvas/UltraCanvasActivity.java
// The Java half of the Android backend.
//
// NativeActivity alone cannot host anything that needs a real Activity on the
// Java UI thread - AlertDialog, the Storage Access Framework's
// startActivityForResult/onActivityResult, an InputConnection for full IME.
// This subclass supplies those, and the C++ side calls into it over JNI.
//
// Declare it in the manifest INSTEAD of android.app.NativeActivity:
//
//   <activity android:name="org.ultraos.ultracanvas.UltraCanvasActivity"
//             android:configChanges="orientation|screenSize|screenLayout|keyboardHidden|density">
//       <meta-data android:name="android.app.lib_name" android:value="YourAppLib"/>
//   </activity>
//
// It is OPTIONAL: with a plain NativeActivity the C++ side finds no bridge
// methods and every dialog falls back to its "cancelled" stub, so an app that
// never opens a native dialog needs no Java at all.
//
// Version: 1.0.0
// Author: UltraCanvas Framework

package org.ultraos.ultracanvas;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.DialogInterface;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import android.content.Context;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.FrameLayout;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;

public class UltraCanvasActivity extends NativeActivity {

    // Result codes - keep in sync with AndroidDialogs::JavaResult (C++).
    private static final int RESULT_CANCEL = 0;
    private static final int RESULT_POSITIVE = 1;
    private static final int RESULT_NEGATIVE = 2;
    private static final int RESULT_NEUTRAL = 3;

    // Icon hints - keep in sync with AndroidDialogs::JavaIcon (C++).
    private static final int ICON_NONE = 0;
    private static final int ICON_INFO = 1;
    private static final int ICON_ALERT = 2;

    /**
     * Show a modal AlertDialog. Called from the native (glue) thread, which
     * blocks until exactly one result comes back through nativeOnDialogResult
     * - so every path out of the dialog, including the back button, MUST
     * deliver one. Returns immediately; the dialog itself is built on the UI
     * thread.
     *
     * A null button label omits that button.
     */
    public void showMessageDialog(final int requestId, final String title,
                                  final String message, final int icon,
                                  final String positive, final String negative,
                                  final String neutral) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                // One result per request, whichever way the dialog ends.
                final boolean[] delivered = { false };

                DialogInterface.OnClickListener onClick =
                        new DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(DialogInterface dialog, int which) {
                                int result = RESULT_CANCEL;
                                if (which == DialogInterface.BUTTON_POSITIVE) {
                                    result = RESULT_POSITIVE;
                                } else if (which == DialogInterface.BUTTON_NEGATIVE) {
                                    result = RESULT_NEGATIVE;
                                } else if (which == DialogInterface.BUTTON_NEUTRAL) {
                                    result = RESULT_NEUTRAL;
                                }
                                if (!delivered[0]) {
                                    delivered[0] = true;
                                    nativeOnDialogResult(requestId, result, null);
                                }
                            }
                        };

                AlertDialog.Builder builder =
                        new AlertDialog.Builder(UltraCanvasActivity.this);
                builder.setTitle(title);
                builder.setMessage(message);
                if (icon == ICON_INFO) {
                    builder.setIcon(android.R.drawable.ic_dialog_info);
                } else if (icon == ICON_ALERT) {
                    builder.setIcon(android.R.drawable.ic_dialog_alert);
                }
                if (positive != null) builder.setPositiveButton(positive, onClick);
                if (negative != null) builder.setNegativeButton(negative, onClick);
                if (neutral != null) builder.setNeutralButton(neutral, onClick);

                // Covers the back button and any other dismissal: if no button
                // listener fired, the native thread still gets its one result
                // instead of waiting forever.
                builder.setOnDismissListener(new DialogInterface.OnDismissListener() {
                    @Override
                    public void onDismiss(DialogInterface dialog) {
                        if (!delivered[0]) {
                            delivered[0] = true;
                            nativeOnDialogResult(requestId, RESULT_CANCEL, null);
                        }
                    }
                });

                try {
                    builder.show();
                } catch (Throwable t) {
                    // Activity finishing / bad window token: never leave the
                    // native thread pumping for a result that cannot arrive.
                    if (!delivered[0]) {
                        delivered[0] = true;
                        nativeOnDialogResult(requestId, RESULT_CANCEL, null);
                    }
                }
            }
        });
    }

    /**
     * Show a modal text-input AlertDialog and deliver what the user typed.
     * Called from the native (glue) thread, which blocks until exactly one
     * result arrives - so, exactly as in showMessageDialog, every way out of
     * this dialog has to deliver one.
     *
     * Only RESULT_POSITIVE carries a value; every other outcome delivers null,
     * so a cancelled dialog can never be mistaken for a deliberately empty
     * string. Buttons use the platform's own OK/Cancel strings, which are
     * localised for the user's device.
     */
    public void showInputDialog(final int requestId, final String title,
                                final String prompt, final String defaultValue,
                                final boolean password) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                final boolean[] delivered = { false };

                final EditText input = new EditText(UltraCanvasActivity.this);
                input.setSingleLine(true);
                input.setInputType(password
                        ? (InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD)
                        : InputType.TYPE_CLASS_TEXT);
                if (defaultValue != null) {
                    input.setText(defaultValue);
                    // Caret after the text, not before it: the common case is
                    // editing a suggested name, not replacing it.
                    input.setSelection(input.getText().length());
                }

                // An EditText handed straight to setView sits flush against the
                // dialog's edges; the platform insets its own.
                final int inset =
                        (int) (getResources().getDisplayMetrics().density * 20);
                FrameLayout container = new FrameLayout(UltraCanvasActivity.this);
                FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.WRAP_CONTENT);
                lp.leftMargin = inset;
                lp.rightMargin = inset;
                container.addView(input, lp);

                DialogInterface.OnClickListener onClick =
                        new DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(DialogInterface dialog, int which) {
                                if (delivered[0]) return;
                                delivered[0] = true;
                                if (which == DialogInterface.BUTTON_POSITIVE) {
                                    nativeOnDialogResult(requestId, RESULT_POSITIVE,
                                                         input.getText().toString());
                                } else {
                                    nativeOnDialogResult(requestId, RESULT_CANCEL, null);
                                }
                            }
                        };

                AlertDialog.Builder builder =
                        new AlertDialog.Builder(UltraCanvasActivity.this);
                builder.setTitle(title);
                if (prompt != null && prompt.length() > 0) {
                    builder.setMessage(prompt);
                }
                builder.setView(container);
                builder.setPositiveButton(getString(android.R.string.ok), onClick);
                builder.setNegativeButton(getString(android.R.string.cancel), onClick);

                builder.setOnDismissListener(new DialogInterface.OnDismissListener() {
                    @Override
                    public void onDismiss(DialogInterface dialog) {
                        if (!delivered[0]) {
                            delivered[0] = true;
                            nativeOnDialogResult(requestId, RESULT_CANCEL, null);
                        }
                    }
                });

                try {
                    AlertDialog dialog = builder.create();
                    // Raise the soft keyboard with the dialog. Without this the
                    // user has to tap the field before they can type, which on a
                    // phone reads as a broken dialog rather than a choice.
                    if (dialog.getWindow() != null) {
                        dialog.getWindow().setSoftInputMode(
                                WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE);
                    }
                    input.requestFocus();
                    dialog.show();
                } catch (Throwable t) {
                    if (!delivered[0]) {
                        delivered[0] = true;
                        nativeOnDialogResult(requestId, RESULT_CANCEL, null);
                    }
                }
            }
        });
    }

    // ===== FILE PICKING (Storage Access Framework) =====

    // The request currently awaiting onActivityResult, or 0 when idle.
    private int pendingPickRequestId = 0;

    /**
     * Launch the system document picker. Called from the native (glue) thread,
     * which blocks until nativeOnDialogResult arrives.
     *
     * mimeTypesCsv narrows the picker ("image/png,image/jpeg"); empty means
     * everything. On success the value is the newline-separated list of cache
     * file paths the documents were copied to - see copyToCache for why the
     * caller gets a real path rather than a content:// URI.
     */
    public void showOpenDocument(final int requestId, final String mimeTypesCsv,
                                 final boolean allowMultiple) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                    intent.addCategory(Intent.CATEGORY_OPENABLE);
                    intent.setType("*/*");
                    if (mimeTypesCsv != null && mimeTypesCsv.length() > 0) {
                        intent.putExtra(Intent.EXTRA_MIME_TYPES, mimeTypesCsv.split(","));
                    }
                    if (allowMultiple) {
                        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                    }
                    pendingPickRequestId = requestId;
                    startActivityForResult(intent, requestId);
                } catch (Throwable t) {
                    // No picker installed, or the activity is finishing. Never
                    // leave the native thread pumping for a result.
                    pendingPickRequestId = 0;
                    nativeOnDialogResult(requestId, RESULT_CANCEL, null);
                }
            }
        });
    }

    /**
     * Launch the system "create document" picker and write `content` to
     * whatever the user chooses. Called from the native (glue) thread, which
     * blocks until nativeOnDialogResult arrives.
     *
     * The bytes come in up front because SAF has no path to hand back: the new
     * document is reachable only through its content:// URI and this app's
     * ContentResolver, so the write has to happen on this side.
     */
    public void showSaveDocument(final int requestId, final String mimeType,
                                 final String suggestedName, final byte[] content) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                    intent.addCategory(Intent.CATEGORY_OPENABLE);
                    intent.setType(mimeType != null && mimeType.length() > 0
                                   ? mimeType : "application/octet-stream");
                    if (suggestedName != null && suggestedName.length() > 0) {
                        intent.putExtra(Intent.EXTRA_TITLE, suggestedName);
                    }
                    pendingPickRequestId = requestId;
                    pendingSaveContent = content;
                    startActivityForResult(intent, requestId);
                } catch (Throwable t) {
                    pendingPickRequestId = 0;
                    pendingSaveContent = null;
                    nativeOnDialogResult(requestId, RESULT_CANCEL, null);
                }
            }
        });
    }

    // Non-null exactly while a showSaveDocument request is outstanding, which
    // is also how onActivityResult tells a save from an open.
    private byte[] pendingSaveContent;

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != pendingPickRequestId || pendingPickRequestId == 0) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        final int requestId = pendingPickRequestId;
        final byte[] saveContent = pendingSaveContent;
        pendingPickRequestId = 0;
        pendingSaveContent = null;

        if (resultCode != RESULT_OK || data == null) {
            nativeOnDialogResult(requestId, RESULT_CANCEL, null);
            return;
        }

        if (saveContent != null) {
            final Uri target = data.getData();
            if (target == null) {
                nativeOnDialogResult(requestId, RESULT_CANCEL, null);
                return;
            }
            // Writing can be slow (a large document, a cloud provider), so it
            // stays off the UI thread. The native thread is parked in its pump
            // regardless.
            new Thread(new Runnable() {
                @Override
                public void run() {
                    boolean ok = writeToUri(target, saveContent);
                    nativeOnDialogResult(requestId,
                                         ok ? RESULT_POSITIVE : RESULT_CANCEL,
                                         ok ? displayNameOf(target) : null);
                }
            }, "UltraCanvas-SAF-save").start();
            return;
        }

        final ArrayList<Uri> uris = new ArrayList<Uri>();
        ClipData clip = data.getClipData();
        if (clip != null) {
            for (int i = 0; i < clip.getItemCount(); i++) {
                Uri uri = clip.getItemAt(i).getUri();
                if (uri != null) uris.add(uri);
            }
        } else if (data.getData() != null) {
            uris.add(data.getData());
        }

        if (uris.isEmpty()) {
            nativeOnDialogResult(requestId, RESULT_CANCEL, null);
            return;
        }

        // Copying can be slow (a large document, a cloud provider streaming it
        // in) and must not run on the UI thread. The native thread is parked in
        // its pump either way, so it costs nothing to take our time here.
        new Thread(new Runnable() {
            @Override
            public void run() {
                StringBuilder paths = new StringBuilder();
                for (Uri uri : uris) {
                    String path = copyToCache(uri);
                    if (path == null) continue;   // skip what we cannot read
                    if (paths.length() > 0) paths.append('\n');
                    paths.append(path);
                }
                if (paths.length() == 0) {
                    nativeOnDialogResult(requestId, RESULT_CANCEL, null);
                } else {
                    nativeOnDialogResult(requestId, RESULT_POSITIVE, paths.toString());
                }
            }
        }, "UltraCanvas-SAF-copy").start();
    }

    /**
     * Copy a picked document into the app cache and return its absolute path,
     * or null if it could not be read.
     *
     * The framework's file API is path-based (callers fopen what the dialog
     * returns), while SAF hands out content:// URIs that no POSIX call can
     * open. Copying is the one approach that works for every provider,
     * including those streaming from the network with no underlying file.
     * The cost is a copy, and edits land in the cache copy rather than the
     * original document - which is why only *opening* goes through here.
     */
    // ===== CLIPBOARD (content:// items) =====
    //
    // The C++ backend talks to ClipboardManager over JNI directly for text.
    // URI items cannot be handled that way: reading one means going through
    // this app's ContentResolver, and the framework's API is path-based. So
    // the same copy-to-cache treatment the document picker uses applies here,
    // and for the same reason - a content:// URI is not openable by any POSIX
    // call, and some providers stream from the network with no file behind
    // them at all.
    //
    // Consequence, identical to the picker's: the caller reads a snapshot.
    // Writing to the returned path does not reach the original document.

    /**
     * Copy every content:// item on the clipboard into the app cache and
     * return their paths, newline-separated. Returns null when the clipboard
     * holds no URI items, or cannot be read at all - which on Android 10+
     * includes every read taken while this app does not have input focus.
     */
    public String getClipboardUriPaths() {
        try {
            ClipboardManager manager =
                    (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            if (manager == null || !manager.hasPrimaryClip()) return null;
            ClipData clip = manager.getPrimaryClip();
            if (clip == null) return null;

            StringBuilder paths = new StringBuilder();
            for (int i = 0; i < clip.getItemCount(); i++) {
                Uri uri = clip.getItemAt(i).getUri();
                if (uri == null) continue;          // a text item among URIs
                String path = copyToCache(uri);
                if (path == null) continue;         // unreadable provider
                if (paths.length() > 0) paths.append('\n');
                paths.append(path);
            }
            return paths.length() > 0 ? paths.toString() : null;
        } catch (Throwable t) {
            return null;
        }
    }

    /**
     * MIME type the clipboard advertises for its first item, or null. Used to
     * tell "an image was copied" from "a file was copied" before paying for
     * the copy in getClipboardUriPaths.
     */
    public String getClipboardMimeType() {
        try {
            ClipboardManager manager =
                    (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            if (manager == null || !manager.hasPrimaryClip()) return null;
            ClipData clip = manager.getPrimaryClip();
            if (clip == null || clip.getDescription() == null) return null;
            if (clip.getDescription().getMimeTypeCount() < 1) return null;
            return clip.getDescription().getMimeType(0);
        } catch (Throwable t) {
            return null;
        }
    }

    private String copyToCache(Uri uri) {
        InputStream in = null;
        OutputStream out = null;
        try {
            File dir = new File(getCacheDir(), "ultracanvas-picked");
            if (!dir.isDirectory() && !dir.mkdirs()) return null;

            File target = new File(dir, uniqueName(dir, displayNameOf(uri)));
            in = getContentResolver().openInputStream(uri);
            if (in == null) return null;
            out = new FileOutputStream(target);

            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
            out.flush();
            return target.getAbsolutePath();
        } catch (Throwable t) {
            return null;
        } finally {
            closeQuietly(in);
            closeQuietly(out);
        }
    }

    /**
     * Write bytes to a document the user just created. Returns false if any
     * part of it failed, so the caller never reports a save that did not
     * happen.
     */
    private boolean writeToUri(Uri uri, byte[] content) {
        OutputStream out = null;
        try {
            out = getContentResolver().openOutputStream(uri, "wt");
            if (out == null) return false;
            out.write(content);
            out.flush();
            return true;
        } catch (Throwable t) {
            return false;
        } finally {
            // The provider only sees the document as complete once the stream
            // is closed, so a failure to close is a failed save.
            if (out != null) {
                try {
                    out.close();
                } catch (Throwable t) {
                    return false;
                }
            }
        }
    }

    /** The document's user-visible name, sanitised into a safe file name. */
    private String displayNameOf(Uri uri) {
        String name = null;
        Cursor cursor = null;
        try {
            cursor = getContentResolver().query(uri, null, null, null, null);
            if (cursor != null && cursor.moveToFirst()) {
                int column = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (column >= 0) name = cursor.getString(column);
            }
        } catch (Throwable t) {
            // fall through to the default below
        } finally {
            if (cursor != null) {
                try { cursor.close(); } catch (Throwable ignored) { }
            }
        }
        if (name == null || name.length() == 0) name = "document";
        // '/' would escape the cache dir and '\n' would break the path list
        // the native side splits on.
        return name.replace('/', '_').replace('\\', '_')
                   .replace('\n', '_').replace('\r', '_');
    }

    /** "notes.txt" -> "notes-2.txt" when the cache already holds that name. */
    private String uniqueName(File dir, String name) {
        if (!new File(dir, name).exists()) return name;
        String stem = name;
        String extension = "";
        int dot = name.lastIndexOf('.');
        if (dot > 0) {
            stem = name.substring(0, dot);
            extension = name.substring(dot);
        }
        for (int i = 2; i < 1000; i++) {
            String candidate = stem + "-" + i + extension;
            if (!new File(dir, candidate).exists()) return candidate;
        }
        return name;
    }

    private static void closeQuietly(java.io.Closeable c) {
        if (c == null) return;
        try { c.close(); } catch (Throwable ignored) { }
    }

    // ===== TEXT INPUT (soft keyboard + IME) =====

    private InputView inputView;

    /**
     * Raise the soft keyboard against our input view. Called over JNI from
     * UltraCanvasAndroidApplication::ShowSoftKeyboard when a widget starts
     * text editing.
     *
     * Focus has to move to the input view for the IME to bind to its
     * InputConnection - that binding is what turns a dumb key stream into
     * autocorrect, suggestions, gesture typing and CJK composition. While it
     * holds focus the view forwards key events to the native side itself, so
     * a hardware keyboard keeps working exactly as before.
     */
    public void showKeyboard() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                InputView view = ensureInputView();
                if (view == null) return;
                view.requestFocus();
                InputMethodManager imm = (InputMethodManager)
                        getSystemService(Context.INPUT_METHOD_SERVICE);
                if (imm != null) imm.showSoftInput(view, 0);
            }
        });
    }

    /** Lower the soft keyboard and release the input view's focus. */
    public void hideKeyboard() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (inputView == null) return;
                InputMethodManager imm = (InputMethodManager)
                        getSystemService(Context.INPUT_METHOD_SERVICE);
                if (imm != null) {
                    imm.hideSoftInputFromWindow(inputView.getWindowToken(), 0);
                }
                inputView.clearFocus();
            }
        });
    }

    /** UI thread only. */
    private InputView ensureInputView() {
        if (inputView != null) return inputView;
        try {
            inputView = new InputView(this);
            // Zero-sized and transparent: it exists purely to own the IME
            // session, and must never cover the NativeActivity's surface.
            addContentView(inputView, new ViewGroup.LayoutParams(0, 0));
        } catch (Throwable t) {
            inputView = null;
        }
        return inputView;
    }

    /**
     * An invisible, focusable editor. NativeActivity's own surface view is not
     * an editor, so without this the IME has nothing to attach to.
     */
    private final class InputView extends View {
        InputView(Context context) {
            super(context);
            setFocusable(true);
            setFocusableInTouchMode(true);
        }

        @Override
        public boolean onCheckIsTextEditor() {
            return true;
        }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
            outAttrs.inputType = InputType.TYPE_CLASS_TEXT
                               | InputType.TYPE_TEXT_FLAG_MULTI_LINE;
            // No "Done" action and no full-screen editor: the app draws its
            // own text, so the IME must not take the screen over with an
            // extract view that the user would edit instead.
            outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI
                                | EditorInfo.IME_FLAG_NO_FULLSCREEN
                                | EditorInfo.IME_ACTION_NONE;
            return new UltraCanvasInputConnection(this);
        }

        // While this view holds focus it is the one receiving key events, so
        // it has to forward them or a hardware keyboard would go dead during
        // text editing.
        @Override
        public boolean onKeyDown(int keyCode, KeyEvent event) {
            if (keyCode == KeyEvent.KEYCODE_BACK) return super.onKeyDown(keyCode, event);
            nativeOnJavaKeyEvent(true, keyCode, event.getMetaState(),
                                 event.getUnicodeChar(event.getMetaState()));
            return true;
        }

        @Override
        public boolean onKeyUp(int keyCode, KeyEvent event) {
            if (keyCode == KeyEvent.KEYCODE_BACK) return super.onKeyUp(keyCode, event);
            nativeOnJavaKeyEvent(false, keyCode, event.getMetaState(), 0);
            return true;
        }
    }

    /**
     * Feeds the IME's output to the native side.
     *
     * Composition (setComposingText) is deliberately left to the superclass's
     * local buffer and never forwarded: the framework has no inline preedit
     * concept on any platform, and the IME shows candidates in its own UI.
     * Only committed text crosses into the app.
     */
    private final class UltraCanvasInputConnection extends BaseInputConnection {
        UltraCanvasInputConnection(View targetView) {
            // fullEditor=true keeps a local Editable, which is what lets the
            // IME read back context for autocorrect and suggestions.
            super(targetView, true);
        }

        @Override
        public boolean commitText(CharSequence text, int newCursorPosition) {
            if (text != null && text.length() > 0) {
                nativeOnCommitText(text.toString());
            }
            return super.commitText(text, newCursorPosition);
        }

        @Override
        public boolean deleteSurroundingText(int beforeLength, int afterLength) {
            nativeOnDeleteSurroundingText(beforeLength, afterLength);
            return super.deleteSurroundingText(beforeLength, afterLength);
        }

        @Override
        public boolean sendKeyEvent(KeyEvent event) {
            if (event != null) {
                if (event.getAction() == KeyEvent.ACTION_DOWN) {
                    nativeOnJavaKeyEvent(true, event.getKeyCode(),
                                         event.getMetaState(),
                                         event.getUnicodeChar(event.getMetaState()));
                } else if (event.getAction() == KeyEvent.ACTION_UP) {
                    nativeOnJavaKeyEvent(false, event.getKeyCode(),
                                         event.getMetaState(), 0);
                }
            }
            return true;
        }
    }

    /**
     * Delivers a dialog outcome to the waiting native thread. Implemented in
     * UltraCanvasAndroidDialogBridge.cpp; resolved by name against the native
     * library NativeActivity already loaded for us.
     */
    private static native void nativeOnDialogResult(int requestId, int result, String value);

    // Implemented in UltraCanvasAndroidTextInput.cpp.
    private static native void nativeOnCommitText(String text);
    private static native void nativeOnJavaKeyEvent(boolean down, int keyCode,
                                                    int metaState, int codePoint);
    private static native void nativeOnDeleteSurroundingText(int before, int after);
}
