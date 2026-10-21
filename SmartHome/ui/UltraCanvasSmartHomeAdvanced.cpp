// ui/UltraCanvasSmartHomeAdvanced.cpp
// The last four dashboard widgets: network topology, energy monitor,
// scheduler and group control.
//
// Each is a container of framework elements: the topology is a node diagram
// under a toolbar (layout, protocol), the energy history a line chart with a
// segmented control for what it plots, the group control buttons, sliders,
// a list view and a dropdown. The scheduler's day and week grid is the one
// picture drawn here - the framework has no calendar element - and its list
// view, view switch and actions are elements.
//
// Version: 2.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHomeAdvancedWidgets.h"
#include "SmartHomeWidgetKit.h"

#include "UltraCanvasApplication.h"
#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
#include "Plugins/Diagrams/UltraCanvasNodeDiagram.h"
#include "UltraCanvasSmartHomeIcons.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace UltraCanvas {
namespace SmartHome {

using namespace Kit;

namespace {

std::string Fixed(double v, int places) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", places, v);   // locale-ok: display text
    return buf;
}

void AsWidget(UltraCanvasContainer& c, float gap) {
    AsColumn(c, gap, 14.0f);
    c.SetBackgroundColor(kPanelBg);
}

std::shared_ptr<UltraCanvasContainer> Gap(const std::string& id) {
    auto s = CreateContainer(id);
    Plain(*s);
    Grow(*s);
    return s;
}

std::shared_ptr<UltraCanvasListView> List(const std::string& id, const std::shared_ptr<RowsModel>& model,
                                          const std::string& accessibleName, bool header) {
    auto list = std::make_shared<UltraCanvasListView>(id, 0, 0, 200, 80);
    list->SetModel(model);
    list->SetShowHeader(header);
    list->SetRowHeight(24);
    list->SetAccessibleName(accessibleName);
    list->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(80));
    Grow(*list);
    return list;
}

std::shared_ptr<UltraCanvasSegmentedControl> Segments(const std::string& id,
                                                      const std::vector<std::string>& labels, float height = 26) {
    constexpr float kSegment = 64.0f;
    auto s = CreateSegmentedControl(id, 0, 0, kSegment * labels.size(), height);
    SegmentedControlStyle style;
    style.fontSize = 10.0f;
    style.selectedColor = kAccent;
    s->SetStyle(style);
    for (const auto& l : labels) s->AddSegment(l);
    Size(*s, kSegment * labels.size(), height);
    return s;
}

// "Sunday" ... for 0-6.
const char* DayName(int day, bool shortName) {
    static const char* kLong[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char* kShort[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    day = std::clamp(day, 0, 6);
    return shortName ? kShort[day] : kLong[day];
}

std::string DeviceName(const std::string& id) {
    const SmartHomeDeviceInfo info = SMARTHOME_API.GetDevice(id);
    return info.Name.empty() ? id : info.Name;
}

}  // namespace

// ============================================================================
// Network topology
// ============================================================================

SmartHomeNetworkTopology::SmartHomeNetworkTopology(const std::string& identifier,
                                                   float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    AsWidget(*this, 8);

    auto bar = Row(identifier + "_Toolbar", 8, 26);
    layoutSelector = Segments(identifier + "_Layout", {"Force", "Radial", "Tree"});
    layoutSelector->SetSelectedIndex(0);
    layoutSelector->onSegmentSelected = [this](int index) {
        static const char* kLayouts[] = {"force", "radial", "tree"};
        if (!syncing && index >= 0 && index < 3) SetLayout(kLayouts[index]);
    };
    protocolChoice = CreateDropdown(identifier + "_Protocol", 0, 0, 130, 26);
    Size(*protocolChoice, 130, 26);
    protocolChoice->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (syncing || index < 0 || index >= static_cast<int>(protocolChoices.size())) return;
        SetProtocolFilter(protocolChoices[static_cast<size_t>(index)]);
    };
    bar->AddChild(layoutSelector);
    bar->AddChild(protocolChoice);
    bar->AddChild(Gap(identifier + "_ToolbarGap"));
    AddChild(bar);

    diagram = CreateNodeDiagram(identifier + "_Diagram", 0, 0, 400, 300);
    diagram->SetBackgroundColor(Color(0xF3, 0xF5, 0xF8));   // the white cards stand out on it
    diagram->SetGridVisible(false);
    diagram->SetNodesConnectable(false);   // a map of the network, not an editor of it
    diagram->SetLinkDistance(190.0);       // room for a card between a router and its devices
    diagram->SetChargeStrength(-30000.0);  // cards are wide: push them well apart
    diagram->SetLegendVisible(true);
    diagram->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(200));
    Grow(*diagram);
    diagram->onNodeClick = [this](const std::string& id) {
        selectedNodeId = id;
        ShowSelection();
        if (onNodeSelected) onNodeSelected(id);
    };
    AddChild(diagram);

    summary = Label(identifier + "_Summary", "", 10, kTextDim);
    AddChild(summary);

    FillProtocolChoice();
}

SmartHomeNetworkTopology::~SmartHomeNetworkTopology() {
    if (refreshTimer) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(refreshTimer);
    }
}

Color SmartHomeNetworkTopology::GetProtocolColor(SmartHomeProtocolType protocol) const {
    switch (protocol) {
        case SmartHomeProtocolType::Matter:    return Color(103, 58, 183);
        case SmartHomeProtocolType::Thread:    return Color(0, 150, 136);
        case SmartHomeProtocolType::Zigbee:    return Color(255, 152, 0);
        case SmartHomeProtocolType::ZWave:     return Color(0, 188, 212);
        case SmartHomeProtocolType::KNX:       return Color(121, 85, 72);
        case SmartHomeProtocolType::WiFi:      return kAccent;
        case SmartHomeProtocolType::Bluetooth: return Color(63, 81, 181);
        default:                               return Color(0x78, 0x90, 0x9C);
    }
}

Color SmartHomeNetworkTopology::GetSignalColor(int signal) const {
    // LQI runs 0-255 and RSSI is negative dBm; treat anything negative as RSSI
    // so one scale does not misreport the other. 0 is "not reported".
    if (signal == 0) return Color(0xB0, 0xBE, 0xC5);
    const int quality = signal < 0 ? std::clamp((signal + 100) * 255 / 70, 0, 255)
                                   : std::clamp(signal, 0, 255);
    if (quality > 170) return kGood;
    if (quality > 85)  return kWarn;
    return kBad;
}

void SmartHomeNetworkTopology::LoadNetworkData() {
    nodes.clear();
    links.clear();

    for (const auto& device : SMARTHOME_API.GetDevices()) {
        if (protocolFilter != SmartHomeProtocolType::Unknown &&
            device.Protocol != protocolFilter) {
            continue;
        }
        TopologyViewNode node;
        node.DeviceId = device.DeviceId;
        node.Name = device.Name.empty() ? device.DeviceId : device.Name;
        node.Protocol = device.Protocol;
        node.State = device.State;
        node.SignalStrength = 0;
        node.X = 0.0f;
        node.Y = 0.0f;
        // Mains-powered kinds route for their neighbours; battery devices do not.
        node.IsRouter = device.Category == SmartHomeDeviceCategory::Gateway ||
                        device.Category == SmartHomeDeviceCategory::Plug ||
                        device.Category == SmartHomeDeviceCategory::Switch;
        node.IsSelected = device.DeviceId == selectedNodeId;
        nodes.push_back(node);
    }

    // Without per-link data from a backend, the honest thing to draw is each
    // device's association with a router of its own protocol, not an invented
    // mesh: a router in the same room when there is one (the nearest, in a
    // mesh), otherwise the one with the fewest devices so far.
    std::map<std::string, std::string> roomOf;
    for (const auto& device : SMARTHOME_API.GetDevices()) roomOf[device.DeviceId] = device.RoomId.empty() ? device.Room : device.RoomId;
    std::map<std::string, int> load;
    for (const auto& node : nodes) {
        if (node.IsRouter) continue;
        const TopologyViewNode* chosen = nullptr;
        for (const auto& candidate : nodes) {
            if (!candidate.IsRouter || candidate.Protocol != node.Protocol) continue;
            if (!roomOf[node.DeviceId].empty() && roomOf[candidate.DeviceId] == roomOf[node.DeviceId]) {
                chosen = &candidate;
                break;
            }
            if (!chosen || load[candidate.DeviceId] < load[chosen->DeviceId]) chosen = &candidate;
        }
        if (!chosen) continue;
        ++load[chosen->DeviceId];
        NetworkLink link;
        link.SourceId = chosen->DeviceId;
        link.TargetId = node.DeviceId;
        link.LinkQuality = node.SignalStrength;
        link.IsActive = node.State == SmartHomeDeviceState::Online;
        links.push_back(link);
    }
}

void SmartHomeNetworkTopology::FillProtocolChoice() {
    syncing = true;
    protocolChoice->ClearItems();
    protocolChoices = {SmartHomeProtocolType::Unknown};
    protocolChoice->AddItem("All radios");
    for (const auto& device : SMARTHOME_API.GetDevices()) {
        if (std::find(protocolChoices.begin(), protocolChoices.end(), device.Protocol) != protocolChoices.end()) continue;
        protocolChoices.push_back(device.Protocol);
        protocolChoice->AddItem(ProtocolTypeToString(device.Protocol));
    }
    const auto it = std::find(protocolChoices.begin(), protocolChoices.end(), protocolFilter);
    protocolChoice->SetSelectedIndex(it == protocolChoices.end() ? 0 : static_cast<int>(it - protocolChoices.begin()), false);
    syncing = false;
}

// The nodes and links into the diagram, placed by the chosen layout, as
// cards: the device's icon on a disc of its radio's colour, its name, and
// under it its room (and "router" for a router). Force runs the diagram's
// simulation; radial puts the routers in the middle and the rest on a ring
// big enough for them; tree puts each router over a block of its own
// devices. Whatever the layout leaves touching is moved apart.
void SmartHomeNetworkTopology::FillDiagram() {
    diagram->Clear();
    diagram->ClearLegendEntries();

    constexpr double kLeafW = 150.0, kLeafH = 40.0;
    constexpr double kHubW = 170.0, kHubH = 48.0;
    constexpr double kGapX = 24.0, kGapY = 18.0;

    std::map<std::string, SmartHomeDeviceInfo> infos;
    for (const auto& d : SMARTHOME_API.GetDevices()) infos[d.DeviceId] = d;

    std::vector<TopologyViewNode*> hubs, leaves;
    for (auto& n : nodes) (n.IsRouter ? hubs : leaves).push_back(&n);

    if (layoutAlgorithm == "tree") {
        // Each router heads a column of its devices (a second column only past
        // eight, so the connectors run straight down);
        // devices with no router of their radio make a block of their own.
        std::map<std::string, std::vector<TopologyViewNode*>> under;
        std::vector<TopologyViewNode*> unrouted;
        for (auto* leaf : leaves) {
            auto it = std::find_if(links.begin(), links.end(),
                                   [&](const NetworkLink& l) { return l.TargetId == leaf->DeviceId; });
            if (it == links.end()) unrouted.push_back(leaf);
            else under[it->SourceId].push_back(leaf);
        }
        constexpr int kRows = 8;
        double x = 0.0;
        auto block = [&](TopologyViewNode* head, const std::vector<TopologyViewNode*>& members) {
            const int columns = std::max(1, static_cast<int>((members.size() + kRows - 1) / kRows));
            const double width = std::max(kHubW, columns * (kLeafW + kGapX) - kGapX);
            if (head) {
                head->X = static_cast<float>(x + width / 2.0);
                head->Y = static_cast<float>(kHubH / 2.0);
            }
            for (size_t i = 0; i < members.size(); ++i) {
                const int col = static_cast<int>(i / kRows), row = static_cast<int>(i % kRows);
                const double blockLeft = x + (width - (columns * (kLeafW + kGapX) - kGapX)) / 2.0;
                members[i]->X = static_cast<float>(blockLeft + col * (kLeafW + kGapX) + kLeafW / 2.0);
                members[i]->Y = static_cast<float>(kHubH + 50.0 + row * (kLeafH + kGapY) + kLeafH / 2.0);
            }
            x += width + 3 * kGapX;
        };
        for (auto* hub : hubs) block(hub, under[hub->DeviceId]);
        if (!unrouted.empty()) block(nullptr, unrouted);
    } else {
        // Devices around the ring in their routers' order, so each router's
        // devices form one arc; the router sits inside, towards its arc.
        auto routerOf = [&](const TopologyViewNode* leaf) -> std::string {
            for (const auto& l : links) if (l.TargetId == leaf->DeviceId) return l.SourceId;
            return {};
        };
        std::stable_sort(leaves.begin(), leaves.end(), [&](const TopologyViewNode* a, const TopologyViewNode* b) {
            auto rank = [&](const std::string& id) {
                for (size_t i = 0; i < hubs.size(); ++i) if (hubs[i]->DeviceId == id) return static_cast<int>(i);
                return static_cast<int>(hubs.size());
            };
            return rank(routerOf(a)) < rank(routerOf(b));
        });
        const double radius = std::max(230.0, leaves.size() * 100.0 / (2.0 * M_PI));
        std::map<std::string, std::pair<double, double>> pull;   // router -> sum of its devices' directions
        for (size_t i = 0; i < leaves.size(); ++i) {
            const double t = 2.0 * M_PI * static_cast<double>(i) / std::max<size_t>(leaves.size(), 1) - M_PI / 2.0;
            // Wider than tall: the cards are.
            leaves[i]->X = static_cast<float>(radius * 1.35 * std::cos(t));
            leaves[i]->Y = static_cast<float>(radius * std::sin(t));
            auto& p = pull[routerOf(leaves[i])];
            p.first += std::cos(t);
            p.second += std::sin(t);
        }
        for (size_t i = 0; i < hubs.size(); ++i) {
            const auto it = pull.find(hubs[i]->DeviceId);
            double t = 2.0 * M_PI * static_cast<double>(i) / std::max<size_t>(hubs.size(), 1);
            if (it != pull.end() && (it->second.first != 0.0 || it->second.second != 0.0)) {
                t = std::atan2(it->second.second, it->second.first);
            }
            const double r = hubs.size() == 1 ? 0.0 : radius * 0.5;
            hubs[i]->X = static_cast<float>(r * 1.35 * std::cos(t));
            hubs[i]->Y = static_cast<float>(r * std::sin(t));
        }
    }

    std::vector<SmartHomeProtocolType> shown;
    for (const auto& n : nodes) {
        const auto info = infos.find(n.DeviceId);
        const bool online = n.State == SmartHomeDeviceState::Online;
        NodeDiagramNode dn(n.DeviceId, n.Name);
        dn.shape = NodeShape::Rectangle;
        dn.cornerRadius = 10.0;
        dn.shadow = true;
        dn.width = n.IsRouter ? kHubW : kLeafW;
        dn.height = n.IsRouter ? kHubH : kLeafH;
        dn.x = n.X - dn.width / 2.0;
        dn.y = n.Y - dn.height / 2.0;
        dn.fillColor = Colors::White;
        dn.borderColor = n.IsRouter ? GetProtocolColor(n.Protocol) : Color(0xDD, 0xE3, 0xEA);
        dn.borderWidth = n.IsRouter ? 2.0 : 1.0;
        dn.textColor = online ? kText : kTextDim;
        dn.fontSize = n.IsRouter ? 11.0 : 10.0;
        if (info != infos.end()) {
            dn.iconPath = SmartHomeDeviceIconPath(info->second);
            dn.iconBackground = online ? SmartHomeCategoryColor(info->second.Category) : Color(0xB0, 0xBE, 0xC5);
            dn.sublabel = info->second.Room.empty() ? std::string("No room") : info->second.Room;
        }
        if (n.IsRouter) dn.sublabel = "Router · " + dn.sublabel;
        if (!online) dn.sublabel = DeviceStateToString(n.State) + " · " + dn.sublabel;
        dn.deletable = false;
        diagram->AddNode(dn);
        if (std::find(shown.begin(), shown.end(), n.Protocol) == shown.end()) {
            shown.push_back(n.Protocol);
            diagram->AddLegendEntry(ProtocolTypeToString(n.Protocol), GetProtocolColor(n.Protocol));
        }
    }
    int linkNo = 0;
    for (const auto& l : links) {
        NodeDiagramLink dl("link" + std::to_string(linkNo++), l.SourceId, l.TargetId);
        dl.directed = false;
        dl.lineColor = l.IsActive ? GetSignalColor(l.LinkQuality) : kTrack;
        dl.lineWidth = l.IsActive ? 1.5 : 1.0;
        dl.style = layoutAlgorithm == "tree" ? LinkStyle::SmoothStep : LinkStyle::Straight;
        dl.deletable = false;
        diagram->AddLink(dl);
    }

    if (layoutAlgorithm == "force") {
        diagram->SetLayout(NodeDiagramLayout::ForceDirected);
        diagram->RunLayout();
    } else {
        diagram->SetLayout(NodeDiagramLayout::Manual);
        diagram->ResolveOverlaps(10.0);
    }
    diagram->FitView();
    needsFit = true;
    if (!selectedNodeId.empty()) diagram->SelectNode(selectedNodeId);
    ShowSelection();
}

void SmartHomeNetworkTopology::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (needsFit && diagram->GetWidth() > 0 && diagram->GetHeight() > 0) {
        needsFit = false;
        if (layoutAlgorithm == "force") diagram->RunLayout();   // the simulation spreads over the real size
        diagram->FitView();
    }
    UltraCanvasContainer::Render(ctx, dirtyRect);
}

void SmartHomeNetworkTopology::ShowSelection() {
    std::string text = std::to_string(nodes.size()) + " devices, " + std::to_string(links.size()) + " links";
    auto it = std::find_if(nodes.begin(), nodes.end(),
                           [&](const TopologyViewNode& n){ return n.DeviceId == selectedNodeId; });
    if (it != nodes.end()) {
        text = it->Name + " · " + ProtocolTypeToString(it->Protocol) + (it->IsRouter ? " · router" : "") +
               "   (" + text + ")";
    }
    if (nodes.empty()) text = "No devices to map";
    summary->SetText(text);
}

void SmartHomeNetworkTopology::RefreshTopology() {
    LoadNetworkData();
    FillProtocolChoice();
    FillDiagram();
    RequestRedraw();
}

void SmartHomeNetworkTopology::SetProtocolFilter(SmartHomeProtocolType protocol) {
    protocolFilter = protocol;
    RefreshTopology();
}

void SmartHomeNetworkTopology::SetLayout(const std::string& algorithm) {
    if (algorithm != "force" && algorithm != "radial" && algorithm != "tree") return;
    layoutAlgorithm = algorithm;
    syncing = true;
    layoutSelector->SetSelectedIndex(algorithm == "radial" ? 1 : algorithm == "tree" ? 2 : 0);
    syncing = false;
    FillDiagram();
    RequestRedraw();
}

void SmartHomeNetworkTopology::SetAutoRefresh(bool enable, int intervalMs) {
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (refreshTimer && app) app->StopTimer(refreshTimer);
    refreshTimer = 0;
    // A zero or negative interval would refresh on every tick; keep a floor.
    if (enable && app) {
        refreshTimer = app->StartTimer(static_cast<unsigned>(std::max(intervalMs, 250)), /*periodic=*/true,
                                       [this](TimerId) { RefreshTopology(); });
    }
}

// ============================================================================
// Energy monitor
// ============================================================================

SmartHomeEnergyMonitor::SmartHomeEnergyMonitor(const std::string& identifier,
                                               float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    AsWidget(*this, 6);

    powerLabel = Label(identifier + "_Power", "0 W", 26, kText, true);
    AddChild(powerLabel);
    sourceLabel = Label(identifier + "_Source", "whole home", 10, kTextDim);
    AddChild(sourceLabel);

    modeSelector = Segments(identifier + "_Mode", {"Power", "Energy", "Cost"});
    modeSelector->SetSelectedIndex(0);
    modeSelector->onSegmentSelected = [this](int index) {
        if (index < 0 || index > 2) return;
        chartMode = index;
        Refresh();
    };
    AddChild(modeSelector);

    chartData = std::make_shared<ChartDataVector>();
    chart = CreateLineChartElement(identifier + "_Chart", 0, 0, 300, 120);
    chart->SetLineColor(kAccent);
    chart->SetLineWidth(1.5f);
    chart->SetShowDataPoints(false);
    chart->SetShowValueLabels(false);
    chart->SetBackgroundColor(kPanelBg);
    chart->SetPlotAreaColor(kPanelBg);
    // A sparkline at this size; the range is written above it.
    chart->SetShowAxes(false);
    chart->SetShowGrid(false);
    chart->SetXAxisLabelMode(XAxisLabelMode::DataLabel);
    chart->SetEmptyMessage("No readings yet");
    chart->SetEmptyMessageColor(kTextDim);
    chart->SetDataSource(chartData);
    chart->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(120));
    Grow(*chart);
    AddChild(chart);

    auto stats = Row(identifier + "_Stats", 6, 34);
    for (const char* name : {"today", "average", "peak", "cost"}) {
        auto cell = CreateContainer(identifier + "_Stat_" + name);
        AsColumn(*cell, 0, 0);
        Grow(*cell);
        cell->layoutItem.SetFlexBasis(CSSLayout::Dimension::Auto());   // wide enough for "0.59 kWh"
        cell->AddChild(Label(identifier + "_StatName_" + name, name, 9, kTextDim));
        auto value = Label(identifier + "_StatValue_" + name, "-", 10, kText, true);
        cell->AddChild(value);
        statValues.push_back(value);
        stats->AddChild(cell);
    }
    AddChild(stats);
    Refresh();
}

SmartHomeEnergyMonitor::~SmartHomeEnergyMonitor() = default;

void SmartHomeEnergyMonitor::SetDevice(const std::string& id) {
    deviceId = id;
    readings.clear();
    stats = EnergyStats{};
    Refresh();
}

void SmartHomeEnergyMonitor::SetTimeRange(int hours) {
    timeRangeHours = std::max(hours, 1);
    CalculateStats();
    Refresh();
}

void SmartHomeEnergyMonitor::SetCostRate(float rate, const std::string& symbol) {
    costPerKwh = rate;
    currency = symbol;
    CalculateStats();
    Refresh();
}

void SmartHomeEnergyMonitor::AddReading(const EnergyReading& reading) {
    readings.push_back(reading);
    constexpr size_t kMax = 4096;
    if (readings.size() > kMax) {
        readings.erase(readings.begin(), readings.begin() + static_cast<long>(readings.size() - kMax));
    }
    CalculateStats();
    Refresh();
}

void SmartHomeEnergyMonitor::CalculateStats() {
    stats = EnergyStats{};
    if (readings.empty()) return;

    const uint64_t newest = readings.back().Timestamp;
    const uint64_t day = 24ull * 3600ull * 1000ull;

    double sum = 0.0;
    size_t counted = 0;
    for (const auto& r : readings) {
        stats.PeakPower = std::max(stats.PeakPower, r.Power);
        sum += r.Power;
        ++counted;
        if (newest <= day || r.Timestamp >= newest - day) {
            stats.TotalEnergyToday = std::max(stats.TotalEnergyToday, r.Energy);
        }
    }
    stats.AveragePower = counted ? static_cast<float>(sum / counted) : 0.0f;

    // Energy is cumulative, so the month's usage is the span across the window
    // rather than a sum of the samples.
    stats.TotalEnergyMonth = readings.back().Energy - readings.front().Energy;
    if (stats.TotalEnergyMonth < 0.0f) stats.TotalEnergyMonth = readings.back().Energy;

    // Today's figure is a high-water mark of a cumulative counter; subtracting
    // the first sample in the window keeps it a delta rather than the total
    // since the meter was installed.
    float base = readings.front().Energy;
    for (const auto& r : readings) {
        if (newest <= day || r.Timestamp >= newest - day) { base = r.Energy; break; }
    }
    stats.TotalEnergyToday = std::max(0.0f, stats.TotalEnergyToday - base);
    stats.EstimatedCost = stats.TotalEnergyMonth * costPerKwh;
}

void SmartHomeEnergyMonitor::Refresh() {
    powerLabel->SetText(Fixed(readings.empty() ? 0.0 : readings.back().Power, 0) + " W");

    // The window the chart shows, from the newest reading back.
    std::vector<ChartDataPoint> points;
    if (!readings.empty()) {
        const uint64_t newest = readings.back().Timestamp;
        const uint64_t window = static_cast<uint64_t>(timeRangeHours) * 3600ull * 1000ull;
        size_t first = 0;
        for (size_t i = readings.size(); i-- > 0;) {
            if (newest > window && readings[i].Timestamp < newest - window) { first = i + 1; break; }
        }
        // Energy and cost from the start of the window, not since the meter
        // was installed.
        const float energyBase = readings[first].Energy;
        for (size_t i = first; i < readings.size(); ++i) {
            const double used = std::max(0.0f, readings[i].Energy - energyBase);
            const double v = chartMode == 0 ? readings[i].Power : chartMode == 1 ? used : used * costPerKwh;
            const time_t t = static_cast<time_t>(readings[i].Timestamp / 1000);
            std::tm local{};
            localtime_r(&t, &local);
            char clock[8];
            std::snprintf(clock, sizeof clock, "%02d:%02d", local.tm_hour, local.tm_min);
            points.emplace_back(static_cast<double>(points.size()), v, 0.0, clock, 0.0);
        }
    }
    static const double kSpan[] = {20.0, 0.1, 0.05};   // W, kWh, money: a floor, as for sensors
    chart->SetMinimumValueSpan(kSpan[std::clamp(chartMode, 0, 2)]);

    std::string source = deviceId.empty() ? "whole home" : DeviceName(deviceId);
    if (points.size() >= 2) {
        auto [lo, hi] = std::minmax_element(points.begin(), points.end(),
            [](const ChartDataPoint& a, const ChartDataPoint& b) { return a.y < b.y; });
        const int places = chartMode == 0 ? 0 : 2;
        const std::string unit = chartMode == 0 ? " W" : chartMode == 1 ? " kWh" : "";
        const std::string money = chartMode == 2 ? currency : "";
        source += " · " + std::to_string(timeRangeHours) + " h: " + money + Fixed(lo->y, places) + " - " +
                  money + Fixed(hi->y, places) + unit;
    }
    sourceLabel->SetText(source);
    chartData->LoadFromArray(points);
    chart->SetDataSource(chartData);

    statValues[0]->SetText(Fixed(stats.TotalEnergyToday, 2) + " kWh");
    statValues[1]->SetText(Fixed(stats.AveragePower, 0) + " W");
    statValues[2]->SetText(Fixed(stats.PeakPower, 0) + " W");
    statValues[3]->SetText(currency + Fixed(stats.EstimatedCost, 2));
    RequestRedraw();
}

// ============================================================================
// Scheduler
// ============================================================================

// The day or week as a time grid with the events on it: a press on an event
// selects it, a press on empty time asks the host to create one there.
// Drawn here because the framework has no calendar or timetable element.
class SmartHomeSchedulerGrid : public UltraCanvasUIElement {
public:
    SmartHomeSchedulerGrid(const std::string& id, SmartHomeScheduler& owner)
        : UltraCanvasUIElement(id, 0, 0, 300, 200), owner(owner) {}

    void Render(IRenderContext* ctx, const Rect2Df&) override {
        if (!ctx || !IsVisible()) return;
        const Rect2Df b = GetLocalBounds();
        ctx->SetFillPaint(Colors::White);
        ctx->FillRectangle(Rect2Dd(b));

        const bool week = owner.viewMode == "week";
        const double dayW = (b.width - kLeft) / (week ? 7.0 : 1.0);
        ctx->SetFontSize(9.0);
        ctx->SetTextPaint(kTextDim);
        if (week) {
            for (int i = 0; i < 7; ++i) {
                ctx->DrawTextInRect(DayName(i, true), Rect2Dd(kLeft + i * dayW + 2.0, 2.0, dayW, 12.0));
            }
        } else {
            ctx->DrawTextInRect(DayName(owner.selectedDay, false), Rect2Dd(kLeft + 2.0, 2.0, 120.0, 12.0));
        }

        // Hour lines every three hours.
        const double height = b.height - kTop;
        ctx->SetStrokePaint(kTrack);
        ctx->SetStrokeWidth(1.0);
        ctx->SetFontSize(8.0);
        for (int hour = 0; hour <= 24; hour += 3) {
            const double y = kTop + height * (hour / 24.0);
            ctx->DrawLine(Point2Dd(kLeft, y), Point2Dd(b.width, y));
            ctx->DrawTextInRect(std::to_string(hour) + ":00", Rect2Dd(0.0, y - 5.0, kLeft - 2.0, 10.0));
        }

        for (const auto& [e, rect] : Blocks()) {
            const bool chosen = e->EventId == owner.selectedEventId;
            ctx->SetFillPaint(!e->Enabled ? kTrack : (chosen ? kText : kAccent));
            ctx->FillRoundedRectangle(rect, 3.0);
            ctx->SetTextPaint(e->Enabled ? Colors::White : kTextDim);
            ctx->DrawTextInRect(e->Name, Rect2Dd(rect.x + 3.0, rect.y + 2.0, rect.width - 6.0, 10.0));
        }
    }

    bool OnEvent(const UCEvent& event) override {
        if (!IsVisible() || IsDisabled()) return false;
        if (event.type != UCEventType::MouseDown && event.type != UCEventType::TouchStart) {
            return UltraCanvasUIElement::OnEvent(event);
        }
        // The pointer arrives in the element's own coordinates.
        const Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
        if (!Contains(p)) return false;
        const Rect2Df b = GetLocalBounds();
        const double lx = p.x, ly = p.y;
        for (const auto& [e, rect] : Blocks()) {
            if (rect.Contains(Point2Dd(lx, ly))) { owner.Select(e->EventId); return true; }
        }
        // Empty time: a request to create something at that day and hour.
        const bool week = owner.viewMode == "week";
        const double dayW = (b.width - kLeft) / (week ? 7.0 : 1.0);
        const double height = b.height - kTop;
        if (ly >= kTop && lx >= kLeft && height > 0.0) {
            const int hour = std::clamp(static_cast<int>((ly - kTop) / height * 24.0), 0, 23);
            const int day = week ? std::clamp(static_cast<int>((lx - kLeft) / dayW), 0, 6) : owner.selectedDay;
            if (owner.onEventCreate) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "%02d:00", hour);
                owner.onEventCreate(buf, day);
            }
        }
        return true;
    }

private:
    static constexpr double kLeft = 30.0;   // hour labels
    static constexpr double kTop = 16.0;    // day names

    // Where each event is drawn, element-local: one block per day it runs.
    std::vector<std::pair<const ScheduledEvent*, Rect2Dd>> Blocks() const {
        std::vector<std::pair<const ScheduledEvent*, Rect2Dd>> out;
        const Rect2Df b = GetBounds();
        const bool week = owner.viewMode == "week";
        const double dayW = (b.width - kLeft) / (week ? 7.0 : 1.0);
        const double height = b.height - kTop;
        for (const auto& e : owner.events) {
            // "HH:MM" - anything else has no place on a clock face.
            int hh = 0, mm = 0;
            if (std::sscanf(e.Time.c_str(), "%d:%d", &hh, &mm) != 2) continue;
            if (hh < 0 || hh > 23 || mm < 0 || mm > 59) continue;
            const double y = kTop + height * ((hh + mm / 60.0) / 24.0);
            for (int day : e.Days) {
                if (day < 0 || day > 6) continue;
                if (!week && day != owner.selectedDay) continue;
                const double x = kLeft + (week ? day * dayW : 0.0);
                out.emplace_back(&e, Rect2Dd(x + 1.0, y, dayW - 2.0, 14.0));
            }
        }
        return out;
    }

    SmartHomeScheduler& owner;
};

SmartHomeScheduler::SmartHomeScheduler(const std::string& identifier,
                                       float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    AsWidget(*this, 8);

    auto bar = Row(identifier + "_Toolbar", 8, 26);
    viewSelector = Segments(identifier + "_View", {"Day", "Week", "List"});
    viewSelector->onSegmentSelected = [this](int index) {
        static const char* kModes[] = {"day", "week", "list"};
        if (!syncing && index >= 0 && index < 3) SetViewMode(kModes[index]);
    };
    bar->AddChild(viewSelector);
    bar->AddChild(Gap(identifier + "_ToolbarGap"));
    AddChild(bar);

    std::vector<std::string> days;
    for (int i = 0; i < 7; ++i) days.push_back(DayName(i, true));
    daySelector = CreateSegmentedControl(identifier + "_Day", 0, 0, 300, 24);
    SegmentedControlStyle dayStyle;
    dayStyle.fontSize = 9.0f;
    dayStyle.selectedColor = kAccent;
    daySelector->SetStyle(dayStyle);
    for (const auto& d : days) daySelector->AddSegment(d);
    Height(*daySelector, 24);
    daySelector->onSegmentSelected = [this](int index) {
        if (syncing || index < 0 || index > 6) return;
        selectedDay = index;
        Refresh();
    };
    AddChild(daySelector);

    grid = std::make_shared<SmartHomeSchedulerGrid>(identifier + "_Grid", *this);
    grid->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(200));
    Grow(*grid);
    AddChild(grid);

    eventModel = std::make_shared<RowsModel>(std::vector<ListColumnDef>{{"Time", 60}, {"Name", 170}, {"Days", 120}, {"", 50}});
    eventList = List(identifier + "_Events", eventModel, "Scheduled events", true);
    eventList->onItemClicked = [this](int row) {
        if (row >= 0 && row < static_cast<int>(events.size())) Select(events[static_cast<size_t>(row)].EventId);
    };
    AddChild(eventList);

    auto actions = Row(identifier + "_Actions", 8, 28);
    toggleButton = Kit::Button(identifier + "_Toggle", "Turn off");
    Size(*toggleButton, 90, 28);
    toggleButton->SetOnClick([this]() { if (!selectedEventId.empty()) ToggleEvent(selectedEventId); });
    removeButton = Kit::Button(identifier + "_Remove", "Remove");
    Size(*removeButton, 90, 28);
    removeButton->SetOnClick([this]() { if (!selectedEventId.empty()) RemoveEvent(selectedEventId); });
    actions->AddChild(toggleButton);
    actions->AddChild(removeButton);
    AddChild(actions);

    Refresh();
}

SmartHomeScheduler::~SmartHomeScheduler() = default;

void SmartHomeScheduler::RefreshSchedule() {
    // Scheduled events belong to automations with a time trigger; until the
    // manager exposes them directly, what the widget already holds is the
    // schedule, and hosts add to it through AddEvent.
    Refresh();
}

void SmartHomeScheduler::AddEvent(const ScheduledEvent& event) {
    auto it = std::find_if(events.begin(), events.end(),
                           [&](const ScheduledEvent& e){ return e.EventId == event.EventId; });
    if (it != events.end()) *it = event;    // adding the same id twice is an edit
    else                    events.push_back(event);
    Refresh();
}

void SmartHomeScheduler::RemoveEvent(const std::string& eventId) {
    events.erase(std::remove_if(events.begin(), events.end(),
                                [&](const ScheduledEvent& e){ return e.EventId == eventId; }),
                 events.end());
    if (selectedEventId == eventId) selectedEventId.clear();
    Refresh();
}

void SmartHomeScheduler::ToggleEvent(const std::string& eventId) {
    for (auto& e : events) {
        if (e.EventId == eventId) { e.Enabled = !e.Enabled; break; }
    }
    Refresh();
}

void SmartHomeScheduler::SetViewMode(const std::string& mode) {
    if (mode != "day" && mode != "week" && mode != "list") return;   // ignore nonsense
    viewMode = mode;
    Refresh();
}

void SmartHomeScheduler::Select(const std::string& eventId) {
    selectedEventId = eventId;
    Refresh();
    if (onEventSelected) onEventSelected(eventId);
}

void SmartHomeScheduler::Refresh() {
    syncing = true;
    viewSelector->SetSelectedIndex(viewMode == "day" ? 0 : viewMode == "list" ? 2 : 1);
    daySelector->SetSelectedIndex(selectedDay);
    syncing = false;
    daySelector->SetVisible(viewMode == "day");
    grid->SetVisible(viewMode != "list");
    eventList->SetVisible(viewMode == "list");

    std::vector<std::vector<std::string>> rows;
    for (const auto& e : events) {
        std::string days;
        for (int d : e.Days) {
            if (d < 0 || d > 6) continue;
            if (!days.empty()) days += " ";
            days += DayName(d, true);
        }
        rows.push_back({e.Time, e.Name, days, e.Enabled ? "on" : "off"});
    }
    eventModel->Set(std::move(rows));

    auto it = std::find_if(events.begin(), events.end(),
                           [&](const ScheduledEvent& e){ return e.EventId == selectedEventId; });
    toggleButton->SetDisabled(it == events.end());
    removeButton->SetDisabled(it == events.end());
    toggleButton->SetText(it != events.end() && !it->Enabled ? "Turn on" : "Turn off");
    grid->RequestRedraw();
    InvalidateLayout();
    RequestRedraw();
}

// ============================================================================
// Group control
// ============================================================================

SmartHomeGroupControl::SmartHomeGroupControl(const std::string& identifier,
                                             float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    AsWidget(*this, 8);

    title = Label(identifier + "_Title", "Group", 14, kText, true);
    AddChild(title);
    countLabel = Label(identifier + "_Count", "", 10, kTextDim);
    AddChild(countLabel);

    auto master = Row(identifier + "_Master", 8, 28);
    allOnButton = Kit::Button(identifier + "_AllOn", "All on");
    Size(*allOnButton, 80, 28);
    allOnButton->SetOnClick([this]() { AllOn(); });
    allOffButton = Kit::Button(identifier + "_AllOff", "All off");
    Size(*allOffButton, 80, 28);
    allOffButton->SetOnClick([this]() { AllOff(); });
    master->AddChild(allOnButton);
    master->AddChild(allOffButton);
    AddChild(master);

    auto sliderRow = [&](const std::string& key, const std::string& caption,
                         std::shared_ptr<UltraCanvasLabel>& value) {
        auto row = Row(identifier + "_" + key + "Row", 6, 18);
        auto label = Label(identifier + "_" + key + "Caption", caption, 11, kTextDim);
        Grow(*label);
        value = Label(identifier + "_" + key + "Value", "", 11, kText, true);
        value->SetAlignment(TextAlignment::Right);
        Size(*value, 70, 18);
        row->AddChild(label);
        row->AddChild(value);
        AddChild(row);
    };
    sliderRow("Brightness", "Brightness", brightnessValue);
    brightness = Kit::Slider(identifier + "_Brightness", 0, 100, kAccent);
    brightness->onValueChanged = [this](float v) {
        if (!syncing) SetGroupBrightness(static_cast<uint8_t>(std::lround(v)));
    };
    AddChild(brightness);
    sliderRow("ColorTemp", "Colour temperature", colorTempValue);
    colorTemp = Kit::Slider(identifier + "_ColorTemp", 2200, 6500, Color(0xFF, 0xC1, 0x07));
    colorTemp->SetStep(50);
    colorTemp->SetTrackGradient({GradientStop(0.0, Color(255, 176, 80)),
                                 GradientStop(0.5, Color(255, 236, 210)),
                                 GradientStop(1.0, Color(200, 222, 255))});
    colorTemp->onValueChanged = [this](float k) {
        if (!syncing) SetGroupColorTemp(static_cast<uint16_t>(std::lround(k)));
    };
    AddChild(colorTemp);

    deviceModel = std::make_shared<RowsModel>(std::vector<ListColumnDef>{{"Device", 140}, {"State", 50}});
    deviceList = List(identifier + "_Devices", deviceModel, "Devices in the group", false);
    AddChild(deviceList);

    auto edit = Row(identifier + "_Edit", 6, 28);
    deviceChoice = CreateDropdown(identifier + "_DeviceChoice", 0, 0, 140, 28);
    Grow(*deviceChoice);
    addButton = Kit::Button(identifier + "_Add", "Add");
    Size(*addButton, 56, 28);
    addButton->SetOnClick([this]() {
        const int index = deviceChoice->GetSelectedIndex();
        if (index >= 0 && index < static_cast<int>(candidates.size())) {
            AddDevice(candidates[static_cast<size_t>(index)].DeviceId);
        }
    });
    removeButton = Kit::Button(identifier + "_Remove", "Remove");
    Size(*removeButton, 72, 28);
    removeButton->SetOnClick([this]() {
        const int row = deviceList->GetSelection() ? deviceList->GetSelection()->GetCurrentRow() : -1;
        if (row >= 0 && row < static_cast<int>(devices.size())) RemoveDevice(devices[static_cast<size_t>(row)].DeviceId);
    });
    edit->AddChild(deviceChoice);
    edit->AddChild(addButton);
    edit->AddChild(removeButton);
    AddChild(edit);

    UpdateGroupState();
}

SmartHomeGroupControl::~SmartHomeGroupControl() = default;

void SmartHomeGroupControl::UpdateGroupState() {
    devices.clear();
    for (const auto& id : group.DeviceIds) {
        SmartHomeDeviceInfo info = SMARTHOME_API.GetDevice(id);
        if (!info.DeviceId.empty()) devices.push_back(info);
    }
    // The master reads on when anything in the group is on, so "All off" is
    // always the way to make the room dark.
    masterOn = false;
    for (const auto& d : devices) {
        if (SMARTHOME_API.GetSwitchState(d.DeviceId)) { masterOn = true; break; }
    }

    syncing = true;
    title->SetText(group.Name.empty() ? "Group" : group.Name);
    countLabel->SetText(std::to_string(group.DeviceIds.size()) + " devices" + (masterOn ? ", some on" : ""));
    brightness->SetValue(masterBrightness);
    brightnessValue->SetText(std::to_string(masterBrightness) + " %");
    colorTemp->SetValue(masterColorTemp);
    colorTempValue->SetText(std::to_string(masterColorTemp) + " K");
    syncing = false;

    std::vector<std::vector<std::string>> rows;
    for (const auto& d : devices) {
        rows.push_back({d.Name.empty() ? d.DeviceId : d.Name,
                        d.State == SmartHomeDeviceState::Online
                            ? (SMARTHOME_API.GetSwitchState(d.DeviceId) ? "on" : "off")
                            : DeviceStateToString(d.State)});
    }
    deviceModel->Set(std::move(rows));
    removeButton->SetDisabled(devices.empty());
    FillDeviceChoice();
    RequestRedraw();
}

void SmartHomeGroupControl::FillDeviceChoice() {
    candidates.clear();
    deviceChoice->ClearItems();
    for (const auto& d : SMARTHOME_API.GetDevices()) {
        if (std::find(group.DeviceIds.begin(), group.DeviceIds.end(), d.DeviceId) != group.DeviceIds.end()) continue;
        candidates.push_back(d);
        deviceChoice->AddItem(d.Name.empty() ? d.DeviceId : d.Name);
    }
    if (!candidates.empty()) deviceChoice->SetSelectedIndex(0, false);
    addButton->SetDisabled(candidates.empty());
}

void SmartHomeGroupControl::SetGroup(const std::string& groupId) {
    group.GroupId = groupId;
    UpdateGroupState();
}

void SmartHomeGroupControl::CreateGroup(const std::string& name,
                                        const std::vector<std::string>& deviceIds) {
    group = DeviceGroup{};
    group.GroupId = "group-" + name;
    group.Name = name;
    group.DeviceIds = deviceIds;
    UpdateGroupState();
    if (onGroupChanged) onGroupChanged(group);
}

void SmartHomeGroupControl::AddDevice(const std::string& deviceId) {
    if (std::find(group.DeviceIds.begin(), group.DeviceIds.end(), deviceId) !=
        group.DeviceIds.end()) {
        return;   // adding twice would double every command sent to it
    }
    group.DeviceIds.push_back(deviceId);
    UpdateGroupState();
    if (onGroupChanged) onGroupChanged(group);
}

void SmartHomeGroupControl::RemoveDevice(const std::string& deviceId) {
    group.DeviceIds.erase(std::remove(group.DeviceIds.begin(), group.DeviceIds.end(), deviceId),
                          group.DeviceIds.end());
    UpdateGroupState();
    if (onGroupChanged) onGroupChanged(group);
}

void SmartHomeGroupControl::SendGroupCommand(const std::string& command,
                                             const std::map<std::string, std::string>& params) {
    for (const auto& id : group.DeviceIds) {
        SmartHomeCommand cmd;
        cmd.DeviceId = id;
        cmd.Command = command;
        cmd.Parameters = params;
        SMARTHOME_API.SendCommand(cmd);
    }
}

void SmartHomeGroupControl::AllOn() {
    SendGroupCommand("on", {});
    masterOn = true;
    RequestRedraw();
}

void SmartHomeGroupControl::AllOff() {
    SendGroupCommand("off", {});
    masterOn = false;
    RequestRedraw();
}

void SmartHomeGroupControl::SetGroupBrightness(uint8_t value) {
    // This API is documented 0-100, while SmartHomeLightState::Brightness is
    // 0-255. Scale here so a group set to 50 matches a single light set to
    // half, rather than arriving as a fifth of full.
    masterBrightness = std::min<uint8_t>(value, 100);
    const int scaled = masterBrightness * 255 / 100;
    SendGroupCommand("setLevel", {{"brightness", std::to_string(scaled)}});
    syncing = true;
    brightness->SetValue(masterBrightness);
    syncing = false;
    brightnessValue->SetText(std::to_string(masterBrightness) + " %");
    RequestRedraw();
}

void SmartHomeGroupControl::SetGroupColorTemp(uint16_t value) {
    masterColorTemp = value;
    SendGroupCommand("setColorTemp", {{"colorTemp", std::to_string(value)}});
    syncing = true;
    colorTemp->SetValue(value);
    syncing = false;
    colorTempValue->SetText(std::to_string(value) + " K");
    RequestRedraw();
}

// ===== FACTORIES =====

std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeNetworkTopologyElement() {
    return std::make_shared<SmartHomeNetworkTopology>("SmartHomeNetworkTopology", 0, 0, 480, 360);
}
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeEnergyMonitorElement() {
    return std::make_shared<SmartHomeEnergyMonitor>("SmartHomeEnergyMonitor", 0, 0, 360, 280);
}
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeSchedulerElement() {
    return std::make_shared<SmartHomeScheduler>("SmartHomeScheduler", 0, 0, 480, 360);
}
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeGroupControlElement() {
    return std::make_shared<SmartHomeGroupControl>("SmartHomeGroupControl", 0, 0, 300, 360);
}

}  // namespace SmartHome
}  // namespace UltraCanvas
