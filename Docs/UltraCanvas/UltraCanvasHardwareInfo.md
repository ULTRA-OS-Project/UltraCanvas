# UltraCanvasHardwareInfo — the machine, described

`UltraCanvasHardwareInfo` answers "what is this computer made of, and how is it
doing?": CPU (with cache sizes and temperature), GPU, NPU, memory (down to the
individual module), storage (with connector, on-drive cache and temperature),
network interfaces including Wi-Fi association, USB controllers and everything
plugged into them, and Bluetooth adapters with their live connections.

Everything here is a **read**. Nothing is opened, configured, claimed or
operated.

Headers: `UltraCanvasHardwareInfo.h` (the data and the API),
`UltraCanvasHardwareInfoPanel.h` (a ready-made view).

## Why this is not part of IODeviceManager

The two modules sound adjacent and are not. **IODeviceManager** operates
peripherals: it enumerates scanners, cameras and printers, connects to one,
configures it, transfers data and disconnects — handles, ownership, protocol
drivers (SANE, TWAIN, WIA, ONVIF, V4L2, CUPS), hot-plug callbacks, a device
lifecycle. **UltraCanvasHardwareInfo** describes the host: an inventory plus the
sensors that report on it, with no handle to hold and nothing to release.

They differ in every dimension that matters for a module boundary:

| | IODeviceManager | UltraCanvasHardwareInfo |
|---|---|---|
| Verb | connect, configure, scan, capture, print | read |
| State | live device handles, sessions, callbacks | none — a value snapshot |
| Failure | a device error the caller must handle | a missing value with a reason |
| Dependencies | vendor SDKs and protocol stacks | the OS's own interfaces only |
| Caller | a scanning or capture workflow | a settings screen, an about box, a support bundle |

Folding the inventory into the device manager would put a permanently-loaded
read-only service behind a singleton that owns driver stacks, and would make
"show me the CPU temperature" depend on SANE being installed. They do meet in
one place — a USB device appears in both — and there the split is still clean:
this module reports *that a device is attached and what it is*; IODeviceManager
is what you use to *talk to it*.

## Quick start

```cpp
#include "UltraCanvasHardwareInfo.h"
using namespace UltraCanvas;

HardwareSnapshot machine = UltraCanvasHardwareInfo::Capture();

std::printf("%s, %d cores / %d threads\n",
            machine.cpu.model.c_str(), machine.cpu.physicalCores, machine.cpu.logicalCores);
std::printf("L3 cache: %s\n",
            UltraCanvasHardwareInfo::FormatBytes(machine.cpu.TotalCacheSize(3)).c_str());
if (machine.cpu.temperatureC)
    std::printf("CPU is at %s\n",
                UltraCanvasHardwareInfo::FormatTemperature(*machine.cpu.temperatureC).c_str());

for (const auto& drive : machine.storage) {
    std::string line = drive.model + "  " +
                       UltraCanvasHardwareInfo::FormatBytes(drive.capacityBytes) + "  " +
                       drive.connector;
    if (drive.temperatureC)
        line += "  " + UltraCanvasHardwareInfo::FormatTemperature(*drive.temperatureC);
    std::printf("%s\n", line.c_str());
}
```

Or ask for one category:

```cpp
CPUInfo cpu = UltraCanvasHardwareInfo::GetCPU();
std::vector<GPUInfo> adapters = UltraCanvasHardwareInfo::ListGPUs();
std::vector<USBDeviceInfo> plugged = UltraCanvasHardwareInfo::ListUSBDevices();
```

## Capturing

```cpp
static HardwareSnapshot Capture(HardwareQuery what = HardwareQuery::All,
                                bool forceRefresh = false);
static void RefreshSensors(HardwareSnapshot& snapshot);
```

`HardwareQuery` is a bit set — `System`, `CPU`, `GPU`, `NPU`, `Memory`,
`Storage`, `Network`, `USB`, `Bluetooth`, plus `Sensors` — because probing costs
differ by orders of magnitude: reading the CPU topology is microseconds, walking
the USB tree is milliseconds.

```cpp
auto quick = UltraCanvasHardwareInfo::Capture(HardwareQuery::CPU |
                                              HardwareQuery::Memory |
                                              HardwareQuery::Sensors);
```

`snapshot.Has(HardwareQuery::USB)` tells you whether a category was actually
filled, so an empty vector never has to be read as "no such hardware".

**`RefreshSensors` is the call a monitor loop makes.** It re-reads temperatures,
clocks, utilisation, free memory and link state into an existing snapshot
without walking the device trees again, and it never adds or removes a device —
indices into `storage`, `network` and `usbDevices` stay valid across it.

Snapshots are cached for `HardwareInfoOptions::cacheLifetimeMs` (2 s by
default), so several widgets asking at once cost one probe. `forceRefresh`
bypasses that.

## Options

```cpp
HardwareInfoOptions options;
options.maskIdentifiers = true;   // default
options.includeSensors  = true;   // default
options.includeUsbHubs  = false;  // default
options.cacheLifetimeMs = 2000;   // default; 0 disables caching
UltraCanvasHardwareInfo::SetOptions(options);
```

**Identifiers are masked by default.** Serial numbers, MAC addresses and BSSIDs
identify the machine and its owner's network, and a hardware panel is exactly
the kind of screen that gets photographed or pasted into a bug report. Masking
keeps the last four characters of a serial (`**********3456`) and the last two
octets of an address (`**:**:**:**:34:56`) — enough to tell two drives apart,
not enough to identify the machine. `UltraCanvasHardwareInfo::MaskIdentifier`
applies the same rule to a string of your own. Turning masking off is a
deliberate call, and it clears the snapshot cache so nothing masked is reused.

## What a snapshot holds

| Struct | Notable fields |
|---|---|
| `SystemInfo` | host name, OS and kernel, vendor/model, mainboard, firmware, chassis, uptime |
| `CPUInfo` | vendor, model, architecture, packages/cores/threads, base/max/current clock, `caches`, `coreGroups` (hybrid P/E tiers), `instructionSets`, `temperatureC`, `loadPercent` |
| `CPUCacheInfo` | level, type, size, line size, associativity, `instanceCount`, `sharedByLogicalCores`, `Describe()` |
| `GPUInfo` | vendor, model, driver, PCI ids, `kind` (integrated/discrete/virtual/software), VRAM, compute units, clock, temperature, utilisation |
| `NPUInfo` | vendor, model, driver, `runtime` (OpenVINO / DirectML / Core ML / RKNN …), integrated flag |
| `MemoryInfo` | installed / available / used, swap, page size, slots, `modules` |
| `MemoryModuleInfo` | slot locator, type (DDR4/DDR5/LPDDR5), form factor, manufacturer, part and serial numbers, configured and rated MT/s, width and ECC, voltage |
| `StorageDeviceInfo` | device path, model, firmware, serial, `bus`, `media`, `connector`, capacity, on-drive `cacheBytes`, rotation, sector sizes, `temperatureC`, `volumes` |
| `NetworkInterfaceInfo` | name, description, MAC, driver, `type`, up/carrier, link speed, MTU, IPv4/IPv6, counters, `wifi` |
| `WiFiInfo` | SSID, BSSID, security, standard, band, channel, frequency, signal (% and dBm), TX/RX rate |
| `USBControllerInfo` / `USBDeviceInfo` | controller name, USB version, port count / VID:PID, names, class, speed, port path, bus power, hub flag, parent index |
| `BluetoothAdapterInfo` / `BluetoothDeviceInfo` | adapter name, address, power, discoverability / device name, address, class, paired, connected, RSSI, battery |

Optional readings use `std::optional`, so "0 °C" is never confused with "not
reported".

### `warnings` is part of the answer

A capture always returns a snapshot. When a value cannot be read, the reason
goes into `snapshot.warnings` in words a user can act on:

```
Drive cache size is unavailable: reading it needs ATA IDENTIFY on the block
device, which requires root (try the disk group or run as root).
```

The panel shows those under a **Notes** section. Prefer them to inventing a
placeholder.

## Platform coverage

Backends live in `OS/<Platform>/UltraCanvas*HardwareInfo.cpp` behind the
internal `UltraCanvasHardwareInfoBackend.h`. `GetBackendName()` returns
`"sysfs"`, `"win32"`, `"iokit"` or `"null"`.

| | Linux (`sysfs`) | Windows (`win32`) | macOS (`iokit`) |
|---|---|---|---|
| System, board, firmware | DMI under `/sys/class/dmi/id` | registry + SMBIOS | sysctl + device tree |
| CPU topology, caches | `/proc/cpuinfo`, `cpu*/cache` | `GetLogicalProcessorInformationEx` | `hw.*` sysctls |
| CPU clocks | `cpufreq` | `CallNtPowerInformation` | `hw.cpufrequency*` (Intel), perf levels (Apple Silicon) |
| CPU temperature | ✅ hwmon / thermal zones | ✖ needs WMI | ✖ private frameworks |
| Hybrid P/E cores | ✅ | ✅ (efficiency class) | ✅ (performance levels) |
| Memory totals | ✅ | ✅ | ✅ |
| Memory modules | root only (DMI) | ✅ `GetSystemFirmwareTable` | ✖ |
| GPU | ✅ DRM + PCI | ✅ display class registry | ✅ IOKit |
| GPU temperature / load | ✅ amdgpu, hwmon | ✖ vendor libraries | ✖ |
| NPU | ✅ `/sys/class/accel`, PCI class 12h | ✅ compute-accelerator class | ✅ Neural Engine service |
| Storage inventory | ✅ | ✅ storage IOCTLs | ✅ IOMedia |
| Storage connector | ✅ PCIe gen/width, SATA speed, USB speed | bus type | interconnect + location |
| Storage temperature | ✅ nvme / drivetemp hwmon | ✅ temperature IOCTL (Win10 1709+) | ✖ |
| Drive cache size | root only (ATA IDENTIFY) | ✖ | ✖ |
| Network | ✅ | ✅ IP Helper | ✅ getifaddrs |
| Wi-Fi association | ✅ wireless extensions | ✅ WLAN API (incl. security, PHY) | ✖ needs CoreWLAN |
| USB tree | ✅ full, with speed and power | ✅ names, class, location | ✅ names, speed, class |
| Bluetooth adapters | ✅ | ✅ | ✅ |
| Bluetooth devices | live connections only | ✅ paired and connected | ✖ needs IOBluetooth |

Everything marked ✖ produces a warning naming the interface that would be
needed, not a zero. Platforms without a native backend (WASM, Android, the
BSDs) link a fallback that reports what the C++ runtime knows and says so.

No new third-party dependency is introduced on any platform: the Linux backend
reads procfs and sysfs, Windows uses documented Win32 (registry, SMBIOS,
storage IOCTLs, IP Helper, WLAN, SetupAPI, Bluetooth — no COM or WMI), and
macOS uses sysctl and the IOKit C API.

## Displaying it: `UltraCanvasHardwareInfoPanel`

A system-information view, ready to drop into a settings screen, an about box
or an OS control panel. It is an `UltraCanvasColumnsTreeView` that fills itself
from a snapshot — nothing in it is hand-painted.

```cpp
#include "UltraCanvasHardwareInfoPanel.h"

auto panel = CreateHardwareInfoPanel("SystemInfo", 0, 0, 640, 480);
window->AddChild(panel);

// Live temperatures and clocks, without disturbing the user's place in the tree.
auto* app = UltraCanvasApplication::GetInstance();
TimerId sensorTimer = app->StartTimer(2000, /*periodic*/ true,
                                      [panel](TimerId) { panel->RefreshSensors(); });
```

| Call | Does |
|---|---|
| `Refresh(force)` | re-probes and rebuilds, keeping open sections open |
| `RefreshSensors()` | re-reads live values into the existing rows — expansion, selection and scroll position all survive |
| `SetQuery(what)` | narrows what is shown (a storage-only panel, say) |
| `SetSnapshot(snapshot)` | shows a snapshot captured elsewhere — another thread, or a saved support bundle |
| `SetSectionsExpanded(bool)` | whether top-level categories start open |
| `ToText()` / `ToJSON()` | export what is on screen |
| `onSnapshotChanged` | fires after every capture |

Categories are section rows; each device (drive, adapter, USB device, memory
module) is a foldable row of its own; long values carry the full form on the
tooltip.

## Exporting

```cpp
std::string report = UltraCanvasHardwareInfo::ToText(machine);   // for a text box or a log
std::string json   = UltraCanvasHardwareInfo::ToJSON(machine);   // for a support bundle
```

`ToText` and the panel are both renderings of
`BuildReport(snapshot)` → `std::vector<HardwarePropertyGroup>`, so a third
rendering of your own starts from the same rows and cannot drift from them.
`ToJSON` serialises the snapshot with real types (byte counts as numbers, not
formatted strings) through `UltraCanvasJSON`, so it round-trips.

## Formatting helpers

`FormatBytes`, `FormatFrequencyMHz`, `FormatTemperature`, `FormatBitrateMbps`
and `FormatDuration` are the ones the report itself uses — use them so a value
you format by hand reads the same as the rest of the panel.

## Threading

Capture is safe to call from any thread; the options and the snapshot cache are
mutex-guarded. A capture takes single-digit milliseconds for CPU and memory and
tens of milliseconds for a full sweep with a large USB tree, so a UI that
refreshes on a timer should either narrow the query or capture on a worker
thread and hand the result to the panel with `SetSnapshot`.

## Testing

`Tests/HardwareInfoTest.cpp` (`ctest -R HardwareInfoTests`) covers the
formatters, masking, the query selector, report shape, and the invariants a live
capture must hold on the machine it runs on — cores ≥ 1, physical ≤ logical,
available memory ≤ installed, a narrowed query touching nothing else, a sensor
refresh preserving the device lists, and the JSON export parsing back.
