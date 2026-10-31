// OS/Linux/UltraCanvasLinuxWireless.cpp
// The Linux backend of UltraCanvasWireless: NetworkManager for Wi-Fi and
// BlueZ for Bluetooth, both on the system bus through GDBus - the services
// every desktop's own network and Bluetooth menus talk to. polkit decides
// what this user may change; on a desktop seat an active local session may
// do everything here without a password.
//
//   - Wi-Fi: the first device of type Wi-Fi (DeviceType 2). Its access
//     points give the networks, the saved connections (Settings) which of
//     them are known. Joining a known network activates its connection;
//     joining a new one is AddAndActivateConnection with the passphrase,
//     and a connection that then fails to activate is deleted again, so a
//     mistyped passphrase is not saved.
//   - Bluetooth: the first adapter of BlueZ's object tree and the devices
//     under it. Discovery belongs to the D-Bus connection that started it,
//     so the shared system-bus connection is held for the life of the
//     process; a discovery stops by itself after a minute.
//
// No agent is registered with BlueZ: a device that needs a code typed or
// confirmed does not pair from here (BlueZ answers AuthenticationFailed),
// which the error says.
// Version: 1.0.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework

#include "UltraCanvasWirelessBackend.h"

#if defined(__linux__) && !defined(__ANDROID__)

#include <gio/gio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace UltraCanvas {
namespace WirelessBackend {

namespace {

    constexpr int kCallTimeoutMs = 5000;
    constexpr int kPairTimeoutMs = 60000;
    constexpr int kConnectTimeoutMs = 30000;
    constexpr auto kJoinDeadline = std::chrono::seconds(30);
    constexpr auto kDiscoveryLength = std::chrono::seconds(60);

    const char* kNM = "org.freedesktop.NetworkManager";
    const char* kNMPath = "/org/freedesktop/NetworkManager";
    const char* kNMSettingsPath = "/org/freedesktop/NetworkManager/Settings";
    const char* kNMDevice = "org.freedesktop.NetworkManager.Device";
    const char* kNMWireless = "org.freedesktop.NetworkManager.Device.Wireless";
    const char* kNMAccessPoint = "org.freedesktop.NetworkManager.AccessPoint";
    const char* kNMSettings = "org.freedesktop.NetworkManager.Settings";
    const char* kNMConnection = "org.freedesktop.NetworkManager.Settings.Connection";
    const char* kNMActive = "org.freedesktop.NetworkManager.Connection.Active";
    const char* kProperties = "org.freedesktop.DBus.Properties";

    const char* kBlueZ = "org.bluez";
    const char* kAdapter = "org.bluez.Adapter1";
    const char* kDevice = "org.bluez.Device1";
    const char* kBattery = "org.bluez.Battery1";

    constexpr uint32_t kDeviceTypeWifi = 2;
    constexpr uint32_t kDeviceStateActivated = 100;
    constexpr uint32_t kActiveActivated = 2;
    constexpr uint32_t kActiveDeactivated = 4;

    // NM_802_11_AP_FLAGS_PRIVACY and the NM_802_11_AP_SEC_KEY_MGMT_* bits.
    constexpr uint32_t kApPrivacy = 0x1;
    constexpr uint32_t kKeyMgmtPsk = 0x100;
    constexpr uint32_t kKeyMgmt8021x = 0x200;
    constexpr uint32_t kKeyMgmtSae = 0x400;
    constexpr uint32_t kKeyMgmtOwe = 0x800;

    // ===== THE BUS =====

    // The shared system-bus connection, held for the process's life: a
    // BlueZ discovery ends when the connection that started it closes.
    GDBusConnection* SystemBus(std::string& error) {
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
    std::string Message(GError* gerror) {
        if (!gerror) return "no answer";
        g_dbus_error_strip_remote_error(gerror);
        std::string text = gerror->message ? gerror->message : "no answer";
        g_error_free(gerror);
        return text;
    }

    // One call; the reply (to unref) or nullptr with `error` set.
    // `parameters` is consumed (floating) either way.
    GVariant* Call(const char* service, const std::string& path, const char* iface, const char* method,
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

    bool CallVoid(const char* service, const std::string& path, const char* iface, const char* method,
                  GVariant* parameters, std::string& error, int timeoutMs = kCallTimeoutMs) {
        GVariant* reply = Call(service, path, iface, method, parameters, nullptr, error, timeoutMs);
        if (!reply) return false;
        g_variant_unref(reply);
        return true;
    }

    // A property's value (to unref), or nullptr.
    GVariant* GetProperty(const char* service, const std::string& path, const char* iface, const char* name) {
        std::string ignored;
        GVariant* reply = Call(service, path, kProperties, "Get", g_variant_new("(ss)", iface, name),
                               G_VARIANT_TYPE("(v)"), ignored);
        if (!reply) return nullptr;
        GVariant* value = nullptr;
        g_variant_get(reply, "(v)", &value);
        g_variant_unref(reply);
        return value;
    }

    bool SetProperty(const char* service, const std::string& path, const char* iface, const char* name,
                     GVariant* value, std::string& error) {
        return CallVoid(service, path, kProperties, "Set", g_variant_new("(ssv)", iface, name, value), error);
    }

    bool GetBool(const char* service, const std::string& path, const char* iface, const char* name,
                 bool fallback = false) {
        GVariant* value = GetProperty(service, path, iface, name);
        if (!value) return fallback;
        const bool result = g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN) ? g_variant_get_boolean(value)
                                                                                  : fallback;
        g_variant_unref(value);
        return result;
    }

    uint32_t GetUint(const char* service, const std::string& path, const char* iface, const char* name,
                     uint32_t fallback = 0) {
        GVariant* value = GetProperty(service, path, iface, name);
        if (!value) return fallback;
        uint32_t result = fallback;
        if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)) result = g_variant_get_uint32(value);
        g_variant_unref(value);
        return result;
    }

    std::string GetString(const char* service, const std::string& path, const char* iface, const char* name) {
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

    std::vector<std::string> PathList(GVariant* reply) {
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

    std::string BytesToString(GVariant* bytes) {
        if (!bytes || !g_variant_is_of_type(bytes, G_VARIANT_TYPE_BYTESTRING)) return "";
        gsize length = 0;
        const auto* data = static_cast<const char*>(g_variant_get_fixed_array(bytes, &length, 1));
        return data ? std::string(data, length) : std::string();
    }

    GVariant* StringToBytes(const std::string& text) {
        return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, text.data(), text.size(), 1);
    }

    // ===== NETWORKMANAGER =====

    struct WifiDevice {
        std::string path;
        std::string name;
    };

    bool FindWifiDevice(WifiDevice& device, std::string& error) {
        GVariant* reply = Call(kNM, kNMPath, kNM, "GetDevices", nullptr, G_VARIANT_TYPE("(ao)"), error);
        if (!reply) {
            error = "NetworkManager does not answer (" + error + ").";
            return false;
        }
        for (const std::string& path : PathList(reply)) {
            if (GetUint(kNM, path, kNMDevice, "DeviceType") != kDeviceTypeWifi) continue;
            device.path = path;
            device.name = GetString(kNM, path, kNMDevice, "Interface");
            return true;
        }
        error = "There is no Wi-Fi adapter.";
        return false;
    }

    struct AccessPoint {
        std::string path;
        std::string ssid;
        int strength = 0;
        uint32_t flags = 0, wpaFlags = 0, rsnFlags = 0;
    };

    AccessPoint ReadAccessPoint(const std::string& path) {
        AccessPoint ap;
        ap.path = path;
        std::string ignored;
        GVariant* reply = Call(kNM, path, kProperties, "GetAll", g_variant_new("(s)", kNMAccessPoint),
                               G_VARIANT_TYPE("(a{sv})"), ignored);
        if (!reply) return ap;
        GVariant* dict = g_variant_get_child_value(reply, 0);
        GVariant* value = nullptr;
        if ((value = g_variant_lookup_value(dict, "Ssid", G_VARIANT_TYPE_BYTESTRING))) {
            ap.ssid = BytesToString(value);
            g_variant_unref(value);
        }
        if ((value = g_variant_lookup_value(dict, "Strength", G_VARIANT_TYPE_BYTE))) {
            ap.strength = g_variant_get_byte(value);
            g_variant_unref(value);
        }
        if ((value = g_variant_lookup_value(dict, "Flags", G_VARIANT_TYPE_UINT32))) {
            ap.flags = g_variant_get_uint32(value);
            g_variant_unref(value);
        }
        if ((value = g_variant_lookup_value(dict, "WpaFlags", G_VARIANT_TYPE_UINT32))) {
            ap.wpaFlags = g_variant_get_uint32(value);
            g_variant_unref(value);
        }
        if ((value = g_variant_lookup_value(dict, "RsnFlags", G_VARIANT_TYPE_UINT32))) {
            ap.rsnFlags = g_variant_get_uint32(value);
            g_variant_unref(value);
        }
        g_variant_unref(dict);
        g_variant_unref(reply);
        return ap;
    }

    std::string SecurityOf(const AccessPoint& ap) {
        const uint32_t all = ap.wpaFlags | ap.rsnFlags;
        if (all & kKeyMgmt8021x) return "Enterprise";
        if (ap.rsnFlags & kKeyMgmtSae) return "WPA3";
        if (ap.rsnFlags & kKeyMgmtPsk) return "WPA2";
        if (ap.wpaFlags & kKeyMgmtPsk) return "WPA";
        if (all & kKeyMgmtOwe) return "Open";      // Enhanced Open: encrypted, no password
        if (ap.flags & kApPrivacy) return "WEP";
        return "Open";
    }

    std::vector<AccessPoint> ReadAccessPoints(const WifiDevice& device) {
        std::vector<AccessPoint> points;
        std::string ignored;
        GVariant* reply = Call(kNM, device.path, kNMWireless, "GetAllAccessPoints", nullptr,
                               G_VARIANT_TYPE("(ao)"), ignored);
        for (const std::string& path : PathList(reply)) points.push_back(ReadAccessPoint(path));
        return points;
    }

    // The saved Wi-Fi connections: path -> SSID.
    std::map<std::string, std::string> ReadSavedWifi() {
        std::map<std::string, std::string> saved;
        std::string ignored;
        GVariant* reply = Call(kNM, kNMSettingsPath, kNMSettings, "ListConnections", nullptr,
                               G_VARIANT_TYPE("(ao)"), ignored);
        for (const std::string& path : PathList(reply)) {
            GVariant* settings = Call(kNM, path, kNMConnection, "GetSettings", nullptr,
                                      G_VARIANT_TYPE("(a{sa{sv}})"), ignored);
            if (!settings) continue;
            GVariant* dict = g_variant_get_child_value(settings, 0);
            GVariant* wireless = g_variant_lookup_value(dict, "802-11-wireless", G_VARIANT_TYPE("a{sv}"));
            if (wireless) {
                GVariant* ssid = g_variant_lookup_value(wireless, "ssid", G_VARIANT_TYPE_BYTESTRING);
                if (ssid) {
                    saved[path] = BytesToString(ssid);
                    g_variant_unref(ssid);
                }
                g_variant_unref(wireless);
            }
            g_variant_unref(dict);
            g_variant_unref(settings);
        }
        return saved;
    }

    std::string ConnectedSsid(const WifiDevice& device) {
        if (GetUint(kNM, device.path, kNMDevice, "State") != kDeviceStateActivated) return "";
        const std::string ap = GetString(kNM, device.path, kNMWireless, "ActiveAccessPoint");
        if (ap.empty() || ap == "/") return "";
        return ReadAccessPoint(ap).ssid;
    }

    // Wait for an active connection to come up; false when it went down
    // instead or the deadline passed.
    bool WaitForActivation(const std::string& active) {
        const auto deadline = std::chrono::steady_clock::now() + kJoinDeadline;
        while (std::chrono::steady_clock::now() < deadline) {
            GVariant* value = GetProperty(kNM, active, kNMActive, "State");
            if (!value) return false;                       // the object is gone: it failed
            const uint32_t state = g_variant_get_uint32(value);
            g_variant_unref(value);
            if (state == kActiveActivated) return true;
            if (state == kActiveDeactivated) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        return false;
    }

    // ===== BLUEZ =====

    struct BlueZTree {
        std::string adapterPath;
        std::map<std::string, std::string> adapterStrings;
        bool powered = false;
        bool discovering = false;
        std::vector<WirelessBluetoothDevice> devices;
        std::map<std::string, std::string> devicePaths;   // address -> object path
    };

    std::string KindOfIcon(const std::string& icon) {
        static const std::map<std::string, std::string> kinds = {
            {"audio-headphones", "Headphones"}, {"audio-headset", "Headset"},
            {"audio-card", "Speaker"},           {"audio-speakers", "Speaker"},
            {"input-keyboard", "Keyboard"},      {"input-mouse", "Mouse"},
            {"input-tablet", "Tablet"},          {"input-gaming", "Game controller"},
            {"phone", "Phone"},                  {"computer", "Computer"},
            {"camera-video", "Camera"},          {"camera-photo", "Camera"},
            {"printer", "Printer"},              {"multimedia-player", "Media player"},
            {"network-wireless", "Network"},     {"scanner", "Scanner"},
        };
        auto found = kinds.find(icon);
        return found == kinds.end() ? "" : found->second;
    }

    bool ReadTree(BlueZTree& tree, std::string& error) {
        GVariant* reply = Call(kBlueZ, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", nullptr,
                               G_VARIANT_TYPE("(a{oa{sa{sv}}})"), error);
        if (!reply) {
            error = "BlueZ does not answer (" + error + ").";
            return false;
        }
        GVariant* objects = g_variant_get_child_value(reply, 0);
        GVariantIter iter;
        g_variant_iter_init(&iter, objects);
        const gchar* path = nullptr;
        GVariant* interfaces = nullptr;
        struct Found {
            std::string path;
            std::string adapter;
            WirelessBluetoothDevice device;
        };
        std::vector<Found> found;
        std::vector<std::string> adapters;
        while (g_variant_iter_loop(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
            GVariant* adapter = g_variant_lookup_value(interfaces, kAdapter, G_VARIANT_TYPE("a{sv}"));
            if (adapter) {
                adapters.emplace_back(path);
                if (tree.adapterPath.empty() || std::string(path) < tree.adapterPath) {
                    tree.adapterPath = path;
                    gboolean flag = FALSE;
                    tree.powered = g_variant_lookup(adapter, "Powered", "b", &flag) && flag;
                    flag = FALSE;
                    tree.discovering = g_variant_lookup(adapter, "Discovering", "b", &flag) && flag;
                    const gchar* text = nullptr;
                    tree.adapterStrings.clear();
                    if (g_variant_lookup(adapter, "Alias", "&s", &text)) tree.adapterStrings["Alias"] = text;
                    if (g_variant_lookup(adapter, "Name", "&s", &text)) tree.adapterStrings["Name"] = text;
                }
                g_variant_unref(adapter);
            }
            GVariant* device = g_variant_lookup_value(interfaces, kDevice, G_VARIANT_TYPE("a{sv}"));
            if (device) {
                Found entry;
                entry.path = path;
                const gchar* text = nullptr;
                if (g_variant_lookup(device, "Address", "&s", &text)) entry.device.address = text;
                if (g_variant_lookup(device, "Alias", "&s", &text)) entry.device.name = text;
                else if (g_variant_lookup(device, "Name", "&s", &text)) entry.device.name = text;
                if (g_variant_lookup(device, "Icon", "&s", &text)) entry.device.kind = KindOfIcon(text);
                if (g_variant_lookup(device, "Adapter", "&o", &text)) entry.adapter = text;
                gboolean flag = FALSE;
                entry.device.paired = g_variant_lookup(device, "Paired", "b", &flag) && flag;
                flag = FALSE;
                entry.device.connected = g_variant_lookup(device, "Connected", "b", &flag) && flag;
                flag = FALSE;
                entry.device.trusted = g_variant_lookup(device, "Trusted", "b", &flag) && flag;
                gint16 rssi = 0;
                if (g_variant_lookup(device, "RSSI", "n", &rssi)) entry.device.rssiDbm = rssi;
                g_variant_unref(device);
                GVariant* battery = g_variant_lookup_value(interfaces, kBattery, G_VARIANT_TYPE("a{sv}"));
                if (battery) {
                    guchar percent = 0;
                    if (g_variant_lookup(battery, "Percentage", "y", &percent)) entry.device.batteryPercent = percent;
                    g_variant_unref(battery);
                }
                found.push_back(std::move(entry));
            }
        }
        g_variant_unref(objects);
        g_variant_unref(reply);
        for (Found& entry : found) {
            if (!entry.adapter.empty() && entry.adapter != tree.adapterPath) continue;
            tree.devicePaths[entry.device.address] = entry.path;
            tree.devices.push_back(std::move(entry.device));
        }
        if (tree.adapterPath.empty()) {
            error = "There is no Bluetooth adapter.";
            return false;
        }
        return true;
    }

    // The device's object path, by address; "" with `error` set.
    std::string DevicePath(const std::string& address, BlueZTree& tree, std::string& error) {
        if (!ReadTree(tree, error)) return "";
        for (const auto& [known, path] : tree.devicePaths) {
            if (g_ascii_strcasecmp(known.c_str(), address.c_str()) == 0) return path;
        }
        error = "The device " + address + " is not known to Bluetooth; look for devices first.";
        return "";
    }

    std::atomic<int> discoveryGeneration{0};

} // namespace

std::string BackendName() { return "networkmanager+bluez"; }

// ===== WI-FI =====

WirelessWifiStatus ReadWifi() {
    WirelessWifiStatus status;
    WifiDevice device;
    if (!FindWifiDevice(device, status.error)) {
        // No adapter is an answer; no NetworkManager is not.
        status.available = status.error == "There is no Wi-Fi adapter.";
        return status;
    }
    status.available = true;
    status.adapterPresent = true;
    status.adapterName = device.name;
    status.enabled = GetBool(kNM, kNMPath, kNM, "WirelessEnabled");
    status.hardwareBlocked = !GetBool(kNM, kNMPath, kNM, "WirelessHardwareEnabled", true);
    if (!status.enabled || status.hardwareBlocked) return status;

    status.connectedSsid = ConnectedSsid(device);
    std::set<std::string> known;
    for (const auto& [path, ssid] : ReadSavedWifi()) known.insert(ssid);
    for (const AccessPoint& ap : ReadAccessPoints(device)) {
        if (ap.ssid.empty()) continue;
        WirelessNetwork network;
        network.ssid = ap.ssid;
        network.signalPercent = ap.strength;
        network.security = SecurityOf(ap);
        network.secured = network.security != "Open";
        network.known = known.count(ap.ssid) > 0;
        network.connected = !status.connectedSsid.empty() && ap.ssid == status.connectedSsid;
        status.networks.push_back(network);
    }
    return status;
}

bool RequestWifiScan(std::string& error) {
    WifiDevice device;
    if (!FindWifiDevice(device, error)) return false;
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
    if (CallVoid(kNM, device.path, kNMWireless, "RequestScan", g_variant_new("(a{sv})", &options), error)) {
        return true;
    }
    // NetworkManager refuses a scan while one runs or one ran a moment ago;
    // the list it has is then as new as a scan would make it.
    if (error.find("not allowed") != std::string::npos || error.find("Scanning") != std::string::npos) {
        error.clear();
        return true;
    }
    return false;
}

bool SetWifiEnabled(bool enabled, std::string& error) {
    if (!SetProperty(kNM, kNMPath, kNM, "WirelessEnabled", g_variant_new_boolean(enabled), error)) {
        error = std::string("Wi-Fi could not be switched ") + (enabled ? "on" : "off") + ": " + error;
        return false;
    }
    return true;
}

bool ConnectWifi(const std::string& ssid, const std::string& passphrase, std::string& error) {
    WifiDevice device;
    if (!FindWifiDevice(device, error)) return false;

    // A saved connection: activate it (the passphrase, if one was given,
    // is ignored - forget the network first to change it).
    for (const auto& [path, saved] : ReadSavedWifi()) {
        if (saved != ssid || !passphrase.empty()) continue;
        GVariant* reply = Call(kNM, kNMPath, kNM, "ActivateConnection",
                               g_variant_new("(ooo)", path.c_str(), device.path.c_str(), "/"),
                               G_VARIANT_TYPE("(o)"), error);
        if (!reply) {
            error = "Could not join " + ssid + ": " + error;
            return false;
        }
        const gchar* active = nullptr;
        g_variant_get(reply, "(&o)", &active);
        const std::string activePath = active;
        g_variant_unref(reply);
        if (WaitForActivation(activePath)) return true;
        error = "Could not join " + ssid + ". If its password changed, forget the network and join it again.";
        return false;
    }

    // A new network: the strongest access point that carries the name.
    AccessPoint best;
    for (const AccessPoint& ap : ReadAccessPoints(device)) {
        if (ap.ssid == ssid && (best.path.empty() || ap.strength > best.strength)) best = ap;
    }
    if (best.path.empty()) {
        error = ssid + " is not in reach. Scan again, or move closer to it.";
        return false;
    }
    const std::string security = SecurityOf(best);
    if (security == "Enterprise") {
        error = ssid + " asks for a user name and certificate (WPA Enterprise), which this panel cannot set up yet.";
        return false;
    }
    if (security != "Open" && passphrase.empty()) {
        error = ssid + " needs its password.";
        return false;
    }

    GVariantBuilder settings;
    g_variant_builder_init(&settings, G_VARIANT_TYPE("a{sa{sv}}"));
    GVariantBuilder wireless;
    g_variant_builder_init(&wireless, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&wireless, "{sv}", "ssid", StringToBytes(ssid));
    g_variant_builder_add(&settings, "{sa{sv}}", "802-11-wireless", &wireless);
    if (security != "Open") {
        GVariantBuilder secrets;
        g_variant_builder_init(&secrets, G_VARIANT_TYPE("a{sv}"));
        if (security == "WEP") {
            g_variant_builder_add(&secrets, "{sv}", "key-mgmt", g_variant_new_string("none"));
            g_variant_builder_add(&secrets, "{sv}", "wep-key0", g_variant_new_string(passphrase.c_str()));
            g_variant_builder_add(&secrets, "{sv}", "wep-key-type", g_variant_new_uint32(1));
        } else {
            g_variant_builder_add(&secrets, "{sv}", "key-mgmt",
                                  g_variant_new_string(security == "WPA3" && !(best.rsnFlags & kKeyMgmtPsk)
                                                           ? "sae" : "wpa-psk"));
            g_variant_builder_add(&secrets, "{sv}", "psk", g_variant_new_string(passphrase.c_str()));
        }
        g_variant_builder_add(&settings, "{sa{sv}}", "802-11-wireless-security", &secrets);
    }
    GVariant* reply = Call(kNM, kNMPath, kNM, "AddAndActivateConnection",
                           g_variant_new("(a{sa{sv}}oo)", &settings, device.path.c_str(), best.path.c_str()),
                           G_VARIANT_TYPE("(oo)"), error);
    if (!reply) {
        error = "Could not join " + ssid + ": " + error;
        return false;
    }
    const gchar* connection = nullptr;
    const gchar* active = nullptr;
    g_variant_get(reply, "(&o&o)", &connection, &active);
    const std::string connectionPath = connection;
    const std::string activePath = active;
    g_variant_unref(reply);
    if (WaitForActivation(activePath)) return true;

    std::string ignored;
    CallVoid(kNM, connectionPath, kNMConnection, "Delete", nullptr, ignored);
    error = security == "Open" ? "Could not join " + ssid + "."
                               : "Could not join " + ssid + " - is the password right?";
    return false;
}

bool DisconnectWifi(std::string& error) {
    WifiDevice device;
    if (!FindWifiDevice(device, error)) return false;
    if (!CallVoid(kNM, device.path, kNMDevice, "Disconnect", nullptr, error)) {
        error = "Could not leave the network: " + error;
        return false;
    }
    return true;
}

bool ForgetWifi(const std::string& ssid, std::string& error) {
    bool any = false;
    for (const auto& [path, saved] : ReadSavedWifi()) {
        if (saved != ssid) continue;
        if (!CallVoid(kNM, path, kNMConnection, "Delete", nullptr, error)) {
            error = "Could not forget " + ssid + ": " + error;
            return false;
        }
        any = true;
    }
    if (!any) error = ssid + " is not saved.";
    return any;
}

// ===== BLUETOOTH =====

WirelessBluetoothStatus ReadBluetooth() {
    WirelessBluetoothStatus status;
    BlueZTree tree;
    if (!ReadTree(tree, status.error)) {
        status.available = status.error == "There is no Bluetooth adapter.";
        return status;
    }
    status.available = true;
    status.adapterPresent = true;
    status.powered = tree.powered;
    status.discovering = tree.discovering;
    status.adapterName = !tree.adapterStrings["Alias"].empty() ? tree.adapterStrings["Alias"]
                                                               : tree.adapterStrings["Name"];
    status.devices = std::move(tree.devices);
    return status;
}

bool CanSetBluetoothPower() { return true; }

bool SetBluetoothPowered(bool powered, std::string& error) {
    BlueZTree tree;
    if (!ReadTree(tree, error)) return false;
    if (!SetProperty(kBlueZ, tree.adapterPath, kAdapter, "Powered", g_variant_new_boolean(powered), error)) {
        if (error.find("Blocked") != std::string::npos || error.find("rfkill") != std::string::npos) {
            error = "Bluetooth is switched off by the computer's wireless switch or key (rfkill).";
        } else {
            error = std::string("Bluetooth could not be switched ") + (powered ? "on" : "off") + ": " + error;
        }
        return false;
    }
    return true;
}

bool StartBluetoothDiscovery(std::string& error) {
    BlueZTree tree;
    if (!ReadTree(tree, error)) return false;
    if (!tree.powered) {
        error = "Switch Bluetooth on first.";
        return false;
    }
    if (!CallVoid(kBlueZ, tree.adapterPath, kAdapter, "StartDiscovery", nullptr, error) &&
        error.find("InProgress") == std::string::npos && error.find("in progress") == std::string::npos) {
        error = "Could not look for devices: " + error;
        return false;
    }
    error.clear();
    // Stop by itself after a minute, unless a newer discovery started.
    const int generation = ++discoveryGeneration;
    const std::string adapter = tree.adapterPath;
    std::thread([generation, adapter]() {
        std::this_thread::sleep_for(kDiscoveryLength);
        if (discoveryGeneration.load() != generation) return;
        std::string ignored;
        CallVoid(kBlueZ, adapter, kAdapter, "StopDiscovery", nullptr, ignored);
    }).detach();
    return true;
}

bool StopBluetoothDiscovery(std::string& error) {
    ++discoveryGeneration;
    BlueZTree tree;
    if (!ReadTree(tree, error)) return false;
    if (!tree.discovering) return true;
    if (!CallVoid(kBlueZ, tree.adapterPath, kAdapter, "StopDiscovery", nullptr, error)) {
        // Another program's discovery: not ours to stop, and not an error.
        if (error.find("NotAuthorized") != std::string::npos || error.find("Not authorized") != std::string::npos) {
            error.clear();
            return true;
        }
        return false;
    }
    return true;
}

bool ConnectBluetoothDevice(const std::string& address, std::string& error) {
    BlueZTree tree;
    const std::string path = DevicePath(address, tree, error);
    if (path.empty()) return false;
    if (!CallVoid(kBlueZ, path, kDevice, "Connect", nullptr, error, kConnectTimeoutMs)) {
        error = "Could not connect: " + error + ". Is the device switched on and in reach?";
        return false;
    }
    return true;
}

bool PairBluetoothDevice(const std::string& address, std::string& error) {
    BlueZTree tree;
    const std::string path = DevicePath(address, tree, error);
    if (path.empty()) return false;
    // Pairing works better without a discovery running beside it.
    std::string ignored;
    if (tree.discovering) CallVoid(kBlueZ, tree.adapterPath, kAdapter, "StopDiscovery", nullptr, ignored);
    if (!CallVoid(kBlueZ, path, kDevice, "Pair", nullptr, error, kPairTimeoutMs) &&
        error.find("AlreadyExists") == std::string::npos && error.find("Already Exists") == std::string::npos) {
        if (error.find("Authentication") != std::string::npos) {
            error = "The device wants a code confirmed or typed, which this panel cannot show yet. "
                    "Pair it with bluetoothctl, then connect it here.";
        } else {
            error = "Could not pair: " + error + ". Is the device in pairing mode?";
        }
        return false;
    }
    error.clear();
    SetProperty(kBlueZ, path, kDevice, "Trusted", g_variant_new_boolean(TRUE), ignored);
    if (!CallVoid(kBlueZ, path, kDevice, "Connect", nullptr, error, kConnectTimeoutMs)) {
        error = "Paired, but could not connect: " + error;
        return false;
    }
    return true;
}

bool DisconnectBluetoothDevice(const std::string& address, std::string& error) {
    BlueZTree tree;
    const std::string path = DevicePath(address, tree, error);
    if (path.empty()) return false;
    if (!CallVoid(kBlueZ, path, kDevice, "Disconnect", nullptr, error, kConnectTimeoutMs)) {
        error = "Could not disconnect: " + error;
        return false;
    }
    return true;
}

bool RemoveBluetoothDevice(const std::string& address, std::string& error) {
    BlueZTree tree;
    const std::string path = DevicePath(address, tree, error);
    if (path.empty()) return false;
    if (!CallVoid(kBlueZ, tree.adapterPath, kAdapter, "RemoveDevice", g_variant_new("(o)", path.c_str()), error)) {
        error = "Could not remove the device: " + error;
        return false;
    }
    return true;
}

// ===== THE SYSTEM'S OWN SETTINGS =====

bool HasSystemSettings(WirelessRadio) { return false; }

bool OpenSystemSettings(WirelessRadio, std::string& error) {
    error = "On Linux the desktop's own panels are the settings.";
    return false;
}

} // namespace WirelessBackend
} // namespace UltraCanvas

#endif // __linux__ && !__ANDROID__
