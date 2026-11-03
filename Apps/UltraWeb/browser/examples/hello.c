/*
 * Apps/UltraWeb/browser/examples/hello.c
 * An UltraWeb app that touches every part of element ABI v2: a counter
 * kept in storage across visits, a text input and a checkbox, a timer, a
 * fetch of hello.txt next to the app, and a clipboard copy. It runs the same
 * in the UltraWeb browser and in Chrome or Firefox through the UltraWeb
 * runtime (Docs/UltraWeb/UltraWebInTheBrowser.md).
 *
 * Freestanding C - no libc - so any clang with the WebAssembly target
 * builds it, the one inside emsdk included:
 *
 *   clang --target=wasm32 -O2 -nostdlib -Wl,--no-entry \
 *       -I UltraWeb/guest hello.c -o hello.wasm
 *
 * (wasi-sdk builds it too, as the apps in Docs/UltraWeb/UltraWeb.md.)
 *
 * Version: 0.1.0
 * Last Modified: 2026-10-27
 * Author: UltraCanvas Framework / ULTRA OS
 */
#include "ultraweb.h"

static uint32_t countLabel, countButton, nameInput, greetButton, greeting, shout, clock, fetched, copyButton;
static int32_t clicks, seconds, fetchId;

static uint32_t Length(const char* s) {
    uint32_t n = 0;
    while (s[n]) ++n;
    return n;
}

/* Writes `value` in decimal at `out` and returns the end. */
static char* Decimal(char* out, int32_t value) {
    char digits[12];
    int n = 0;
    if (value < 0) { *out++ = '-'; value = -value; }
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) *out++ = digits[--n];
    return out;
}

static char* Append(char* out, const char* text) {
    while (*text) *out++ = *text++;
    return out;
}

static uint32_t Add(uint32_t parent, const char* kind) {
    uint32_t h = uc_create(kind, Length(kind));
    uc_insert(parent, h, UC_NO_HANDLE);
    return h;
}

static void SetText(uint32_t h, const char* text) { uc_set_text(h, UC_PROP_TEXT, text, Length(text)); }

static uint32_t Row(uint32_t parent) {
    uint32_t row = Add(parent, UC_KIND_CONTAINER);
    uc_set_number(row, UC_PROP_DIRECTION, UC_DIRECTION_ROW);
    uc_set_number(row, UC_PROP_GAP, 12);
    uc_set_number(row, UC_PROP_ALIGN, UC_ALIGN_CENTER);
    return row;
}

static void ShowCount(void) {
    char text[64];
    char* end = Append(text, "Clicked ");
    end = Decimal(end, clicks);
    end = Append(end, clicks == 1 ? " time (kept across visits)" : " times (kept across visits)");
    *end = 0;
    SetText(countLabel, text);
}

static void ShowClock(void) {
    char text[64];
    char* end = Append(text, "Running for ");
    end = Decimal(end, seconds);
    end = Append(end, " s");
    *end = 0;
    SetText(clock, text);
}

static void Greet(void) {
    char name[128];
    char text[200];
    int32_t n = uc_get_text(nameInput, UC_PROP_TEXT, name, sizeof name - 1);
    if (n < 0) n = 0;
    if (n > (int32_t)sizeof name - 1) n = sizeof name - 1;
    name[n] = 0;
    double loud = uc_get_number(shout, UC_PROP_CHECKED);
    char* end = Append(text, "Hello, ");
    end = Append(end, n ? name : "world");
    end = Append(end, loud == 1 ? "!!!" : ".");
    *end = 0;
    SetText(greeting, text);
}

UC_EXPORT(uc_abi_version) int32_t uc_abi_version(void) { return 2; }

UC_EXPORT(uc_main) void uc_main(void) {
    uc_set_number(UC_ROOT_HANDLE, UC_PROP_PADDING, 24);
    uc_set_number(UC_ROOT_HANDLE, UC_PROP_GAP, 14);

    uint32_t title = Add(UC_ROOT_HANDLE, UC_KIND_LABEL);
    SetText(title, "Hello from an UltraWeb app");
    uc_set_number(title, UC_PROP_FONT_SIZE, 20);
    uint32_t note = Add(UC_ROOT_HANDLE, UC_KIND_LABEL);
    SetText(note, "This app is a few kilobytes of WebAssembly. The elements are UltraCanvas, from the runtime.");

    uint32_t row = Row(UC_ROOT_HANDLE);
    countButton = Add(row, UC_KIND_BUTTON);
    SetText(countButton, "Count");
    uc_listen(countButton, UC_EVENT_CLICK);
    countLabel = Add(row, UC_KIND_LABEL);
    char stored[16];
    int32_t n = uc_storage_get("clicks", 6, stored, sizeof stored);
    if (n > 0 && n <= (int32_t)sizeof stored) {
        for (int32_t i = 0; i < n && stored[i] >= '0' && stored[i] <= '9'; ++i) clicks = clicks * 10 + (stored[i] - '0');
    }
    ShowCount();

    row = Row(UC_ROOT_HANDLE);
    nameInput = Add(row, UC_KIND_TEXT_INPUT);
    uc_set_text(nameInput, UC_PROP_PLACEHOLDER, "Your name", 9);
    uc_listen(nameInput, UC_EVENT_SUBMIT);
    shout = Add(row, UC_KIND_CHECKBOX);
    SetText(shout, "Shout");
    uc_listen(shout, UC_EVENT_TOGGLE);
    greetButton = Add(row, UC_KIND_BUTTON);
    SetText(greetButton, "Greet");
    uc_listen(greetButton, UC_EVENT_CLICK);
    greeting = Add(UC_ROOT_HANDLE, UC_KIND_LABEL);
    SetText(greeting, "Type a name and press Greet or Enter.");

    clock = Add(UC_ROOT_HANDLE, UC_KIND_LABEL);
    ShowClock();
    uc_timer_start(1000, 1);

    fetched = Add(UC_ROOT_HANDLE, UC_KIND_LABEL);
    SetText(fetched, "Fetching hello.txt ...");
    fetchId = uc_fetch("hello.txt", 9, UC_METHOD_GET, 0, 0, 0, 0);
    if (fetchId < 0) SetText(fetched, "Fetch refused");

    copyButton = Add(UC_ROOT_HANDLE, UC_KIND_BUTTON);
    SetText(copyButton, "Copy the greeting");
    uc_listen(copyButton, UC_EVENT_CLICK);
}

UC_EXPORT(uc_event) void uc_event(uint32_t handle, uint32_t event, int32_t detail) {
    if (handle == countButton && event == UC_EVENT_CLICK) {
        ++clicks;
        ShowCount();
        char text[12];
        char* end = Decimal(text, clicks);
        uc_storage_set("clicks", 6, text, (uint32_t)(end - text));
    } else if ((handle == greetButton && event == UC_EVENT_CLICK) || (handle == nameInput && event == UC_EVENT_SUBMIT)
               || handle == shout) {
        Greet();
    } else if (handle == copyButton && event == UC_EVENT_CLICK) {
        char text[200];
        int32_t n = uc_get_text(greeting, UC_PROP_TEXT, text, sizeof text);
        if (n > (int32_t)sizeof text) n = sizeof text;
        if (n > 0) uc_clipboard_write(text, (uint32_t)n);
    } else if (handle == UC_NO_HANDLE && event == UC_EVENT_TIMER) {
        ++seconds;
        ShowClock();
    } else if (handle == UC_NO_HANDLE && event == UC_EVENT_FETCH && detail == fetchId) {
        char body[200];
        char text[240];
        int32_t status = uc_fetch_status(fetchId);
        int32_t n = uc_fetch_body(fetchId, body, sizeof body - 1);
        char* end;
        if (status == 200 && n >= 0) {
            if (n > (int32_t)sizeof body - 1) n = sizeof body - 1;
            while (n > 0 && (body[n - 1] == '\n' || body[n - 1] == '\r')) --n;
            body[n] = 0;
            end = Append(text, "hello.txt says: ");
            end = Append(end, body);
        } else {
            end = Append(text, "hello.txt: status ");
            end = Decimal(end, status);
        }
        *end = 0;
        SetText(fetched, text);
        uc_fetch_close(fetchId);
    }
}
