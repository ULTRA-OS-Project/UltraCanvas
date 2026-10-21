// OS/Linux/UltraCanvasLinuxSessionLock.cpp
// The session-lock backend of UltraCanvasSessionLock.h for Linux and the BSDs.
// Two sources, either of which is enough:
//   - logind (system bus): the Lock / Unlock signals of this process's
//     session object - what `loginctl lock-session` and the desktop's own
//     lock send - and its LockedHint property, which the screen locker sets
//     while it is showing.
//   - the screensaver (session bus): ActiveChanged(b) of
//     org.freedesktop.ScreenSaver (KDE, Xfce, most others) and
//     org.gnome.ScreenSaver (GNOME), sent when the screensaver or the lock
//     screen comes up and when it goes again.
// GDBus runs on a private GMainContext whose wake-up fd the application's
// loop watches - the arrangement of the AT-SPI bridge - so no second loop or
// thread is involved and every report arrives on the UI thread.
// Version: 1.0.0
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework
#include "UltraCanvasSessionLock.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasDebug.h"

#include <gio/gio.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace UltraCanvas {

    namespace {
        struct Watcher {
            GMainContext* context = nullptr;
            FdWatchId contextWatch = 0;
            bool watching = false;
            GDBusConnection* system = nullptr;
            GDBusConnection* session = nullptr;
            std::vector<guint> systemSubscriptions;
            std::vector<guint> sessionSubscriptions;
        };

        Watcher* gWatcher = nullptr;

        // GDBus attaches its callbacks to the thread-default context at the
        // time a subscription or call is made. This pins ours for a scope.
        class ContextScope {
        public:
            explicit ContextScope(GMainContext* context) : ctx(context) { g_main_context_push_thread_default(ctx); }
            ~ContextScope() { g_main_context_pop_thread_default(ctx); }
        private:
            GMainContext* ctx;
        };

        void PumpContext() {
            if (!gWatcher) return;
            // The first iteration also acknowledges the wake-up that brought us here.
            while (g_main_context_iteration(gWatcher->context, FALSE)) {}
        }

        // Reported from a fresh turn of the loop rather than from inside the
        // GDBus dispatch: a listener that removes itself stops the backend,
        // and the context must not go while it is dispatching.
        void Report(bool locked) {
            if (auto* app = UltraCanvasApplication::GetInstance()) {
                app->PostToUIThread([locked]() { NotifySessionLockChanged(locked); });
            }
        }

        void OnLogindLock(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* signal,
                          GVariant*, gpointer) {
            Report(g_strcmp0(signal, "Lock") == 0);
        }

        void OnLogindProperties(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                                GVariant* parameters, gpointer) {
            if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(sa{sv}as)"))) return;
            GVariant* changed = g_variant_get_child_value(parameters, 1);
            gboolean locked = FALSE;
            if (g_variant_lookup(changed, "LockedHint", "b", &locked)) Report(locked != FALSE);
            g_variant_unref(changed);
        }

        void OnScreenSaverActive(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                                 GVariant* parameters, gpointer) {
            if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(b)"))) return;
            gboolean active = FALSE;
            g_variant_get(parameters, "(b)", &active);
            Report(active != FALSE);
        }

        // The object path of this process's logind session: the caller's own
        // ("auto" falls back to the user's graphical session for a program
        // started outside one, from a user service), else the one this PID
        // is in, for a logind too old to know "auto".
        std::string LogindSessionPath(GDBusConnection* system) {
            GVariant* reply = g_dbus_connection_call_sync(
                system, "org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                "GetSession", g_variant_new("(s)", "auto"), G_VARIANT_TYPE("(o)"),
                G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
            if (!reply) {
                reply = g_dbus_connection_call_sync(
                    system, "org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                    "GetSessionByPID", g_variant_new("(u)", static_cast<guint32>(getpid())), G_VARIANT_TYPE("(o)"),
                    G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
            }
            if (!reply) return {};
            const gchar* path = nullptr;
            g_variant_get(reply, "(&o)", &path);
            std::string result = path ? path : "";
            g_variant_unref(reply);
            return result;
        }

        void WatchLogind(Watcher& w) {
            GError* error = nullptr;
            w.system = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
            if (!w.system) {
                debugOutput << "UltraCanvas: session lock: no system bus: " << (error ? error->message : "?") << std::endl;
                if (error) g_error_free(error);
                return;
            }
            const std::string path = LogindSessionPath(w.system);
            if (path.empty()) {
                debugOutput << "UltraCanvas: session lock: no logind session for this process" << std::endl;
                return;
            }
            for (const char* signal : {"Lock", "Unlock"}) {
                w.systemSubscriptions.push_back(g_dbus_connection_signal_subscribe(
                    w.system, "org.freedesktop.login1", "org.freedesktop.login1.Session", signal, path.c_str(),
                    nullptr, G_DBUS_SIGNAL_FLAGS_NONE, OnLogindLock, nullptr, nullptr));
            }
            w.systemSubscriptions.push_back(g_dbus_connection_signal_subscribe(
                w.system, "org.freedesktop.login1", "org.freedesktop.DBus.Properties", "PropertiesChanged",
                path.c_str(), "org.freedesktop.login1.Session", G_DBUS_SIGNAL_FLAGS_NONE, OnLogindProperties,
                nullptr, nullptr));
        }

        bool HaveSessionBus() {
            const char* address = std::getenv("DBUS_SESSION_BUS_ADDRESS");
            if (address && *address) return true;
            // systemd's per-user bus, which GIO finds without the variable.
            const char* runtime = std::getenv("XDG_RUNTIME_DIR");
            return runtime && *runtime && access((std::string(runtime) + "/bus").c_str(), F_OK) == 0;
        }

        void WatchScreenSaver(Watcher& w) {
            // Without a session bus GIO would try to autolaunch one.
            if (!HaveSessionBus()) return;
            GError* error = nullptr;
            w.session = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
            if (!w.session) {
                debugOutput << "UltraCanvas: session lock: no session bus: " << (error ? error->message : "?") << std::endl;
                if (error) g_error_free(error);
                return;
            }
            // Any sender and any path: the screensaver is whichever program
            // owns the name, and each desktop emits on a path of its own
            // (/org/gnome/ScreenSaver, /ScreenSaver, /org/freedesktop/ScreenSaver).
            // A forged signal can do no more than make a listener forget.
            for (const char* iface : {"org.freedesktop.ScreenSaver", "org.gnome.ScreenSaver"}) {
                w.sessionSubscriptions.push_back(g_dbus_connection_signal_subscribe(
                    w.session, nullptr, iface, "ActiveChanged", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
                    OnScreenSaverActive, nullptr, nullptr));
            }
        }
    }

    namespace SessionLockBackend {

        bool Start() {
            if (gWatcher) return true;
            auto* app = UltraCanvasApplication::GetInstance();
            if (!app) return false;

            gWatcher = new Watcher();
            gWatcher->context = g_main_context_new();
            // Owning the context makes GDBus wake it (its wake-up fd becomes
            // readable) whenever it queues a signal for us from its worker thread.
            g_main_context_acquire(gWatcher->context);
            gint priority = 0, timeout = 0;
            GPollFD fds[4];
            g_main_context_prepare(gWatcher->context, &priority);
            const gint count = g_main_context_query(gWatcher->context, priority, &timeout, fds, 4);
            g_main_context_check(gWatcher->context, priority, fds, std::min<gint>(count, 4));
            if (count != 1) {
                // A fresh context polls exactly its wake-up fd; anything else
                // is a GLib this code does not know.
                debugOutput << "UltraCanvas: session lock: unexpected GMainContext layout" << std::endl;
                Stop();
                return false;
            }
            gWatcher->contextWatch = app->AddFdWatch(fds[0].fd, FdWatchType::Read, PumpContext);
            gWatcher->watching = true;

            {
                ContextScope scope(gWatcher->context);
                WatchLogind(*gWatcher);
                WatchScreenSaver(*gWatcher);
            }
            if (gWatcher->systemSubscriptions.empty() && gWatcher->sessionSubscriptions.empty()) {
                Stop();
                return false;
            }
            return true;
        }

        void Stop() {
            if (!gWatcher) return;
            if (gWatcher->system) {
                for (guint id : gWatcher->systemSubscriptions) g_dbus_connection_signal_unsubscribe(gWatcher->system, id);
                g_object_unref(gWatcher->system);
            }
            if (gWatcher->session) {
                for (guint id : gWatcher->sessionSubscriptions) g_dbus_connection_signal_unsubscribe(gWatcher->session, id);
                g_object_unref(gWatcher->session);
            }
            if (gWatcher->watching) {
                if (auto* app = UltraCanvasApplication::GetInstance()) app->RemoveFdWatch(gWatcher->contextWatch);
            }
            if (gWatcher->context) {
                // Run what GDBus queued for the context before it goes.
                while (g_main_context_iteration(gWatcher->context, FALSE)) {}
                g_main_context_release(gWatcher->context);
                g_main_context_unref(gWatcher->context);
            }
            delete gWatcher;
            gWatcher = nullptr;
        }

    } // namespace SessionLockBackend

} // namespace UltraCanvas
