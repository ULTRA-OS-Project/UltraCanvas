// include/UltraCanvasWirelessBackend.h
// Internal contract between UltraCanvasWireless and its per-platform
// backend. Applications include UltraCanvasWireless.h instead; this header
// exists so OS/<Platform>/UltraCanvas*Wireless.cpp and the shared core agree
// on one set of entry points. The core validates the arguments, sorts what
// the backend lists and fills `error` when a call fails without a reason.
// Version: 1.1.0 - AskPairing / HasPairingHandler
// Version: 1.0.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASWIRELESSBACKEND_H
#define ULTRACANVASWIRELESSBACKEND_H

#include "UltraCanvasWireless.h"

// A platform has a native backend when one of the OS/<Platform> files is
// compiled into the build. Everything else links the fallback in
// core/UltraCanvasWireless.cpp, which answers unavailable and says so.
#if defined(__APPLE__)
    #include <TargetConditionals.h>
#endif
#if (defined(__linux__) && !defined(__ANDROID__)) || defined(_WIN32) || \
    (defined(__APPLE__) && TARGET_OS_OSX)
    #define ULTRACANVAS_WIRELESS_NATIVE 1
#endif

namespace UltraCanvas {
namespace WirelessBackend {

    std::string BackendName();

    WirelessWifiStatus ReadWifi();
    bool RequestWifiScan(std::string& error);
    bool SetWifiEnabled(bool enabled, std::string& error);
    bool ConnectWifi(const std::string& ssid, const std::string& passphrase, std::string& error);
    bool DisconnectWifi(std::string& error);
    bool ForgetWifi(const std::string& ssid, std::string& error);

    WirelessBluetoothStatus ReadBluetooth();
    bool CanSetBluetoothPower();
    bool SetBluetoothPowered(bool powered, std::string& error);
    bool StartBluetoothDiscovery(std::string& error);
    bool StopBluetoothDiscovery(std::string& error);
    bool PairBluetoothDevice(const std::string& address, std::string& error);
    bool ConnectBluetoothDevice(const std::string& address, std::string& error);
    bool DisconnectBluetoothDevice(const std::string& address, std::string& error);
    bool RemoveBluetoothDevice(const std::string& address, std::string& error);

    // The pairing handler, in the core: what SetPairingHandler stored. A
    // backend asks it when a device wants a code; with none set the answer
    // is a refusal. HasPairingHandler says whether there is someone to ask.
    WirelessPairingAnswer AskPairing(const WirelessPairingRequest& request);
    bool HasPairingHandler();

    bool HasSystemSettings(WirelessRadio radio);
    bool OpenSystemSettings(WirelessRadio radio, std::string& error);

} // namespace WirelessBackend
} // namespace UltraCanvas

#endif // ULTRACANVASWIRELESSBACKEND_H
