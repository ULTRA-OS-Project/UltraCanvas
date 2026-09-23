// Tests/DeviceExplorerModelTest.cpp
// DeviceExplorer's model: the snapshot it takes of IODeviceManager's
// registry, the grouping that becomes the tree on the left, the filter, and
// the property sections the panel on the right lists.
//
// The parts worth guarding are the ones a user would read as a claim about
// their hardware: a category this build can search must show up even when it
// found nothing ("no scanners"), a category it cannot search must not (it
// would claim an emptiness nobody checked), a filter must not leave empty
// groups behind, and a serial number must go through the mask.
//
// Fake devices registered with the manager stand in for hardware, so it runs
// the same everywhere - no display, no printer, no camera.
// Version: 1.0.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework

#include "DeviceExplorerModel.h"

#include "IODeviceManager/UltraCanvasIODeviceManager.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace DeviceExplorer;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

class FakeDevice : public IODevice {
public:
    explicit FakeDevice(const IODeviceInfo& info) : IODevice(info) {}

protected:
    IODeviceResult DoConnect() override { return IODeviceResult::Ok(); }
    void DoDisconnect() override {}
};

IODeviceInfo MakeInfo(const std::string& id, const std::string& name,
                      IODeviceCategory category, IODeviceTransport transport,
                      const std::string& backend) {
    IODeviceInfo info;
    info.deviceId = id;
    info.name = name;
    info.category = category;
    info.transport = transport;
    info.backend = backend;
    return info;
}

const PropertySection* FindSection(const std::vector<PropertySection>& sections,
                                   const std::string& title) {
    for (const auto& section : sections) {
        if (section.title == title) return &section;
    }
    return nullptr;
}

std::string ValueOf(const PropertySection* section, const std::string& name) {
    if (!section) return {};
    for (const auto& row : section->rows) {
        if (row.name == name) return row.value;
    }
    return {};
}

const DeviceGroup* FindGroup(const std::vector<DeviceGroup>& groups, const std::string& title) {
    for (const auto& group : groups) {
        if (group.title == title) return &group;
    }
    return nullptr;
}

} // namespace

int main() {
    std::cout << "DeviceExplorerModelTest\n";
    auto& manager = IODeviceManager::GetInstance();
    manager.Shutdown();

    // Two backends: printers and scanners are searchable, cameras are not.
    manager.RegisterEnumerator(IODeviceCategory::Printer, "TestPrint",
                               [] { return std::vector<IODevicePtr>{}; });
    manager.RegisterEnumerator(IODeviceCategory::Scanner, "TestScan",
                               [] { return std::vector<IODevicePtr>{}; });

    IODeviceInfo laser = MakeInfo("p1", "Laser 400", IODeviceCategory::Printer,
                                  IODeviceTransport::Network, "TestPrint");
    laser.manufacturer = "Acme";
    laser.model = "L400";
    laser.serialNumber = "SN-1234567890";
    laser.location = "Reception";
    laser.attributes["ppd"] = "acme-l400.ppd";
    IODeviceInfo inkjet = MakeInfo("p2", "Inkjet 10", IODeviceCategory::Printer,
                                   IODeviceTransport::USB, "TestPrint");
    IODeviceInfo webcam = MakeInfo("c1", "", IODeviceCategory::Camera,
                                   IODeviceTransport::USB, "TestCam");
    webcam.manufacturer = "Acme";
    webcam.model = "Cam HD";

    Check(static_cast<bool>(manager.RegisterDevice(std::make_shared<FakeDevice>(laser))),
          "register the network printer");
    Check(static_cast<bool>(manager.RegisterDevice(std::make_shared<FakeDevice>(inkjet))),
          "register the USB printer");
    Check(static_cast<bool>(manager.RegisterDevice(std::make_shared<FakeDevice>(webcam))),
          "register the webcam");

    const DeviceInventory inventory = SnapshotInventory(manager);
    Check(inventory.devices.size() == 3, "snapshot holds every registered device");
    Check(inventory.backends.count(IODeviceCategory::Printer) == 1 &&
          inventory.backends.count(IODeviceCategory::Scanner) == 1 &&
          inventory.backends.count(IODeviceCategory::Camera) == 0,
          "snapshot records the backends per category");
    Check(inventory.CountIn(IODeviceCategory::Printer) == 2, "two printers counted");

    // ===== GROUPING =====
    std::cout << "Grouping\n";
    const auto byCategory = GroupDevices(inventory, DeviceGrouping::Category);
    Check(byCategory.size() == 3, "by category: printers, scanners, cameras");
    Check(!byCategory.empty() && byCategory[0].title == "Printers", "printers come first");
    const DeviceGroup* scanners = FindGroup(byCategory, "Scanners");
    Check(scanners && scanners->devices.empty(),
          "a searchable category with nothing found is still listed");
    const DeviceGroup* printers = FindGroup(byCategory, "Printers");
    Check(printers && printers->devices.size() == 2 &&
          printers->devices[0]->info.name == "Inkjet 10",
          "devices in a group are sorted by name");
    Check(FindGroup(byCategory, "Cameras") != nullptr,
          "a category with a device but no registered backend still shows the device");

    manager.UnregisterDevice("c1");
    const DeviceInventory withoutCamera = SnapshotInventory(manager);
    Check(FindGroup(GroupDevices(withoutCamera, DeviceGrouping::Category), "Cameras") == nullptr,
          "a category the build cannot search is not claimed to be empty");
    manager.RegisterDevice(std::make_shared<FakeDevice>(webcam));
    const DeviceInventory full = SnapshotInventory(manager);

    const auto byConnection = GroupDevices(full, DeviceGrouping::Connection);
    Check(byConnection.size() == 2 && byConnection[0].title == "USB" &&
          byConnection[1].title == "Network",
          "by connection: USB before Network, no empty groups");
    Check(byConnection[0].devices.size() == 2, "two USB devices");

    const auto byBackend = GroupDevices(full, DeviceGrouping::Backend);
    Check(byBackend.size() == 2 && byBackend[0].title == "TestCam" &&
          byBackend[1].title == "TestPrint",
          "by backend: alphabetical");

    // ===== FILTER =====
    std::cout << "Filter\n";
    const auto acme = GroupDevices(full, DeviceGrouping::Category, "ACME");
    size_t acmeCount = 0;
    for (const auto& group : acme) acmeCount += group.devices.size();
    Check(acmeCount == 2, "filter is case-insensitive over the manufacturer");
    Check(FindGroup(acme, "Scanners") == nullptr, "a filter leaves no empty group behind");
    Check(GroupDevices(full, DeviceGrouping::Category, "reception").size() == 1,
          "filter matches the location");
    Check(GroupDevices(full, DeviceGrouping::Category, "network").size() == 1,
          "filter matches the connection");
    Check(GroupDevices(full, DeviceGrouping::Category, "no such thing").empty(),
          "filter that matches nothing leaves an empty tree");

    // ===== DETAILS =====
    std::cout << "Details\n";
    const DeviceRecord* laserRecord = nullptr;
    const DeviceRecord* webcamRecord = nullptr;
    for (const auto& record : full.devices) {
        if (record.info.deviceId == "p1") laserRecord = &record;
        if (record.info.deviceId == "c1") webcamRecord = &record;
    }
    Check(laserRecord && webcamRecord, "records found by id");
    if (laserRecord && webcamRecord) {
        const auto masked = DescribeDevice(*laserRecord, [](const std::string& value) {
            return std::string(value.size() - 4, '*') + value.substr(value.size() - 4);
        });
        const PropertySection* general = FindSection(masked, "General");
        Check(ValueOf(general, "Serial number") == "*********7890", "serial number goes through the mask");
        Check(ValueOf(general, "Manufacturer") == "Acme", "manufacturer listed");
        Check(ValueOf(FindSection(masked, "Connection"), "Connection") == "Network",
              "connection listed");
        Check(ValueOf(FindSection(masked, "Backend details"), "ppd") == "acme-l400.ppd",
              "backend attributes get a section of their own");
        Check(ValueOf(FindSection(masked, "Status"), "Last error") == "None", "no error reported");

        const auto plain = DescribeDevice(*laserRecord);
        Check(ValueOf(FindSection(plain, "General"), "Serial number") == "SN-1234567890",
              "without a mask the serial is shown verbatim");

        const auto webcamSections = DescribeDevice(*webcamRecord);
        Check(ValueOf(FindSection(webcamSections, "General"), "Name") == "Acme Cam HD",
              "a device without a name is named by manufacturer and model");
        Check(ValueOf(FindSection(webcamSections, "General"), "Serial number").empty(),
              "an empty property is left out rather than shown blank");
        Check(FindSection(webcamSections, "Backend details") == nullptr,
              "no attributes, no attributes section");
    }

    const auto categoryGroups = GroupDevices(full, DeviceGrouping::Category);
    if (const DeviceGroup* printerGroup = FindGroup(categoryGroups, "Printers")) {
        const auto sections = DescribeGroup(*printerGroup, DeviceGrouping::Category, full);
        Check(ValueOf(FindSection(sections, "Summary"), "Devices") == "2", "group summary counts");
        Check(ValueOf(FindSection(sections, "Searched by"), "TestPrint") == "2 devices",
              "group lists what each backend found");
    } else {
        Check(false, "printer group present");
    }
    if (const DeviceGroup* scannerGroup = FindGroup(categoryGroups, "Scanners")) {
        const auto sections = DescribeGroup(*scannerGroup, DeviceGrouping::Category, full);
        const PropertySection* members = FindSection(sections, "Devices");
        Check(members && members->rows.size() == 1 && members->rows[0].name == "(none found)",
              "an empty group says so");
    }

    MachineSummary machine;
    machine.hostName = "testhost";
    const auto computer = DescribeComputer(full, machine, false);
    Check(ValueOf(FindSection(computer, "Computer"), "Host name") == "testhost", "host name listed");
    Check(ValueOf(FindSection(computer, "Devices"), "Total") == "3", "total device count");
    Check(ValueOf(FindSection(computer, "Devices"), "Scanners") == "0",
          "a searchable empty category is counted as zero");
    Check(ValueOf(FindSection(computer, "Backends in this build"), "Printers") == "TestPrint",
          "backends per category listed");

    // ===== TEXT =====
    std::cout << "Text\n";
    const std::string text = FormatInventoryText(full, DeviceGrouping::Category, machine, false);
    Check(text.find("testhost  (3 devices, grouped by Category)") == 0, "text starts with the host");
    Check(text.find("├─ Printers (2)") != std::string::npos, "text lists the printer group");
    Check(text.find("(none found)") != std::string::npos, "text marks the empty scanner group");
    Check(text.find("Serial number") == std::string::npos, "no properties without --details");
    const std::string detailed = FormatInventoryText(full, DeviceGrouping::Category, machine, true);
    Check(detailed.find("Serial number: SN-1234567890") != std::string::npos,
          "--details prints the properties");

    // ===== NAMES =====
    DeviceGrouping parsed = DeviceGrouping::Category;
    Check(ParseGrouping("Connection", parsed) && parsed == DeviceGrouping::Connection,
          "--group connection parses");
    Check(ParseGrouping("transport", parsed) && parsed == DeviceGrouping::Connection,
          "--group transport is an alias");
    Check(!ParseGrouping("colour", parsed), "an unknown grouping is refused");
    Check(DeviceNodeId("p1") != GroupNodeId(categoryGroups.front()) &&
          DeviceNodeId("computer") != kComputerNodeId,
          "node ids cannot collide with each other or with the computer");

    manager.Shutdown();
    std::cout << (g_failures == 0 ? "All checks passed\n" : "Some checks FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
