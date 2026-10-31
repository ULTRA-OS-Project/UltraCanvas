# UltraCanvasWireless

<!-- doc-check: std::string error; std::string password; -->

Wi-Fi and Bluetooth as a person operates them from a desktop's panel: the
networks in reach and the one joined, joining with a password, leaving and
forgetting; the Bluetooth devices paired and in reach, pairing, connecting,
disconnecting and removing; and the radios on and off.

```cpp
#include "UltraCanvasWireless.h"

WirelessWifiStatus wifi = UltraCanvasWireless::ReadWifi();
for (const WirelessNetwork& network : wifi.networks) {
    if (network.connected) continue;
    if (network.known || !network.secured) {
        UltraCanvasWireless::ConnectWifi(network.ssid, "", &error);   // saved or open: no password
    } else {
        UltraCanvasWireless::ConnectWifi(network.ssid, password, &error);
    }
}

WirelessBluetoothStatus bluetooth = UltraCanvasWireless::ReadBluetooth();
for (const WirelessBluetoothDevice& device : bluetooth.devices) {
    if (device.paired && !device.connected) UltraCanvasWireless::ConnectBluetoothDevice(device.address, &error);
}
```

It is what the ULTRA OS desktop's Wi-Fi and Bluetooth panels
([UltraDesktop](../UltraDesktop/README.md)) are built on. Distinct from
[UltraCanvasHardwareInfo](UltraCanvasHardwareInfo.md), which describes the
adapters (and masks their addresses), and from
[UltraCanvasDesktopShell](UltraCanvasDesktopShell.md), which reports what is
happening on the desktop: this module changes the radios' state.

Headers: `UltraCanvasWireless.h` (the data and the API);
`UltraCanvasWirelessBackend.h` is the internal contract with the platform
backends.

## Threads

Every call is synchronous, and the changes take seconds: joining a network
waits until the system has joined it or given up (up to 30 s), pairing waits
for the device (up to a minute). Call them from a worker thread and hand the
answer back with `UltraCanvasApplicationBase::PostToUIThread`. The reads take
tens of milliseconds, but they too talk to a system service, so a panel that
polls reads off the UI thread as well.

```cpp
std::thread([]() {
    std::string reason;
    const bool joined = UltraCanvasWireless::ConnectWifi("HomeNet", "", &reason);
    UltraCanvasApplicationBase::GetCurrent()->PostToUIThread([joined, reason]() {
        // show `reason` when !joined
    });
}).detach();
```

Every call that changes something answers `false` with a sentence for the
person in `error` ("Could not join HomeNet - is the password right?").

## Wi-Fi

| Call | What it does |
|---|---|
| `ReadWifi()` | `WirelessWifiStatus`: `available` (the backend answered), `adapterPresent`, `enabled`, `hardwareBlocked` (a switch or key on the computer), `adapterName`, `connectedSsid`, and `networks` - one `WirelessNetwork` per name (`ssid`, `signalPercent`, `secured`, `security`, `known`, `connected`), the joined one first, then by signal. Hidden networks are left out. |
| `RequestWifiScan(&error)` | Ask the adapter to scan; the networks show in `ReadWifi` a few seconds later. |
| `SetWifiEnabled(on, &error)` | The radio on or off. |
| `ConnectWifi(ssid, password, &error)` | Join. A `known` network or an open one needs no password. For a new secured network the profile is saved only once the network has been joined, so a mistyped password leaves nothing behind. WPA2/WPA3 personal, WPA and WEP; WPA Enterprise is refused with a reason. |
| `DisconnectWifi(&error)` | Leave the network; the adapter stays on. |
| `ForgetWifi(ssid, &error)` | Delete the saved profiles for the network (leaving it first if joined). |

`security` is one of `"WPA3"`, `"WPA2"`, `"WPA"`, `"WEP"`, `"Enterprise"`,
`"Open"` (Enhanced Open counts as open: it asks for no password).

## Bluetooth

| Call | What it does |
|---|---|
| `ReadBluetooth()` | `WirelessBluetoothStatus`: `available`, `adapterPresent`, `powered`, `discovering`, `adapterName`, and `devices` - `WirelessBluetoothDevice` (`address`, `name`, `kind`, `paired`, `connected`, `trusted`, `batteryPercent`, `rssiDbm`), paired first (connected before the others), then the ones a discovery found. |
| `CanSetBluetoothPower()` / `SetBluetoothPowered(on, &error)` | The radio on or off, where the system lets a program switch it. |
| `StartBluetoothDiscovery(&error)` / `StopBluetoothDiscovery(&error)` | Look for devices in pairing mode; it stops by itself after about a minute. |
| `PairBluetoothDevice(address, &error)` | Pair with a found device, trust it and connect it. |
| `ConnectBluetoothDevice` / `DisconnectBluetoothDevice(address, &error)` | A paired device's link. |
| `RemoveBluetoothDevice(address, &error)` | Unpair: the device must be paired again. |

`address` is the unmasked `"AA:BB:CC:DD:EE:FF"`; the calls accept it in any
case and with `-` for `:`. `kind` is for a person: `"Headphones"`,
`"Headset"`, `"Speaker"`, `"Keyboard"`, `"Mouse"`, `"Game controller"`,
`"Phone"`, `"Computer"`, `"Watch"`, or `""` when the device does not say.

## The system's own settings

`HasSystemSettings(WirelessRadio::WiFi)` / `OpenSystemSettings(radio, &error)`
open the platform's own page for the radio, for what this module leaves to it
(Windows: `ms-settings:network-wifi` / `ms-settings:bluetooth`; macOS: System
Settings' Wi-Fi and Bluetooth). Linux has none to open: the desktop's panels
are the settings.

## Platforms

| | Wi-Fi | Bluetooth |
|---|---|---|
| Linux | NetworkManager on the system bus (GDBus); polkit decides what the user may change - on a desktop seat everything here | BlueZ on the system bus. No pairing agent is registered, so a device that needs a code typed or confirmed (most keyboards) does not pair from here yet; the error says so |
| Windows | Native Wifi (`wlanapi`); a new network gets a WLAN profile written from its security, deleted again - or the replaced one restored - when the join fails | The Bluetooth API (`bthprops`). The radio is switched only in Windows' settings (`CanSetBluetoothPower()` is false). Pairing shows Windows' own prompt when the device wants a code. Connect and disconnect switch the device's installed services (`BluetoothSetServiceState`), as Windows does for headphones |
| macOS | CoreWLAN. From macOS 14 network names are shown only to an application Location Services allows; the first read asks, and `error` says why the list is empty while it is refused. Forgetting a network needs an administrator, so it is left to System Settings | IOBluetooth. Switching the radio and removing a pairing use the private calls every Bluetooth utility uses, looked up at run time; where they are gone the call fails saying so. The application's `Info.plist` needs `NSBluetoothAlwaysUsageDescription` and `NSLocationWhenInUseUsageDescription` (`package-macos.sh` writes both for UltraDesktop) |
| Elsewhere | `available` false; every call fails saying so | the same |
