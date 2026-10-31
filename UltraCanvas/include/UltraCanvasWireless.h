// include/UltraCanvasWireless.h
// Wi-Fi and Bluetooth as a person operates them from a desktop's panel: the
// networks in reach and the one joined, joining with a passphrase, leaving
// and forgetting; the Bluetooth devices paired and in reach, pairing,
// connecting, disconnecting and removing; and the radios on and off.
//
// doc-check: std::string error;
// doc-check: example
//   WirelessWifiStatus wifi = UltraCanvasWireless::ReadWifi();
//   for (const WirelessNetwork& network : wifi.networks) {
//       if (!network.connected && network.known) {
//           UltraCanvasWireless::ConnectWifi(network.ssid, "", &error);   // a saved network: no passphrase
//       }
//   }
//   WirelessBluetoothStatus bluetooth = UltraCanvasWireless::ReadBluetooth();
//   for (const WirelessBluetoothDevice& device : bluetooth.devices) {
//       if (device.paired && !device.connected) UltraCanvasWireless::ConnectBluetoothDevice(device.address, &error);
//   }
//
// Distinct from UltraCanvasHardwareInfo, which describes the adapters (and
// masks their addresses), and from UltraCanvasDesktopShell, which reports
// what is happening on the desktop: this module changes the radios' state.
//
// Every call is synchronous and some take seconds - joining a network waits
// until the system has joined it or given up (up to 30 s), pairing waits for
// the device - so an application calls them from a worker thread, never
// from the UI thread. The reads are quick (tens of milliseconds) but still
// talk to a system service.
//
// Backends:
//   - Linux: NetworkManager and BlueZ on the system bus (GDBus). What a
//     local, active session may do is what polkit lets it, which on a
//     desktop seat is everything here. Without NetworkManager (or BlueZ)
//     the status says so in `error`.
//   - Windows: the Native Wifi API (wlanapi) and the Bluetooth API
//     (bthprops). Windows switches the Bluetooth radio only in its Settings
//     (CanSetBluetoothPower() is false there; OpenSystemSettings opens the
//     page); connecting and disconnecting a paired device switches its
//     installed services (BluetoothSetServiceState), which is how Windows
//     itself connects headphones.
//   - Elsewhere every read answers `available` false and every call fails
//     saying so.
// Version: 1.0.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASWIRELESS_H
#define ULTRACANVASWIRELESS_H

#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {

    // ===== WI-FI =====
    struct WirelessNetwork {
        std::string ssid;               // as broadcast, UTF-8; hidden networks are left out
        int  signalPercent = 0;         // 0..100, the strongest access point of the network
        bool secured = false;
        std::string security;           // "WPA3", "WPA2", "WPA", "WEP", "Enterprise", "Open"
        bool known = false;             // a saved profile: joins without a passphrase
        bool connected = false;
    };

    struct WirelessWifiStatus {
        bool available = false;         // the backend answered
        bool adapterPresent = false;
        bool enabled = false;           // the radio is on
        bool hardwareBlocked = false;   // a switch or key on the computer has it off
        std::string adapterName;        // "wlan0", "Wi-Fi"
        std::string connectedSsid;      // "" when no network is joined
        // One row per network name, the joined one first, then by signal.
        std::vector<WirelessNetwork> networks;
        std::string error;              // why `available` is false, or what the last read missed
    };

    // ===== BLUETOOTH =====
    struct WirelessBluetoothDevice {
        std::string address;            // "AA:BB:CC:DD:EE:FF", unmasked: the handle the calls take
        std::string name;               // the device's alias or name; the address when it has none
        // What it is, for a person: "Headphones", "Headset", "Speaker",
        // "Keyboard", "Mouse", "Game controller", "Phone", "Computer",
        // "Watch", "" when the device does not say.
        std::string kind;
        bool paired = false;
        bool connected = false;
        bool trusted = false;
        std::optional<int> batteryPercent;
        std::optional<int> rssiDbm;     // set while a discovery sees the device
    };

    struct WirelessBluetoothStatus {
        bool available = false;         // the backend answered
        bool adapterPresent = false;
        bool powered = false;
        bool discovering = false;       // a StartBluetoothDiscovery is running
        std::string adapterName;
        // Paired devices first (connected before the others), then the ones
        // a discovery found, each group by name.
        std::vector<WirelessBluetoothDevice> devices;
        std::string error;
    };

    enum class WirelessRadio { WiFi, Bluetooth };

    class UltraCanvasWireless {
    public:
        // "networkmanager+bluez" on Linux, "win32" on Windows, "null" elsewhere.
        static std::string GetBackendName();

        // ----- Wi-Fi -----
        // The adapter, the network joined and the networks of the last scan.
        static WirelessWifiStatus ReadWifi();
        // Ask the adapter to scan again; the networks it finds show in
        // ReadWifi a few seconds later. False with the reason in `error`.
        static bool RequestWifiScan(std::string* error = nullptr);
        static bool SetWifiEnabled(bool enabled, std::string* error = nullptr);
        // Join `ssid`. A known network (WirelessNetwork::known) or an open
        // one needs no passphrase; a secured one the system has no profile
        // for does, and the profile is saved only once the network has been
        // joined, so a mistyped passphrase leaves nothing behind. Waits until
        // the network is joined or the system has given up.
        static bool ConnectWifi(const std::string& ssid, const std::string& passphrase = "",
                                std::string* error = nullptr);
        // Leave the network joined; the adapter stays on and does not rejoin
        // it by itself until asked.
        static bool DisconnectWifi(std::string* error = nullptr);
        // Delete every saved profile for `ssid` (leaving it first if joined).
        static bool ForgetWifi(const std::string& ssid, std::string* error = nullptr);

        // ----- Bluetooth -----
        static WirelessBluetoothStatus ReadBluetooth();
        // False where the system keeps that switch to itself (Windows).
        static bool CanSetBluetoothPower();
        static bool SetBluetoothPowered(bool powered, std::string* error = nullptr);
        // Look for devices in pairing mode until StopBluetoothDiscovery or,
        // at the latest, about a minute; ReadBluetooth lists what it finds.
        static bool StartBluetoothDiscovery(std::string* error = nullptr);
        static bool StopBluetoothDiscovery(std::string* error = nullptr);
        // Pair with a device a discovery found, trust it and connect it.
        // A device that shows or asks for a code is paired through the
        // system's own prompt where it has one (Windows); on Linux only
        // devices that need no code pair (headphones, speakers, most mice).
        static bool PairBluetoothDevice(const std::string& address, std::string* error = nullptr);
        static bool ConnectBluetoothDevice(const std::string& address, std::string* error = nullptr);
        static bool DisconnectBluetoothDevice(const std::string& address, std::string* error = nullptr);
        // Unpair: the device is forgotten and must be paired again.
        static bool RemoveBluetoothDevice(const std::string& address, std::string* error = nullptr);

        // Open the system's own settings page for the radio (Windows:
        // ms-settings:network-wifi / ms-settings:bluetooth). False where
        // there is none to open.
        static bool HasSystemSettings(WirelessRadio radio);
        static bool OpenSystemSettings(WirelessRadio radio, std::string* error = nullptr);
    };

} // namespace UltraCanvas

#endif // ULTRACANVASWIRELESS_H
