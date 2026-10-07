/*
 * UltraWeb/guest/ultraweb.h
 * The UltraWeb element ABI, version 2: how a WebAssembly app builds its UI
 * out of UltraCanvas elements that the UltraWeb browser owns, and the
 * services it may use beside them - timers, fetch, storage, the clipboard.
 *
 * One header for both sides. A guest (built for wasm32-wasip1) includes it
 * for the imports below; the host (Apps/UltraWeb) includes it for the same
 * numbers, so the kinds, properties, events and error codes cannot drift
 * apart. Plain C so that any language with a C FFI can bind it.
 *
 * The model:
 * - The host owns every element. A guest holds handles (uint32_t, never 0).
 *   Handle UC_ROOT_HANDLE is the app's area of the browser window, a
 *   flex column; everything the guest shows is inserted under it.
 * - The guest exports uc_main(), which builds the UI and returns, and
 *   uc_event(), which the host calls when something the guest listens to
 *   happens. Nothing runs between calls: the module is a reactor, built
 *   with -mexec-model=reactor (wasi-sdk), and its state stays alive.
 * - Strings cross as (pointer, length) into the guest's own memory, UTF-8.
 * - Every call returns UC_OK or a negative UC_ERR_*; nothing a guest passes
 *   can crash the host.
 * - Changes a guest makes do not raise events back into it: setting a text
 *   input's text does not deliver UC_EVENT_CHANGE.
 * - Events that belong to no element - a timer firing, a fetch finishing -
 *   arrive through the same uc_event with handle UC_NO_HANDLE.
 *
 * Version 2 adds the services (timers, fetch, storage, clipboard); a
 * version 1 app runs unchanged. An app that needs them exports
 * uc_abi_version() returning 2, so an older UltraWeb refuses it with a
 * message instead of failing at an import it does not have.
 *
 * Version: 2.0.0
 * Last Modified: 2026-10-06
 * Author: UltraCanvas Framework / ULTRA OS
 */
#ifndef ULTRAWEB_GUEST_ULTRAWEB_H
#define ULTRAWEB_GUEST_ULTRAWEB_H

#include <stdint.h>

#define UC_ABI_VERSION 2

/* ===== Handles ===== */
#define UC_NO_HANDLE   0u
#define UC_ROOT_HANDLE 1u

/* ===== Element kinds (the string passed to uc_create) ===== */
#define UC_KIND_CONTAINER  "Container"   /* a flex box: column (default) or row */
#define UC_KIND_LABEL      "Label"
#define UC_KIND_BUTTON     "Button"
#define UC_KIND_TEXT_INPUT "TextInput"
#define UC_KIND_CHECKBOX   "Checkbox"

/* ===== Properties =====
 * Text properties go through uc_set_text / uc_get_text, numbers through
 * uc_set_number / uc_get_number. A property an element kind does not have
 * answers UC_ERR_PROPERTY. */
#define UC_PROP_TEXT        1u   /* text:   Label, Button, TextInput, Checkbox */
#define UC_PROP_PLACEHOLDER 2u   /* text:   TextInput */
#define UC_PROP_CHECKED     3u   /* 0 / 1:  Checkbox */
#define UC_PROP_ENABLED     4u   /* 0 / 1:  every kind (default 1) */
#define UC_PROP_VISIBLE     5u   /* 0 / 1:  every kind (default 1) */
#define UC_PROP_WIDTH       6u   /* px, 0 = automatic (the content's size): every kind */
#define UC_PROP_HEIGHT      7u   /* px, 0 = automatic: every kind */
#define UC_PROP_GROW        8u   /* flex-grow inside the parent: every kind */
#define UC_PROP_DIRECTION   9u   /* UC_DIRECTION_*: Container */
#define UC_PROP_GAP        10u   /* px between children: Container */
#define UC_PROP_PADDING    11u   /* px on every side: Container */
#define UC_PROP_FONT_SIZE  12u   /* points: Label, Button, TextInput */
#define UC_PROP_TEXT_COLOR 13u   /* 0xRRGGBBAA: Label */
#define UC_PROP_BACKGROUND 14u   /* 0xRRGGBBAA: Container, Label */
#define UC_PROP_ALIGN      15u   /* UC_ALIGN_*: Container (v2). Where the children
                                    sit across it - across a column means
                                    horizontally. START (the default) leaves each
                                    at its own size; STRETCH fills the width of a
                                    column (the height of a row) for every child
                                    whose size on that axis is automatic: a child
                                    with a set width keeps it. */

#define UC_DIRECTION_COLUMN 0
#define UC_DIRECTION_ROW    1

#define UC_ALIGN_START   0
#define UC_ALIGN_CENTER  1
#define UC_ALIGN_END     2
#define UC_ALIGN_STRETCH 3

/* ===== Events (bits for uc_listen; one value per uc_event call) ===== */
#define UC_EVENT_CLICK  1u   /* Button pressed.                          detail 0 */
#define UC_EVENT_CHANGE 2u   /* TextInput text edited by the user.       detail 0 */
#define UC_EVENT_SUBMIT 4u   /* Enter pressed in a TextInput.            detail 0 */
#define UC_EVENT_TOGGLE 8u   /* Checkbox toggled by the user. detail 1 checked, 0 not */

/* Events of the app itself: handle UC_NO_HANDLE, nothing to listen to - a
 * guest that starts a timer or a fetch gets them (v2). */
#define UC_EVENT_TIMER 16u   /* a timer fired.    detail = its id (uc_timer_start) */
#define UC_EVENT_FETCH 32u   /* a fetch finished. detail = its id (uc_fetch) */

/* ===== Results ===== */
#define UC_OK                0
#define UC_ERR_HANDLE       -1   /* no such handle, or released */
#define UC_ERR_KIND         -2   /* unknown element kind */
#define UC_ERR_PROPERTY     -3   /* the element kind has no such property */
#define UC_ERR_MEMORY       -4   /* a pointer and length outside guest memory */
#define UC_ERR_STATE        -5   /* not allowed here: a cycle, the root, a non-container parent */
#define UC_ERR_LIMIT        -6   /* too many elements, or a string too long */
#define UC_ERR_NOT_FOUND    -7   /* no such storage key, header, timer or fetch (v2) */
#define UC_ERR_DENIED       -8   /* the sandbox does not allow it (v2): another origin
                                    without CORS, http from an https app, a scheme
                                    other than http(s), the clipboard outside a user
                                    action, or a service this UltraWeb lacks */
#define UC_ERR_NETWORK      -9   /* a fetch got no complete HTTP answer (v2): DNS,
                                    connection, TLS, timeout, cut off */

/* ===== Fetch methods (uc_fetch) ===== */
#define UC_METHOD_GET    0u
#define UC_METHOD_POST   1u
#define UC_METHOD_PUT    2u
#define UC_METHOD_DELETE 3u
#define UC_METHOD_PATCH  4u
#define UC_METHOD_HEAD   5u

/* ===== Imports (module "ultracanvas") — declared for guests only ===== */
#if defined(__wasm__)

#define UC_IMPORT(name) __attribute__((import_module("ultracanvas"), import_name(#name)))

#ifdef __cplusplus
extern "C" {
#endif

/* Create an element of a kind (one of UC_KIND_*). Returns its handle, or
 * UC_NO_HANDLE for an unknown kind or when the element limit is reached. */
UC_IMPORT(uc_create) uint32_t uc_create(const char* kind, uint32_t kindLength);

/* Detach the element (if inserted) and forget the handle. Its children are
 * released too. The root cannot be released. */
UC_IMPORT(uc_release) int32_t uc_release(uint32_t handle);

/* Insert child into parent (a Container or the root) before the child
 * `before`, or at the end when before is UC_NO_HANDLE. A child that already
 * has a parent is moved. */
UC_IMPORT(uc_insert) int32_t uc_insert(uint32_t parent, uint32_t child, uint32_t before);

/* Detach the element from its parent; the handle stays valid. */
UC_IMPORT(uc_remove) int32_t uc_remove(uint32_t child);

UC_IMPORT(uc_set_text) int32_t uc_set_text(uint32_t handle, uint32_t property,
                                           const char* value, uint32_t valueLength);
UC_IMPORT(uc_set_number) int32_t uc_set_number(uint32_t handle, uint32_t property, double value);

/* Copies at most `capacity` bytes (no terminator) and returns the full
 * length, so a guest can call again with a bigger buffer. */
UC_IMPORT(uc_get_text) int32_t uc_get_text(uint32_t handle, uint32_t property,
                                           char* out, uint32_t capacity);
/* NaN for an error. */
UC_IMPORT(uc_get_number) double uc_get_number(uint32_t handle, uint32_t property);

/* Deliver these events (an OR of UC_EVENT_*) for the element; 0 stops. */
UC_IMPORT(uc_listen) int32_t uc_listen(uint32_t handle, uint32_t eventMask);

/* x, y, width, height in the app area, as of the last layout pass. Right
 * after a change the element may not have been laid out yet. */
UC_IMPORT(uc_bounds) int32_t uc_bounds(uint32_t handle, float* outXYWH);

/* A line for UltraWeb's console (stderr in this version). */
UC_IMPORT(uc_log) void uc_log(const char* message, uint32_t messageLength);

/* ----- Timers (v2) -----
 * Fires uc_event(UC_NO_HANDLE, UC_EVENT_TIMER, id) after delayMs, and again
 * every delayMs when repeat is non-zero, until uc_timer_stop. Delays under
 * 4 ms count as 4. Returns the id (> 0), UC_ERR_STATE when the guest exports
 * no uc_event, UC_ERR_LIMIT past 256 timers or a delay over 2^31 - 1 ms. A
 * one-shot timer is gone once it has fired. */
UC_IMPORT(uc_timer_start) int32_t uc_timer_start(uint32_t delayMs, uint32_t repeat);
UC_IMPORT(uc_timer_stop) int32_t uc_timer_stop(int32_t id);

/* ----- Fetch (v2) -----
 * Starts an HTTP request and returns its id (> 0); when it has finished,
 * successfully or not, uc_event(UC_NO_HANDLE, UC_EVENT_FETCH, id) follows.
 * url may be relative to the app's own address. contentType goes with a
 * body (length 0: text/plain;charset=UTF-8).
 *
 * The rules are a browser's fetch without credentials: no cookies are sent.
 * Only http and https; an https app cannot fetch http. A request to the
 * app's own origin may use any method and content type. A request to
 * another origin must be a "simple" one - GET, HEAD, or POST with
 * text/plain, application/x-www-form-urlencoded or multipart/form-data -
 * because UltraWeb sends no CORS preflight yet, and its answer reaches the
 * app only when the server allows it: Access-Control-Allow-Origin "*" or
 * the app's origin. An app opened from a file or about: has no origin, so
 * every fetch is another origin's and needs "*".
 *
 * Returns the id, or UC_ERR_DENIED (the rules above, checked before
 * anything is sent), UC_ERR_LIMIT (more than 16 fetches open, a body over
 * 4 MB, a URL over 8 KB), UC_ERR_STATE (no uc_event export, a URL that
 * does not parse, a GET or HEAD with a body, an unknown method, a content
 * type with a character a header cannot carry), UC_ERR_MEMORY. A response
 * is limited to 16 MB, one request to 30 s, and up to 5 redirects are
 * followed. */
UC_IMPORT(uc_fetch) int32_t uc_fetch(const char* url, uint32_t urlLength, uint32_t method,
                                     const void* body, uint32_t bodyLength,
                                     const char* contentType, uint32_t contentTypeLength);
/* The HTTP status (100-599), 0 while the request runs, or why it failed:
 * UC_ERR_NETWORK, UC_ERR_DENIED (the server did not allow this origin, or
 * redirected somewhere the rules forbid), UC_ERR_LIMIT (over 16 MB).
 * UC_ERR_NOT_FOUND for an id that is not open. */
UC_IMPORT(uc_fetch_status) int32_t uc_fetch_status(int32_t id);
/* Copies at most capacity bytes of the response body and returns its full
 * length; UC_ERR_STATE while running, the failure code when it failed. */
UC_IMPORT(uc_fetch_body) int32_t uc_fetch_body(int32_t id, void* out, uint32_t capacity);
/* A response header by name (any case), values of a repeated header joined
 * with ", ", the same way as uc_fetch_body. From another origin only the
 * CORS-safelisted headers and those in Access-Control-Expose-Headers are
 * visible; Set-Cookie never is. UC_ERR_NOT_FOUND when absent or hidden. */
UC_IMPORT(uc_fetch_header) int32_t uc_fetch_header(int32_t id, const char* name, uint32_t nameLength,
                                                   char* out, uint32_t capacity);
/* Forgets the request, cancelling it if it still runs; no event follows. */
UC_IMPORT(uc_fetch_close) int32_t uc_fetch_close(int32_t id);

/* ----- Storage (v2) -----
 * Key/value bytes kept across runs, one store per origin (an app from a
 * file: per file), like a web page's localStorage. Keys up to 1 KB; a store
 * holds up to 5 MB, keys and values counted, 32 bytes more per key.
 * UC_ERR_DENIED when this UltraWeb keeps no storage. */
/* The full length of the value, copying at most capacity bytes, or
 * UC_ERR_NOT_FOUND. */
UC_IMPORT(uc_storage_get) int32_t uc_storage_get(const char* key, uint32_t keyLength,
                                                 void* out, uint32_t capacity);
/* UC_OK, or UC_ERR_LIMIT when the key is too long or the store would go
 * over its quota (the old value then stays). */
UC_IMPORT(uc_storage_set) int32_t uc_storage_set(const char* key, uint32_t keyLength,
                                                 const void* value, uint32_t valueLength);
UC_IMPORT(uc_storage_remove) int32_t uc_storage_remove(const char* key, uint32_t keyLength);
/* The index-th key in byte order (0-based), the same way as uc_storage_get;
 * UC_ERR_NOT_FOUND past the last. Stable while nothing is set or removed. */
UC_IMPORT(uc_storage_key) int32_t uc_storage_key(uint32_t index, char* out, uint32_t capacity);
UC_IMPORT(uc_storage_clear) int32_t uc_storage_clear(void);

/* ----- Clipboard (v2) -----
 * Puts text on the system clipboard. Only while the app handles a user
 * action - a click, a toggle, an edit or Enter delivered to its uc_event -
 * as a browser asks for a user gesture; elsewhere UC_ERR_DENIED. UC_ERR_LIMIT
 * past 1 MB, UC_ERR_STATE when the system clipboard refused it. Reading the
 * clipboard comes with per-app permissions. */
UC_IMPORT(uc_clipboard_write) int32_t uc_clipboard_write(const char* text, uint32_t textLength);

#ifdef __cplusplus
}
#endif

/* The exports a guest provides:
 *   void uc_main(void);
 *   void uc_event(uint32_t handle, uint32_t event, int32_t detail);   (if it listens,
 *                                                  or uses timers or fetch)
 *   int32_t uc_abi_version(void);                  (optional: the version it needs) */
#define UC_EXPORT(name) __attribute__((export_name(#name)))

#endif /* __wasm__ */

#endif /* ULTRAWEB_GUEST_ULTRAWEB_H */
