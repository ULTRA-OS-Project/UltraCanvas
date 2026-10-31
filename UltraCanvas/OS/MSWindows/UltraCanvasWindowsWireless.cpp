// OS/MSWindows/UltraCanvasWindowsWireless.cpp
// The Windows backend of UltraCanvasWireless: the Native Wifi API (wlanapi)
// for Wi-Fi and the Bluetooth API (bthprops) for Bluetooth - both linked by
// the framework already for UltraCanvasHardwareInfo.
//
//   - Wi-Fi: the interface that is connected, else the first. Networks come
//     from WlanGetAvailableNetworkList, one entry per profile and one for
//     the network itself, which the core folds into one row. A known
//     network joins through its profile; a new one gets a profile written
//     from the network's security (WPA2/WPA3 personal, WPA, WEP, open), and
//     the profile is deleted again - or the one it replaced restored - when
//     the network is not joined within 30 s.
//   - Bluetooth: the first radio. Windows switches the radio only from its
//     Settings (the Radio Management API is WinRT, which this module does
//     not take on), so CanSetBluetoothPower is false and the panel offers
//     ms-settings:bluetooth. A discovery is an inquiry on a thread of its
//     own, repeated for about a minute; what it finds is merged into
//     ReadBluetooth. Pairing is BluetoothAuthenticateDeviceEx, which shows
//     Windows' own prompt when the device wants a code. Connecting and
//     disconnecting a paired device switch its installed services
//     (BluetoothSetServiceState) - disabling them drops the link, enabling
//     them makes Windows connect again - as Windows does for headphones.
// Version: 1.0.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework

#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00
#endif

#include "UltraCanvasWirelessBackend.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#if __has_include(<wlanapi.h>)
    #include <wlanapi.h>
    #define ULTRACANVAS_WIRELESS_HAS_WLANAPI 1
#endif
#if __has_include(<bluetoothapis.h>)
    #include <bluetoothapis.h>
    #define ULTRACANVAS_WIRELESS_HAS_BLUETOOTHAPI 1
#endif

namespace UltraCanvas {
namespace WirelessBackend {

namespace {

    std::string WideToUtf8(const wchar_t* text) {
        if (!text || !*text) return std::string();
        const int length = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (length <= 1) return std::string();
        std::string out(static_cast<size_t>(length - 1), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), length, nullptr, nullptr);
        return out;
    }

    std::wstring Utf8ToWide(const std::string& text) {
        if (text.empty()) return std::wstring();
        const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring out(static_cast<size_t>(std::max(length, 0)), L'\0');
        if (length > 0) {
            ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
        }
        return out;
    }

    std::string SystemMessage(DWORD code) {
        wchar_t* buffer = nullptr;
        const DWORD length = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                                  FORMAT_MESSAGE_IGNORE_INSERTS,
                                              nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        std::string text = length && buffer ? WideToUtf8(buffer) : "error " + std::to_string(code);
        if (buffer) ::LocalFree(buffer);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' ||
                                 text.back() == '.')) {
            text.pop_back();
        }
        return text;
    }

#ifdef ULTRACANVAS_WIRELESS_HAS_WLANAPI

    // Newer than some SDKs' headers: WLAN_AVAILABLE_NETWORK's flags and the
    // WPA3 / OWE authentication algorithms (wlantypes.h, Windows 10 2004).
    constexpr DWORD kNetworkConnected = 0x00000001;     // WLAN_AVAILABLE_NETWORK_CONNECTED
    constexpr DWORD kNetworkHasProfile = 0x00000002;    // WLAN_AVAILABLE_NETWORK_HAS_PROFILE
    constexpr int kAuthWpa3Enterprise192 = 8;           // DOT11_AUTH_ALGO_WPA3
    constexpr int kAuthWpa3Sae = 9;                     // DOT11_AUTH_ALGO_WPA3_SAE
    constexpr int kAuthOwe = 10;                        // DOT11_AUTH_ALGO_OWE
    constexpr int kAuthWpa3Enterprise = 11;             // DOT11_AUTH_ALGO_WPA3_ENT
    constexpr auto kJoinDeadline = std::chrono::seconds(30);

    struct Wlan {
        HANDLE client = nullptr;
        GUID interfaceGuid{};
        std::string interfaceName;

        ~Wlan() {
            if (client) ::WlanCloseHandle(client, nullptr);
        }
    };

    bool OpenWlan(Wlan& wlan, std::string& error) {
        DWORD negotiated = 0;
        const DWORD opened = ::WlanOpenHandle(2, nullptr, &negotiated, &wlan.client);
        if (opened != ERROR_SUCCESS) {
            wlan.client = nullptr;
            error = opened == ERROR_SERVICE_NOT_ACTIVE
                        ? "The WLAN AutoConfig service is not running."
                        : "The Wi-Fi service does not answer (" + SystemMessage(opened) + ").";
            return false;
        }
        PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
        if (::WlanEnumInterfaces(wlan.client, nullptr, &interfaces) != ERROR_SUCCESS || !interfaces ||
            interfaces->dwNumberOfItems == 0) {
            if (interfaces) ::WlanFreeMemory(interfaces);
            error = "There is no Wi-Fi adapter.";
            return false;
        }
        DWORD pick = 0;
        for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i) {
            if (interfaces->InterfaceInfo[i].isState == wlan_interface_state_connected) {
                pick = i;
                break;
            }
        }
        wlan.interfaceGuid = interfaces->InterfaceInfo[pick].InterfaceGuid;
        wlan.interfaceName = WideToUtf8(interfaces->InterfaceInfo[pick].strInterfaceDescription);
        ::WlanFreeMemory(interfaces);
        return true;
    }

    std::string SsidOf(const DOT11_SSID& ssid) {
        const ULONG length = std::min<ULONG>(ssid.uSSIDLength, DOT11_SSID_MAX_LENGTH);
        return std::string(reinterpret_cast<const char*>(ssid.ucSSID), length);
    }

    std::string SecurityOf(const WLAN_AVAILABLE_NETWORK& network) {
        if (!network.bSecurityEnabled) return "Open";
        switch (static_cast<int>(network.dot11DefaultAuthAlgorithm)) {
            case DOT11_AUTH_ALGO_RSNA_PSK: return "WPA2";
            case DOT11_AUTH_ALGO_WPA_PSK:  return "WPA";
            case kAuthWpa3Sae:             return "WPA3";
            case kAuthOwe:                 return "Open";
            case DOT11_AUTH_ALGO_RSNA:
            case DOT11_AUTH_ALGO_WPA:
            case kAuthWpa3Enterprise192:
            case kAuthWpa3Enterprise:      return "Enterprise";
            default:                       return "WEP";
        }
    }

    // The interface's state and the joined network's name ("" when none).
    std::string CurrentSsid(const Wlan& wlan, WLAN_INTERFACE_STATE* state = nullptr) {
        DWORD size = 0;
        PWLAN_CONNECTION_ATTRIBUTES connection = nullptr;
        if (::WlanQueryInterface(wlan.client, &wlan.interfaceGuid, wlan_intf_opcode_current_connection, nullptr,
                                 &size, reinterpret_cast<PVOID*>(&connection), nullptr) != ERROR_SUCCESS ||
            !connection) {
            if (state) *state = wlan_interface_state_disconnected;
            return "";
        }
        if (state) *state = connection->isState;
        std::string ssid;
        if (connection->isState == wlan_interface_state_connected) {
            ssid = SsidOf(connection->wlanAssociationAttributes.dot11Ssid);
        }
        ::WlanFreeMemory(connection);
        return ssid;
    }

    std::vector<WLAN_AVAILABLE_NETWORK> AvailableNetworks(const Wlan& wlan) {
        std::vector<WLAN_AVAILABLE_NETWORK> out;
        PWLAN_AVAILABLE_NETWORK_LIST list = nullptr;
        if (::WlanGetAvailableNetworkList(wlan.client, &wlan.interfaceGuid, 0, nullptr, &list) != ERROR_SUCCESS ||
            !list) {
            return out;
        }
        for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
            if (list->Network[i].dot11BssType == dot11_BSS_type_infrastructure) out.push_back(list->Network[i]);
        }
        ::WlanFreeMemory(list);
        return out;
    }

    std::wstring XmlEscape(const std::wstring& text) {
        std::wstring out;
        for (wchar_t c : text) {
            switch (c) {
                case L'&':  out += L"&amp;"; break;
                case L'<':  out += L"&lt;"; break;
                case L'>':  out += L"&gt;"; break;
                case L'"':  out += L"&quot;"; break;
                case L'\'': out += L"&apos;"; break;
                default:    out += c;
            }
        }
        return out;
    }

    std::wstring HexOf(const std::string& bytes) {
        std::wstring out;
        wchar_t pair[3];
        for (unsigned char c : bytes) {
            ::swprintf(pair, 3, L"%02X", c);
            out += pair;
        }
        return out;
    }

    // A WLANProfile for a personal network; "" when the security is one
    // this backend does not write (Enterprise).
    std::wstring ProfileXml(const std::string& ssid, const std::string& passphrase,
                            const WLAN_AVAILABLE_NETWORK& network) {
        const std::string security = SecurityOf(network);
        std::wstring authentication, encryption, keyType = L"passPhrase";
        if (security == "Open") {
            authentication = static_cast<int>(network.dot11DefaultAuthAlgorithm) == kAuthOwe ? L"OWE" : L"open";
            encryption = static_cast<int>(network.dot11DefaultAuthAlgorithm) == kAuthOwe ? L"AES" : L"none";
        } else if (security == "WPA3") {
            authentication = L"WPA3SAE";
            encryption = L"AES";
        } else if (security == "WPA2" || security == "WPA") {
            authentication = security == "WPA2" ? L"WPA2PSK" : L"WPAPSK";
            encryption = network.dot11DefaultCipherAlgorithm == DOT11_CIPHER_ALGO_TKIP ? L"TKIP" : L"AES";
        } else if (security == "WEP") {
            authentication = network.dot11DefaultAuthAlgorithm == DOT11_AUTH_ALGO_80211_SHARED_KEY ? L"shared"
                                                                                                   : L"open";
            encryption = L"WEP";
            keyType = L"networkKey";
        } else {
            return L"";
        }
        const std::wstring name = XmlEscape(Utf8ToWide(ssid));
        std::wstring xml =
            L"<?xml version=\"1.0\"?>\n"
            L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\n"
            L"  <name>" + name + L"</name>\n"
            L"  <SSIDConfig><SSID><hex>" + HexOf(ssid) + L"</hex><name>" + name + L"</name></SSID></SSIDConfig>\n"
            L"  <connectionType>ESS</connectionType>\n"
            L"  <connectionMode>auto</connectionMode>\n"
            L"  <MSM><security>\n"
            L"    <authEncryption><authentication>" + authentication + L"</authentication><encryption>" +
            encryption + L"</encryption><useOneX>false</useOneX></authEncryption>\n";
        if (security != "Open") {
            xml += L"    <sharedKey><keyType>" + keyType + L"</keyType><protected>false</protected><keyMaterial>" +
                   XmlEscape(Utf8ToWide(passphrase)) + L"</keyMaterial></sharedKey>\n";
        }
        xml += L"  </security></MSM>\n</WLANProfile>\n";
        return xml;
    }

    // Wait until `ssid` is joined; false when the interface gave up or the
    // deadline passed.
    bool WaitForJoin(const Wlan& wlan, const std::string& ssid) {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < kJoinDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            WLAN_INTERFACE_STATE state = wlan_interface_state_disconnected;
            const std::string joined = CurrentSsid(wlan, &state);
            if (state == wlan_interface_state_connected && joined == ssid) return true;
            // Back to disconnected after the attempt had time to start: the
            // network refused (most often the password).
            if (state == wlan_interface_state_disconnected &&
                std::chrono::steady_clock::now() - start > std::chrono::seconds(6)) {
                return false;
            }
        }
        return false;
    }

    bool ConnectProfile(const Wlan& wlan, const std::wstring& profile, std::string& error) {
        WLAN_CONNECTION_PARAMETERS parameters{};
        parameters.wlanConnectionMode = wlan_connection_mode_profile;
        parameters.strProfile = profile.c_str();
        parameters.pDot11Ssid = nullptr;
        parameters.pDesiredBssidList = nullptr;
        parameters.dot11BssType = dot11_BSS_type_infrastructure;
        parameters.dwFlags = 0;
        const DWORD result = ::WlanConnect(wlan.client, &wlan.interfaceGuid, &parameters, nullptr);
        if (result != ERROR_SUCCESS) {
            error = SystemMessage(result);
            return false;
        }
        return true;
    }

#endif // ULTRACANVAS_WIRELESS_HAS_WLANAPI

#ifdef ULTRACANVAS_WIRELESS_HAS_BLUETOOTHAPI

    constexpr UCHAR kServiceDisable = 0x00;   // BLUETOOTH_SERVICE_DISABLE
    constexpr UCHAR kServiceEnable = 0x01;    // BLUETOOTH_SERVICE_ENABLE

    // The Bluetooth SIG's base UUID with a 16-bit service class.
    GUID ServiceClass(uint16_t shortId) {
        return GUID{static_cast<unsigned long>(shortId), 0x0000, 0x1000,
                    {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};
    }

    // What a device commonly offers: audio sink, hands-free, headset,
    // remote control and human interface (keyboards, mice, controllers).
    std::vector<GUID> CommonServices() {
        return {ServiceClass(0x110B), ServiceClass(0x111E), ServiceClass(0x1108),
                ServiceClass(0x110E), ServiceClass(0x1124)};
    }

    struct Radio {
        HANDLE handle = nullptr;
        ~Radio() {
            if (handle) ::CloseHandle(handle);
        }
    };

    bool OpenRadio(Radio& radio, std::string& error) {
        BLUETOOTH_FIND_RADIO_PARAMS params{};
        params.dwSize = sizeof(params);
        HBLUETOOTH_RADIO_FIND find = ::BluetoothFindFirstRadio(&params, &radio.handle);
        if (!find) {
            radio.handle = nullptr;
            error = "There is no Bluetooth adapter, or Bluetooth is switched off in Windows' settings.";
            return false;
        }
        ::BluetoothFindRadioClose(find);
        return true;
    }

    std::string FormatAddress(const BLUETOOTH_ADDRESS& address) {
        char text[18];
        ::snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", address.rgBytes[5], address.rgBytes[4],
                   address.rgBytes[3], address.rgBytes[2], address.rgBytes[1], address.rgBytes[0]);
        return text;
    }

    // "AA:BB:CC:DD:EE:FF" (the core has normalised it).
    BLUETOOTH_ADDRESS ParseAddress(const std::string& text) {
        BLUETOOTH_ADDRESS address{};
        unsigned values[6] = {};
        if (::sscanf(text.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x", &values[0], &values[1], &values[2], &values[3],
                     &values[4], &values[5]) == 6) {
            for (int i = 0; i < 6; ++i) address.rgBytes[5 - i] = static_cast<BYTE>(values[i]);
        }
        return address;
    }

    std::string KindOfClass(ULONG cod) {
        const ULONG major = (cod >> 8) & 0x1F;
        const ULONG minor = (cod >> 2) & 0x3F;
        switch (major) {
            case 1: return "Computer";
            case 2: return "Phone";
            case 4:
                if (minor == 1) return "Headset";
                if (minor == 2) return "Headset";
                if (minor == 6) return "Headphones";
                if (minor == 5 || minor == 7 || minor == 8) return "Speaker";
                return "Speaker";
            case 5: {
                const ULONG pointer = (cod >> 6) & 0x3;
                const ULONG sub = (cod >> 2) & 0xF;
                if (pointer == 1 || pointer == 3) return "Keyboard";
                if (pointer == 2) return "Mouse";
                if (sub == 1 || sub == 2) return "Game controller";
                return "";
            }
            case 6: return "Printer";
            case 7: return "Watch";
            default: return "";
        }
    }

    WirelessBluetoothDevice DeviceOf(const BLUETOOTH_DEVICE_INFO& info) {
        WirelessBluetoothDevice device;
        device.address = FormatAddress(info.Address);
        device.name = WideToUtf8(info.szName);
        device.kind = KindOfClass(info.ulClassofDevice);
        device.paired = info.fAuthenticated || info.fRemembered;
        device.connected = info.fConnected != FALSE;
        device.trusted = device.paired;
        return device;
    }

    // The discovery: an inquiry thread and what it found.
    struct Discovery {
        std::mutex mutex;
        std::vector<WirelessBluetoothDevice> found;
        std::atomic<bool> running{false};
        std::atomic<bool> stop{false};
    };

    Discovery& TheDiscovery() {
        static Discovery* discovery = new Discovery();   // never destroyed: a detached thread may outlive main
        return *discovery;
    }

    void RunInquiry() {
        Discovery& discovery = TheDiscovery();
        const auto start = std::chrono::steady_clock::now();
        while (!discovery.stop.load() && std::chrono::steady_clock::now() - start < std::chrono::seconds(60)) {
            Radio radio;
            std::string ignored;
            if (!OpenRadio(radio, ignored)) break;
            BLUETOOTH_DEVICE_SEARCH_PARAMS params{};
            params.dwSize = sizeof(params);
            params.fReturnAuthenticated = FALSE;
            params.fReturnRemembered = FALSE;
            params.fReturnUnknown = TRUE;
            params.fReturnConnected = FALSE;
            params.fIssueInquiry = TRUE;
            params.cTimeoutMultiplier = 6;   // 6 x 1.28 s
            params.hRadio = radio.handle;
            BLUETOOTH_DEVICE_INFO info{};
            info.dwSize = sizeof(info);
            HBLUETOOTH_DEVICE_FIND find = ::BluetoothFindFirstDevice(&params, &info);
            if (find) {
                do {
                    if (info.fAuthenticated || info.fRemembered) continue;
                    WirelessBluetoothDevice device = DeviceOf(info);
                    std::lock_guard<std::mutex> lock(discovery.mutex);
                    auto known = std::find_if(discovery.found.begin(), discovery.found.end(),
                                              [&](const WirelessBluetoothDevice& d) { return d.address == device.address; });
                    if (known == discovery.found.end()) discovery.found.push_back(device);
                    else if (!device.name.empty()) *known = device;
                    info = BLUETOOTH_DEVICE_INFO{};
                    info.dwSize = sizeof(info);
                } while (::BluetoothFindNextDevice(find, &info));
                ::BluetoothFindDeviceClose(find);
            }
        }
        discovery.running.store(false);
    }

    bool DeviceInfo(const Radio& radio, const std::string& address, BLUETOOTH_DEVICE_INFO& info, std::string& error) {
        info = BLUETOOTH_DEVICE_INFO{};
        info.dwSize = sizeof(info);
        info.Address = ParseAddress(address);
        const DWORD result = ::BluetoothGetDeviceInfo(radio.handle, &info);
        if (result != ERROR_SUCCESS) {
            error = "The device " + address + " is not known to Bluetooth; look for devices first.";
            return false;
        }
        return true;
    }

    std::vector<GUID> InstalledServices(const Radio& radio, BLUETOOTH_DEVICE_INFO& info) {
        DWORD count = 32;
        std::vector<GUID> services(count);
        if (::BluetoothEnumerateInstalledServices(radio.handle, &info, &count, services.data()) != ERROR_SUCCESS) {
            return {};
        }
        services.resize(std::min<DWORD>(count, 32));
        return services;
    }

#endif // ULTRACANVAS_WIRELESS_HAS_BLUETOOTHAPI

} // namespace

std::string BackendName() { return "win32"; }

// ===== WI-FI =====

WirelessWifiStatus ReadWifi() {
    WirelessWifiStatus status;
#ifdef ULTRACANVAS_WIRELESS_HAS_WLANAPI
    Wlan wlan;
    if (!OpenWlan(wlan, status.error)) {
        status.available = status.error == "There is no Wi-Fi adapter.";
        return status;
    }
    status.available = true;
    status.adapterPresent = true;
    status.adapterName = wlan.interfaceName;

    DWORD size = 0;
    PWLAN_RADIO_STATE radio = nullptr;
    status.enabled = true;
    if (::WlanQueryInterface(wlan.client, &wlan.interfaceGuid, wlan_intf_opcode_radio_state, nullptr, &size,
                             reinterpret_cast<PVOID*>(&radio), nullptr) == ERROR_SUCCESS && radio) {
        for (DWORD i = 0; i < radio->dwNumberOfPhys && i < WLAN_MAX_PHY_INDEX; ++i) {
            if (radio->PhyRadioState[i].dot11SoftwareRadioState != dot11_radio_state_on) status.enabled = false;
            if (radio->PhyRadioState[i].dot11HardwareRadioState != dot11_radio_state_on) {
                status.hardwareBlocked = true;
            }
        }
        ::WlanFreeMemory(radio);
    }
    if (!status.enabled || status.hardwareBlocked) return status;

    status.connectedSsid = CurrentSsid(wlan);
    for (const WLAN_AVAILABLE_NETWORK& available : AvailableNetworks(wlan)) {
        WirelessNetwork network;
        network.ssid = SsidOf(available.dot11Ssid);
        if (network.ssid.empty()) continue;
        network.signalPercent = static_cast<int>(available.wlanSignalQuality);
        network.security = SecurityOf(available);
        network.secured = network.security != "Open";
        network.known = (available.dwFlags & kNetworkHasProfile) != 0;
        network.connected = (available.dwFlags & kNetworkConnected) != 0;
        status.networks.push_back(network);
    }
#else
    status.error = "This build has no Wi-Fi support (wlanapi.h was missing).";
#endif
    return status;
}

bool RequestWifiScan(std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_WLANAPI
    Wlan wlan;
    if (!OpenWlan(wlan, error)) return false;
    const DWORD result = ::WlanScan(wlan.client, &wlan.interfaceGuid, nullptr, nullptr, nullptr);
    if (result != ERROR_SUCCESS) {
        error = "The Wi-Fi adapter could not scan: " + SystemMessage(result);
        return false;
    }
    return true;
#else
    error = "This build has no Wi-Fi support.";
    return false;
#endif
}

bool SetWifiEnabled(bool enabled, std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_WLANAPI
    Wlan wlan;
    if (!OpenWlan(wlan, error)) return false;
    DWORD size = 0;
    PWLAN_RADIO_STATE radio = nullptr;
    DWORD phys = 1;
    if (::WlanQueryInterface(wlan.client, &wlan.interfaceGuid, wlan_intf_opcode_radio_state, nullptr, &size,
                             reinterpret_cast<PVOID*>(&radio), nullptr) == ERROR_SUCCESS && radio) {
        phys = std::max<DWORD>(1, std::min<DWORD>(radio->dwNumberOfPhys, WLAN_MAX_PHY_INDEX));
        ::WlanFreeMemory(radio);
    }
    for (DWORD i = 0; i < phys; ++i) {
        WLAN_PHY_RADIO_STATE state{};
        state.dwPhyIndex = i;
        state.dot11SoftwareRadioState = enabled ? dot11_radio_state_on : dot11_radio_state_off;
        const DWORD result = ::WlanSetInterface(wlan.client, &wlan.interfaceGuid, wlan_intf_opcode_radio_state,
                                                sizeof(state), &state, nullptr);
        if (result != ERROR_SUCCESS) {
            error = result == ERROR_ACCESS_DENIED
                        ? std::string("Windows switches Wi-Fi ") + (enabled ? "on" : "off") +
                              " only from its own settings here."
                        : std::string("Wi-Fi could not be switched ") + (enabled ? "on: " : "off: ") +
                              SystemMessage(result);
            return false;
        }
    }
    return true;
#else
    (void)enabled;
    error = "This build has no Wi-Fi support.";
    return false;
#endif
}

bool ConnectWifi(const std::string& ssid, const std::string& passphrase, std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_WLANAPI
    Wlan wlan;
    if (!OpenWlan(wlan, error)) return false;
    const std::vector<WLAN_AVAILABLE_NETWORK> networks = AvailableNetworks(wlan);

    // A known network: join through its profile.
    if (passphrase.empty()) {
        for (const WLAN_AVAILABLE_NETWORK& network : networks) {
            if (SsidOf(network.dot11Ssid) != ssid || !(network.dwFlags & kNetworkHasProfile)) continue;
            if (!ConnectProfile(wlan, network.strProfileName, error)) {
                error = "Could not join " + ssid + ": " + error;
                return false;
            }
            if (WaitForJoin(wlan, ssid)) return true;
            error = "Could not join " + ssid + ". If its password changed, forget the network and join it again.";
            return false;
        }
    }

    // A new one (or a new password): the network as the scan saw it.
    const WLAN_AVAILABLE_NETWORK* best = nullptr;
    for (const WLAN_AVAILABLE_NETWORK& network : networks) {
        if (SsidOf(network.dot11Ssid) != ssid) continue;
        if (!best || network.wlanSignalQuality > best->wlanSignalQuality) best = &network;
    }
    if (!best) {
        error = ssid + " is not in reach. Scan again, or move closer to it.";
        return false;
    }
    const std::string security = SecurityOf(*best);
    if (security == "Enterprise") {
        error = ssid + " asks for a user name and certificate (WPA Enterprise); join it from Windows' settings.";
        return false;
    }
    if (security != "Open" && passphrase.empty()) {
        error = ssid + " needs its password.";
        return false;
    }
    const std::wstring xml = ProfileXml(ssid, passphrase, *best);
    if (xml.empty()) {
        error = "Could not write a profile for " + ssid + ".";
        return false;
    }

    // Keep a profile of the same name to put back if this one fails.
    const std::wstring profileName = Utf8ToWide(ssid);
    LPWSTR previous = nullptr;
    DWORD flags = 0;
    std::wstring previousXml;
    if (::WlanGetProfile(wlan.client, &wlan.interfaceGuid, profileName.c_str(), nullptr, &previous, &flags,
                         nullptr) == ERROR_SUCCESS && previous) {
        previousXml = previous;
        ::WlanFreeMemory(previous);
    }

    DWORD reason = 0;
    const DWORD written = ::WlanSetProfile(wlan.client, &wlan.interfaceGuid, 0, xml.c_str(), nullptr, TRUE, nullptr,
                                           &reason);
    if (written != ERROR_SUCCESS) {
        wchar_t text[256] = {};
        if (reason != 0) ::WlanReasonCodeToString(reason, 256, text, nullptr);
        error = "Windows refused the profile for " + ssid + ": " +
                (text[0] ? WideToUtf8(text) : SystemMessage(written));
        return false;
    }
    if (ConnectProfile(wlan, profileName, error) && WaitForJoin(wlan, ssid)) return true;

    if (previousXml.empty()) {
        ::WlanDeleteProfile(wlan.client, &wlan.interfaceGuid, profileName.c_str(), nullptr);
    } else {
        ::WlanSetProfile(wlan.client, &wlan.interfaceGuid, flags, previousXml.c_str(), nullptr, TRUE, nullptr,
                         &reason);
    }
    error = security == "Open" ? "Could not join " + ssid + "."
                               : "Could not join " + ssid + " - is the password right?";
    return false;
#else
    (void)ssid;
    (void)passphrase;
    error = "This build has no Wi-Fi support.";
    return false;
#endif
}

bool DisconnectWifi(std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_WLANAPI
    Wlan wlan;
    if (!OpenWlan(wlan, error)) return false;
    const DWORD result = ::WlanDisconnect(wlan.client, &wlan.interfaceGuid, nullptr);
    if (result != ERROR_SUCCESS) {
        error = "Could not leave the network: " + SystemMessage(result);
        return false;
    }
    return true;
#else
    error = "This build has no Wi-Fi support.";
    return false;
#endif
}

bool ForgetWifi(const std::string& ssid, std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_WLANAPI
    Wlan wlan;
    if (!OpenWlan(wlan, error)) return false;
    // The profiles the scan ties to the network, and one named after it.
    std::set<std::wstring> profiles;
    for (const WLAN_AVAILABLE_NETWORK& network : AvailableNetworks(wlan)) {
        if (SsidOf(network.dot11Ssid) == ssid && network.strProfileName[0]) profiles.insert(network.strProfileName);
    }
    PWLAN_PROFILE_INFO_LIST list = nullptr;
    if (::WlanGetProfileList(wlan.client, &wlan.interfaceGuid, nullptr, &list) == ERROR_SUCCESS && list) {
        const std::wstring wide = Utf8ToWide(ssid);
        for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
            if (wide == list->ProfileInfo[i].strProfileName) profiles.insert(list->ProfileInfo[i].strProfileName);
        }
        ::WlanFreeMemory(list);
    }
    if (profiles.empty()) {
        error = ssid + " is not saved.";
        return false;
    }
    if (CurrentSsid(wlan) == ssid) ::WlanDisconnect(wlan.client, &wlan.interfaceGuid, nullptr);
    for (const std::wstring& profile : profiles) {
        const DWORD result = ::WlanDeleteProfile(wlan.client, &wlan.interfaceGuid, profile.c_str(), nullptr);
        if (result != ERROR_SUCCESS && result != ERROR_NOT_FOUND) {
            error = "Could not forget " + ssid + ": " + SystemMessage(result);
            return false;
        }
    }
    return true;
#else
    (void)ssid;
    error = "This build has no Wi-Fi support.";
    return false;
#endif
}

// ===== BLUETOOTH =====

WirelessBluetoothStatus ReadBluetooth() {
    WirelessBluetoothStatus status;
#ifdef ULTRACANVAS_WIRELESS_HAS_BLUETOOTHAPI
    status.available = true;
    Radio radio;
    if (!OpenRadio(radio, status.error)) return status;
    status.adapterPresent = true;
    BLUETOOTH_RADIO_INFO radioInfo{};
    radioInfo.dwSize = sizeof(radioInfo);
    if (::BluetoothGetRadioInfo(radio.handle, &radioInfo) == ERROR_SUCCESS) {
        status.adapterName = WideToUtf8(radioInfo.szName);
    }
    status.powered = ::BluetoothIsConnectable(radio.handle) != FALSE ||
                     ::BluetoothIsDiscoverable(radio.handle) != FALSE;
    Discovery& discovery = TheDiscovery();
    status.discovering = discovery.running.load();

    BLUETOOTH_DEVICE_SEARCH_PARAMS params{};
    params.dwSize = sizeof(params);
    params.fReturnAuthenticated = TRUE;
    params.fReturnRemembered = TRUE;
    params.fReturnConnected = TRUE;
    params.fReturnUnknown = FALSE;
    params.fIssueInquiry = FALSE;
    params.hRadio = radio.handle;
    BLUETOOTH_DEVICE_INFO info{};
    info.dwSize = sizeof(info);
    std::set<std::string> listed;
    HBLUETOOTH_DEVICE_FIND find = ::BluetoothFindFirstDevice(&params, &info);
    if (find) {
        do {
            WirelessBluetoothDevice device = DeviceOf(info);
            if (listed.insert(device.address).second) status.devices.push_back(device);
            info = BLUETOOTH_DEVICE_INFO{};
            info.dwSize = sizeof(info);
        } while (::BluetoothFindNextDevice(find, &info));
        ::BluetoothFindDeviceClose(find);
    }
    std::lock_guard<std::mutex> lock(discovery.mutex);
    for (const WirelessBluetoothDevice& device : discovery.found) {
        if (listed.insert(device.address).second) status.devices.push_back(device);
    }
#else
    status.error = "This build has no Bluetooth support (bluetoothapis.h was missing).";
#endif
    return status;
}

bool CanSetBluetoothPower() { return false; }

bool SetBluetoothPowered(bool, std::string& error) {
    error = "Windows switches Bluetooth on and off in its own settings.";
    return false;
}

bool StartBluetoothDiscovery(std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_BLUETOOTHAPI
    Radio radio;
    if (!OpenRadio(radio, error)) return false;
    Discovery& discovery = TheDiscovery();
    if (discovery.running.exchange(true)) return true;
    discovery.stop.store(false);
    {
        std::lock_guard<std::mutex> lock(discovery.mutex);
        discovery.found.clear();
    }
    std::thread(RunInquiry).detach();
    return true;
#else
    error = "This build has no Bluetooth support.";
    return false;
#endif
}

bool StopBluetoothDiscovery(std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_BLUETOOTHAPI
    (void)error;
    // The inquiry under way finishes its few seconds, then the thread ends.
    TheDiscovery().stop.store(true);
    return true;
#else
    error = "This build has no Bluetooth support.";
    return false;
#endif
}

bool ConnectBluetoothDevice(const std::string& address, std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_BLUETOOTHAPI
    Radio radio;
    BLUETOOTH_DEVICE_INFO info{};
    if (!OpenRadio(radio, error) || !DeviceInfo(radio, address, info, error)) return false;
    if (!info.fAuthenticated && !info.fRemembered) {
        error = "Pair the device first.";
        return false;
    }
    // Switching the installed services off and on again makes Windows
    // connect them; services a disconnect switched off are no longer
    // installed, so the common ones are switched on too.
    std::vector<GUID> installed = InstalledServices(radio, info);
    for (GUID& service : installed) ::BluetoothSetServiceState(radio.handle, &info, &service, kServiceDisable);
    bool any = false;
    DWORD last = ERROR_SUCCESS;
    for (GUID& service : installed) {
        last = ::BluetoothSetServiceState(radio.handle, &info, &service, kServiceEnable);
        any = any || last == ERROR_SUCCESS;
    }
    for (GUID service : CommonServices()) {
        if (std::any_of(installed.begin(), installed.end(),
                        [&](const GUID& g) { return ::IsEqualGUID(g, service) != FALSE; })) {
            continue;
        }
        if (::BluetoothSetServiceState(radio.handle, &info, &service, kServiceEnable) == ERROR_SUCCESS) any = true;
    }
    if (!any) {
        error = "Could not connect: " + (last != ERROR_SUCCESS ? SystemMessage(last) : std::string("the device offers no service Windows can connect")) +
                ". Is the device switched on and in reach?";
        return false;
    }
    return true;
#else
    (void)address;
    error = "This build has no Bluetooth support.";
    return false;
#endif
}

bool PairBluetoothDevice(const std::string& address, std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_BLUETOOTHAPI
    Radio radio;
    BLUETOOTH_DEVICE_INFO info{};
    if (!OpenRadio(radio, error) || !DeviceInfo(radio, address, info, error)) return false;
    TheDiscovery().stop.store(true);
    if (!info.fAuthenticated) {
        const DWORD result = ::BluetoothAuthenticateDeviceEx(nullptr, radio.handle, &info, nullptr,
                                                             MITMProtectionNotRequired);
        if (result != ERROR_SUCCESS && result != ERROR_NO_MORE_ITEMS) {
            error = result == ERROR_CANCELLED ? "Pairing was cancelled."
                                              : "Could not pair: " + SystemMessage(result) +
                                                    ". Is the device in pairing mode?";
            return false;
        }
    }
    // Windows installs the device's services as it pairs; ask for the
    // common ones too, so the device connects now.
    for (GUID service : CommonServices()) ::BluetoothSetServiceState(radio.handle, &info, &service, kServiceEnable);
    {
        Discovery& discovery = TheDiscovery();
        std::lock_guard<std::mutex> lock(discovery.mutex);
        discovery.found.erase(std::remove_if(discovery.found.begin(), discovery.found.end(),
                                             [&](const WirelessBluetoothDevice& d) { return d.address == address; }),
                              discovery.found.end());
    }
    return true;
#else
    (void)address;
    error = "This build has no Bluetooth support.";
    return false;
#endif
}

bool DisconnectBluetoothDevice(const std::string& address, std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_BLUETOOTHAPI
    Radio radio;
    BLUETOOTH_DEVICE_INFO info{};
    if (!OpenRadio(radio, error) || !DeviceInfo(radio, address, info, error)) return false;
    std::vector<GUID> installed = InstalledServices(radio, info);
    if (installed.empty()) {
        if (!info.fConnected) return true;
        error = "Windows has no service of this device to switch off.";
        return false;
    }
    DWORD last = ERROR_SUCCESS;
    for (GUID& service : installed) {
        const DWORD result = ::BluetoothSetServiceState(radio.handle, &info, &service, kServiceDisable);
        if (result != ERROR_SUCCESS) last = result;
    }
    if (last != ERROR_SUCCESS) {
        error = "Could not disconnect every part of the device: " + SystemMessage(last);
        return false;
    }
    return true;
#else
    (void)address;
    error = "This build has no Bluetooth support.";
    return false;
#endif
}

bool RemoveBluetoothDevice(const std::string& address, std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_BLUETOOTHAPI
    BLUETOOTH_ADDRESS parsed = ParseAddress(address);
    const DWORD result = ::BluetoothRemoveDevice(&parsed);
    if (result != ERROR_SUCCESS && result != ERROR_NOT_FOUND) {
        error = "Could not remove the device: " + SystemMessage(result);
        return false;
    }
    return true;
#else
    (void)address;
    error = "This build has no Bluetooth support.";
    return false;
#endif
}

// ===== THE SYSTEM'S OWN SETTINGS =====

bool HasSystemSettings(WirelessRadio) { return true; }

bool OpenSystemSettings(WirelessRadio radio, std::string& error) {
    const wchar_t* page = radio == WirelessRadio::WiFi ? L"ms-settings:network-wifi" : L"ms-settings:bluetooth";
    const auto result = reinterpret_cast<INT_PTR>(::ShellExecuteW(nullptr, L"open", page, nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        error = "Windows' settings did not open.";
        return false;
    }
    return true;
}

} // namespace WirelessBackend
} // namespace UltraCanvas
