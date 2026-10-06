#!/usr/bin/env python3
# Apps/UltraWeb/host/generate_demo_app.py
# Generates UltraWebDemoApp.h: the about:demo guest, an UltraWeb app in
# WebAssembly text, with every string's offset and length computed here so
# the module cannot point past its own data. Run after editing:
#     python3 Apps/UltraWeb/host/generate_demo_app.py
# The ids it uses (property 1 = UC_PROP_TEXT, event 1 = UC_EVENT_CLICK, ...)
# are the ones in UltraWeb/guest/ultraweb.h.
import pathlib
strings = [
    ("kContainer", "Container"), ("kLabel", "Label"), ("kButton", "Button"),
    ("kTextInput", "TextInput"), ("kCheckbox", "Checkbox"),
    ("title", "Hello from WebAssembly"),
    ("about", "This page is a small WebAssembly module, written by hand in WebAssembly text. "
              "Every control below is a real UltraCanvas element that UltraWeb created for it "
              "through the element ABI; the module only holds handles. It keeps its count in "
              "UltraWeb's storage, so the count survives a reload, and a timer tells how long it has run."),
    ("count", "Count"),
    ("clicked", "Clicked "),
    ("times", " times"),
    ("placeholder", "Type something"),
    ("typed", "You typed: "),
    ("nothing", "You typed: nothing yet"),
    ("show", "Show the counter"),
    ("started", "about:demo started"),
    ("clicksKey", "clicks"),
    ("copy", "Copy what you typed"),
    ("copied", "Copied to the clipboard"),
    ("refused", "The clipboard did not take it"),
    ("running", "Running for "),
    ("seconds", " s"),
]
offsets = {}
data = []
at = 0
for name, text in strings:
    raw = text.encode()
    offsets[name] = (at, len(raw))
    esc = ''.join(c if 32 <= ord(c) < 127 and c not in '"\\' else '\\%02x' % ord(c) for c in text)
    data.append(f'  (data (i32.const {at}) "{esc}")')
    at += (len(raw) + 15) // 16 * 16
SCRATCH = 4096          # text being built for a label
INPUT_MAX = 1024
STORED = 6144           # the count as digits, to and from storage
CLIP = 8192             # the input's text on its way to the clipboard
CLIP_MAX = 32768
assert at < SCRATCH and SCRATCH + 64 + INPUT_MAX < STORED and STORED + 16 < CLIP and CLIP + CLIP_MAX <= 65536
def s(name):
    o, l = offsets[name]
    return f"(i32.const {o}) (i32.const {l})"
wat = f"""
(module
  (import "ultracanvas" "uc_create" (func $create (param i32 i32) (result i32)))
  (import "ultracanvas" "uc_insert" (func $insert (param i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_set_text" (func $set_text (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_set_number" (func $set_number (param i32 i32 f64) (result i32)))
  (import "ultracanvas" "uc_get_text" (func $get_text (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_listen" (func $listen (param i32 i32) (result i32)))
  (import "ultracanvas" "uc_log" (func $log (param i32 i32)))
  (import "ultracanvas" "uc_timer_start" (func $timer_start (param i32 i32) (result i32)))
  (import "ultracanvas" "uc_storage_get" (func $storage_get (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_storage_set" (func $storage_set (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_clipboard_write" (func $clipboard_write (param i32 i32) (result i32)))
  (memory (export "memory") 1)
{chr(10).join(data)}
  (global $count (mut i32) (i32.const 0))
  (global $button (mut i32) (i32.const 0))
  (global $counter (mut i32) (i32.const 0))
  (global $input (mut i32) (i32.const 0))
  (global $echo (mut i32) (i32.const 0))
  (global $check (mut i32) (i32.const 0))
  (global $copy (mut i32) (i32.const 0))
  (global $uptime (mut i32) (i32.const 0))
  (global $tick (mut i32) (i32.const 0))
  (global $seconds (mut i32) (i32.const 0))

  ;; Element ABI v2: timers, storage and the clipboard.
  (func (export "uc_abi_version") (result i32) (i32.const 2))

  ;; A new element of a kind, inserted at the end of a parent.
  (func $add (param $parent i32) (param $kind i32) (param $kindLength i32) (result i32)
    (local $h i32)
    (local.set $h (call $create (local.get $kind) (local.get $kindLength)))
    (drop (call $insert (local.get $parent) (local.get $h) (i32.const 0)))
    (local.get $h))

  (func $text (param $h i32) (param $ptr i32) (param $length i32)
    (drop (call $set_text (local.get $h) (i32.const 1) (local.get $ptr) (local.get $length))))

  ;; Decimal digits of a non-negative value at dst; returns their count.
  (func $digits (param $value i32) (param $dst i32) (result i32)
    (local $n i32) (local $i i32) (local $t i32)
    (loop $emit
      (i32.store8 (i32.add (local.get $dst) (local.get $n))
                  (i32.add (i32.const 48) (i32.rem_u (local.get $value) (i32.const 10))))
      (local.set $n (i32.add (local.get $n) (i32.const 1)))
      (local.set $value (i32.div_u (local.get $value) (i32.const 10)))
      (br_if $emit (local.get $value)))
    ;; reverse in place
    (local.set $i (i32.const 0))
    (block $done
      (loop $swap
        (br_if $done (i32.ge_u (local.get $i) (i32.div_u (local.get $n) (i32.const 2))))
        (local.set $t (i32.load8_u (i32.add (local.get $dst) (local.get $i))))
        (i32.store8 (i32.add (local.get $dst) (local.get $i))
                    (i32.load8_u (i32.add (local.get $dst) (i32.sub (i32.sub (local.get $n) (i32.const 1)) (local.get $i)))))
        (i32.store8 (i32.add (local.get $dst) (i32.sub (i32.sub (local.get $n) (i32.const 1)) (local.get $i))) (local.get $t))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $swap)))
    (local.get $n))

  ;; "Clicked N times" into the counter label.
  (func $show_count
    (local $length i32)
    (memory.copy (i32.const {SCRATCH}) {s("clicked")})
    (local.set $length (i32.add (i32.const {offsets["clicked"][1]})
      (call $digits (global.get $count) (i32.const {SCRATCH + offsets["clicked"][1]}))))
    (memory.copy (i32.add (i32.const {SCRATCH}) (local.get $length)) {s("times")})
    (local.set $length (i32.add (local.get $length) (i32.const {offsets["times"][1]})))
    (call $text (global.get $counter) (i32.const {SCRATCH}) (local.get $length)))

  ;; "Running for N s" into the uptime label.
  (func $show_uptime
    (local $length i32)
    (memory.copy (i32.const {SCRATCH}) {s("running")})
    (local.set $length (i32.add (i32.const {offsets["running"][1]})
      (call $digits (global.get $seconds) (i32.const {SCRATCH + offsets["running"][1]}))))
    (memory.copy (i32.add (i32.const {SCRATCH}) (local.get $length)) {s("seconds")})
    (local.set $length (i32.add (local.get $length) (i32.const {offsets["seconds"][1]})))
    (call $text (global.get $uptime) (i32.const {SCRATCH}) (local.get $length)))

  ;; The count from storage, if an earlier run left one: up to 9 digits.
  (func $load_count
    (local $length i32) (local $i i32) (local $c i32) (local $value i32)
    (local.set $length (call $storage_get {s("clicksKey")} (i32.const {STORED}) (i32.const 16)))
    (if (i32.or (i32.lt_s (local.get $length) (i32.const 1)) (i32.gt_s (local.get $length) (i32.const 9))) (then (return)))
    (block $bad
      (loop $parse
        (local.set $c (i32.load8_u (i32.add (i32.const {STORED}) (local.get $i))))
        (br_if $bad (i32.or (i32.lt_u (local.get $c) (i32.const 48)) (i32.gt_u (local.get $c) (i32.const 57))))
        (local.set $value (i32.add (i32.mul (local.get $value) (i32.const 10)) (i32.sub (local.get $c) (i32.const 48))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $parse (i32.lt_u (local.get $i) (local.get $length))))
      (global.set $count (local.get $value))))

  (func $store_count
    (drop (call $storage_set {s("clicksKey")} (i32.const {STORED})
                             (call $digits (global.get $count) (i32.const {STORED})))))

  (func (export "uc_main")
    (local $h i32) (local $row i32)
    (call $log {s("started")})
    ;; The root: a padded column.
    (drop (call $set_number (i32.const 1) (i32.const 10) (f64.const 10)))
    (drop (call $set_number (i32.const 1) (i32.const 11) (f64.const 16)))

    (local.set $h (call $add (i32.const 1) {s("kLabel")}))
    (call $text (local.get $h) {s("title")})
    (drop (call $set_number (local.get $h) (i32.const 12) (f64.const 18)))

    (local.set $h (call $add (i32.const 1) {s("kLabel")}))
    (call $text (local.get $h) {s("about")})

    ;; A row: the button and the count beside it.
    (local.set $row (call $add (i32.const 1) {s("kContainer")}))
    (drop (call $set_number (local.get $row) (i32.const 9) (f64.const 1)))
    (drop (call $set_number (local.get $row) (i32.const 10) (f64.const 12)))
    (global.set $button (call $add (local.get $row) {s("kButton")}))
    (call $text (global.get $button) {s("count")})
    (drop (call $listen (global.get $button) (i32.const 1)))
    (global.set $counter (call $add (local.get $row) {s("kLabel")}))
    (drop (call $set_number (global.get $counter) (i32.const 8) (f64.const 1)))
    (call $load_count)
    (call $show_count)

    (global.set $input (call $add (i32.const 1) {s("kTextInput")}))
    (drop (call $set_text (global.get $input) (i32.const 2) {s("placeholder")}))
    (drop (call $listen (global.get $input) (i32.const 2)))
    (global.set $echo (call $add (i32.const 1) {s("kLabel")}))
    (call $text (global.get $echo) {s("nothing")})

    (global.set $check (call $add (i32.const 1) {s("kCheckbox")}))
    (call $text (global.get $check) {s("show")})
    (drop (call $set_number (global.get $check) (i32.const 3) (f64.const 1)))
    (drop (call $listen (global.get $check) (i32.const 8)))

    ;; The copy button goes between the input and its echo.
    (global.set $copy (call $create {s("kButton")}))
    (drop (call $insert (i32.const 1) (global.get $copy) (global.get $echo)))
    (call $text (global.get $copy) {s("copy")})
    (drop (call $set_number (global.get $copy) (i32.const 6) (f64.const 200)))
    (drop (call $listen (global.get $copy) (i32.const 1)))

    ;; A second-by-second timer.
    (global.set $uptime (call $add (i32.const 1) {s("kLabel")}))
    (call $show_uptime)
    (global.set $tick (call $timer_start (i32.const 1000) (i32.const 1))))

  (func (export "uc_event") (param $h i32) (param $event i32) (param $detail i32)
    (local $length i32)
    ;; Count clicks.
    (if (i32.and (i32.eq (local.get $h) (global.get $button)) (i32.eq (local.get $event) (i32.const 1)))
      (then
        (global.set $count (i32.add (global.get $count) (i32.const 1)))
        (call $show_count)
        (call $store_count)))
    ;; Copy the input's text; a click is a user action, so the clipboard takes it.
    (if (i32.and (i32.eq (local.get $h) (global.get $copy)) (i32.eq (local.get $event) (i32.const 1)))
      (then
        (local.set $length (call $get_text (global.get $input) (i32.const 1) (i32.const {CLIP}) (i32.const {CLIP_MAX})))
        (if (i32.gt_u (local.get $length) (i32.const {CLIP_MAX})) (then (local.set $length (i32.const {CLIP_MAX}))))
        (if (i32.lt_s (local.get $length) (i32.const 0)) (then (local.set $length (i32.const 0))))
        (if (i32.eqz (call $clipboard_write (i32.const {CLIP}) (local.get $length)))
          (then (call $text (global.get $echo) {s("copied")}))
          (else (call $text (global.get $echo) {s("refused")})))))
    ;; The timer: handle 0 (no element), event 16, detail its id.
    (if (i32.and (i32.eqz (local.get $h)) (i32.and (i32.eq (local.get $event) (i32.const 16))
                                                  (i32.eq (local.get $detail) (global.get $tick))))
      (then
        (global.set $seconds (i32.add (global.get $seconds) (i32.const 1)))
        (call $show_uptime)))
    ;; Echo the input: its text straight after the prefix.
    (if (i32.and (i32.eq (local.get $h) (global.get $input)) (i32.eq (local.get $event) (i32.const 2)))
      (then
        (memory.copy (i32.const {SCRATCH}) {s("typed")})
        (local.set $length (call $get_text (global.get $input) (i32.const 1)
                                           (i32.const {SCRATCH + offsets["typed"][1]}) (i32.const {INPUT_MAX})))
        (if (i32.gt_u (local.get $length) (i32.const {INPUT_MAX})) (then (local.set $length (i32.const {INPUT_MAX}))))
        (if (i32.lt_s (local.get $length) (i32.const 0)) (then (local.set $length (i32.const 0))))
        (call $text (global.get $echo) (i32.const {SCRATCH}) (i32.add (local.get $length) (i32.const {offsets["typed"][1]})))))
    ;; The checkbox shows or hides the count.
    (if (i32.and (i32.eq (local.get $h) (global.get $check)) (i32.eq (local.get $event) (i32.const 8)))
      (then
        (drop (call $set_number (global.get $counter) (i32.const 5) (f64.convert_i32_s (local.get $detail))))))))
"""
header = f"""// Apps/UltraWeb/host/UltraWebDemoApp.h
// GENERATED by generate_demo_app.py - edit that, not this.
// about:demo, the app UltraWeb ships with: a WebAssembly module in text
// form that exercises the element ABI (UltraWeb/guest/ultraweb.h) - labels,
// a button counting clicks into storage, a text input echoed into a label
// and copied to the clipboard, a checkbox showing and hiding an element, a
// timer counting seconds. WasmHost compiles the text itself, so it
// needs no wasm toolchain to build, and the UltraWeb tests run it too.
#pragma once

namespace UltraWeb {{

inline const char* DemoAppWat() {{
    return R"WAT({wat})WAT";
}}

}} // namespace UltraWeb
"""
pathlib.Path(__file__).with_name("UltraWebDemoApp.h").write_text(header)
