// Apps/DeviceExplorer/ui/DeviceExplorerWindow.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
// Before the window header: on Linux that one reaches X11, whose `None`
// macro would otherwise break HardwareQuery::None in this header.
#include "UltraCanvasHardwareInfo.h"

#include "DeviceExplorerWindow.h"

#include "IODeviceManager/UltraCanvasIODeviceManager.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasUtils.h"

#include <algorithm>

// DEVICEEXPLORER_VERSION comes from the build alone: CMake reads the first
// line of Docs/DeviceExplorer/CHANGELOG.md (cmake/UltraCanvasVersion.cmake)
// and passes it as a compile definition. No fallback here, so a build that
// lost it fails instead of reporting a wrong number.
#ifndef DEVICEEXPLORER_VERSION
#error "DEVICEEXPLORER_VERSION is not defined: build through CMake, which reads it from Docs/DeviceExplorer/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace DeviceExplorer {
namespace {

constexpr float kWindowWidth  = 1100.0f;
constexpr float kWindowHeight = 700.0f;
constexpr float kSideMargin   = 8.0f;
constexpr float kPageTop      = 44.0f;
constexpr float kPagePadding  = 10.0f;
constexpr float kSectionGap   = 8.0f;
constexpr int   kTreeWidth    = 360;
constexpr int   kTreeMinWidth = 240;
constexpr int   kDetailsMin   = 360;
constexpr unsigned kUiTimerMs = 200;

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

DeviceExplorerWindow::~DeviceExplorerWindow() {
    // A scan still running would start the watcher behind our back, so it
    // is joined first; then the watcher, whose StopMonitoring() joins its
    // thread, so no change callback runs into a half-destroyed window.
    JoinScan();
    auto& manager = IODeviceManager::GetInstance();
    manager.StopMonitoring();
    manager.SetDeviceChangeCallback(nullptr);
    if (uiTimer_ != 0) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(uiTimer_);
    }
}

bool DeviceExplorerWindow::Initialize(const MachineSummary& machine, DeviceGrouping grouping) {
    machine_ = machine;
    grouping_ = grouping;
    iconsDir_ = NormalizePath(GetResourcesDir() + "media/icons/");

    WindowConfig config;
    config.title  = std::string("DeviceExplorer ") + DEVICEEXPLORER_VERSION;
    config.width  = static_cast<int>(kWindowWidth);
    config.height = static_cast<int>(kWindowHeight);
    config.minWidth  = 760;
    config.minHeight = 460;
    window_ = CreateWindow(config);
    if (!window_) return false;

    auto title = CreateLabel("deTitle", 16, 10, 200, 26, "DeviceExplorer");
    title->SetFontSize(18);
    window_->AddChild(title);
    subtitleLabel_ = CreateLabel("deSubtitle", 190, 14, 860, 22,
                                 "The devices connected to this computer, as IODeviceManager finds them");
    window_->AddChild(subtitleLabel_);

    page_ = CreateContainer("dePage", 0, 0, 0, 0);
    MakePlainColumn(page_);
    page_->SetPadding(kPagePadding);

    auto toolbar = BuildToolbar();
    page_->AddChild(toolbar);
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
    FillWith(detailsPane, BuildDetailsPane());
    FillWith(page_, split_);

    window_->AddChild(page_);
    LayoutForSize(kWindowWidth, kWindowHeight);
    window_->onWindowResize = [this](int width, int height) {
        LayoutForSize(static_cast<float>(width), static_cast<float>(height));
    };

    // Hot-plug: the manager re-enumerates the category on its own thread and
    // tells us; the UI timer picks the flag up and re-reads the registry.
    auto& manager = IODeviceManager::GetInstance();
    manager.SetDeviceChangeCallback([this](IODeviceChange, const IODeviceInfo&) {
        hotplugChanged_ = true;
    });

    // Show the computer node straight away, then fill it in.
    RebuildTree();
    selectedNodeId_ = kComputerNodeId;
    ShowDetailsFor(selectedNodeId_);
    StartScan();

    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
        uiTimer_ = app->StartTimer(kUiTimerMs, /*periodic=*/true,
                                   [this](TimerId) { OnTimer(); });
    }
    return true;
}

void DeviceExplorerWindow::Show() {
    if (window_) window_->Show();
}

void DeviceExplorerWindow::LayoutForSize(float width, float height) {
    if (!page_) return;
    const float pageWidth  = std::max(400.0f, width  - 2 * kSideMargin);
    const float pageHeight = std::max(240.0f, height - kPageTop - kSideMargin);
    // SetElementSize, not SetBounds: the layout engine sizes from the CSS
    // dimensions, so a bounds-only change is overwritten on the next pass.
    page_->SetElementAbsolutePosition(Point2Df(kSideMargin, kPageTop));
    page_->SetElementSize(Size2Df(pageWidth, pageHeight));
    page_->InvalidateLayout();
    if (window_) window_->AddDirtyRectangle(
        Rect2Di(0, 0, static_cast<int>(width), static_cast<int>(height)));
}

// ===== CONSTRUCTION =====

std::shared_ptr<UltraCanvasContainer> DeviceExplorerWindow::BuildToolbar() {
    auto bar = CreateContainer("deToolbar", 0, 0, 0, 34);
    bar->layout.SetFlexRow()
               .SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bar->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(34));
    ContainerStyle plainBar;
    plainBar.autoShowScrollbars = false;
    bar->SetContainerStyle(plainBar);

    auto groupLabel = CreateLabel("deGroupLabel", 0, 0, 72, 24, "Group by");
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

    statusLabel_ = CreateLabel("deStatus", 0, 0, 0, 24, "");
    statusLabel_->layoutItem.SetFlexGrow(1);
    statusLabel_->layoutItem.SetFlexShrink(1);
    bar->AddChild(statusLabel_);
    return bar;
}

std::shared_ptr<UltraCanvasTreeView> DeviceExplorerWindow::BuildDeviceTree() {
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

std::shared_ptr<UltraCanvasContainer> DeviceExplorerWindow::BuildDetailsPane() {
    auto details = CreateContainer("deDetails", 0, 0, 0, 0);
    MakePlainColumn(details);
    details->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());

    detailTitle_ = CreateLabel("deDetailTitle", 0, 0, 0, 26, "");
    detailTitle_->SetFontSize(16);
    detailTitle_->SetFontWeight(FontWeight::Bold);
    details->AddChild(detailTitle_);
    PinRow(detailTitle_);

    detailSubtitle_ = CreateLabel("deDetailSubtitle", 0, 0, 0, 20, "");
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

void DeviceExplorerWindow::StartScan() {
    if (scanning_.exchange(true)) return;
    JoinScan();
    if (rescanButton_) rescanButton_->SetDisabled(true);
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

void DeviceExplorerWindow::JoinScan() {
    if (scanThread_.joinable()) scanThread_.join();
}

void DeviceExplorerWindow::OnTimer() {
    std::optional<DeviceInventory> ready;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        ready.swap(pending_);
    }
    if (ready) {
        JoinScan();
        if (rescanButton_) rescanButton_->SetDisabled(false);
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

void DeviceExplorerWindow::ApplyInventory(DeviceInventory inventory) {
    inventory_ = std::move(inventory);
    RebuildTree();
}

// ===== TREE =====

std::string DeviceExplorerWindow::IconFor(const IODeviceInfo& info) const {
    switch (info.category) {
        case IODeviceCategory::Printer: return iconsDir_ + "print.png";
        case IODeviceCategory::Scanner: return iconsDir_ + "DeviceExplorer/scanner.svg";
        case IODeviceCategory::Camera:  return iconsDir_ + "DeviceExplorer/camera.svg";
        default:                        return iconsDir_ + "DeviceExplorer/device.svg";
    }
}

std::string DeviceExplorerWindow::IconFor(const DeviceGroup& group) const {
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

void DeviceExplorerWindow::RebuildTree() {
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

void DeviceExplorerWindow::ShowDetailsFor(const std::string& nodeId) {
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
            ShowSections(DeviceDisplayName(info),
                         CategoryDisplayName(info.category) + " · " +
                             TransportDisplayName(info.transport) + " · " +
                             StateDisplayName(info.state),
                         DescribeDevice(*record, &UltraCanvasHardwareInfo::MaskIdentifier));
            return;
        }
    }
    ShowSections("", "", {});
}

void DeviceExplorerWindow::ShowSections(const std::string& title, const std::string& subtitle,
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

void DeviceExplorerWindow::RefreshStatus() {
    std::string status;
    if (scanning_) {
        status = "Scanning for devices…";
    } else if (!inventory_.enumerated) {
        status = "Not scanned yet";
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

void DeviceExplorerWindow::ShowAbout() {
    // The same list the computer node shows, so the two never disagree.
    std::string backends;
    for (const PropertySection& section : DescribeComputer(inventory_, machine_, monitoring_)) {
        if (section.title != "Backends in this build") continue;
        for (const PropertyRow& row : section.rows) backends += "\n  " + row.name + ": " + row.value;
    }

    const std::string message =
        std::string("DeviceExplorer ") + DEVICEEXPLORER_VERSION + "\n"
        "UltraCanvas Framework " + UltraCanvas::versionString + "\n\n"
        "Shows the printers, scanners and cameras connected to this computer, "
        "as the IODeviceManager module finds them. Pick a device in the tree "
        "to see everything its backend reports about it.\n\n"
        "DeviceExplorer only looks: it never opens, configures or prints to a "
        "device. Serial numbers are masked.\n\n"
        "Backends in this build:" + backends;
    UltraCanvasDialogManager::ShowInformation(message, "About DeviceExplorer", nullptr, window_.get());
}

} // namespace DeviceExplorer
