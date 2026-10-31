// OS/Linux/UltraCanvasLinuxSystemBus.h
// INTERNAL: the system-bus helpers the Linux backends share - the wireless
// one (NetworkManager, BlueZ) and the removable drives (UDisks2): one
// shared GDBus connection held for the process's life, and synchronous
// calls with a deadline whose errors come back as sentences. Not a public
// header.
// Version: 1.0.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework
#pragma once

#include <gio/gio.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace LinuxSystemBus {

    constexpr int kCallTimeoutMs = 5000;
    inline constexpr const char* kProperties = "org.freedesktop.DBus.Properties";

    // The shared system-bus connection, held for the process's life: a
    // BlueZ discovery ends when the connection that started it closes.
    inline GDBusConnection* SystemBus(std::string& error) {
        static std::mutex mutex;
        static GDBusConnection* bus = nullptr;
        std::lock_guard<std::mutex> lock(mutex);
        if (bus && !g_dbus_connection_is_closed(bus)) return bus;
        if (bus) {
            g_object_unref(bus);
            bus = nullptr;
        }
        GError* gerror = nullptr;
        bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &gerror);
        if (!bus) {
            error = std::string("The system bus is not reachable: ") +
                    (gerror && gerror->message ? gerror->message : "no answer");
            if (gerror) g_error_free(gerror);
        }
        return bus;
    }

    // GDBus's message without its "GDBus.Error:org.bluez.Error.Failed: "
    // prefix, which means nothing to a person.
    inline std::string Message(GError* gerror) {
        if (!gerror) return "no answer";
        g_dbus_error_strip_remote_error(gerror);
        std::string text = gerror->message ? gerror->message : "no answer";
        g_error_free(gerror);
        return text;
    }

    // One call; the reply (to unref) or nullptr with `error` set.
    // `parameters` is consumed (floating) either way.
    inline GVariant* Call(const char* service, const std::string& path, const char* iface, const char* method,
                   GVariant* parameters, const GVariantType* replyType, std::string& error,
                   int timeoutMs = kCallTimeoutMs) {
        GDBusConnection* bus = SystemBus(error);
        if (!bus) {
            if (parameters) g_variant_unref(g_variant_ref_sink(parameters));
            return nullptr;
        }
        GError* gerror = nullptr;
        GVariant* reply = g_dbus_connection_call_sync(bus, service, path.c_str(), iface, method, parameters,
                                                      replyType, G_DBUS_CALL_FLAGS_NONE, timeoutMs, nullptr,
                                                      &gerror);
        if (!reply) error = Message(gerror);
        return reply;
    }

    inline bool CallVoid(const char* service, const std::string& path, const char* iface, const char* method,
                  GVariant* parameters, std::string& error, int timeoutMs = kCallTimeoutMs) {
        GVariant* reply = Call(service, path, iface, method, parameters, nullptr, error, timeoutMs);
        if (!reply) return false;
        g_variant_unref(reply);
        return true;
    }

    // A property's value (to unref), or nullptr.
    inline GVariant* GetProperty(const char* service, const std::string& path, const char* iface, const char* name) {
        std::string ignored;
        GVariant* reply = Call(service, path, kProperties, "Get", g_variant_new("(ss)", iface, name),
                               G_VARIANT_TYPE("(v)"), ignored);
        if (!reply) return nullptr;
        GVariant* value = nullptr;
        g_variant_get(reply, "(v)", &value);
        g_variant_unref(reply);
        return value;
    }

    inline bool SetProperty(const char* service, const std::string& path, const char* iface, const char* name,
                     GVariant* value, std::string& error) {
        return CallVoid(service, path, kProperties, "Set", g_variant_new("(ssv)", iface, name, value), error);
    }

    inline bool GetBool(const char* service, const std::string& path, const char* iface, const char* name,
                 bool fallback = false) {
        GVariant* value = GetProperty(service, path, iface, name);
        if (!value) return fallback;
        const bool result = g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN) ? g_variant_get_boolean(value)
                                                                                  : fallback;
        g_variant_unref(value);
        return result;
    }

    inline uint32_t GetUint(const char* service, const std::string& path, const char* iface, const char* name,
                     uint32_t fallback = 0) {
        GVariant* value = GetProperty(service, path, iface, name);
        if (!value) return fallback;
        uint32_t result = fallback;
        if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)) result = g_variant_get_uint32(value);
        g_variant_unref(value);
        return result;
    }

    inline std::string GetString(const char* service, const std::string& path, const char* iface, const char* name) {
        GVariant* value = GetProperty(service, path, iface, name);
        if (!value) return "";
        std::string result;
        if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING) ||
            g_variant_is_of_type(value, G_VARIANT_TYPE_OBJECT_PATH)) {
            result = g_variant_get_string(value, nullptr);
        }
        g_variant_unref(value);
        return result;
    }

    inline std::vector<std::string> PathList(GVariant* reply) {
        std::vector<std::string> paths;
        if (!reply) return paths;
        GVariantIter* iter = nullptr;
        g_variant_get(reply, "(ao)", &iter);
        const gchar* path = nullptr;
        while (g_variant_iter_next(iter, "&o", &path)) paths.emplace_back(path);
        g_variant_iter_free(iter);
        g_variant_unref(reply);
        return paths;
    }

    inline std::string BytesToString(GVariant* bytes) {
        if (!bytes || !g_variant_is_of_type(bytes, G_VARIANT_TYPE_BYTESTRING)) return "";
        gsize length = 0;
        const auto* data = static_cast<const char*>(g_variant_get_fixed_array(bytes, &length, 1));
        return data ? std::string(data, length) : std::string();
    }

    inline GVariant* StringToBytes(const std::string& text) {
        return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, text.data(), text.size(), 1);
    }


} // namespace LinuxSystemBus
} // namespace UltraCanvas
