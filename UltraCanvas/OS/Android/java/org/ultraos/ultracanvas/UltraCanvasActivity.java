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
import android.content.DialogInterface;

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
     * Delivers a dialog outcome to the waiting native thread. Implemented in
     * UltraCanvasAndroidDialogBridge.cpp; resolved by name against the native
     * library NativeActivity already loaded for us.
     */
    private static native void nativeOnDialogResult(int requestId, int result, String value);
}
