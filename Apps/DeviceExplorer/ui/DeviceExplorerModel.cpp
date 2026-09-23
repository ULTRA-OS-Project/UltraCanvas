// Apps/DeviceExplorer/ui/DeviceExplorerModel.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DeviceExplorerModel.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iterator>
#include <sstream>

using namespace UltraCanvas;

namespace DeviceExplorer {

const char* const kComputerNodeId = "computer";

namespace {

// Categories in the order the tree lists them: the ones IODeviceManager has
// backends for first, the planned ones after, Custom and Unknown last.
const IODeviceCategory kCategoryOrder[] = {
    IODeviceCategory::Printer,
    IODeviceCategory::Scanner,
    IODeviceCategory::Camera,
    IODeviceCategory::Microphone,
    IODeviceCategory::Speaker,
    IODeviceCategory::Storage,
    IODeviceCategory::NetworkAdapter,
    IODeviceCategory::Serial,
    IODeviceCategory::Bluetooth,
    IODeviceCategory::GPIO,
    IODeviceCategory::Barcode,
    IODeviceCategory::Biometric,
    IODeviceCategory::Custom,
    IODeviceCategory::Unknown,
};

const IODeviceTransport kTransportOrder[] = {
    IODeviceTransport::USB,
    IODeviceTransport::Network,
    IODeviceTransport::Bluetooth,
    IODeviceTransport::Serial,
    IODeviceTransport::Parallel,
    IODeviceTransport::PCI,
    IODeviceTransport::Virtual,
    IODeviceTransport::Unknown,
};

int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool ContainsFolded(const std::string& haystack, const std::string& foldedNeedle) {
    return Lower(haystack).find(foldedNeedle) != std::string::npos;
}

int CategoryRank(IODeviceCategory category) {
    for (size_t i = 0; i < std::size(kCategoryOrder); ++i) {
        if (kCategoryOrder[i] == category) return static_cast<int>(i);
    }
    return static_cast<int>(std::size(kCategoryOrder));
}

int TransportRank(IODeviceTransport transport) {
    for (size_t i = 0; i < std::size(kTransportOrder); ++i) {
        if (kTransportOrder[i] == transport) return static_cast<int>(i);
    }
    return static_cast<int>(std::size(kTransportOrder));
}

std::string BackendOf(const IODeviceInfo& info) {
    return info.backend.empty() ? std::string("Unknown") : info.backend;
}

void AddRow(PropertySection& section, const std::string& name, const std::string& value) {
    // A blank value says nothing: an empty row would only look like a
    // rendering fault. The sections list what the backend reported.
    if (!value.empty()) section.rows.push_back({name, value});
}

void AddSection(std::vector<PropertySection>& sections, PropertySection section) {
    if (!section.rows.empty()) sections.push_back(std::move(section));
}

std::string FormatTime(int64_t seconds) {
    if (seconds <= 0) return {};
    const std::time_t time = static_cast<std::time_t>(seconds);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}

std::string JoinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (const auto& name : names) {
        if (!joined.empty()) joined += ", ";
        joined += name;
    }
    return joined;
}

std::string CountText(size_t count, const char* singular, const char* plural) {
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

} // namespace

// ============================================================================
// SNAPSHOT
// ============================================================================

size_t DeviceInventory::CountIn(IODeviceCategory category) const {
    return static_cast<size_t>(std::count_if(devices.begin(), devices.end(),
        [category](const DeviceRecord& record) { return record.info.category == category; }));
}

DeviceInventory SnapshotInventory(const IODeviceManager& manager) {
    DeviceInventory inventory;
    inventory.capturedAt = NowSeconds();
    for (IODeviceCategory category : kCategoryOrder) {
        auto names = manager.GetRegisteredBackends(category);
        if (!names.empty()) inventory.backends[category] = std::move(names);
    }
    for (const IODevicePtr& device : manager.GetDevices()) {
        if (!device) continue;
        DeviceRecord record;
        record.info = device->GetDeviceInfo();
        record.connected = device->IsConnected();
        const IODeviceResult error = device->GetLastDeviceError();
        if (!error.success && error.code != IODeviceResultCode::Unknown) {
            record.lastError = std::string(IODeviceResultCodeToString(error.code));
            if (!error.message.empty()) record.lastError += ": " + error.message;
        }
        inventory.devices.push_back(std::move(record));
    }
    return inventory;
}

DeviceInventory ScanInventory(IODeviceManager& manager) {
    std::string message;
    if (!manager.IsInitialized()) {
        const IODeviceResult init = manager.Initialize();
        if (!init) message = init.message;
    }
    const IODeviceResult scan = manager.EnumerateAllDevices();
    if (!scan.message.empty()) {
        if (!message.empty()) message += "\n";
        message += scan.message;
    }
    DeviceInventory inventory = SnapshotInventory(manager);
    inventory.enumerationMessage = message;
    inventory.enumerated = true;
    return inventory;
}

// ============================================================================
// NAMES
// ============================================================================

const char* GroupingName(DeviceGrouping grouping) {
    switch (grouping) {
        case DeviceGrouping::Category:   return "Category";
        case DeviceGrouping::Connection: return "Connection";
        case DeviceGrouping::Backend:    return "Backend";
    }
    return "Category";
}

DeviceGrouping GroupingFromIndex(int index) {
    switch (index) {
        case 1:  return DeviceGrouping::Connection;
        case 2:  return DeviceGrouping::Backend;
        default: return DeviceGrouping::Category;
    }
}

bool ParseGrouping(const std::string& text, DeviceGrouping& grouping) {
    const std::string folded = Lower(text);
    for (int i = 0; i < kGroupingCount; ++i) {
        const DeviceGrouping candidate = GroupingFromIndex(i);
        if (folded == Lower(GroupingName(candidate))) {
            grouping = candidate;
            return true;
        }
    }
    if (folded == "transport") { grouping = DeviceGrouping::Connection; return true; }
    return false;
}

std::string CategoryDisplayName(IODeviceCategory category) {
    switch (category) {
        case IODeviceCategory::NetworkAdapter: return "Network adapter";
        case IODeviceCategory::Barcode:        return "Barcode reader";
        case IODeviceCategory::Biometric:      return "Biometric device";
        case IODeviceCategory::Custom:         return "Custom device";
        case IODeviceCategory::Unknown:        return "Other device";
        default: return IODeviceCategoryToString(category);
    }
}

std::string CategoryGroupTitle(IODeviceCategory category) {
    switch (category) {
        case IODeviceCategory::Printer:        return "Printers";
        case IODeviceCategory::Scanner:        return "Scanners";
        case IODeviceCategory::Camera:         return "Cameras";
        case IODeviceCategory::Microphone:     return "Microphones";
        case IODeviceCategory::Speaker:        return "Speakers";
        case IODeviceCategory::Storage:        return "Storage";
        case IODeviceCategory::NetworkAdapter: return "Network adapters";
        case IODeviceCategory::Serial:         return "Serial ports";
        case IODeviceCategory::Bluetooth:      return "Bluetooth";
        case IODeviceCategory::GPIO:           return "GPIO";
        case IODeviceCategory::Barcode:        return "Barcode readers";
        case IODeviceCategory::Biometric:      return "Biometric devices";
        case IODeviceCategory::Custom:         return "Custom devices";
        case IODeviceCategory::Unknown:        return "Other devices";
    }
    return "Other devices";
}

std::string TransportDisplayName(IODeviceTransport transport) {
    switch (transport) {
        case IODeviceTransport::USB:       return "USB";
        case IODeviceTransport::Network:   return "Network";
        case IODeviceTransport::Bluetooth: return "Bluetooth";
        case IODeviceTransport::Serial:    return "Serial";
        case IODeviceTransport::Parallel:  return "Parallel";
        case IODeviceTransport::PCI:       return "PCI";
        case IODeviceTransport::Virtual:   return "Virtual";
        case IODeviceTransport::Unknown:   return "Unknown connection";
    }
    return "Unknown connection";
}

std::string StateDisplayName(IODeviceState state) {
    switch (state) {
        case IODeviceState::Disconnected: return "Available (not open)";
        case IODeviceState::Connecting:   return "Connecting";
        case IODeviceState::Connected:    return "Open";
        case IODeviceState::Ready:        return "Ready";
        case IODeviceState::Busy:         return "Busy";
        case IODeviceState::Paused:       return "Paused";
        case IODeviceState::Error:        return "Error";
        case IODeviceState::Offline:      return "Offline";
        case IODeviceState::Unknown:      return "Unknown";
    }
    return "Unknown";
}

std::string DeviceDisplayName(const IODeviceInfo& info) {
    if (!info.name.empty()) return info.name;
    if (!info.model.empty()) {
        return info.manufacturer.empty() ? info.model : info.manufacturer + " " + info.model;
    }
    return info.deviceId;
}

std::string DeviceNodeId(const IODeviceId& deviceId) {
    return "device:" + deviceId;
}

std::string GroupNodeId(const DeviceGroup& group) {
    return "group:" + group.key;
}

// ============================================================================
// GROUPING
// ============================================================================

bool MatchesFilter(const DeviceRecord& record, const std::string& filter) {
    if (filter.empty()) return true;
    const std::string needle = Lower(filter);
    const IODeviceInfo& info = record.info;
    return ContainsFolded(DeviceDisplayName(info), needle) ||
           ContainsFolded(info.manufacturer, needle) ||
           ContainsFolded(info.model, needle) ||
           ContainsFolded(info.backend, needle) ||
           ContainsFolded(info.location, needle) ||
           ContainsFolded(info.description, needle) ||
           ContainsFolded(CategoryDisplayName(info.category), needle) ||
           ContainsFolded(CategoryGroupTitle(info.category), needle) ||
           ContainsFolded(TransportDisplayName(info.transport), needle);
}

std::vector<DeviceGroup> GroupDevices(const DeviceInventory& inventory,
                                      DeviceGrouping grouping,
                                      const std::string& filter) {
    std::vector<DeviceGroup> groups;
    auto groupFor = [&groups](const std::string& key) -> DeviceGroup* {
        for (auto& group : groups) {
            if (group.key == key) return &group;
        }
        return nullptr;
    };

    // Grouped by category, a category the build can search is shown even
    // when it found nothing - but only while unfiltered, since a filter that
    // hides every printer should not leave an empty "Printers" behind.
    if (grouping == DeviceGrouping::Category && filter.empty()) {
        for (const auto& [category, backendNames] : inventory.backends) {
            (void)backendNames;
            DeviceGroup group;
            group.key = std::string("category:") + IODeviceCategoryToString(category);
            group.title = CategoryGroupTitle(category);
            group.category = category;
            groups.push_back(std::move(group));
        }
    }

    for (const DeviceRecord& record : inventory.devices) {
        if (!MatchesFilter(record, filter)) continue;
        const IODeviceInfo& info = record.info;
        std::string key;
        switch (grouping) {
            case DeviceGrouping::Category:
                key = std::string("category:") + IODeviceCategoryToString(info.category);
                break;
            case DeviceGrouping::Connection:
                key = std::string("transport:") + IODeviceTransportToString(info.transport);
                break;
            case DeviceGrouping::Backend:
                key = "backend:" + BackendOf(info);
                break;
        }
        DeviceGroup* group = groupFor(key);
        if (!group) {
            DeviceGroup created;
            created.key = key;
            created.category = info.category;
            created.transport = info.transport;
            switch (grouping) {
                case DeviceGrouping::Category:   created.title = CategoryGroupTitle(info.category); break;
                case DeviceGrouping::Connection: created.title = TransportDisplayName(info.transport); break;
                case DeviceGrouping::Backend:    created.title = BackendOf(info); break;
            }
            groups.push_back(std::move(created));
            group = &groups.back();
        }
        group->devices.push_back(&record);
    }

    for (auto& group : groups) {
        std::stable_sort(group.devices.begin(), group.devices.end(),
            [](const DeviceRecord* a, const DeviceRecord* b) {
                return Lower(DeviceDisplayName(a->info)) < Lower(DeviceDisplayName(b->info));
            });
    }

    std::stable_sort(groups.begin(), groups.end(),
        [grouping](const DeviceGroup& a, const DeviceGroup& b) {
            switch (grouping) {
                case DeviceGrouping::Category:
                    return CategoryRank(a.category) < CategoryRank(b.category);
                case DeviceGrouping::Connection:
                    return TransportRank(a.transport) < TransportRank(b.transport);
                case DeviceGrouping::Backend:
                    return Lower(a.title) < Lower(b.title);
            }
            return false;
        });
    return groups;
}

// ============================================================================
// DETAILS
// ============================================================================

std::vector<PropertySection> DescribeDevice(const DeviceRecord& record,
                                            const IdentifierMask& mask) {
    const IODeviceInfo& info = record.info;
    std::vector<PropertySection> sections;

    PropertySection general{"General", {}};
    AddRow(general, "Name", DeviceDisplayName(info));
    AddRow(general, "Type", CategoryDisplayName(info.category));
    AddRow(general, "Manufacturer", info.manufacturer);
    AddRow(general, "Model", info.model);
    AddRow(general, "Serial number",
           info.serialNumber.empty() || !mask ? info.serialNumber : mask(info.serialNumber));
    AddRow(general, "Description", info.description);
    AddRow(general, "Location", info.location);
    AddSection(sections, std::move(general));

    PropertySection connection{"Connection", {}};
    AddRow(connection, "Connection", TransportDisplayName(info.transport));
    AddRow(connection, "Backend", BackendOf(info));
    AddRow(connection, "Connection path", info.connectionPath);
    AddRow(connection, "Device id", info.deviceId);
    AddSection(sections, std::move(connection));

    PropertySection status{"Status", {}};
    AddRow(status, "State", StateDisplayName(info.state));
    AddRow(status, "Session", record.connected ? "Open in this application" : "Not open");
    AddRow(status, "Last error", record.lastError.empty() ? std::string("None") : record.lastError);
    AddSection(sections, std::move(status));

    PropertySection attributes{"Backend details", {}};
    for (const auto& [key, value] : info.attributes) AddRow(attributes, key, value);
    AddSection(sections, std::move(attributes));
    return sections;
}

std::vector<PropertySection> DescribeGroup(const DeviceGroup& group,
                                           DeviceGrouping grouping,
                                           const DeviceInventory& inventory) {
    std::vector<PropertySection> sections;

    PropertySection summary{"Summary", {}};
    AddRow(summary, "Group", group.title);
    AddRow(summary, "Grouped by", GroupingName(grouping));
    AddRow(summary, "Devices", std::to_string(group.devices.size()));
    size_t open = 0;
    size_t failing = 0;
    for (const DeviceRecord* record : group.devices) {
        if (record->connected) ++open;
        if (record->info.state == IODeviceState::Error || !record->lastError.empty()) ++failing;
    }
    AddRow(summary, "Open in this application", std::to_string(open));
    if (failing > 0) AddRow(summary, "Reporting an error", std::to_string(failing));
    AddSection(sections, std::move(summary));

    if (grouping == DeviceGrouping::Category) {
        PropertySection search{"Searched by", {}};
        const auto it = inventory.backends.find(group.category);
        if (it != inventory.backends.end()) {
            for (const std::string& backend : it->second) {
                size_t found = 0;
                for (const DeviceRecord* record : group.devices) {
                    if (record->info.backend == backend) ++found;
                }
                AddRow(search, backend, CountText(found, "device", "devices"));
            }
        }
        AddSection(sections, std::move(search));
    }

    PropertySection members{"Devices", {}};
    for (const DeviceRecord* record : group.devices) {
        AddRow(members, DeviceDisplayName(record->info),
               grouping == DeviceGrouping::Category
                   ? TransportDisplayName(record->info.transport) + " · " + BackendOf(record->info)
                   : CategoryDisplayName(record->info.category) + " · " + StateDisplayName(record->info.state));
    }
    if (members.rows.empty()) members.rows.push_back({"(none found)", ""});
    sections.push_back(std::move(members));
    return sections;
}

std::vector<PropertySection> DescribeComputer(const DeviceInventory& inventory,
                                              const MachineSummary& machine,
                                              bool monitoring) {
    std::vector<PropertySection> sections;

    PropertySection computer{"Computer", {}};
    AddRow(computer, "Host name", machine.hostName);
    AddRow(computer, "Operating system", machine.operatingSystem);
    AddRow(computer, "Kernel", machine.kernel);
    AddRow(computer, "Manufacturer", machine.manufacturer);
    AddRow(computer, "Model", machine.model);
    AddSection(sections, std::move(computer));

    PropertySection devices{"Devices", {}};
    devices.rows.push_back({"Total", std::to_string(inventory.devices.size())});
    for (IODeviceCategory category : kCategoryOrder) {
        const size_t count = inventory.CountIn(category);
        const bool searched = inventory.backends.count(category) > 0;
        if (count == 0 && !searched) continue;
        devices.rows.push_back({CategoryGroupTitle(category), std::to_string(count)});
    }
    sections.push_back(std::move(devices));

    PropertySection backends{"Backends in this build", {}};
    for (IODeviceCategory category : kCategoryOrder) {
        const auto it = inventory.backends.find(category);
        if (it != inventory.backends.end()) {
            AddRow(backends, CategoryGroupTitle(category), JoinNames(it->second));
        }
    }
    if (backends.rows.empty()) {
        backends.rows.push_back({"(none)", "This build has no device backend compiled in"});
    }
    sections.push_back(std::move(backends));

    PropertySection scan{"Scan", {}};
    AddRow(scan, "Last scan", inventory.enumerated ? FormatTime(inventory.capturedAt)
                                                   : std::string("Not scanned yet"));
    AddRow(scan, "Hot-plug monitoring", monitoring ? "On - changes appear by themselves"
                                                   : "Off - press Rescan after plugging in");
    AddRow(scan, "Backend messages", inventory.enumerationMessage);
    AddSection(sections, std::move(scan));
    return sections;
}

std::string FormatInventoryText(const DeviceInventory& inventory,
                                DeviceGrouping grouping,
                                const MachineSummary& machine,
                                bool details,
                                const IdentifierMask& mask) {
    std::ostringstream out;
    const std::string host = machine.hostName.empty() ? std::string("This computer") : machine.hostName;
    out << host << "  (" << CountText(inventory.devices.size(), "device", "devices")
        << ", grouped by " << GroupingName(grouping) << ")\n";

    const auto groups = GroupDevices(inventory, grouping);
    for (size_t g = 0; g < groups.size(); ++g) {
        const DeviceGroup& group = groups[g];
        const bool lastGroup = g + 1 == groups.size();
        out << (lastGroup ? "└─ " : "├─ ") << group.title << " (" << group.devices.size() << ")\n";
        const std::string trunk = lastGroup ? "   " : "│  ";
        if (group.devices.empty()) {
            out << trunk << "└─ (none found)\n";
            continue;
        }
        for (size_t d = 0; d < group.devices.size(); ++d) {
            const DeviceRecord& record = *group.devices[d];
            const bool lastDevice = d + 1 == group.devices.size();
            out << trunk << (lastDevice ? "└─ " : "├─ ") << DeviceDisplayName(record.info)
                << "  [" << TransportDisplayName(record.info.transport) << ", "
                << BackendOf(record.info) << ", " << StateDisplayName(record.info.state) << "]\n";
            if (!details) continue;
            const std::string branch = trunk + (lastDevice ? "   " : "│  ");
            for (const PropertySection& section : DescribeDevice(record, mask)) {
                out << branch << "  " << section.title << "\n";
                for (const PropertyRow& row : section.rows) {
                    out << branch << "    " << row.name << ": " << row.value << "\n";
                }
            }
        }
    }
    if (inventory.backends.empty()) {
        out << "\nThis build has no device backend compiled in, so nothing can be found.\n";
    }
    if (!inventory.enumerationMessage.empty()) {
        out << "\nBackend messages:\n" << inventory.enumerationMessage << "\n";
    }
    return out.str();
}

} // namespace DeviceExplorer
