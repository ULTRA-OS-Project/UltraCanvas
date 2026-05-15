# IODeviceManager - Universal Hardware Device Management for Modern Applications

## Unify Every Device Behind a Single API

**IODeviceManager** is a comprehensive, cross-platform hardware abstraction layer that eliminates the complexity of working with diverse input/output devices. Whether you're building scanning workflows, imaging applications, document processing systems, or full-featured operating system services, IODeviceManager provides a single, unified API to discover, configure, and operate hardware across dozens of protocols and platforms.

---

## Purpose

Stop wrestling with platform-specific SDKs, proprietary drivers, and dozens of device APIs. IODeviceManager gives you:

- **One API for Every Device** - Scanners, cameras, printers, audio devices, GPIO, and network hardware through a single, consistent interface
- **Automatic Protocol Selection** - Native protocols (SANE, WIA, TWAIN, ICA, V4L2, AVFoundation, MediaFoundation, CUPS) selected transparently per platform
- **Effortless Device Discovery** - Enumerate, hot-plug, and connect to local USB, network, and embedded devices automatically
- **Cross-Platform Compatibility** - Same code runs on Linux, Windows, macOS, and ULTRA OS

---

## Key Functionality

### Universal Device Enumeration
Discover every connected device with one simple call. IODeviceManager automatically detects available hardware and applies the appropriate native driver:

```cpp
auto& manager = IODeviceManager::GetInstance();
manager.EnumerateDevices(IODeviceCategory::Scanner);
manager.EnumerateDevices(IODeviceCategory::Camera);
manager.EnumerateDevices(IODeviceCategory::Printer);
```

### Seamless Scanner Operation
Acquire images from any scanner — flatbed, ADF, network — without writing protocol-specific code. Perfect for document workflows, archival, and capture pipelines:

```cpp
auto scanner = manager.GetDeviceByProtocol(ScannerProtocol::Auto, 0);
scanner->Connect();

ScanConfiguration config;
config.resolution = 300;
config.colorMode = ScanColorMode::RGB;

std::vector<uint8_t> imageData;
scanner->Scan(config, imageData);
```

### Comprehensive Camera Support
Stream video, capture stills, and control PTZ from webcams, DSLRs, and network cameras through one unified interface:

```cpp
// Webcams, DSLRs, IP cameras — all the same API
auto camera = manager.GetDeviceByCategory(IODeviceCategory::Camera, 0);
camera->StartStream();
camera->CapturePhoto("snapshot.jpg");
camera->SetPTZ(pan, tilt, zoom);  // Network cameras
```

### Network Device Discovery
Automatically find and connect to network-based devices using mDNS, Bonjour, ONVIF, and UPnP — no manual IP configuration required:

```cpp
// Driverless network scanners (eSCL/AirPrint/Mopria)
manager.AddeSCLScanner("192.168.1.50", 80);

// ONVIF network cameras with auto-discovery
manager.DiscoverNetworkCameras();
```

### Extensible Plugin Architecture
Add support for proprietary or specialized hardware without modifying core code:

```cpp
// Register your custom device handler
auto customDevice = std::make_shared<MyDevicePlugin>();
manager.RegisterDevice(customDevice);
```

---

## Supported Device Categories

| Category | Description | Examples | Status |
|----------|-------------|----------|--------|
| **Scanner** | Document and image scanners | Flatbed, ADF, film, network scanners | ✅ Production |
| **Camera** | Still and video imaging devices | Webcams, DSLRs, IP cameras | ✅ Production |
| **NetworkCamera** | IP-based cameras with PTZ | ONVIF, RTSP, HTTP cameras | ✅ Production |
| **Printer** | Local and network printers | Inkjet, laser, label, 3D printers | 🚧 In Progress |
| **GPIO** | General purpose I/O pins | Raspberry Pi, Arduino, embedded boards | 🚧 In Progress |
| **Microphone** | Audio input devices | USB mics, headset mics, XLR interfaces | 📋 Planned |
| **Speaker** | Audio output devices | Speakers, headphones, monitors | 📋 Planned |
| **Storage** | Removable storage media | USB drives, SD cards, optical drives | 📋 Planned |
| **Keyboard** | Keyboard input devices | USB, Bluetooth, virtual keyboards | 📋 Planned |
| **Mouse** | Pointing devices | Mice, trackpads, trackballs | 📋 Planned |
| **Touchscreen** | Touch input surfaces | Capacitive, resistive displays | 📋 Planned |
| **Stylus** | Pen and stylus input | Graphics tablets, digitizers | 📋 Planned |
| **Gamepad** | Game controllers | Gamepads, joysticks, racing wheels | 📋 Planned |
| **Serial** | Serial communication | COM ports, USB-Serial adapters | 📋 Planned |
| **Bluetooth** | Bluetooth devices | BLE peripherals, classic Bluetooth | 📋 Planned |
| **Barcode** | Barcode scanners | Handheld, embedded, 2D scanners | 📋 Planned |
| **RFID** | RFID/NFC readers | Proximity, smart card readers | 📋 Planned |
| **Biometric** | Biometric input devices | Fingerprint, face recognition | 📋 Planned |
| **Custom** | User-defined devices | Plugin-based custom hardware | ✅ Available |

---

## Supported Protocols by Device Category

| Device Category | Linux | Windows | macOS | Network |
|-----------------|-------|---------|-------|---------|
| **Scanner** | SANE | WIA, TWAIN | ICA | eSCL (AirPrint/Mopria) |
| **Camera (Webcam)** | V4L2 | MediaFoundation | AVFoundation | — |
| **Camera (DSLR)** | libgphoto2 | WIA | ImageCapture | — |
| **Network Camera** | RTSP, ONVIF | RTSP, ONVIF | RTSP, ONVIF | RTSP, ONVIF, mDNS, UPnP |
| **Printer** | CUPS | Windows Print API, GDI | CUPS | IPP, SNMP, Bonjour |
| **Microphone** | ALSA, PulseAudio | WASAPI | CoreAudio | — |
| **Speaker** | ALSA, PulseAudio | WASAPI | CoreAudio | — |
| **Storage** | udev | Windows Storage API | DiskArbitration | — |
| **Network Adapter** | NetworkManager | Windows Network API | SystemConfiguration | — |
| **GPIO** | libgpiod, sysfs | — | — | — |
| **Serial** | termios | Win32 Serial API | IOKit Serial | — |
| **Bluetooth** | BlueZ | Windows Bluetooth API | CoreBluetooth | — |

*...and easily extensible for proprietary protocols and custom hardware*

---

## Why IODeviceManager?

### For Developers
- **Reduce Development Time** - Stop integrating dozens of vendor SDKs and platform APIs
- **Single Source of Truth** - One module handles all hardware operations consistently
- **Automatic Platform Routing** - Linux, Windows, macOS handled transparently
- **Production-Grade Reliability** - Thread-safe, RAII-managed, with comprehensive error handling

### For Applications
- **Hardware Independence** - Same code works with any compatible device
- **Future-Proof Architecture** - Plugin system adapts to new protocols and standards
- **Enterprise Integration** - Network discovery, capability queries, hot-plug support
- **Better Performance** - Optimized native protocols and efficient resource management

### For Users
- **Plug-and-Play Experience** - Devices detected and ready to use without configuration
- **Universal Compatibility** - One application supports every scanner, camera, or printer they own
- **Reliable Operation** - Robust error handling and automatic recovery
- **Modern Features** - Network device support, driverless scanning, ONVIF cameras out of the box

---

## Perfect For

✅ **Document Management Systems** - Scan-to-file, OCR pipelines, digital archiving
✅ **Imaging Applications** - Photo capture, multi-camera systems, video surveillance
✅ **Print Services** - Local and network printing, supply monitoring, fleet management
✅ **Industrial Software** - Embedded device control, GPIO automation, SCADA systems
✅ **Operating System Services** - Device managers, hardware control panels, system monitors
✅ **Enterprise Software** - Multi-platform business applications with diverse hardware needs

---

## Architecture Highlights

- **Cross-Platform Abstraction** - Unified API in `/include/`, platform-specific implementations in `/OS/[Platform]/`
- **Extensible Base Class** - `IODevice` base with category-specific derived classes (`ScannerDevice`, `CameraDevice`, `PrinterDevice`)
- **Transparent Protocol Selection** - Right driver chosen automatically per platform
- **Singleton Manager** - Single `IODeviceManager` instance manages all device categories
- **Plugin-Based Drivers** - Add new protocols without touching core code

---

## Get Started

IODeviceManager integrates seamlessly into any C++20 application:

```cpp
// Initialize once
auto& manager = IODeviceManager::GetInstance();
manager.Initialize();

// Discover devices
manager.EnumerateDevices(IODeviceCategory::Scanner);

// Use any compatible device
auto device = manager.GetDeviceByProtocol(ScannerProtocol::Auto, 0);
if (device && device->Connect()) {
    // Operate device through unified API
    device->Disconnect();
}

// Clean shutdown
manager.Shutdown();
```

---

## The Bottom Line

**Stop managing hardware. Start building features.**

IODeviceManager handles the complexity of device communication so you can focus on what makes your application unique. With comprehensive protocol support, automatic platform routing, and a simple unified API, IODeviceManager is the universal hardware management solution for modern C++ applications.

**Universal. Unified. Cross-Platform.**

*IODeviceManager - Part of the UltraCanvas Framework*
