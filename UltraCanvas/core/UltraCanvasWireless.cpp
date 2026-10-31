// core/UltraCanvasWireless.cpp
// The shared half of UltraCanvasWireless: argument checks, the order the
// lists come in, and the fallback backend for platforms with none. The
// platform halves are OS/Linux/UltraCanvasLinuxWireless.cpp and
// OS/MSWindows/UltraCanvasWindowsWireless.cpp.
// Version: 1.0.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework

#include "UltraCanvasWirelessBackend.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace UltraCanvas {

namespace {

    std::string Lower(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    // "aa:bb:cc:dd:ee:ff" or "AA-BB-..." -> "AA:BB:CC:DD:EE:FF"; "" when it
    // is not a Bluetooth address.
    std::string NormalizeAddress(const std::string& address) {
        std::string out;
        int digits = 0;
        for (char c : address) {
            if (std::isxdigit(static_cast<unsigned char>(c))) {
                if (digits > 0 && digits % 2 == 0) out += ':';
                out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                ++digits;
            } else if (c != ':' && c != '-') {
                return "";
            }
        }
        return digits == 12 ? out : "";
    }

    bool Fail(std::string* error, const std::string& reason) {
        if (error) *error = reason;
        return false;
    }

    // One row per name: the strongest signal, known / connected if any of
    // the network's access points is.
    void ArrangeNetworks(WirelessWifiStatus& status) {
        std::map<std::string, WirelessNetwork> byName;
        for (const WirelessNetwork& network : status.networks) {
            if (network.ssid.empty()) continue;
            auto found = byName.find(network.ssid);
            if (found == byName.end()) {
                byName.emplace(network.ssid, network);
                continue;
            }
            WirelessNetwork& kept = found->second;
            if (network.signalPercent > kept.signalPercent) {
                kept.signalPercent = network.signalPercent;
                if (!network.security.empty()) kept.security = network.security;
            }
            kept.known = kept.known || network.known;
            kept.connected = kept.connected || network.connected;
            kept.secured = kept.secured || network.secured;
        }
        status.networks.clear();
        for (auto& [name, network] : byName) {
            if (!status.connectedSsid.empty() && name == status.connectedSsid) network.connected = true;
            network.signalPercent = std::clamp(network.signalPercent, 0, 100);
            if (network.security.empty()) network.security = network.secured ? "WPA2" : "Open";
            status.networks.push_back(network);
        }
        std::stable_sort(status.networks.begin(), status.networks.end(),
                         [](const WirelessNetwork& a, const WirelessNetwork& b) {
                             if (a.connected != b.connected) return a.connected;
                             if (a.signalPercent != b.signalPercent) return a.signalPercent > b.signalPercent;
                             return Lower(a.ssid) < Lower(b.ssid);
                         });
    }

    void ArrangeDevices(WirelessBluetoothStatus& status) {
        for (WirelessBluetoothDevice& device : status.devices) {
            device.address = NormalizeAddress(device.address);
            if (device.name.empty()) device.name = device.address;
        }
        status.devices.erase(std::remove_if(status.devices.begin(), status.devices.end(),
                                            [](const WirelessBluetoothDevice& d) { return d.address.empty(); }),
                             status.devices.end());
        std::stable_sort(status.devices.begin(), status.devices.end(),
                         [](const WirelessBluetoothDevice& a, const WirelessBluetoothDevice& b) {
                             if (a.paired != b.paired) return a.paired;
                             if (a.connected != b.connected) return a.connected;
                             return Lower(a.name) < Lower(b.name);
                         });
    }

} // namespace

std::string UltraCanvasWireless::GetBackendName() {
    return WirelessBackend::BackendName();
}

// ===== WI-FI =====

WirelessWifiStatus UltraCanvasWireless::ReadWifi() {
    WirelessWifiStatus status = WirelessBackend::ReadWifi();
    ArrangeNetworks(status);
    return status;
}

bool UltraCanvasWireless::RequestWifiScan(std::string* error) {
    std::string reason;
    if (WirelessBackend::RequestWifiScan(reason)) return true;
    return Fail(error, reason.empty() ? "The Wi-Fi adapter could not scan." : reason);
}

bool UltraCanvasWireless::SetWifiEnabled(bool enabled, std::string* error) {
    std::string reason;
    if (WirelessBackend::SetWifiEnabled(enabled, reason)) return true;
    return Fail(error, reason.empty() ? std::string("Wi-Fi could not be switched ") + (enabled ? "on." : "off.")
                                      : reason);
}

bool UltraCanvasWireless::ConnectWifi(const std::string& ssid, const std::string& passphrase, std::string* error) {
    if (ssid.empty()) return Fail(error, "No network was named.");
    if (ssid.size() > 32) return Fail(error, "A network name is at most 32 bytes long.");
    if (!passphrase.empty() && (passphrase.size() < 8 || passphrase.size() > 63)) {
        // WEP keys are 5 or 13 characters; the backends say whether they
        // take them.
        if (passphrase.size() != 5 && passphrase.size() != 13) {
            return Fail(error, "A Wi-Fi password has 8 to 63 characters.");
        }
    }
    std::string reason;
    if (WirelessBackend::ConnectWifi(ssid, passphrase, reason)) return true;
    return Fail(error, reason.empty() ? "Could not join " + ssid + "." : reason);
}

bool UltraCanvasWireless::DisconnectWifi(std::string* error) {
    std::string reason;
    if (WirelessBackend::DisconnectWifi(reason)) return true;
    return Fail(error, reason.empty() ? "Could not leave the network." : reason);
}

bool UltraCanvasWireless::ForgetWifi(const std::string& ssid, std::string* error) {
    if (ssid.empty()) return Fail(error, "No network was named.");
    std::string reason;
    if (WirelessBackend::ForgetWifi(ssid, reason)) return true;
    return Fail(error, reason.empty() ? "Could not forget " + ssid + "." : reason);
}

// ===== BLUETOOTH =====

WirelessBluetoothStatus UltraCanvasWireless::ReadBluetooth() {
    WirelessBluetoothStatus status = WirelessBackend::ReadBluetooth();
    ArrangeDevices(status);
    return status;
}

bool UltraCanvasWireless::CanSetBluetoothPower() {
    return WirelessBackend::CanSetBluetoothPower();
}

bool UltraCanvasWireless::SetBluetoothPowered(bool powered, std::string* error) {
    std::string reason;
    if (WirelessBackend::SetBluetoothPowered(powered, reason)) return true;
    return Fail(error, reason.empty() ? std::string("Bluetooth could not be switched ") + (powered ? "on." : "off.")
                                      : reason);
}

bool UltraCanvasWireless::StartBluetoothDiscovery(std::string* error) {
    std::string reason;
    if (WirelessBackend::StartBluetoothDiscovery(reason)) return true;
    return Fail(error, reason.empty() ? "Could not look for Bluetooth devices." : reason);
}

bool UltraCanvasWireless::StopBluetoothDiscovery(std::string* error) {
    std::string reason;
    if (WirelessBackend::StopBluetoothDiscovery(reason)) return true;
    return Fail(error, reason.empty() ? "Could not stop looking for Bluetooth devices." : reason);
}

namespace {
    template <typename Call>
    bool DeviceCall(const std::string& address, std::string* error, const char* failure, Call call) {
        const std::string normalized = NormalizeAddress(address);
        if (normalized.empty()) return Fail(error, "\"" + address + "\" is not a Bluetooth address.");
        std::string reason;
        if (call(normalized, reason)) return true;
        return Fail(error, reason.empty() ? std::string(failure) : reason);
    }
} // namespace

bool UltraCanvasWireless::PairBluetoothDevice(const std::string& address, std::string* error) {
    return DeviceCall(address, error, "The device could not be paired.", WirelessBackend::PairBluetoothDevice);
}

bool UltraCanvasWireless::ConnectBluetoothDevice(const std::string& address, std::string* error) {
    return DeviceCall(address, error, "The device could not be connected.", WirelessBackend::ConnectBluetoothDevice);
}

bool UltraCanvasWireless::DisconnectBluetoothDevice(const std::string& address, std::string* error) {
    return DeviceCall(address, error, "The device could not be disconnected.",
                      WirelessBackend::DisconnectBluetoothDevice);
}

bool UltraCanvasWireless::RemoveBluetoothDevice(const std::string& address, std::string* error) {
    return DeviceCall(address, error, "The device could not be removed.", WirelessBackend::RemoveBluetoothDevice);
}

// ===== THE SYSTEM'S OWN SETTINGS =====

bool UltraCanvasWireless::HasSystemSettings(WirelessRadio radio) {
    return WirelessBackend::HasSystemSettings(radio);
}

bool UltraCanvasWireless::OpenSystemSettings(WirelessRadio radio, std::string* error) {
    std::string reason;
    if (WirelessBackend::OpenSystemSettings(radio, reason)) return true;
    return Fail(error, reason.empty() ? "There are no system settings to open for this." : reason);
}

// ===== FALLBACK BACKEND =====

#ifndef ULTRACANVAS_WIRELESS_NATIVE
namespace WirelessBackend {

    namespace {
        const char* kNone = "Wi-Fi and Bluetooth are not operated on this platform yet.";
    }

    std::string BackendName() { return "null"; }

    WirelessWifiStatus ReadWifi() {
        WirelessWifiStatus status;
        status.error = kNone;
        return status;
    }
    bool RequestWifiScan(std::string& error) { error = kNone; return false; }
    bool SetWifiEnabled(bool, std::string& error) { error = kNone; return false; }
    bool ConnectWifi(const std::string&, const std::string&, std::string& error) { error = kNone; return false; }
    bool DisconnectWifi(std::string& error) { error = kNone; return false; }
    bool ForgetWifi(const std::string&, std::string& error) { error = kNone; return false; }

    WirelessBluetoothStatus ReadBluetooth() {
        WirelessBluetoothStatus status;
        status.error = kNone;
        return status;
    }
    bool CanSetBluetoothPower() { return false; }
    bool SetBluetoothPowered(bool, std::string& error) { error = kNone; return false; }
    bool StartBluetoothDiscovery(std::string& error) { error = kNone; return false; }
    bool StopBluetoothDiscovery(std::string& error) { error = kNone; return false; }
    bool PairBluetoothDevice(const std::string&, std::string& error) { error = kNone; return false; }
    bool ConnectBluetoothDevice(const std::string&, std::string& error) { error = kNone; return false; }
    bool DisconnectBluetoothDevice(const std::string&, std::string& error) { error = kNone; return false; }
    bool RemoveBluetoothDevice(const std::string&, std::string& error) { error = kNone; return false; }

    bool HasSystemSettings(WirelessRadio) { return false; }
    bool OpenSystemSettings(WirelessRadio, std::string& error) { error = kNone; return false; }

} // namespace WirelessBackend
#endif // !ULTRACANVAS_WIRELESS_NATIVE

} // namespace UltraCanvas
