/*
 * UltraWeb/guest/ultraweb.h
 * The UltraWeb element ABI, version 1: how a WebAssembly app builds its UI
 * out of UltraCanvas elements that the UltraWeb browser owns.
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
 *
 * Version: 1.0.0
 * Last Modified: 2026-10-06
 * Author: UltraCanvas Framework / ULTRA OS
 */
#ifndef ULTRAWEB_GUEST_ULTRAWEB_H
#define ULTRAWEB_GUEST_ULTRAWEB_H

#include <stdint.h>

#define UC_ABI_VERSION 1

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
#define UC_PROP_WIDTH       6u   /* px, 0 = automatic: every kind */
#define UC_PROP_HEIGHT      7u   /* px, 0 = automatic: every kind */
#define UC_PROP_GROW        8u   /* flex-grow inside the parent: every kind */
#define UC_PROP_DIRECTION   9u   /* UC_DIRECTION_*: Container */
#define UC_PROP_GAP        10u   /* px between children: Container */
#define UC_PROP_PADDING    11u   /* px on every side: Container */
#define UC_PROP_FONT_SIZE  12u   /* points: Label, Button, TextInput */
#define UC_PROP_TEXT_COLOR 13u   /* 0xRRGGBBAA: Label */
#define UC_PROP_BACKGROUND 14u   /* 0xRRGGBBAA: Container, Label */

#define UC_DIRECTION_COLUMN 0
#define UC_DIRECTION_ROW    1

/* ===== Events (bits for uc_listen; one value per uc_event call) ===== */
#define UC_EVENT_CLICK  1u   /* Button pressed.                          detail 0 */
#define UC_EVENT_CHANGE 2u   /* TextInput text edited by the user.       detail 0 */
#define UC_EVENT_SUBMIT 4u   /* Enter pressed in a TextInput.            detail 0 */
#define UC_EVENT_TOGGLE 8u   /* Checkbox toggled by the user. detail 1 checked, 0 not */

/* ===== Results ===== */
#define UC_OK                0
#define UC_ERR_HANDLE       -1   /* no such handle, or released */
#define UC_ERR_KIND         -2   /* unknown element kind */
#define UC_ERR_PROPERTY     -3   /* the element kind has no such property */
#define UC_ERR_MEMORY       -4   /* a pointer and length outside guest memory */
#define UC_ERR_STATE        -5   /* not allowed here: a cycle, the root, a non-container parent */
#define UC_ERR_LIMIT        -6   /* too many elements, or a string too long */

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

#ifdef __cplusplus
}
#endif

/* The exports a guest provides:
 *   void uc_main(void);
 *   void uc_event(uint32_t handle, uint32_t event, int32_t detail);   (if it listens) */
#define UC_EXPORT(name) __attribute__((export_name(#name)))

#endif /* __wasm__ */

#endif /* ULTRAWEB_GUEST_ULTRAWEB_H */
