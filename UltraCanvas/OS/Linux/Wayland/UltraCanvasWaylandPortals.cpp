// OS/Linux/Wayland/UltraCanvasWaylandPortals.cpp
// Screenshots and global shortcuts for the Wayland desktop-shell backend,
// through the freedesktop portals (org.freedesktop.portal.Desktop on the
// session bus, served by xdg-desktop-portal with the session's backend:
// xdg-desktop-portal-wlr on a wlroots compositor, ULTRA OS's among them):
// no Wayland client may read the screen or grab a key, so the session's
// own services answer for it, on every desktop that has a portal.
//
//   - A screenshot is org.freedesktop.portal.Screenshot.Screenshot: the
//     portal writes a PNG and answers with its URI; CaptureScreen moves it
//     to the caller's path, CaptureScreenImage reads it into memory and
//     deletes it at once (the portal cannot hand pixels over, so the file
//     exists for the moment between the two), CaptureScreenArea crops
//     that. A portal that asks the user is waited for, up to a minute.
//   - A global shortcut is org.freedesktop.portal.GlobalShortcuts: a
//     session, one shortcut bound with the accelerator as its preferred
//     trigger ("Ctrl+Alt+V" -> "CTRL+ALT+v", the shortcuts specification's
//     spelling; "Super" alone -> "LOGO"), the Activated signal when the
//     compositor fires it. The compositor's own bindings decide whether
//     the trigger is honoured; a portal that asks the user is waited for by
//     the shortcut's thread, so Start returns at once with the session.
//
// Every exchange is a Request object whose Response signal is awaited on a
// GMainContext of its own (the tray host's arrangement), so nothing here
// needs the application's loop and every call works from any thread.
// Without a portal (no xdg-desktop-portal, no session bus) every call
// fails with the reason.
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "UltraCanvasDesktopShellBackend.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasDebug.h"

#include <gio/gio.h>
#include <cairo/cairo.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

namespace UltraCanvas {
namespace DesktopShellBackend {

namespace {

    constexpr const char* kPortalName = "org.freedesktop.portal.Desktop";
    constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
    constexpr const char* kRequestInterface = "org.freedesktop.portal.Request";
    constexpr const char* kSessionInterface = "org.freedesktop.portal.Session";
    constexpr const char* kScreenshotInterface = "org.freedesktop.portal.Screenshot";
    constexpr const char* kShortcutsInterface = "org.freedesktop.portal.GlobalShortcuts";

    constexpr int kCallTimeoutMs = 5000;        // the portal's own reply to a call
    constexpr int kScreenshotTimeoutMs = 60000; // the user may be asked
    constexpr int kBindTimeoutMs = 3000;        // Start waits this long for the binding; the thread the rest

    struct VariantRef {
        GVariant* value = nullptr;
        explicit VariantRef(GVariant* v = nullptr) : value(v) {}
        ~VariantRef() { if (value) g_variant_unref(value); }
        VariantRef(const VariantRef&) = delete;
        VariantRef& operator=(const VariantRef&) = delete;
        GVariant* operator->() const { return value; }
        explicit operator bool() const { return value != nullptr; }
    };

    std::string ErrorText(GError* gerror) {
        std::string text = gerror && gerror->message ? gerror->message : "unknown error";
        if (gerror) g_error_free(gerror);
        return text;
    }

    // The sender part of a request or session path: the connection's
    // unique name without the colon, dots as underscores (the portal's rule).
    std::string SenderToken(GDBusConnection* bus) {
        const char* unique = g_dbus_connection_get_unique_name(bus);
        std::string token = unique ? unique : "";
        if (!token.empty() && token[0] == ':') token.erase(0, 1);
        std::replace(token.begin(), token.end(), '.', '_');
        return token;
    }

    std::string NewToken(const char* what) {
        static std::atomic<unsigned> counter{0};
        return std::string("ultracanvas_") + what + "_" + std::to_string(getpid()) + "_" + std::to_string(++counter);
    }

    // ----- one request, its Response awaited -----
    struct PortalRequest {
        GDBusConnection* bus = nullptr;
        GMainContext* context = nullptr;
        std::string token;
        std::string path;
        guint subscription = 0;
        bool answered = false;
        uint32_t response = 2;
        GVariant* results = nullptr;

        ~PortalRequest() {
            if (subscription && bus) g_dbus_connection_signal_unsubscribe(bus, subscription);
            if (results) g_variant_unref(results);
        }
    };

    void OnResponse(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters,
                    gpointer data) {
        auto* request = static_cast<PortalRequest*>(data);
        if (request->results) {
            g_variant_unref(request->results);
            request->results = nullptr;
        }
        g_variant_get(parameters, "(u@a{sv})", &request->response, &request->results);
        request->answered = true;
        g_main_context_wakeup(request->context);
    }

    // Subscribes to the Response of the request the portal will create for
    // `token` (its path is known before the call, so no answer is missed).
    void Expect(PortalRequest& request, GDBusConnection* bus, GMainContext* context, const char* what) {
        request.bus = bus;
        request.context = context;
        request.token = NewToken(what);
        request.path = std::string(kPortalPath) + "/request/" + SenderToken(bus) + "/" + request.token;
        request.subscription = g_dbus_connection_signal_subscribe(bus, kPortalName, kRequestInterface, "Response",
                                                                  request.path.c_str(), nullptr,
                                                                  G_DBUS_SIGNAL_FLAGS_NONE, OnResponse, &request, nullptr);
    }

    // A portal older than the token convention answers with a path of its
    // own: follow it.
    void FollowHandle(PortalRequest& request, GVariant* reply) {
        if (!reply) return;
        const char* handle = nullptr;
        g_variant_get(reply, "(&o)", &handle);
        if (!handle || request.path == handle) return;
        g_dbus_connection_signal_unsubscribe(request.bus, request.subscription);
        request.path = handle;
        request.subscription = g_dbus_connection_signal_subscribe(request.bus, kPortalName, kRequestInterface,
                                                                  "Response", request.path.c_str(), nullptr,
                                                                  G_DBUS_SIGNAL_FLAGS_NONE, OnResponse, &request, nullptr);
    }

    gboolean OnTimeout(gpointer data) {
        *static_cast<bool*>(data) = true;
        return G_SOURCE_REMOVE;
    }

    // Iterates the context until the response is in, `wake` is set, or
    // `timeoutMs` pass. True when answered.
    bool Await(PortalRequest& request, int timeoutMs, std::atomic<bool>* wake = nullptr) {
        bool expired = false;
        GSource* timer = g_timeout_source_new(static_cast<guint>(timeoutMs));
        g_source_set_callback(timer, OnTimeout, &expired, nullptr);
        g_source_attach(timer, request.context);
        while (!request.answered && !expired && !(wake && wake->load())) {
            g_main_context_iteration(request.context, TRUE);
        }
        g_source_destroy(timer);
        g_source_unref(timer);
        return request.answered;
    }

    // A string out of a response's results, "" when absent.
    std::string ResultString(GVariant* results, const char* key) {
        if (!results) return {};
        const char* value = nullptr;
        if (g_variant_lookup(results, key, "&s", &value) && value) return value;
        if (g_variant_lookup(results, key, "&o", &value) && value) return value;
        return {};
    }

    // ----- a context and the bus, for the duration of one call -----
    struct PortalScope {
        GMainContext* context = nullptr;
        GDBusConnection* bus = nullptr;
        std::string error;

        PortalScope() {
            context = g_main_context_new();
            g_main_context_push_thread_default(context);
            GError* gerror = nullptr;
            bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &gerror);
            if (!bus) error = "The D-Bus session bus, which the portals answer on, cannot be reached: " + ErrorText(gerror);
        }
        ~PortalScope() {
            if (bus) g_object_unref(bus);
            g_main_context_pop_thread_default(context);
            g_main_context_unref(context);
        }
        PortalScope(const PortalScope&) = delete;
        PortalScope& operator=(const PortalScope&) = delete;
    };

    void AddToken(GVariantBuilder& options, const PortalRequest& request) {
        g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(request.token.c_str()));
    }

    // ===== THE SCREENSHOT =====
    // The portal's PNG, as the path it was written to. False with the reason.
    bool PortalScreenshot(std::string& pngPath, std::string& error) {
        PortalScope scope;
        if (!scope.bus) {
            error = scope.error;
            return false;
        }
        PortalRequest request;
        Expect(request, scope.bus, scope.context, "shot");
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
        AddToken(options, request);
        g_variant_builder_add(&options, "{sv}", "interactive", g_variant_new_boolean(FALSE));
        GError* gerror = nullptr;
        VariantRef reply(g_dbus_connection_call_sync(scope.bus, kPortalName, kPortalPath, kScreenshotInterface,
                                                     "Screenshot", g_variant_new("(sa{sv})", "", &options),
                                                     G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs,
                                                     nullptr, &gerror));
        if (!reply) {
            error = "The screenshot portal (xdg-desktop-portal with this session's backend) did not answer: " +
                    ErrorText(gerror);
            return false;
        }
        FollowHandle(request, reply.value);
        if (!Await(request, kScreenshotTimeoutMs)) {
            error = "The screenshot portal did not finish within a minute.";
            return false;
        }
        if (request.response == 1) {
            error = "The screenshot was cancelled.";
            return false;
        }
        if (request.response != 0) {
            error = "The screenshot portal could not take the screenshot (no screen capture backend for this session?).";
            return false;
        }
        const std::string uri = ResultString(request.results, "uri");
        gchar* filename = uri.empty() ? nullptr : g_filename_from_uri(uri.c_str(), nullptr, nullptr);
        if (!filename) {
            error = "The screenshot portal answered without a file: \"" + uri + "\".";
            return false;
        }
        pngPath = filename;
        g_free(filename);
        return true;
    }

    // The PNG into the BGRx rows DesktopScreenImage holds (cairo's own
    // layout); the file goes.
    bool ReadAndDelete(const std::string& pngPath, DesktopScreenImage& out, std::string& error) {
        cairo_surface_t* surface = cairo_image_surface_create_from_png(pngPath.c_str());
        const cairo_status_t status = cairo_surface_status(surface);
        if (status != CAIRO_STATUS_SUCCESS) {
            error = std::string("The screenshot could not be read: ") + cairo_status_to_string(status);
            cairo_surface_destroy(surface);
            std::error_code ec;
            fs::remove(PathFromUtf8(pngPath), ec);
            return false;
        }
        cairo_surface_flush(surface);
        const int width = cairo_image_surface_get_width(surface);
        const int height = cairo_image_surface_get_height(surface);
        const int sourceStride = cairo_image_surface_get_stride(surface);
        const unsigned char* source = cairo_image_surface_get_data(surface);
        const cairo_format_t format = cairo_image_surface_get_format(surface);
        out = DesktopScreenImage();
        if (width > 0 && height > 0 && source && (format == CAIRO_FORMAT_ARGB32 || format == CAIRO_FORMAT_RGB24)) {
            out.width = width;
            out.height = height;
            out.stride = width * 4;
            out.pixels.resize(static_cast<size_t>(out.stride) * height);
            for (int y = 0; y < height; ++y) {
                std::memcpy(out.pixels.data() + static_cast<size_t>(y) * out.stride, source + static_cast<size_t>(y) * sourceStride,
                            static_cast<size_t>(width) * 4);
            }
            // The unused top byte of RGB24 reads as opaque, as X11's does.
            if (format == CAIRO_FORMAT_RGB24) {
                for (size_t i = 3; i < out.pixels.size(); i += 4) out.pixels[i] = 0xFF;
            }
        } else {
            error = "The screenshot is in a form this program cannot read.";
        }
        cairo_surface_destroy(surface);
        std::error_code ec;
        fs::remove(PathFromUtf8(pngPath), ec);
        return out.IsValid();
    }

    // ===== THE TRIGGER =====
    // "Ctrl+Alt+Shift+F11" in the shortcuts specification's spelling the
    // portal takes: modifiers CTRL, ALT, SHIFT, LOGO, then the key as an xkb
    // keysym name. "" with the reason when the accelerator is no shortcut.
    std::string TriggerFor(const std::string& accelerator, std::string& error) {
        std::vector<std::string> modifiers;
        std::string key;
        bool superAlone = false;
        size_t start = 0;
        while (start <= accelerator.size()) {
            size_t end = accelerator.find('+', start);
            if (end == std::string::npos) end = accelerator.size();
            std::string part = accelerator.substr(start, end - start);
            part.erase(part.begin(), std::find_if(part.begin(), part.end(), [](unsigned char c) { return !std::isspace(c); }));
            part.erase(std::find_if(part.rbegin(), part.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), part.end());
            start = end + 1;
            if (part.empty()) continue;
            std::string lower = part;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower == "ctrl" || lower == "control") modifiers.push_back("CTRL");
            else if (lower == "alt") modifiers.push_back("ALT");
            else if (lower == "shift") modifiers.push_back("SHIFT");
            else if (lower == "super" || lower == "win" || lower == "meta" || lower == "logo") modifiers.push_back("LOGO");
            else if (!key.empty()) {
                error = accelerator + " names two keys.";
                return {};
            } else if (part.size() == 1) key = std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(part[0]))));
            else if (lower == "space") key = "space";
            else if (lower == "enter" || lower == "return") key = "Return";
            else if (lower == "esc" || lower == "escape") key = "Escape";
            else if (lower == "tab") key = "Tab";
            else if (lower == "backspace") key = "BackSpace";
            else if (lower == "delete" || lower == "del") key = "Delete";
            else if (lower == "print" || lower == "printscreen") key = "Print";
            else if (lower[0] == 'f' && lower.size() <= 3 && std::all_of(lower.begin() + 1, lower.end(), ::isdigit)) key = "F" + lower.substr(1);
            else key = part;
        }
        if (key.empty()) {
            if (modifiers.size() == 1 && modifiers[0] == "LOGO") {
                superAlone = true;
            } else {
                error = accelerator.empty() ? "No shortcut was given." : "A modifier alone (" + accelerator + ") is no shortcut; the Super key alone is the one exception.";
                return {};
            }
        }
        std::string trigger;
        for (const std::string& m : modifiers) trigger += m + "+";
        if (superAlone) {
            trigger = "LOGO";
        } else {
            trigger += key;
        }
        return trigger;
    }

} // namespace

// ===== THE SCREENSHOT =====

bool CaptureScreen(const std::string& pngPath, std::string& error) {
    std::string portalFile;
    if (!PortalScreenshot(portalFile, error)) return false;
    const fs::path from = PathFromUtf8(portalFile);
    const fs::path to = PathFromUtf8(pngPath);
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) {
        // Another file system: a copy, then the original goes.
        ec.clear();
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        std::error_code ignored;
        fs::remove(from, ignored);
        if (ec) {
            error = "The screenshot could not be moved to \"" + pngPath + "\": " + ec.message();
            return false;
        }
    }
    return true;
}

bool CaptureScreenImage(DesktopScreenImage& out, std::string& error) {
    std::string portalFile;
    if (!PortalScreenshot(portalFile, error)) return false;
    return ReadAndDelete(portalFile, out, error);
}

bool CaptureScreenArea(int x, int y, int width, int height, DesktopScreenImage& out, std::string& error) {
    DesktopScreenImage whole;
    if (!CaptureScreenImage(whole, error)) return false;
    // The portal's picture starts at the screen's origin; the area is
    // clipped to it.
    const int left = std::max(x, 0);
    const int top = std::max(y, 0);
    const int right = std::min(x + width, whole.width);
    const int bottom = std::min(y + height, whole.height);
    if (right <= left || bottom <= top) {
        error = "Nothing of the area is on the screen.";
        out = DesktopScreenImage();
        return false;
    }
    out = DesktopScreenImage();
    out.x = left;
    out.y = top;
    out.width = right - left;
    out.height = bottom - top;
    out.stride = out.width * 4;
    out.pixels.resize(static_cast<size_t>(out.stride) * out.height);
    for (int row = 0; row < out.height; ++row) {
        std::memcpy(out.pixels.data() + static_cast<size_t>(row) * out.stride,
                    whole.pixels.data() + static_cast<size_t>(top + row) * whole.stride + static_cast<size_t>(left) * 4,
                    static_cast<size_t>(out.width) * 4);
    }
    return true;
}

// ===== A SHORTCUT FOR THE WHOLE DESKTOP =====

struct ShortcutState {
    GMainContext* context = nullptr;
    GDBusConnection* bus = nullptr;
    std::string accelerator;
    std::string sessionHandle;          // the Session object's path
    guint activatedSubscription = 0;
    std::unique_ptr<PortalRequest> bind;   // the BindShortcuts request, until answered
    bool bound = false;
    std::atomic<bool> wake{false};
    std::atomic<int> activations{0};
};

namespace {
    void OnActivated(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters,
                     gpointer data) {
        auto* state = static_cast<ShortcutState*>(data);
        const char* session = nullptr;
        const char* id = nullptr;
        guint64 timestamp = 0;
        GVariant* options = nullptr;
        g_variant_get(parameters, "(&o&st@a{sv})", &session, &id, &timestamp, &options);
        if (options) g_variant_unref(options);
        if (!session || state->sessionHandle != session) return;
        ++state->activations;
        g_main_context_wakeup(state->context);
    }

    void CloseSession(ShortcutState* state) {
        if (!state->bus || state->sessionHandle.empty()) return;
        GError* gerror = nullptr;
        VariantRef reply(g_dbus_connection_call_sync(state->bus, kPortalName, state->sessionHandle.c_str(),
                                                     kSessionInterface, "Close", nullptr, nullptr,
                                                     G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &gerror));
        if (!reply && gerror) g_error_free(gerror);
        state->sessionHandle.clear();
    }

    // The binding's answer: true when bound; false with the reason.
    bool BindAnswered(ShortcutState* state, std::string& error) {
        PortalRequest* bind = state->bind.get();
        if (!bind || !bind->answered) return false;
        const uint32_t response = bind->response;
        state->bind.reset();
        if (response == 0) {
            state->bound = true;
            return true;
        }
        error = response == 1 ? "The user did not allow the shortcut " + state->accelerator + "."
                              : "The compositor did not bind the shortcut " + state->accelerator + ".";
        return false;
    }
} // namespace

namespace {
    // The session: true with state->sessionHandle set.
    bool OpenSession(ShortcutState* state, const std::string& accelerator, std::string& error) {
        PortalRequest request;
        Expect(request, state->bus, state->context, "session");
        const std::string sessionToken = NewToken("keys");
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
        AddToken(options, request);
        g_variant_builder_add(&options, "{sv}", "session_handle_token", g_variant_new_string(sessionToken.c_str()));
        GError* gerror = nullptr;
        VariantRef reply(g_dbus_connection_call_sync(state->bus, kPortalName, kPortalPath, kShortcutsInterface,
                                                     "CreateSession", g_variant_new("(a{sv})", &options),
                                                     G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs,
                                                     nullptr, &gerror));
        if (!reply) {
            error = "The global shortcuts portal (xdg-desktop-portal with a backend that binds keys for this session) did not answer: " +
                    ErrorText(gerror);
            return false;
        }
        FollowHandle(request, reply.value);
        if (!Await(request, kCallTimeoutMs) || request.response != 0) {
            error = "The global shortcuts portal opened no session for " + accelerator + ".";
            return false;
        }
        state->sessionHandle = ResultString(request.results, "session_handle");
        if (state->sessionHandle.empty()) {
            // The convention's path, for a portal that names none.
            state->sessionHandle = std::string(kPortalPath) + "/session/" + SenderToken(state->bus) + "/" + sessionToken;
        }
        return true;
    }

    // The binding: true when sent (answered, or still to be answered on
    // the shortcut's thread); false with the reason.
    bool Bind(ShortcutState* state, const std::string& accelerator, const std::string& trigger, std::string& error) {
        state->bind = std::make_unique<PortalRequest>();
        Expect(*state->bind, state->bus, state->context, "bind");
        GVariantBuilder shortcutOptions;
        g_variant_builder_init(&shortcutOptions, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&shortcutOptions, "{sv}", "description", g_variant_new_string(accelerator.c_str()));
        g_variant_builder_add(&shortcutOptions, "{sv}", "preferred_trigger", g_variant_new_string(trigger.c_str()));
        GVariantBuilder shortcuts;
        g_variant_builder_init(&shortcuts, G_VARIANT_TYPE("a(sa{sv})"));
        g_variant_builder_add(&shortcuts, "(sa{sv})", "shortcut", &shortcutOptions);
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
        AddToken(options, *state->bind);
        GError* gerror = nullptr;
        VariantRef reply(g_dbus_connection_call_sync(state->bus, kPortalName, kPortalPath, kShortcutsInterface,
                                                     "BindShortcuts",
                                                     g_variant_new("(oa(sa{sv})sa{sv})", state->sessionHandle.c_str(),
                                                                   &shortcuts, "", &options),
                                                     G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs,
                                                     nullptr, &gerror));
        if (!reply) {
            error = "The global shortcuts portal did not take " + accelerator + ": " + ErrorText(gerror);
            return false;
        }
        FollowHandle(*state->bind, reply.value);
        // A portal that answers at once (one that binds without asking, or
        // refuses) is answered here; one that asks the user answers the
        // shortcut's thread (ShortcutWait).
        if (Await(*state->bind, kBindTimeoutMs) && !BindAnswered(state, error)) return false;
        return true;
    }
} // namespace

// `exclusive` asks nothing more: the compositor decides who else hears the
// shortcut, and a portal cannot take a key from it.
ShortcutState* ShortcutOpen(const std::string& accelerator, std::string& error, bool /*exclusive*/) {
    const std::string trigger = TriggerFor(accelerator, error);
    if (trigger.empty()) return nullptr;
    auto* state = new ShortcutState();
    state->accelerator = accelerator;
    state->context = g_main_context_new();
    g_main_context_push_thread_default(state->context);
    GError* gerror = nullptr;
    state->bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &gerror);
    bool ok = state->bus != nullptr;
    if (!ok) error = "The D-Bus session bus, which the portals answer on, cannot be reached: " + ErrorText(gerror);
    if (ok) ok = OpenSession(state, accelerator, error);
    if (ok) {
        // The activations, before the binding so none is missed.
        state->activatedSubscription = g_dbus_connection_signal_subscribe(state->bus, kPortalName, kShortcutsInterface,
                                                                          "Activated", kPortalPath, nullptr,
                                                                          G_DBUS_SIGNAL_FLAGS_NONE, OnActivated, state, nullptr);
        ok = Bind(state, accelerator, trigger, error);
    }
    if (!ok) {
        ShortcutClose(state);   // pops the context too
        return nullptr;
    }
    g_main_context_pop_thread_default(state->context);
    return state;
}

bool ShortcutWait(ShortcutState* state) {
    if (!state || !state->context) return false;
    for (;;) {
        if (state->wake.load()) return false;
        if (!state->bound) {
            std::string why;
            if (state->bind && state->bind->answered && !BindAnswered(state, why)) {
                debugOutput << "UltraCanvas Wayland: " << why << std::endl;
                return false;
            }
        }
        int pending = state->activations.load();
        while (pending > 0) {
            if (state->activations.compare_exchange_weak(pending, pending - 1)) return true;
        }
        g_main_context_iteration(state->context, TRUE);
    }
}

void ShortcutWake(ShortcutState* state) {
    if (!state) return;
    state->wake.store(true);
    if (state->context) g_main_context_wakeup(state->context);
}

void ShortcutClose(ShortcutState* state) {
    if (!state) return;
    if (state->bus) {
        if (state->activatedSubscription) g_dbus_connection_signal_unsubscribe(state->bus, state->activatedSubscription);
        state->bind.reset();
        CloseSession(state);
        g_object_unref(state->bus);
        state->bus = nullptr;
    }
    if (state->context) {
        // Whatever the bus delivered last; then the context goes (pushed
        // by ShortcutOpen on its thread, popped there or here).
        for (int i = 0; i < 100 && g_main_context_iteration(state->context, FALSE); ++i) {}
        if (g_main_context_get_thread_default() == state->context) g_main_context_pop_thread_default(state->context);
        g_main_context_unref(state->context);
        state->context = nullptr;
    }
    delete state;
}

} // namespace DesktopShellBackend
} // namespace UltraCanvas
