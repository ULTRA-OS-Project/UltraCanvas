// Apps/DeviceExplorer/ui/DeviceExplorerPanel.cpp
// Version: 0.3.0 - DeviceExplorerWindow's contents as a container of their own
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCanvasHardwareInfo.h"

#include "DeviceExplorerPanel.h"

#include "IODeviceManager/UltraCanvasIODeviceManager.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <chrono>

using namespace UltraCanvas;

namespace DeviceExplorer {
namespace {

constexpr float kPagePadding  = 10.0f;
constexpr float kSectionGap   = 8.0f;
constexpr int   kTreeWidth    = 360;
constexpr int   kTreeMinWidth = 240;
constexpr int   kDetailsMin   = 360;
constexpr unsigned kUiTimerMs = 200;
// A printer's status is asked again when it is selected and the last answer
// is older than this: fresh enough to click back and forth without asking
// every time, stale enough that a refilled cartridge shows up.
constexpr int64_t kPrinterStatusMaxAgeSeconds = 30;

int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

const char* const kSectionPrefix = "section:";

// A split pane's panes are plain containers; an element fills one edge to
// edge. Basis zero and grow one, so the pane's size decides.
void FillWith(const std::shared_ptr<UltraCanvasContainer>& container,
              const std::shared_ptr<UltraCanvasUIElement>& element) {
    container->AddChild(element);
    element->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    element->layoutItem.SetFlexGrow(1);
    element->layoutItem.SetFlexShrink(1);
    element->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

// A fixed-height row inside a flex column: never grows, never shrinks.
void PinRow(const std::shared_ptr<UltraCanvasUIElement>& element) {
    element->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    element->layoutItem.SetFlexGrow(0);
    element->layoutItem.SetFlexShrink(0);
}

void MakePlainColumn(const std::shared_ptr<UltraCanvasContainer>& container) {
    container->layout.SetFlexColumn()
                     .SetFlexGap(kSectionGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    container->SetContainerStyle(plain);
}

std::string CountText(size_t count, const char* singular, const char* plural) {
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

} // namespace

DeviceExplorerPanel::DeviceExplorerPanel(const std::string& identifier, const MachineSummary& machine,
                                         DeviceGrouping grouping, std::string version)
    : UltraCanvasContainer(identifier), machine_(machine), grouping_(grouping),
      version_(std::move(version)) {
    iconsDir_ = NormalizePath(GetResourcesDir() + "media/icons/");

    layout.SetFlexColumn()
          .SetFlexGap(kSectionGap)
          .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    SetContainerStyle(plain);
    SetPadding(kPagePadding);

    // The title and the line under it wrap rather than run into each other
    // when a translation is longer: a wrapping row, each label as wide as
    // its text.
    auto heading = CreateContainer("deHeading", 0, 0, 0, 0);
    heading->layout.SetFlexRow()
                   .SetFlexWrap(CSSLayout::FlexWrap::Wrap)
                   .SetFlexGap(4, 16)
                   .SetFlexAlignItems(CSSLayout::AlignItems::End);
    heading->SetContainerStyle(plain);
    auto title = CreateLabel("deTitle", 0, 0, 0, 0, "DeviceExplorer");
    title->SetFontSize(18);
    title->SetFontWeight(FontWeight::Bold);
    heading->AddChild(title);
    subtitleLabel_ = CreateLabel("deSubtitle", 0, 0, 0, 0,
                                 "The devices connected to this computer, as IODeviceManager finds them");
    subtitleLabel_->SetTextColor(Color(96, 96, 104));
    subtitleLabel_->SetWrap(TextWrap::WrapWord);
    subtitleLabel_->layoutItem.SetFlexShrink(1);
    heading->AddChild(subtitleLabel_);
    AddChild(heading);
    PinRow(heading);

    auto toolbar = BuildToolbar();
    AddChild(toolbar);
    PinRow(toolbar);

    // Tree left, details right. A split pane rather than a flex row: the
    // details keep every pixel a wider window adds, and the divider is real.
    split_ = CreateHorizontalSplitPane("deSplit", 0, 0, 0, 0);
    split_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    SplitPaneStyle splitStyle;
    splitStyle.splitterThickness = 6;
    splitStyle.splitterHitMargin = 3;
    splitStyle.splitterColor     = Color(232, 232, 236);
    splitStyle.handle.shape      = SplitterHandleShape::RoundedSquare;
    splitStyle.handle.crossSize  = 9;
    splitStyle.handle.axisLength = 44;
    split_->SetSplitPaneStyle(splitStyle);
    auto treePane    = split_->AddPane(1.0);
    auto detailsPane = split_->AddPane(2.0);
    split_->SetPaneFixedSize(0, kTreeWidth);
    split_->SetPaneMinSize(0, kTreeMinWidth);
    split_->SetPaneMinSize(1, kDetailsMin);
    MakePlainColumn(treePane);
    MakePlainColumn(detailsPane);
    FillWith(treePane, BuildDeviceTree());
    scanRow_ = BuildScanRow();
    treePane->AddChild(scanRow_);
    PinRow(scanRow_);
    FillWith(detailsPane, BuildDetailsPane());
    AddChild(split_);
    split_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    split_->layoutItem.SetFlexGrow(1);
    split_->layoutItem.SetFlexShrink(1);
    split_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // Show the computer node straight away; Start() fills it in.
    RebuildTree();
    selectedNodeId_ = kComputerNodeId;
    ShowDetailsFor(selectedNodeId_);
}

DeviceExplorerPanel::~DeviceExplorerPanel() {
    // A scan still running would start the watcher behind our back, so it
    // is joined first; then the watcher, whose StopMonitoring() joins its
    // thread, so no change callback runs into a half-destroyed panel.
    JoinScan();
    JoinStatusQuery();
    if (started_) {
        auto& manager = IODeviceManager::GetInstance();
        manager.StopMonitoring();
        manager.SetDeviceChangeCallback(nullptr);
    }
    if (uiTimer_ != 0) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(uiTimer_);
    }
}

void DeviceExplorerPanel::Start() {
    if (started_) return;
    started_ = true;
    // Hot-plug: the manager re-enumerates the category on its own thread and
    // tells us; the UI timer picks the flag up and re-reads the registry.
    auto& manager = IODeviceManager::GetInstance();
    manager.SetDeviceChangeCallback([this](IODeviceChange, const IODeviceInfo&) {
        hotplugChanged_ = true;
    });
    StartScan();
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
        uiTimer_ = app->StartTimer(kUiTimerMs, /*periodic=*/true,
                                   [this](TimerId) { OnTimer(); });
    }
}

MachineSummary DeviceExplorerPanel::DescribeThisMachine() {
    MachineSummary machine;
    const HardwareSnapshot snapshot = UltraCanvasHardwareInfo::Capture(HardwareQuery::System);
    machine.hostName = snapshot.system.hostName;
    machine.operatingSystem = snapshot.system.osName;
    if (!snapshot.system.osVersion.empty() &&
        machine.operatingSystem.find(snapshot.system.osVersion) == std::string::npos) {
        if (!machine.operatingSystem.empty()) machine.operatingSystem += " ";
        machine.operatingSystem += snapshot.system.osVersion;
    }
    machine.kernel = snapshot.system.kernelVersion;
    machine.manufacturer = snapshot.system.manufacturer;
    machine.model = snapshot.system.productName;
    return machine;
}

// ===== CONSTRUCTION =====

std::shared_ptr<UltraCanvasContainer> DeviceExplorerPanel::BuildToolbar() {
    // A wrapping row: a longer translation of the buttons moves the last
    // ones onto a second line instead of pushing them out of the window.
    auto bar = CreateContainer("deToolbar", 0, 0, 0, 0);
    bar->layout.SetFlexRow()
               .SetFlexWrap(CSSLayout::FlexWrap::Wrap)
               .SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    ContainerStyle plainBar;
    plainBar.autoShowScrollbars = false;
    bar->SetContainerStyle(plainBar);

    auto groupLabel = CreateLabel("deGroupLabel", 0, 0, 0, 0, "Group by");
    groupLabel->layoutItem.SetFlexShrink(0);
    bar->AddChild(groupLabel);

    groupingDropdown_ = CreateDropdown("deGrouping", 0, 0, 130, 28);
    for (int i = 0; i < kGroupingCount; ++i) {
        groupingDropdown_->AddItem(GroupingName(GroupingFromIndex(i)));
    }
    groupingDropdown_->SetSelectedIndex(static_cast<int>(grouping_), /*runNotifications=*/false);
    groupingDropdown_->onSelectionChanged = [this](int index, const DropdownItem&) {
        const DeviceGrouping chosen = GroupingFromIndex(index);
        if (chosen == grouping_) return;
        grouping_ = chosen;
        // Group keys differ per grouping, so what was collapsed means
        // nothing now; a device stays selected across the switch.
        collapsedGroups_.clear();
        RebuildTree();
    };
    groupingDropdown_->layoutItem.SetFlexShrink(0);
    bar->AddChild(groupingDropdown_);

    filterInput_ = CreateTextInput("deFilter", 0, 0, 240, 28);
    filterInput_->SetPlaceholder("Filter devices (name, model, backend, connection)");
    filterInput_->onTextChanged = [this](const std::string& text) {
        filter_ = text;
        RebuildTree();
    };
    bar->AddChild(filterInput_);

    rescanButton_ = CreateButton("deRescan", 0, 0, 90, 28, "Rescan");
    rescanButton_->layoutItem.SetFlexShrink(0);
    rescanButton_->SetOnClick([this]() { StartScan(); });
    bar->AddChild(rescanButton_);

    expandButton_ = CreateButton("deExpand", 0, 0, 100, 28, "Expand all");
    expandButton_->layoutItem.SetFlexShrink(0);
    expandButton_->SetOnClick([this]() {
        collapsedGroups_.clear();
        if (deviceTree_) deviceTree_->ExpandAll();
    });
    bar->AddChild(expandButton_);

    collapseButton_ = CreateButton("deCollapse", 0, 0, 110, 28, "Collapse all");
    collapseButton_->layoutItem.SetFlexShrink(0);
    collapseButton_->SetOnClick([this]() {
        if (!deviceTree_) return;
        for (const DeviceGroup& group : groups_) {
            collapsedGroups_.insert(group.key);
            if (TreeNode* node = deviceTree_->FindNode(GroupNodeId(group))) {
                deviceTree_->CollapseNode(node);
            }
        }
    });
    bar->AddChild(collapseButton_);

    aboutButton_ = CreateButton("deAbout", 0, 0, 70, 28, "About");
    aboutButton_->layoutItem.SetFlexShrink(0);
    aboutButton_->SetOnClick([this]() { ShowAbout(); });
    bar->AddChild(aboutButton_);

    statusLabel_ = CreateLabel("deStatus", 0, 0, 0, 0, "");
    statusLabel_->SetWrap(TextWrap::WrapWord);
    statusLabel_->layoutItem.SetFlexGrow(1);
    statusLabel_->layoutItem.SetFlexShrink(1);
    bar->AddChild(statusLabel_);
    return bar;
}

std::shared_ptr<UltraCanvasTreeView> DeviceExplorerPanel::BuildDeviceTree() {
    deviceTree_ = std::make_shared<UltraCanvasTreeView>("deDevices", 0, 0, kTreeWidth, 400);
    deviceTree_->SetRowHeight(24);
    deviceTree_->SetSelectionMode(TreeSelectionMode::Single);
    deviceTree_->SetLineStyle(TreeLineStyle::Dotted);
    deviceTree_->SetShowExpandButtons(true);

    deviceTree_->onNodeSelected = [this](TreeNode* node) {
        if (rebuilding_ || !node) return;
        selectedNodeId_ = node->data.nodeId;
        ShowDetailsFor(selectedNodeId_);
    };
    // Remember which groups the user closed, so a rescan or a hot-plug
    // rebuild does not reopen them under the pointer.
    deviceTree_->onNodeCollapsed = [this](TreeNode* node) {
        if (rebuilding_ || !node) return;
        for (const DeviceGroup& group : groups_) {
            if (GroupNodeId(group) == node->data.nodeId) collapsedGroups_.insert(group.key);
        }
    };
    deviceTree_->onNodeExpanded = [this](TreeNode* node) {
        if (rebuilding_ || !node) return;
        for (const DeviceGroup& group : groups_) {
            if (GroupNodeId(group) == node->data.nodeId) collapsedGroups_.erase(group.key);
        }
    };
    return deviceTree_;
}

// "Scanning for devices" and a segment sliding along a track, under the tree
// where the devices will appear. Hidden, and its timer stopped, between scans.
std::shared_ptr<UltraCanvasContainer> DeviceExplorerPanel::BuildScanRow() {
    auto row = CreateContainer("deScanRow", 0, 0, 0, 0);
    row->layout.SetFlexRow()
               .SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    ContainerStyle plainRow;
    plainRow.autoShowScrollbars = false;
    row->SetContainerStyle(plainRow);

    // As wide as its text, in whatever language.
    auto label = CreateLabel("deScanLabel", 0, 0, 0, 0, "Scanning for devices…");
    label->SetTextColor(Color(96, 96, 104));
    label->layoutItem.SetFlexShrink(0);
    row->AddChild(label);

    scanBar_ = CreateBusyIndicator("deScanBar", 0, 0, 120, 4, BusyIndicatorKind::Bar);
    scanBar_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    scanBar_->layoutItem.SetFlexGrow(1);
    scanBar_->layoutItem.SetFlexShrink(1);
    row->AddChild(scanBar_);

    row->SetVisible(false);
    return row;
}

std::shared_ptr<UltraCanvasContainer> DeviceExplorerPanel::BuildDetailsPane() {
    auto details = CreateContainer("deDetails", 0, 0, 0, 0);
    MakePlainColumn(details);
    details->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());

    detailTitle_ = CreateLabel("deDetailTitle", 0, 0, 0, 0, "");
    detailTitle_->SetWrap(TextWrap::WrapWord);
    detailTitle_->SetFontSize(16);
    detailTitle_->SetFontWeight(FontWeight::Bold);
    details->AddChild(detailTitle_);
    PinRow(detailTitle_);

    detailSubtitle_ = CreateLabel("deDetailSubtitle", 0, 0, 0, 0, "");
    detailSubtitle_->SetWrap(TextWrap::WrapWord);
    detailSubtitle_->SetTextColor(Color(96, 96, 104));
    details->AddChild(detailSubtitle_);
    PinRow(detailSubtitle_);

    // Property / Value, with a full-width bar per section. A columns tree
    // rather than a list view because the section bars are what make a long
    // property list readable, and the columns tree draws them natively.
    detailView_ = std::make_shared<UltraCanvasColumnsTreeView>("deProperties", 0, 0, 500, 400);
    detailView_->SetDisplayMode(TreeDisplayMode::Columns);
    detailView_->SetRowHeight(24);
    detailView_->SetSelectionMode(TreeSelectionMode::Single);
    detailView_->SetShowExpandButtons(false);
    detailView_->SetLineStyle(TreeLineStyle::NoLine);
    detailView_->SetRootVisible(false);
    detailView_->SetColumns({
        { "name",  "Property", 200, 120, 1.0f, TextAlignment::Left, Color(70, 70, 78),
          Colors::Transparent, 0, /*isTreeColumn*/ true },
        { "value", "Value",    0,   160, 2.0f, TextAlignment::Left, Color(30, 30, 30),
          Colors::Transparent, 0, false },
    });
    detailView_->SetShowColumnHeader(true);
    TreeColumnStyle columnStyle = detailView_->GetColumnStyle();
    columnStyle.groupHeaderBackground = Color(222, 228, 238);
    columnStyle.groupHeaderTextColor  = Color(30, 40, 60);
    detailView_->SetColumnStyle(columnStyle);
    FillWith(details, detailView_);
    return details;
}

// ===== DATA FLOW =====

void DeviceExplorerPanel::StartScan() {
    if (scanning_.exchange(true)) return;
    JoinScan();
    if (rescanButton_) rescanButton_->SetDisabled(true);
    ShowScanning(true);
    RefreshStatus();
    scanThread_ = std::thread([this]() {
        auto& manager = IODeviceManager::GetInstance();
        DeviceInventory scanned = ScanInventory(manager);
        // Monitoring needs the backends Initialize() registered, so it is
        // started after the first scan rather than before it.
        if (!manager.IsMonitoring()) manager.StartMonitoring();
        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            pending_ = std::move(scanned);
        }
        scanning_ = false;
    });
}

void DeviceExplorerPanel::JoinScan() {
    if (scanThread_.joinable()) scanThread_.join();
}

void DeviceExplorerPanel::OnTimer() {
    ApplyPendingPrinterStatus();

    std::optional<DeviceInventory> ready;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        ready.swap(pending_);
    }
    if (ready) {
        JoinScan();
        if (rescanButton_) rescanButton_->SetDisabled(false);
        ShowScanning(false);
        monitoring_ = IODeviceManager::GetInstance().IsMonitoring();
        hotplugChanged_ = false;
        ApplyInventory(std::move(*ready));
        return;
    }
    if (hotplugChanged_.exchange(false) && !scanning_) {
        // The manager has already re-enumerated the category that changed;
        // reading the registry is enough.
        DeviceInventory refreshed = SnapshotInventory(IODeviceManager::GetInstance());
        refreshed.enumerated = inventory_.enumerated;
        refreshed.enumerationMessage = inventory_.enumerationMessage;
        ApplyInventory(std::move(refreshed));
    }
}

void DeviceExplorerPanel::ApplyInventory(DeviceInventory inventory) {
    // A rescan or a hot-plug change can mean a printer was swapped or woke
    // up; what it said before is no longer worth showing.
    statusCache_.clear();
    inventory_ = std::move(inventory);
    RebuildTree();
}

void DeviceExplorerPanel::ShowScanning(bool scanning) {
    if (scanBar_) scanBar_->SetRunning(scanning);
    if (scanRow_) scanRow_->SetVisible(scanning);
}

// ===== PRINTER STATUS =====

void DeviceExplorerPanel::RequestPrinterStatus(const std::string& deviceId) {
    if (deviceId == statusRunningId_ && statusBusy_) return;   // already being asked
    if (statusBusy_) {
        statusQueuedId_ = deviceId;   // latest selection wins
        return;
    }
    JoinStatusQuery();
    statusBusy_ = true;
    statusRunningId_ = deviceId;
    statusThread_ = std::thread([this, deviceId]() {
        PrinterStatusReport report = QueryPrinterStatus(IODeviceManager::GetInstance(), deviceId);
        {
            std::lock_guard<std::mutex> lock(statusMutex_);
            statusPending_ = std::make_pair(deviceId, std::move(report));
        }
        statusBusy_ = false;
    });
}

void DeviceExplorerPanel::JoinStatusQuery() {
    if (statusThread_.joinable()) statusThread_.join();
}

void DeviceExplorerPanel::ApplyPendingPrinterStatus() {
    std::optional<std::pair<std::string, PrinterStatusReport>> ready;
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        ready.swap(statusPending_);
    }
    if (ready) {
        JoinStatusQuery();
        statusRunningId_.clear();
        statusCache_[ready->first] = std::move(ready->second);
        // Only redraw when the answer is for what is on screen; an answer for
        // a printer the user has already clicked away from just waits in the
        // cache.
        if (DeviceNodeId(ready->first) == selectedNodeId_) ShowDetailsFor(selectedNodeId_);
    }
    if (!statusBusy_ && !statusQueuedId_.empty()) {
        const std::string next = statusQueuedId_;
        statusQueuedId_.clear();
        RequestPrinterStatus(next);
    }
}

// ===== TREE =====

std::string DeviceExplorerPanel::IconFor(const IODeviceInfo& info) const {
    switch (info.category) {
        case IODeviceCategory::Printer: return iconsDir_ + "print.png";
        case IODeviceCategory::Scanner: return iconsDir_ + "DeviceExplorer/scanner.svg";
        case IODeviceCategory::Camera:  return iconsDir_ + "DeviceExplorer/camera.svg";
        default:                        return iconsDir_ + "DeviceExplorer/device.svg";
    }
}

std::string DeviceExplorerPanel::IconFor(const DeviceGroup& group) const {
    switch (grouping_) {
        case DeviceGrouping::Category: {
            IODeviceInfo probe;
            probe.category = group.category;
            return IconFor(probe);
        }
        case DeviceGrouping::Connection:
            if (group.transport == IODeviceTransport::Network) return iconsDir_ + "network.svg";
            if (group.transport == IODeviceTransport::USB) return iconsDir_ + "DeviceExplorer/usb.svg";
            return iconsDir_ + "DeviceExplorer/connection.svg";
        case DeviceGrouping::Backend:
            return iconsDir_ + "DeviceExplorer/backend.svg";
    }
    return iconsDir_ + "DeviceExplorer/device.svg";
}

void DeviceExplorerPanel::RebuildTree() {
    if (!deviceTree_) return;
    rebuilding_ = true;
    groups_ = GroupDevices(inventory_, grouping_, filter_);

    const std::string host = machine_.hostName.empty() ? std::string("This computer")
                                                       : machine_.hostName;
    TreeNodeData rootData(kComputerNodeId, host);
    rootData.leftIcon = TreeNodeIcon(iconsDir_ + "DeviceExplorer/computer.svg", 16, 16);
    rootData.tooltip = machine_.operatingSystem;
    deviceTree_->SetRootNode(rootData);

    for (const DeviceGroup& group : groups_) {
        TreeNodeData groupData(GroupNodeId(group),
                               group.title + " (" + std::to_string(group.devices.size()) + ")");
        groupData.leftIcon = TreeNodeIcon(IconFor(group), 16, 16);
        if (group.devices.empty()) groupData.textColor = Color(128, 128, 136);
        deviceTree_->AddNode(kComputerNodeId, groupData);

        for (const DeviceRecord* record : group.devices) {
            const IODeviceInfo& info = record->info;
            TreeNodeData deviceData(DeviceNodeId(info.deviceId), DeviceDisplayName(info));
            deviceData.leftIcon = TreeNodeIcon(IconFor(info), 16, 16);
            deviceData.tooltip = CategoryDisplayName(info.category) + " · " +
                                 TransportDisplayName(info.transport) + " · " +
                                 StateDisplayName(info.state);
            if (info.state == IODeviceState::Offline) deviceData.textColor = Color(128, 128, 136);
            if (info.state == IODeviceState::Error || !record->lastError.empty()) {
                deviceData.textColor = Color(176, 32, 32);
            }
            deviceTree_->AddNode(GroupNodeId(group), deviceData);
        }
    }

    // Open what the user had open: the root always, every group except the
    // ones collapsed by hand. A filter opens everything, since the point of
    // typing one is to see the matches.
    if (TreeNode* root = deviceTree_->GetRootNode()) {
        if (root->HasChildren()) deviceTree_->ExpandNode(root);
        for (const DeviceGroup& group : groups_) {
            TreeNode* node = deviceTree_->FindNode(GroupNodeId(group));
            if (!node || !node->HasChildren()) continue;
            if (filter_.empty() && collapsedGroups_.count(group.key)) continue;
            deviceTree_->ExpandNode(node);
        }
    }

    // Keep the selection on the same device or group. When it went away
    // (unplugged, filtered out) the computer is shown instead, but the choice
    // is remembered: clearing the filter or plugging the device back in
    // brings it back.
    std::string shownId = selectedNodeId_;
    TreeNode* selected = deviceTree_->FindNode(shownId);
    if (!selected) {
        shownId = kComputerNodeId;
        selected = deviceTree_->GetRootNode();
    }
    if (selected) {
        deviceTree_->SelectNode(selected);
        deviceTree_->ScrollTo(selected);
    }
    rebuilding_ = false;

    ShowDetailsFor(shownId);
    RefreshStatus();
    deviceTree_->RequestRedraw();
}

void DeviceExplorerPanel::ShowDetailsFor(const std::string& nodeId) {
    if (nodeId == kComputerNodeId) {
        const std::string host = machine_.hostName.empty() ? std::string("This computer")
                                                           : machine_.hostName;
        std::string subtitle = CountText(inventory_.devices.size(), "device", "devices");
        if (!machine_.operatingSystem.empty()) subtitle = machine_.operatingSystem + " · " + subtitle;
        ShowSections(host, subtitle, DescribeComputer(inventory_, machine_, monitoring_));
        return;
    }
    for (const DeviceGroup& group : groups_) {
        if (GroupNodeId(group) == nodeId) {
            ShowSections(group.title,
                         std::string("Grouped by ") + GroupingName(grouping_) + " · " +
                             CountText(group.devices.size(), "device", "devices"),
                         DescribeGroup(group, grouping_, inventory_));
            return;
        }
        for (const DeviceRecord* record : group.devices) {
            if (DeviceNodeId(record->info.deviceId) != nodeId) continue;
            const IODeviceInfo& info = record->info;
            std::vector<PropertySection> sections =
                DescribeDevice(*record, &UltraCanvasHardwareInfo::MaskIdentifier);
            if (info.category == IODeviceCategory::Printer) {
                // Status first, where the eye lands: it is what someone
                // selecting a printer usually came to see.
                const auto cached = statusCache_.find(info.deviceId);
                const bool fresh = cached != statusCache_.end() &&
                                   NowSeconds() - cached->second.queriedAt <= kPrinterStatusMaxAgeSeconds;
                std::vector<PropertySection> status;
                if (cached != statusCache_.end()) {
                    status = DescribePrinterStatus(cached->second);
                } else {
                    status.push_back({"Printer status", {{"State", "Asking the printer…"}}});
                }
                sections.insert(sections.begin(), status.begin(), status.end());
                if (!fresh) RequestPrinterStatus(info.deviceId);
            }
            ShowSections(DeviceDisplayName(info),
                         CategoryDisplayName(info.category) + " · " +
                             TransportDisplayName(info.transport) + " · " +
                             StateDisplayName(info.state),
                         sections);
            return;
        }
    }
    ShowSections("", "", {});
}

void DeviceExplorerPanel::ShowSections(const std::string& title, const std::string& subtitle,
                                        const std::vector<PropertySection>& sections) {
    if (detailTitle_) detailTitle_->SetText(title);
    if (detailSubtitle_) detailSubtitle_->SetText(subtitle);
    if (!detailView_) return;

    detailView_->SetRootNode(TreeNodeData("details", ""));
    int sectionIndex = 0;
    int rowIndex = 0;
    for (const PropertySection& section : sections) {
        const std::string sectionId = kSectionPrefix + std::to_string(sectionIndex++);
        TreeNodeData header(sectionId, section.title);
        header.isGroupHeader = true;
        detailView_->AddNode("details", header);
        for (const PropertyRow& row : section.rows) {
            TreeNodeData data("row:" + std::to_string(rowIndex++), row.name);
            data.SetCell("value", row.value);
            data.tooltip = row.name + ": " + row.value;
            detailView_->AddNode(sectionId, data);
        }
    }
    detailView_->ExpandAll();
    detailView_->RequestRedraw();
}

void DeviceExplorerPanel::RefreshStatus() {
    std::string status;
    // A running scan is shown by the bar under the tree, not here: the first
    // one leaves this empty, a rescan keeps the last count until it ends.
    if (!inventory_.enumerated) {
        if (!scanning_) status = "Not scanned yet";
    } else {
        status = CountText(inventory_.devices.size(), "device", "devices");
        if (!filter_.empty()) {
            size_t shown = 0;
            for (const DeviceGroup& group : groups_) shown += group.devices.size();
            status += ", " + std::to_string(shown) + " shown";
        }
        status += monitoring_ ? " · watching for changes" : " · press Rescan after plugging in";
        if (inventory_.backends.empty()) status = "No device backend in this build";
    }
    if (statusLabel_) statusLabel_->SetText(status);

    if (subtitleLabel_) {
        std::string subtitle = "The devices connected to ";
        subtitle += machine_.hostName.empty() ? std::string("this computer") : machine_.hostName;
        subtitle += ", as IODeviceManager finds them";
        subtitleLabel_->SetText(subtitle);
    }
}

void DeviceExplorerPanel::ShowAbout() {
    // The same list the computer node shows, so the two never disagree.
    std::string backends;
    for (const PropertySection& section : DescribeComputer(inventory_, machine_, monitoring_)) {
        if (section.title != "Backends in this build") continue;
        for (const PropertyRow& row : section.rows) backends += "\n  " + row.name + ": " + row.value;
    }

    const std::string message =
        "DeviceExplorer " + version_ + "\n"
        "UltraCanvas Framework " + UltraCanvas::versionString + "\n\n"
        "Shows the printers, scanners and cameras connected to this computer, "
        "as the IODeviceManager module finds them. Pick a device in the tree "
        "to see everything its backend reports about it.\n\n"
        "DeviceExplorer only looks. The one thing it opens a device for is to "
        "ask a selected printer for its status and ink or toner, and it closes "
        "it again straight away; it never configures a device or prints. "
        "Serial numbers are masked.\n\n"
        "Backends in this build:" + backends;
    UltraCanvasDialogManager::ShowInformation(message, "About DeviceExplorer", nullptr, GetWindow());
}

} // namespace DeviceExplorer
