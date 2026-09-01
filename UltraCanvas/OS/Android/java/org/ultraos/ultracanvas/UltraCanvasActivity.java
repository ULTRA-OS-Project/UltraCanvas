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
import android.content.DialogInterface;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

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

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != pendingPickRequestId || pendingPickRequestId == 0) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        final int requestId = pendingPickRequestId;
        pendingPickRequestId = 0;

        if (resultCode != RESULT_OK || data == null) {
            nativeOnDialogResult(requestId, RESULT_CANCEL, null);
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

    /**
     * Delivers a dialog outcome to the waiting native thread. Implemented in
     * UltraCanvasAndroidDialogBridge.cpp; resolved by name against the native
     * library NativeActivity already loaded for us.
     */
    private static native void nativeOnDialogResult(int requestId, int result, String value);
}
