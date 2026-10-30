// OS/Linux/UltraCanvasLinuxDesktopShellSession.cpp
// The end of the session and the lock, for the X11 backend of
// UltraCanvasDesktopShell: through logind on the system bus, which is what
// every desktop's own "Shut down" and "Lock" talk to on a systemd system.
//
//   - Restart and shut down are the Manager's Reboot(true) and
//     PowerOff(true): logind asks polkit whether this user may, which on a
//     desktop seat an active local session may without a password.
//   - Signing out terminates this process's own session
//     (Session.Terminate): the display manager then shows the sign-in again,
//     exactly as its own "Log out" would. The session is found as the
//     session-lock backend finds it: GetSession("auto") for the caller's own,
//     GetSessionByPID for a logind too old to know "auto".
//   - The lock is Session.Lock on that session: logind sends the Lock signal
//     the screen locker waits for (and UltraCanvasSessionLock reports). A
//     session without logind falls back to org.freedesktop.ScreenSaver.Lock
//     on the session bus, which every screensaver implements.
//
// Every call is synchronous with a short timeout: they are the answer to a
// click, and a missing logind answers at once. Nothing here opens a display.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasDesktopShellBackend.h"

#include <gio/gio.h>

#include <cstdlib>
#include <string>
#include <unistd.h>

namespace UltraCanvas {
namespace DesktopShellBackend {

namespace {

    constexpr int kCallTimeoutMs = 3000;

    // A session bus without the variable is systemd's per-user one; GIO
    // would autolaunch one otherwise, which a shutdown must not wait for.
    bool HaveSessionBus() {
        const char* address = std::getenv("DBUS_SESSION_BUS_ADDRESS");
        if (address && *address) return true;
        const char* runtime = std::getenv("XDG_RUNTIME_DIR");
        return runtime && *runtime && access((std::string(runtime) + "/bus").c_str(), F_OK) == 0;
    }

    // One call, its reply dropped; false with GDBus's words in `error`.
    bool Call(GDBusConnection* bus, const char* name, const char* path, const char* iface, const char* method,
              GVariant* parameters, std::string& error) {
        GError* gerror = nullptr;
        GVariant* reply = g_dbus_connection_call_sync(bus, name, path, iface, method, parameters, nullptr,
                                                      G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs, nullptr, &gerror);
        if (!reply) {
            error = gerror && gerror->message ? gerror->message : "no answer";
            if (gerror) g_error_free(gerror);
            return false;
        }
        g_variant_unref(reply);
        return true;
    }

    GDBusConnection* SystemBus(std::string& error) {
        GError* gerror = nullptr;
        GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &gerror);
        if (!bus) {
            error = std::string("The system bus is not reachable: ") +
                    (gerror && gerror->message ? gerror->message : "no answer");
            if (gerror) g_error_free(gerror);
        }
        return bus;
    }

    // The object path of this process's logind session; "" with the reason.
    std::string OwnSessionPath(GDBusConnection* system, std::string& error) {
        GVariant* reply = g_dbus_connection_call_sync(
            system, "org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
            "GetSession", g_variant_new("(s)", "auto"), G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE,
            kCallTimeoutMs, nullptr, nullptr);
        if (!reply) {
            GError* gerror = nullptr;
            reply = g_dbus_connection_call_sync(
                system, "org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                "GetSessionByPID", g_variant_new("(u)", static_cast<guint32>(getpid())), G_VARIANT_TYPE("(o)"),
                G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs, nullptr, &gerror);
            if (!reply) {
                error = std::string("logind knows no session for this program: ") +
                        (gerror && gerror->message ? gerror->message : "no answer");
                if (gerror) g_error_free(gerror);
                return {};
            }
        }
        const gchar* path = nullptr;
        g_variant_get(reply, "(&o)", &path);
        std::string result = path ? path : "";
        g_variant_unref(reply);
        if (result.empty()) error = "logind knows no session for this program.";
        return result;
    }

    bool LogindReachable() {
        std::string error;
        GDBusConnection* system = SystemBus(error);
        if (!system) return false;
        GVariant* reply = g_dbus_connection_call_sync(
            system, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner",
            g_variant_new("(s)", "org.freedesktop.login1"), G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE,
            kCallTimeoutMs, nullptr, nullptr);
        bool owned = false;
        if (reply) {
            gboolean value = FALSE;
            g_variant_get(reply, "(b)", &value);
            owned = value != FALSE;
            g_variant_unref(reply);
        }
        g_object_unref(system);
        return owned;
    }

} // namespace

// ===== THE END OF THE SESSION =====

bool CanEndSession() {
    // Decided once: logind is there for the session's whole life or not at all.
    static const bool reachable = LogindReachable();
    return reachable;
}

bool EndSession(SessionEnd how, std::string& error) {
    GDBusConnection* system = SystemBus(error);
    if (!system) return false;
    bool ok = false;
    if (how == SessionEnd::SignOut) {
        const std::string session = OwnSessionPath(system, error);
        if (!session.empty()) {
            ok = Call(system, "org.freedesktop.login1", session.c_str(), "org.freedesktop.login1.Session",
                      "Terminate", nullptr, error);
            if (!ok) error = "logind did not end the session: " + error;
        }
    } else {
        // `true`: ask polkit as the desktop's own menu does, so an active
        // local session needs no password and a remote one is refused.
        const char* method = how == SessionEnd::Restart ? "Reboot" : "PowerOff";
        ok = Call(system, "org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                  method, g_variant_new("(b)", TRUE), error);
        if (!ok) {
            error = std::string(how == SessionEnd::Restart ? "logind did not restart the computer: "
                                                           : "logind did not shut the computer down: ") + error;
        }
    }
    g_object_unref(system);
    return ok;
}

// ===== THE LOCK =====

bool CanLockSession() {
    // A screensaver on the session bus locks too, so a session bus is enough.
    return CanEndSession() || HaveSessionBus();
}

bool LockSession(std::string& error) {
    std::string logindError;
    if (GDBusConnection* system = SystemBus(logindError)) {
        const std::string session = OwnSessionPath(system, logindError);
        bool ok = false;
        if (!session.empty()) {
            ok = Call(system, "org.freedesktop.login1", session.c_str(), "org.freedesktop.login1.Session", "Lock",
                      nullptr, logindError);
        }
        g_object_unref(system);
        if (ok) return true;
    }
    // No logind, or no session of ours in it: the screensaver's own call.
    if (!HaveSessionBus()) {
        error = "No screen locker is reachable: " + logindError;
        return false;
    }
    GError* gerror = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &gerror);
    if (!bus) {
        error = std::string("The session bus is not reachable: ") +
                (gerror && gerror->message ? gerror->message : "no answer");
        if (gerror) g_error_free(gerror);
        return false;
    }
    std::string saverError;
    const bool ok = Call(bus, "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
                         "org.freedesktop.ScreenSaver", "Lock", nullptr, saverError) ||
                    Call(bus, "org.freedesktop.ScreenSaver", "/ScreenSaver", "org.freedesktop.ScreenSaver", "Lock",
                         nullptr, saverError);
    g_object_unref(bus);
    if (!ok) error = "No screen locker answered (logind: " + logindError + "; screensaver: " + saverError + ").";
    return ok;
}

} // namespace DesktopShellBackend
} // namespace UltraCanvas
