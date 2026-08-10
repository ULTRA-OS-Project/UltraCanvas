# OS/Android — Android platform backend (not implemented yet)

This directory is the staging point for the Google Android backend. The full
investigation and phased plan live in
[`Docs/UltraCanvas/AndroidPortInvestigation.md`](../../../Docs/UltraCanvas/AndroidPortInvestigation.md).

CMake already recognizes Android (`UltraCanvas/CMakeLists.txt` platform
detection tests `ANDROID` before `UNIX`, because the NDK toolchain satisfies
`UNIX` and bionic defines `__linux__`) and currently stops with a clear
configure error pointing here. The public headers
(`include/UltraCanvasApplication.h`, `include/UltraCanvasWindow.h`) contain
`__ANDROID__` alias branches expecting the files below — note those branches
must also be reordered ahead of the `__linux__` test when the backend lands
(investigation §3.1 lists every affected site).

Deliberately **no placeholder sources** are committed here: the platform
source glob would compile anything in this directory, and the repository has
already been burned twice (`OS/WASM/`, `OS/BSD/`) by code drops written
against base-class APIs that later drifted. Every file added here must build
in the same change that adds it.

## Planned files (contracts as of the investigation)

| File | Contract |
|---|---|
| `UltraCanvasAndroidApplication.{h,cpp}` | 13 pure virtuals of `UltraCanvasApplicationBase` + `static GetInstance()`; ALooper-based `CollectAndProcessNativeEvents`, eventfd wakeup |
| `UltraCanvasAndroidWindow.{h,cpp}` | 19 pure virtuals of `UltraCanvasWindowBase`; Cairo image surface presented via `ANativeWindow_lock`/`unlockAndPost`; `QueryNativeDeviceScale()` = densityDpi/160 |
| `UltraCanvasAndroidClipboard.{h,cpp}` | `UltraCanvasClipboardBackend` via JNI `ClipboardManager` |
| `UltraCanvasAndroidNativeDialogs.cpp` | `UltraCanvasNativeDialogs` statics via JNI (AlertDialog / Storage Access Framework) |
| `UltraCanvasAndroidCursor.cpp` | no-op cursor stubs |
| `UltraCanvasAndroidFileLoader.cpp` | `NotifyRecentFile` stub |
| `GLContextManagerEGL_Android.cpp` | EGL/GLES context manager (derived from `OS/Linux/GLContextManagerEGL_Linux.cpp`) |
| `UltraNetSupport.cpp` | proxy detection (copy of Linux getenv implementation) |
| `UltraNetTlsImpl.cpp` | OpenSSL TLS (copy of Linux implementation + bundled CA store) |
| DNS | no `UltraNetDnsImpl.cpp` — Android must build with c-ares (`ULTRANET_HAS_CARES`); bionic lacks the `res_n*` API the Linux file uses |
