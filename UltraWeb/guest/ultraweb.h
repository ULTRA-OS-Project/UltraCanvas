/*
 * UltraWeb/guest/ultraweb.h
 * The UltraWeb element ABI, version 4: how a WebAssembly app builds its UI
 * out of UltraCanvas elements that the UltraWeb browser owns, and the
 * services it may use beside them - timers, fetch, storage, the clipboard,
 * WebSockets, peer-to-peer data channels (WebRTC, through UltraRTC), and
 * calls: audio and video tracks, the camera and the microphone, pictures in
 * a VideoTile element.
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
 * Version 3 adds WebSockets (uc_ws_*) and peer connections with data
 * channels (uc_rtc_*); an app that uses them returns 3. Version 4 adds audio
 * and video tracks, the devices (behind the user's permission) and the
 * VideoTile element; an app that uses them returns 4.
 *
 * Version: 4.0.0 - audio and video tracks, the camera and the microphone, VideoTile
 * Version: 3.0.0 - WebSockets and peer-to-peer data channels
 * Version: 2.0.0
 * Last Modified: 2026-11-11
 * Author: UltraCanvas Framework / ULTRA OS
 */
#ifndef ULTRAWEB_GUEST_ULTRAWEB_H
#define ULTRAWEB_GUEST_ULTRAWEB_H

#include <stdint.h>

#define UC_ABI_VERSION 4

/* ===== Handles ===== */
#define UC_NO_HANDLE   0u
#define UC_ROOT_HANDLE 1u

/* ===== Element kinds (the string passed to uc_create) ===== */
#define UC_KIND_CONTAINER  "Container"   /* a flex box: column (default) or row */
#define UC_KIND_LABEL      "Label"
#define UC_KIND_BUTTON     "Button"
#define UC_KIND_TEXT_INPUT "TextInput"
#define UC_KIND_CHECKBOX   "Checkbox"
#define UC_KIND_VIDEO_TILE "VideoTile"   /* v4: a live picture - a track's video
                                            (uc_rtc_video_show) or the camera's
                                            self view (uc_rtc_camera); its TEXT is
                                            the caption shown without a picture */

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
#define UC_PROP_FIT        16u   /* UC_FIT_*: VideoTile (v4) */
#define UC_PROP_MIRRORED   17u   /* 0 / 1: VideoTile (v4); a self view is mirrored by itself */

#define UC_FIT_CONTAIN 0         /* the whole picture, bars where the shapes differ */
#define UC_FIT_COVER   1         /* fills the tile, the overflow cut off */

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
#define UC_EVENT_WS    64u   /* a WebSocket opened, received, closed or failed (v3).
                                detail = its id (uc_ws_open) */
#define UC_EVENT_RTC  128u   /* a peer has events to read with uc_rtc_poll (v3).
                                detail = the peer's id (uc_rtc_peer_open) */

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
                                    connection, TLS, timeout, cut off; a WebSocket
                                    that could not connect or was lost (v3) */

/* ===== WebSocket states (uc_ws_state, v3) ===== */
#define UC_WS_CONNECTING 0
#define UC_WS_OPEN       1
#define UC_WS_CLOSED     2      /* closed by either side; uc_ws_close_code says how */

/* ===== Peer events (uc_rtc_poll, v3) =====
 * Each event is a kind, a channel id (0 when none), a number and a text. */
#define UC_RTC_LOCAL_DESCRIPTION 1u  /* send it to the other side. number: UC_RTC_OFFER or
                                        UC_RTC_ANSWER; text: the SDP */
#define UC_RTC_CANDIDATE         2u  /* send it to the other side. text: the media id,
                                        a newline, then the candidate */
#define UC_RTC_STATE             3u  /* the connection's state: number UC_RTC_STATE_* */
#define UC_RTC_CHANNEL           4u  /* the other side opened a channel: channel, text its label */
#define UC_RTC_CHANNEL_OPEN      5u  /* channel: it can send */
#define UC_RTC_CHANNEL_CLOSE     6u  /* channel: it closed (its id stays until uc_rtc_channel_close) */
#define UC_RTC_MESSAGE           7u  /* channel: a message. number 1 binary, 0 text; text: the bytes */
/* v4: in these the channel slot carries a track's id. */
#define UC_RTC_TRACK             8u  /* the other side added a track this side has not: track,
                                        number UC_RTC_AUDIO or UC_RTC_VIDEO, text its media id */
#define UC_RTC_TRACK_OPEN        9u  /* track: media can flow */
#define UC_RTC_TRACK_CLOSE      10u  /* track: it ended */
#define UC_RTC_PERMISSION       11u  /* track: what became of uc_rtc_camera / uc_rtc_microphone.
                                        number UC_RTC_DEVICE_*; text "camera" or "microphone" */

#define UC_RTC_AUDIO 0u
#define UC_RTC_VIDEO 1u

#define UC_RTC_DEVICE_BLOCKED 0      /* the user said no (now or before) */
#define UC_RTC_DEVICE_ON      1      /* allowed, and the device runs */
#define UC_RTC_DEVICE_FAILED  2      /* allowed, but the device could not be opened */

/* Directions for uc_rtc_audio_add / uc_rtc_video_add. */
#define UC_RTC_SENDRECV 0u
#define UC_RTC_SENDONLY 1u
#define UC_RTC_RECVONLY 2u

#define UC_RTC_OFFER  1u
#define UC_RTC_ANSWER 2u

#define UC_RTC_STATE_NEW          0
#define UC_RTC_STATE_CONNECTING   1
#define UC_RTC_STATE_CONNECTED    2
#define UC_RTC_STATE_DISCONNECTED 3
#define UC_RTC_STATE_FAILED       4
#define UC_RTC_STATE_CLOSED       5

/* Flags for uc_rtc_channel_open. */
#define UC_RTC_UNORDERED 1u          /* messages may arrive out of order */

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

/* ----- WebSockets (v3) -----
 * Opens a WebSocket and returns its id (> 0); uc_event(UC_NO_HANDLE,
 * UC_EVENT_WS, id) follows when it opens, when it closes or fails, and when
 * a message arrives while none was waiting: read with uc_ws_receive until
 * UC_ERR_NOT_FOUND, or the next message brings no event. url is ws: or wss: (http: and https: count as ws: and
 * wss:), and may be relative to the app's address. protocols is a
 * comma-separated list of subprotocols ("" none).
 *
 * The rules are a browser's: the request carries the app's origin in an
 * Origin header ("null" for an app from a file or about:), and it is the
 * server that decides whether to accept it; no cookies are sent; an https
 * app cannot open ws: (wss: only). Returns the id, or UC_ERR_DENIED (those
 * rules, or no WebSockets in this UltraWeb), UC_ERR_LIMIT (more than 8 open,
 * a URL over 8 KB), UC_ERR_STATE (no uc_event export, a URL that does not
 * parse), UC_ERR_MEMORY. A message is limited to 1 MB either way; one that
 * arrives larger ends the connection. Up to 4 MB of received messages wait
 * to be read; past that the oldest are dropped (the console says so). */
UC_IMPORT(uc_ws_open) int32_t uc_ws_open(const char* url, uint32_t urlLength,
                                         const char* protocols, uint32_t protocolsLength);
/* UC_WS_CONNECTING, UC_WS_OPEN, UC_WS_CLOSED, or why it failed
 * (UC_ERR_NETWORK, UC_ERR_LIMIT for a message over 1 MB). UC_ERR_NOT_FOUND
 * for an id that is not open. */
UC_IMPORT(uc_ws_state) int32_t uc_ws_state(int32_t id);
/* Sends a message: text (UTF-8) or, when binary is non-zero, bytes.
 * UC_ERR_STATE unless the socket is open, UC_ERR_LIMIT past 1 MB. */
UC_IMPORT(uc_ws_send) int32_t uc_ws_send(int32_t id, const void* data, uint32_t length, uint32_t binary);
/* The oldest message not yet read: copies at most capacity bytes, writes 1
 * (binary) or 0 (text) to *outBinary when it is not null, and returns the
 * message's full length. It is taken off the queue only when it fitted, so
 * a guest can call again with a bigger buffer. UC_ERR_NOT_FOUND when no
 * message waits. */
UC_IMPORT(uc_ws_receive) int32_t uc_ws_receive(int32_t id, void* out, uint32_t capacity, uint32_t* outBinary);
/* The close code the other side (or the failure) gave, 0 while open. */
UC_IMPORT(uc_ws_close_code) int32_t uc_ws_close_code(int32_t id);
/* Closes the socket (code 1000 when code is 0) and forgets it; no event
 * follows. */
UC_IMPORT(uc_ws_close) int32_t uc_ws_close(int32_t id, uint32_t code, const char* reason, uint32_t reasonLength);

/* ----- Peer connections and data channels (v3) -----
 * WebRTC through UltraRTC: DTLS-encrypted, peer to peer, with data channels
 * carrying text or bytes. The app does its own signalling - it sends the
 * other side what UC_RTC_LOCAL_DESCRIPTION and UC_RTC_CANDIDATE events
 * carry (over uc_ws_* or uc_fetch), and hands what comes back to
 * uc_rtc_set_remote and uc_rtc_add_candidate, untouched. Negotiation is
 * automatic: opening the first channel makes the offer, a remote offer
 * makes the answer.
 *
 * config is JSON in the shape of RTCConfiguration:
 *   {"iceServers": [{"urls": "stun:stun.example.org:3478"},
 *                   {"urls": ["turn:turn.example.org"], "username": "u", "credential": "p"}]}
 * ("" or {} for none). An app from the network connects through TURN relays
 * only, which keeps the machine's local addresses from the other side; an
 * app from a file or about: may connect directly.
 *
 * Events wait per peer; uc_event(UC_NO_HANDLE, UC_EVENT_RTC, peer) comes
 * when one arrives while none was waiting: read them with uc_rtc_poll until
 * UC_ERR_NOT_FOUND, or the next one brings no event. Limits: 4 peers,
 * 16 channels a peer, 256 KB a message, 1000 events waiting a peer (past
 * that the oldest messages are dropped). UC_ERR_DENIED when this UltraWeb
 * has no WebRTC engine. */
UC_IMPORT(uc_rtc_peer_open) int32_t uc_rtc_peer_open(const char* config, uint32_t configLength);
/* Closes the peer and its channels, and forgets them all; no event follows. */
UC_IMPORT(uc_rtc_peer_close) int32_t uc_rtc_peer_close(int32_t peer);
/* type UC_RTC_OFFER or UC_RTC_ANSWER. UC_ERR_STATE for an SDP the engine refuses. */
UC_IMPORT(uc_rtc_set_remote) int32_t uc_rtc_set_remote(int32_t peer, uint32_t type,
                                                       const char* sdp, uint32_t sdpLength);
UC_IMPORT(uc_rtc_add_candidate) int32_t uc_rtc_add_candidate(int32_t peer, const char* candidate,
                                                             uint32_t candidateLength,
                                                             const char* mid, uint32_t midLength);
/* Opens a data channel and returns its id (> 0). flags: UC_RTC_UNORDERED;
 * maxRetransmits < 0: reliable, else at most that many resends. */
UC_IMPORT(uc_rtc_channel_open) int32_t uc_rtc_channel_open(int32_t peer, const char* label,
                                                           uint32_t labelLength, uint32_t flags,
                                                           int32_t maxRetransmits);
/* Sends text or (binary non-zero) bytes. UC_ERR_STATE until the channel is open. */
UC_IMPORT(uc_rtc_send) int32_t uc_rtc_send(int32_t channel, const void* data, uint32_t length, uint32_t binary);
UC_IMPORT(uc_rtc_channel_close) int32_t uc_rtc_channel_close(int32_t channel);
/* ----- Calls (v4) -----
 * Audio (Opus, 48 kHz) and video (VP8) tracks on a peer. Add a track on both
 * sides with the same media id ("audio", "video") before the offer - the
 * two then share one m-line - then make the offer with uc_rtc_offer on the
 * side that calls (tracks, unlike channels, do not start a negotiation by
 * themselves); the answer follows by itself. A track the other side added
 * that this side has not arrives as UC_RTC_TRACK. Track ids share the peer
 * and channel id space. Limits: 4 audio and 4 video tracks a peer.
 *
 * The camera and the microphone are the user's: the first uc_rtc_camera or
 * uc_rtc_microphone of an app shows a bar in its tab - "x.org wants to use
 * your camera: Allow / Block" - and the answer is kept for the app's
 * origin. The call returns UC_OK at once (or UC_ERR_DENIED when the user
 * blocked it before); what became of it arrives as UC_RTC_PERMISSION. Off
 * (on = 0) stops the device at once. A closed peer stops its devices. */
/* direction UC_RTC_SENDRECV / SENDONLY / RECVONLY. Returns the track's id. */
UC_IMPORT(uc_rtc_audio_add) int32_t uc_rtc_audio_add(int32_t peer, const char* mid, uint32_t midLength,
                                                     uint32_t direction);
/* What it sends: width x height (16..1920 x 16..1080), fps (1..30), kbps
 * (50..4000); 0 for any of them takes the default, 640 x 480 at 24 fps and
 * 700 kbit/s. Pictures of another size are scaled. */
UC_IMPORT(uc_rtc_video_add) int32_t uc_rtc_video_add(int32_t peer, const char* mid, uint32_t midLength,
                                                     uint32_t direction, uint32_t width, uint32_t height,
                                                     uint32_t fps, uint32_t kbps);
/* Makes the offer (a UC_RTC_LOCAL_DESCRIPTION follows): once the tracks are added. */
UC_IMPORT(uc_rtc_offer) int32_t uc_rtc_offer(int32_t peer);
/* The microphone feeds an audio track. */
UC_IMPORT(uc_rtc_microphone) int32_t uc_rtc_microphone(int32_t track, uint32_t on);
/* The camera feeds a video track; its pictures show, mirrored, in selfView
 * (a VideoTile, or UC_NO_HANDLE for none). */
UC_IMPORT(uc_rtc_camera) int32_t uc_rtc_camera(int32_t track, uint32_t on, uint32_t selfView);
/* Received audio plays on the speaker. No permission is asked. */
UC_IMPORT(uc_rtc_playback) int32_t uc_rtc_playback(int32_t track, uint32_t on);
/* Received pictures show in the VideoTile `tile` (UC_NO_HANDLE: nowhere).
 * The pixels never pass through the app's memory. */
UC_IMPORT(uc_rtc_video_show) int32_t uc_rtc_video_show(int32_t track, uint32_t tile);
/* Mute / camera off: a disabled track sends nothing; the device keeps running. */
UC_IMPORT(uc_rtc_track_enabled) int32_t uc_rtc_track_enabled(int32_t track, uint32_t enabled);

/* The peer's oldest event: writes kind, channel and number to info[0..2],
 * copies at most capacity bytes of its text, and returns the text's full
 * length. The event is taken off the queue only when its text fitted.
 * UC_ERR_NOT_FOUND when none waits. */
UC_IMPORT(uc_rtc_poll) int32_t uc_rtc_poll(int32_t peer, uint32_t* info, char* out, uint32_t capacity);

#ifdef __cplusplus
}
#endif

/* The exports a guest provides:
 *   void uc_main(void);
 *   void uc_event(uint32_t handle, uint32_t event, int32_t detail);   (if it listens,
 *                                    or uses timers, fetch, WebSockets or peers)
 *   int32_t uc_abi_version(void);                  (optional: the version it needs) */
#define UC_EXPORT(name) __attribute__((export_name(#name)))

#endif /* __wasm__ */

#endif /* ULTRAWEB_GUEST_ULTRAWEB_H */
