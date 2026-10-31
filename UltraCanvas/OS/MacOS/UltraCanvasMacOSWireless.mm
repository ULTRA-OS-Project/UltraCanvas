// OS/MacOS/UltraCanvasMacOSWireless.mm
// The macOS backend of UltraCanvasWireless: CoreWLAN for Wi-Fi and
// IOBluetooth for Bluetooth.
//
//   - Wi-Fi: CWWiFiClient's default interface. ReadWifi lists the cached
//     scan results; RequestWifiScan scans (a few seconds, synchronous - the
//     caller is on a worker thread). Since macOS 14 the network names are
//     shown only to an application Location Services allows, so the first
//     read asks for that (CLLocationManager) and, while it is refused, says
//     why the list is empty. A known network joins with the password the
//     system keychain holds for it; forgetting a network needs an
//     administrator's authorization, so that is left to System Settings.
//   - Bluetooth: the default host controller and IOBluetoothDevice. Device
//     inquiries and pairing deliver their answers to a run loop, so they
//     run on the main queue and a worker thread waits for them. Switching
//     the radio and removing a pairing have no public API; the private
//     calls every Bluetooth utility uses (IOBluetoothPreferenceSet-
//     ControllerPowerState, -[IOBluetoothDevice remove]) are looked up at
//     run time and the call fails saying so where they are gone.
//
// The application's Info.plist needs NSBluetoothAlwaysUsageDescription (TCC
// ends an application that touches Bluetooth without it) and
// NSLocationWhenInUseUsageDescription; package-macos.sh writes both for
// UltraDesktop.
// Version: 1.0.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework

#include "UltraCanvasWirelessBackend.h"

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

#if __has_include(<CoreWLAN/CoreWLAN.h>)
    #import <CoreWLAN/CoreWLAN.h>
    #define ULTRACANVAS_WIRELESS_HAS_COREWLAN 1
#endif
#if __has_include(<IOBluetooth/IOBluetooth.h>)
    #import <IOBluetooth/IOBluetooth.h>
    #define ULTRACANVAS_WIRELESS_HAS_IOBLUETOOTH 1
#endif
#if __has_include(<CoreLocation/CoreLocation.h>)
    #import <CoreLocation/CoreLocation.h>
    #define ULTRACANVAS_WIRELESS_HAS_CORELOCATION 1
#endif

#include <objc/message.h>
#include <dlfcn.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#ifdef ULTRACANVAS_WIRELESS_HAS_IOBLUETOOTH

// Collects what an inquiry finds; restarts it until a minute has passed.
@interface UCWirelessInquiryDelegate : NSObject <IOBluetoothDeviceInquiryDelegate>
@property (nonatomic, strong) IOBluetoothDeviceInquiry* inquiry;
@property (nonatomic, strong) NSDate* until;
@property (atomic, assign) BOOL running;
@end

// Waits for one pairing to finish; accepts a numeric comparison the user
// started by asking to pair.
@interface UCWirelessPairDelegate : NSObject
@property (atomic, assign) IOReturn result;
@property (nonatomic, strong) dispatch_semaphore_t done;
@end

#endif

namespace UltraCanvas {
namespace WirelessBackend {

namespace {

    std::string ToUtf8(NSString* text) {
        return text ? std::string([text UTF8String] ?: "") : std::string();
    }

    std::string Describe(NSError* error) {
        return error ? ToUtf8(error.localizedDescription) : std::string("no answer");
    }

    // Run `work` on the main thread and wait for it - at once when this is
    // the main thread, where a dispatch_sync would wait for itself.
    void OnMain(void (^work)(void)) {
        if ([NSThread isMainThread]) work();
        else dispatch_sync(dispatch_get_main_queue(), work);
    }

    bool OpenUrl(NSString* url) {
        __block BOOL opened = NO;
        OnMain(^{
            NSURL* target = [NSURL URLWithString:url];
            opened = target && [[NSWorkspace sharedWorkspace] openURL:target];
        });
        return opened;
    }

#ifdef ULTRACANVAS_WIRELESS_HAS_CORELOCATION
    // Asks once for Location Services, which macOS 14 requires for network
    // names. The manager must live on the main thread and stay alive.
    bool LocationAllowed() {
        static CLLocationManager* manager = nil;
        static std::once_flag asked;
        std::call_once(asked, [] {
            dispatch_async(dispatch_get_main_queue(), ^{
                manager = [[CLLocationManager alloc] init];
                if (@available(macOS 10.15, *)) [manager requestWhenInUseAuthorization];
            });
        });
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        const CLAuthorizationStatus status = [CLLocationManager authorizationStatus];
#pragma clang diagnostic pop
        return status != kCLAuthorizationStatusDenied && status != kCLAuthorizationStatusRestricted;
    }
#else
    bool LocationAllowed() { return true; }
#endif

#ifdef ULTRACANVAS_WIRELESS_HAS_COREWLAN
    CWInterface* WifiInterface(std::string& error) {
        CWInterface* interface = [[CWWiFiClient sharedWiFiClient] interface];
        if (!interface) error = "There is no Wi-Fi adapter.";
        return interface;
    }

    std::string SecurityOf(CWNetwork* network) {
        if ([network supportsSecurity:kCWSecurityWPA2Enterprise] || [network supportsSecurity:kCWSecurityWPAEnterprise] ||
            [network supportsSecurity:kCWSecurityEnterprise]) {
            return "Enterprise";
        }
        if (@available(macOS 10.15, *)) {
            if ([network supportsSecurity:kCWSecurityWPA3Enterprise]) return "Enterprise";
            if ([network supportsSecurity:kCWSecurityWPA3Personal] ||
                [network supportsSecurity:kCWSecurityWPA3Transition]) {
                return [network supportsSecurity:kCWSecurityWPA2Personal] ? "WPA2" : "WPA3";
            }
            if ([network supportsSecurity:kCWSecurityOWE] || [network supportsSecurity:kCWSecurityOWETransition]) {
                return "Open";
            }
        }
        if ([network supportsSecurity:kCWSecurityWPA2Personal] || [network supportsSecurity:kCWSecurityPersonal]) {
            return "WPA2";
        }
        if ([network supportsSecurity:kCWSecurityWPAPersonal] ||
            [network supportsSecurity:kCWSecurityWPAPersonalMixed]) {
            return "WPA";
        }
        if ([network supportsSecurity:kCWSecurityWEP] || [network supportsSecurity:kCWSecurityDynamicWEP]) return "WEP";
        return "Open";
    }

    // -90 dBm and below is nothing, -40 and above is full.
    int PercentOfRssi(NSInteger rssi) {
        return std::clamp(static_cast<int>((rssi + 90) * 2), 0, 100);
    }

    std::set<std::string> KnownSsids(CWInterface* interface) {
        std::set<std::string> known;
        for (CWNetworkProfile* profile in interface.configuration.networkProfiles) {
            if (profile.ssid) known.insert(ToUtf8(profile.ssid));
        }
        return known;
    }

    CWNetwork* FindNetwork(CWInterface* interface, const std::string& ssid, bool scan) {
        NSSet<CWNetwork*>* networks = interface.cachedScanResults;
        if (scan || networks.count == 0) {
            NSError* error = nil;
            NSSet<CWNetwork*>* scanned = [interface scanForNetworksWithName:nil error:&error];
            if (scanned) networks = scanned;
        }
        CWNetwork* best = nil;
        for (CWNetwork* network in networks) {
            if (ToUtf8(network.ssid) != ssid) continue;
            if (!best || network.rssiValue > best.rssiValue) best = network;
        }
        return best;
    }
#endif

#ifdef ULTRACANVAS_WIRELESS_HAS_IOBLUETOOTH
    constexpr auto kDiscoveryLength = std::chrono::seconds(60);

    UCWirelessInquiryDelegate* TheInquiry() {
        static UCWirelessInquiryDelegate* delegate = [[UCWirelessInquiryDelegate alloc] init];
        return delegate;
    }

    std::string KindOfClass(BluetoothClassOfDevice cod) {
        const unsigned major = (cod >> 8) & 0x1F;
        const unsigned minor = (cod >> 2) & 0x3F;
        switch (major) {
            case 1: return "Computer";
            case 2: return "Phone";
            case 4:
                if (minor == 1 || minor == 2) return "Headset";
                if (minor == 6) return "Headphones";
                return "Speaker";
            case 5: {
                const unsigned pointer = (cod >> 6) & 0x3;
                const unsigned sub = (cod >> 2) & 0xF;
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

    WirelessBluetoothDevice DeviceOf(IOBluetoothDevice* device) {
        WirelessBluetoothDevice out;
        out.address = ToUtf8(device.addressString);
        out.name = ToUtf8(device.name);
        out.kind = KindOfClass(device.classOfDevice);
        out.paired = device.isPaired;
        out.connected = device.isConnected;
        out.trusted = out.paired;
        return out;
    }

    // "AA:BB:..." (the core's form) -> the "aa-bb-..." IOBluetooth takes.
    IOBluetoothDevice* DeviceFor(const std::string& address, std::string& error) {
        std::string dashed = address;
        std::replace(dashed.begin(), dashed.end(), ':', '-');
        std::transform(dashed.begin(), dashed.end(), dashed.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        IOBluetoothDevice* device = [IOBluetoothDevice deviceWithAddressString:@(dashed.c_str())];
        if (!device) error = "The device " + address + " is not known to Bluetooth; look for devices first.";
        return device;
    }

    using PowerSetter = int (*)(int);
    using PowerGetter = int (*)();

    PowerSetter PowerSetterFunction() {
        return reinterpret_cast<PowerSetter>(dlsym(RTLD_DEFAULT, "IOBluetoothPreferenceSetControllerPowerState"));
    }
#endif

} // namespace

std::string BackendName() { return "corewlan+iobluetooth"; }

// ===== WI-FI =====

WirelessWifiStatus ReadWifi() {
    WirelessWifiStatus status;
#ifdef ULTRACANVAS_WIRELESS_HAS_COREWLAN
    @autoreleasepool {
        CWInterface* interface = WifiInterface(status.error);
        status.available = true;
        if (!interface) return status;
        status.adapterPresent = true;
        status.adapterName = ToUtf8(interface.interfaceName);
        status.enabled = interface.powerOn;
        if (!status.enabled) return status;
        const bool located = LocationAllowed();
        status.connectedSsid = ToUtf8(interface.ssid);
        const std::set<std::string> known = KnownSsids(interface);
        size_t nameless = 0;
        for (CWNetwork* found in interface.cachedScanResults) {
            WirelessNetwork network;
            network.ssid = ToUtf8(found.ssid);
            if (network.ssid.empty()) {
                ++nameless;
                continue;
            }
            network.signalPercent = PercentOfRssi(found.rssiValue);
            network.security = SecurityOf(found);
            network.secured = network.security != "Open";
            network.known = known.count(network.ssid) > 0;
            network.connected = !status.connectedSsid.empty() && network.ssid == status.connectedSsid;
            status.networks.push_back(network);
        }
        if (status.networks.empty() && (nameless > 0 || !located)) {
            status.error = "macOS shows the names of Wi-Fi networks only to applications allowed to use Location "
                           "Services: allow this one in System Settings > Privacy & Security > Location Services.";
        }
    }
#else
    status.error = "This build has no Wi-Fi support (CoreWLAN was missing).";
#endif
    return status;
}

bool RequestWifiScan(std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_COREWLAN
    @autoreleasepool {
        CWInterface* interface = WifiInterface(error);
        if (!interface) return false;
        NSError* failure = nil;
        if (![interface scanForNetworksWithName:nil error:&failure]) {
            error = "The Wi-Fi adapter could not scan: " + Describe(failure);
            return false;
        }
        return true;
    }
#else
    error = "This build has no Wi-Fi support.";
    return false;
#endif
}

bool SetWifiEnabled(bool enabled, std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_COREWLAN
    @autoreleasepool {
        CWInterface* interface = WifiInterface(error);
        if (!interface) return false;
        NSError* failure = nil;
        if (![interface setPower:enabled error:&failure]) {
            error = std::string("Wi-Fi could not be switched ") + (enabled ? "on: " : "off: ") + Describe(failure);
            return false;
        }
        return true;
    }
#else
    (void)enabled;
    error = "This build has no Wi-Fi support.";
    return false;
#endif
}

bool ConnectWifi(const std::string& ssid, const std::string& passphrase, std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_COREWLAN
    @autoreleasepool {
        CWInterface* interface = WifiInterface(error);
        if (!interface) return false;
        CWNetwork* network = FindNetwork(interface, ssid, false);
        if (!network) network = FindNetwork(interface, ssid, true);
        if (!network) {
            error = ssid + " is not in reach. Scan again, or move closer to it.";
            return false;
        }
        const std::string security = SecurityOf(network);
        if (security == "Enterprise") {
            error = ssid + " asks for a user name and certificate (WPA Enterprise); join it from System Settings.";
            return false;
        }
        NSString* password = passphrase.empty() ? nil : @(passphrase.c_str());
        if (!password && security != "Open") {
            // A known network: the password the system keychain keeps.
            NSString* stored = nil;
            NSData* ssidData = [NSData dataWithBytes:ssid.data() length:ssid.size()];
            if (CWKeychainFindWiFiPassword(kCWKeychainDomainSystem, ssidData, &stored) == noErr && stored) {
                password = stored;
            } else {
                error = ssid + " needs its password.";
                return false;
            }
        }
        NSError* failure = nil;
        if (![interface associateToNetwork:network password:password error:&failure]) {
            error = security == "Open" ? "Could not join " + ssid + ": " + Describe(failure)
                                       : "Could not join " + ssid + " - is the password right? (" +
                                             Describe(failure) + ")";
            return false;
        }
        return true;
    }
#else
    (void)ssid;
    (void)passphrase;
    error = "This build has no Wi-Fi support.";
    return false;
#endif
}

bool DisconnectWifi(std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_COREWLAN
    @autoreleasepool {
        CWInterface* interface = WifiInterface(error);
        if (!interface) return false;
        [interface disassociate];
        return true;
    }
#else
    error = "This build has no Wi-Fi support.";
    return false;
#endif
}

bool ForgetWifi(const std::string& ssid, std::string& error) {
    error = "macOS forgets " + ssid + " only with an administrator's permission: use System Settings > Wi-Fi.";
    return false;
}

// ===== BLUETOOTH =====

WirelessBluetoothStatus ReadBluetooth() {
    WirelessBluetoothStatus status;
#ifdef ULTRACANVAS_WIRELESS_HAS_IOBLUETOOTH
    @autoreleasepool {
        status.available = true;
        IOBluetoothHostController* controller = [IOBluetoothHostController defaultController];
        if (!controller) {
            status.error = "There is no Bluetooth adapter.";
            return status;
        }
        status.adapterPresent = true;
        status.adapterName = ToUtf8(controller.nameAsString);
        status.powered = controller.powerState == kBluetoothHCIPowerStateON;
        UCWirelessInquiryDelegate* inquiry = TheInquiry();
        status.discovering = inquiry.running;
        std::set<std::string> listed;
        for (IOBluetoothDevice* device in [IOBluetoothDevice pairedDevices]) {
            WirelessBluetoothDevice entry = DeviceOf(device);
            if (listed.insert(entry.address).second) status.devices.push_back(entry);
        }
        __block NSArray* found = nil;
        OnMain(^{
            found = [inquiry.inquiry.foundDevices copy];
        });
        for (IOBluetoothDevice* device in found) {
            WirelessBluetoothDevice entry = DeviceOf(device);
            if (listed.insert(entry.address).second) status.devices.push_back(entry);
        }
    }
#else
    status.error = "This build has no Bluetooth support (IOBluetooth was missing).";
#endif
    return status;
}

bool CanSetBluetoothPower() {
#ifdef ULTRACANVAS_WIRELESS_HAS_IOBLUETOOTH
    return PowerSetterFunction() != nullptr;
#else
    return false;
#endif
}

bool SetBluetoothPowered(bool powered, std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_IOBLUETOOTH
    PowerSetter setter = PowerSetterFunction();
    if (!setter) {
        error = "This macOS switches Bluetooth only in System Settings.";
        return false;
    }
    setter(powered ? 1 : 0);
    // The controller takes a moment; give it up to three seconds.
    for (int i = 0; i < 30; ++i) {
        @autoreleasepool {
            IOBluetoothHostController* controller = [IOBluetoothHostController defaultController];
            if (controller && (controller.powerState == kBluetoothHCIPowerStateON) == powered) return true;
        }
        [NSThread sleepForTimeInterval:0.1];
    }
    error = std::string("Bluetooth did not switch ") + (powered ? "on." : "off.");
    return false;
#else
    (void)powered;
    error = "This build has no Bluetooth support.";
    return false;
#endif
}

bool StartBluetoothDiscovery(std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_IOBLUETOOTH
    UCWirelessInquiryDelegate* inquiry = TheInquiry();
    __block IOReturn result = kIOReturnSuccess;
    void (^start)(void) = ^{
        if (inquiry.running) return;
        inquiry.until = [NSDate dateWithTimeIntervalSinceNow:std::chrono::duration<double>(kDiscoveryLength).count()];
        inquiry.inquiry = [IOBluetoothDeviceInquiry inquiryWithDelegate:inquiry];
        [inquiry.inquiry setInquiryLength:10];
        [inquiry.inquiry setUpdateNewDeviceNames:YES];
        result = [inquiry.inquiry start];
        inquiry.running = result == kIOReturnSuccess;
    };
    OnMain(start);
    if (result != kIOReturnSuccess) {
        error = "Could not look for devices - is Bluetooth on?";
        return false;
    }
    return true;
#else
    error = "This build has no Bluetooth support.";
    return false;
#endif
}

bool StopBluetoothDiscovery(std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_IOBLUETOOTH
    (void)error;
    UCWirelessInquiryDelegate* inquiry = TheInquiry();
    void (^stop)(void) = ^{
        inquiry.until = [NSDate date];
        [inquiry.inquiry stop];
        inquiry.running = NO;
    };
    OnMain(stop);
    return true;
#else
    error = "This build has no Bluetooth support.";
    return false;
#endif
}

bool ConnectBluetoothDevice(const std::string& address, std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_IOBLUETOOTH
    @autoreleasepool {
        IOBluetoothDevice* device = DeviceFor(address, error);
        if (!device) return false;
        const IOReturn result = [device openConnection];
        if (result != kIOReturnSuccess) {
            error = "Could not connect. Is the device switched on and in reach?";
            return false;
        }
        return true;
    }
#else
    (void)address;
    error = "This build has no Bluetooth support.";
    return false;
#endif
}

bool PairBluetoothDevice(const std::string& address, std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_IOBLUETOOTH
    if ([NSThread isMainThread]) {
        error = "Pairing waits for the device: call it from a worker thread.";
        return false;
    }
    std::string ignored;
    StopBluetoothDiscovery(ignored);
    @autoreleasepool {
        IOBluetoothDevice* device = DeviceFor(address, error);
        if (!device) return false;
        if (!device.isPaired) {
            UCWirelessPairDelegate* delegate = [[UCWirelessPairDelegate alloc] init];
            delegate.done = dispatch_semaphore_create(0);
            delegate.result = kIOReturnError;
            __block IOBluetoothDevicePair* pair = nil;
            __block IOReturn started = kIOReturnSuccess;
            dispatch_sync(dispatch_get_main_queue(), ^{
                pair = [IOBluetoothDevicePair pairWithDevice:device];
                pair.delegate = delegate;
                started = [pair start];
            });
            if (started != kIOReturnSuccess) {
                error = "Could not pair. Is the device in pairing mode?";
                return false;
            }
            const long waited = dispatch_semaphore_wait(delegate.done, dispatch_time(DISPATCH_TIME_NOW, 60 * NSEC_PER_SEC));
            dispatch_sync(dispatch_get_main_queue(), ^{
                [pair stop];
                pair.delegate = nil;
            });
            if (waited != 0 || delegate.result != kIOReturnSuccess) {
                error = waited != 0 ? "The device did not answer. Is it in pairing mode?"
                                    : "Could not pair. Is the device in pairing mode?";
                return false;
            }
        }
        if ([device openConnection] != kIOReturnSuccess) {
            error = "Paired, but could not connect.";
            return false;
        }
        return true;
    }
#else
    (void)address;
    error = "This build has no Bluetooth support.";
    return false;
#endif
}

bool DisconnectBluetoothDevice(const std::string& address, std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_IOBLUETOOTH
    @autoreleasepool {
        IOBluetoothDevice* device = DeviceFor(address, error);
        if (!device) return false;
        if (!device.isConnected) return true;
        if ([device closeConnection] != kIOReturnSuccess) {
            error = "Could not disconnect the device.";
            return false;
        }
        return true;
    }
#else
    (void)address;
    error = "This build has no Bluetooth support.";
    return false;
#endif
}

bool RemoveBluetoothDevice(const std::string& address, std::string& error) {
#ifdef ULTRACANVAS_WIRELESS_HAS_IOBLUETOOTH
    @autoreleasepool {
        IOBluetoothDevice* device = DeviceFor(address, error);
        if (!device) return false;
        SEL remove = NSSelectorFromString(@"remove");
        if (![device respondsToSelector:remove]) {
            error = "This macOS removes a device only in System Settings > Bluetooth.";
            return false;
        }
        if (device.isConnected) [device closeConnection];
        reinterpret_cast<void (*)(id, SEL)>(objc_msgSend)(device, remove);
        return true;
    }
#else
    (void)address;
    error = "This build has no Bluetooth support.";
    return false;
#endif
}

// ===== THE SYSTEM'S OWN SETTINGS =====

bool HasSystemSettings(WirelessRadio) { return true; }

bool OpenSystemSettings(WirelessRadio radio, std::string& error) {
    NSString* page = radio == WirelessRadio::WiFi ? @"x-apple.systempreferences:com.apple.wifi-settings-extension"
                                                  : @"x-apple.systempreferences:com.apple.BluetoothSettings";
    if (OpenUrl(page)) return true;
    // macOS 12 and older name the panes differently.
    NSString* legacy = radio == WirelessRadio::WiFi ? @"x-apple.systempreferences:com.apple.preference.network"
                                                    : @"x-apple.systempreferences:com.apple.preferences.Bluetooth";
    if (OpenUrl(legacy)) return true;
    error = "System Settings did not open.";
    return false;
}

} // namespace WirelessBackend
} // namespace UltraCanvas

#ifdef ULTRACANVAS_WIRELESS_HAS_IOBLUETOOTH

@implementation UCWirelessInquiryDelegate

- (void)deviceInquiryComplete:(IOBluetoothDeviceInquiry*)sender error:(IOReturn)error aborted:(BOOL)aborted {
    (void)error;
    // An inquiry lasts ten seconds; run another until the minute is up.
    if (!aborted && self.until && [self.until timeIntervalSinceNow] > 0 && sender == self.inquiry) {
        [sender clearFoundDevices];
        if ([sender start] == kIOReturnSuccess) return;
    }
    self.running = NO;
}

@end

@implementation UCWirelessPairDelegate

- (void)devicePairingFinished:(id)sender error:(IOReturn)error {
    (void)sender;
    self.result = error;
    dispatch_semaphore_signal(self.done);
}

- (void)devicePairingUserConfirmationRequest:(id)sender numericValue:(BluetoothNumericValue)numericValue {
    (void)numericValue;
    // The person asked to pair this device; the comparison has no screen
    // here to show on.
    [sender replyUserConfirmation:YES];
}

@end

#endif
