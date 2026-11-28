// OS/Linux/UltraCanvasLinuxGeolocation.cpp
// UltraCanvasGeolocation on Linux: GeoClue 2 over the system D-Bus.
//
// A request runs on a thread of its own with a GLib main context of its
// own: the manager gives a client (GetClient), the client is told who asks
// (DesktopId: this program's name, the .desktop file GeoClue's agent shows
// the person) and how exactly (RequestedAccuracyLevel 8, "exact"), Start
// begins it, and the first LocationUpdated signal names the Location object
// whose Latitude, Longitude and Accuracy are the answer. Stop and done.
// GeoClue picks its sources (Wi-Fi, a GPS, a static file, the network's
// address) and, where the desktop runs its agent, asks the person first.
// Version: 1.0.0
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework

#include "UltraCanvasGeolocation.h"

#if defined(__linux__) && !defined(__ANDROID__)

#include <gio/gio.h>

#include <climits>
#include <string>
#include <thread>
#include <unistd.h>

namespace UltraCanvas {
namespace GeolocationBackend {

namespace {

const char* const kService = "org.freedesktop.GeoClue2";
const char* const kClientInterface = "org.freedesktop.GeoClue2.Client";
const char* const kLocationInterface = "org.freedesktop.GeoClue2.Location";
constexpr guint32 kAccuracyExact = 8;

// This program's name, as its .desktop file is named: the executable's
// file name ("ChatCube")
std::string DesktopId() {
    char path[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n > 0) {
        path[n] = '\0';
        std::string name = path;
        const size_t slash = name.rfind('/');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (!name.empty()) return name;
    }
    const char* prg = g_get_prgname();
    return prg ? prg : "ultracanvas";
}

std::string Reason(GError* error, const std::string& fallback) {
    if (!error) return fallback;
    std::string why;
    if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
        g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER)) {
        why = "GeoClue, the location service, is not installed or not running.";
    } else if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED) ||
               (error->message && std::string(error->message).find("denied") != std::string::npos)) {
        why = "Finding this computer's location was not allowed (GeoClue refused it).";
    } else {
        why = fallback + " (" + (error->message ? error->message : "") + ")";
    }
    g_error_free(error);
    return why;
}

bool SetProperty(GDBusConnection* bus, const char* client, const char* name, GVariant* value, GError** error) {
    GVariant* r = g_dbus_connection_call_sync(bus, kService, client, "org.freedesktop.DBus.Properties", "Set",
                                              g_variant_new("(ssv)", kClientInterface, name, value), nullptr,
                                              G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, error);
    if (!r) return false;
    g_variant_unref(r);
    return true;
}

struct Wait {
    GMainLoop* loop = nullptr;
    std::string locationPath;
    bool timedOut = false;
};

void OnLocationUpdated(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                       GVariant* parameters, gpointer data) {
    auto* wait = static_cast<Wait*>(data);
    const gchar* oldPath = nullptr;
    const gchar* newPath = nullptr;
    g_variant_get(parameters, "(&o&o)", &oldPath, &newPath);
    if (newPath && std::string(newPath) != "/") {
        wait->locationPath = newPath;
        g_main_loop_quit(wait->loop);
    }
}

gboolean OnTimeout(gpointer data) {
    auto* wait = static_cast<Wait*>(data);
    wait->timedOut = true;
    g_main_loop_quit(wait->loop);
    return G_SOURCE_REMOVE;
}

GeoPositionResult Locate(int timeoutMs) {
    GeoPositionResult result;
    GError* error = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (!bus) {
        result.error = Reason(error, "The system bus could not be reached.");
        return result;
    }
    GVariant* reply = g_dbus_connection_call_sync(bus, kService, "/org/freedesktop/GeoClue2/Manager",
                                                  "org.freedesktop.GeoClue2.Manager", "GetClient", nullptr,
                                                  G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
    if (!reply) {
        result.error = Reason(error, "GeoClue did not give a client.");
        g_object_unref(bus);
        return result;
    }
    const gchar* clientPath = nullptr;
    g_variant_get(reply, "(&o)", &clientPath);
    const std::string client = clientPath ? clientPath : "";
    g_variant_unref(reply);

    const std::string desktopId = DesktopId();
    if (!SetProperty(bus, client.c_str(), "DesktopId", g_variant_new_string(desktopId.c_str()), &error) ||
        !SetProperty(bus, client.c_str(), "RequestedAccuracyLevel", g_variant_new_uint32(kAccuracyExact), &error)) {
        result.error = Reason(error, "GeoClue did not take the request.");
        g_object_unref(bus);
        return result;
    }

    // The signal is dispatched to this thread's own context
    GMainContext* context = g_main_context_new();
    g_main_context_push_thread_default(context);
    Wait wait;
    wait.loop = g_main_loop_new(context, FALSE);
    const guint subscription = g_dbus_connection_signal_subscribe(
        bus, kService, kClientInterface, "LocationUpdated", client.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        OnLocationUpdated, &wait, nullptr);

    reply = g_dbus_connection_call_sync(bus, kService, client.c_str(), kClientInterface, "Start", nullptr, nullptr,
                                        G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &error);
    if (!reply) {
        result.error = Reason(error, "GeoClue did not start looking.");
    } else {
        g_variant_unref(reply);
        GSource* timeout = g_timeout_source_new(static_cast<guint>(timeoutMs));
        g_source_set_callback(timeout, OnTimeout, &wait, nullptr);
        g_source_attach(timeout, context);
        g_main_loop_run(wait.loop);
        g_source_destroy(timeout);
        g_source_unref(timeout);

        if (wait.locationPath.empty()) {
            result.error = "GeoClue found no location in time.";
        } else {
            reply = g_dbus_connection_call_sync(bus, kService, wait.locationPath.c_str(),
                                                "org.freedesktop.DBus.Properties", "GetAll",
                                                g_variant_new("(s)", kLocationInterface), G_VARIANT_TYPE("(a{sv})"),
                                                G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
            if (!reply) {
                result.error = Reason(error, "GeoClue's location could not be read.");
            } else {
                GVariant* props = g_variant_get_child_value(reply, 0);
                double lat = 0, lon = 0, accuracy = -1;
                const bool hasLat = g_variant_lookup(props, "Latitude", "d", &lat);
                const bool hasLon = g_variant_lookup(props, "Longitude", "d", &lon);
                g_variant_lookup(props, "Accuracy", "d", &accuracy);
                g_variant_unref(props);
                g_variant_unref(reply);
                if (hasLat && hasLon) {
                    result.ok = true;
                    result.latitude = lat;
                    result.longitude = lon;
                    result.accuracyMeters = accuracy;
                } else {
                    result.error = "GeoClue's location had no coordinates.";
                }
            }
        }
        GVariant* stopped = g_dbus_connection_call_sync(bus, kService, client.c_str(), kClientInterface, "Stop",
                                                        nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, nullptr);
        if (stopped) g_variant_unref(stopped);
    }

    g_dbus_connection_signal_unsubscribe(bus, subscription);
    g_main_loop_unref(wait.loop);
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    g_object_unref(bus);
    return result;
}

} // namespace

bool Available() {
    return true;
}

std::string Name() {
    return "GeoClue";
}

void Request(int timeoutMs, std::function<void(const GeoPositionResult&)> answer) {
    std::thread([timeoutMs, answer = std::move(answer)]() { answer(Locate(timeoutMs)); }).detach();
}

} // namespace GeolocationBackend
} // namespace UltraCanvas

#endif
