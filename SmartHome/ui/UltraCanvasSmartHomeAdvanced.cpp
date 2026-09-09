// ui/UltraCanvasSmartHomeAdvanced.cpp
// The last four dashboard widgets: network topology, energy monitor,
// scheduler and group control.
//
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHomeAdvancedWidgets.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {
namespace SmartHome {

namespace {

constexpr double kPad = 14.0;
constexpr double kRow = 26.0;

const Color kText    (0x21, 0x21, 0x21);
const Color kTextDim (0x75, 0x75, 0x75);
const Color kTrack   (0xE0, 0xE0, 0xE0);
const Color kAccent  (33, 150, 243);
const Color kSheet   (0xFF, 0xFF, 0xFF);
const Color kBg      (0xFA, 0xFA, 0xFA);

uint64_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::string Fixed(double v, int places) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", places, v);
    return buf;
}

bool Hit(const Rect2Dd& r, int x, int y) {
    return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

void Button(IRenderContext* ctx, const Rect2Dd& r, const std::string& label,
            const Color& fill, const Color& textColor) {
    ctx->SetFillPaint(fill);
    ctx->FillRoundedRectangle(r, r.height / 2.0);
    ctx->SetTextPaint(textColor);
    ctx->SetFontSize(11.0);
    ctx->DrawTextInRect(label, Rect2Dd(r.x + 8.0, r.y + r.height / 2.0 - 7.0,
                                       r.width - 16.0, 14.0));
}

}  // namespace

// ============================================================================
// Network topology
// ============================================================================

SmartHomeNetworkTopology::SmartHomeNetworkTopology(const std::string& identifier,
                                                   float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

SmartHomeNetworkTopology::~SmartHomeNetworkTopology() = default;

Color SmartHomeNetworkTopology::GetProtocolColor(SmartHomeProtocolType protocol) const {
    switch (protocol) {
        case SmartHomeProtocolType::Matter:    return Color(103, 58, 183);
        case SmartHomeProtocolType::Thread:    return Color(0, 150, 136);
        case SmartHomeProtocolType::Zigbee:    return Color(255, 152, 0);
        case SmartHomeProtocolType::ZWave:     return Color(0, 188, 212);
        case SmartHomeProtocolType::KNX:       return Color(121, 85, 72);
        case SmartHomeProtocolType::WiFi:      return kAccent;
        case SmartHomeProtocolType::Bluetooth: return Color(63, 81, 181);
        default:                               return Color(0x9E, 0x9E, 0x9E);
    }
}

Color SmartHomeNetworkTopology::GetSignalColor(int signal) const {
    // LQI runs 0-255 and RSSI is negative dBm; treat anything negative as RSSI
    // so one scale does not misreport the other.
    const int quality = signal < 0 ? std::clamp((signal + 100) * 255 / 70, 0, 255)
                                   : std::clamp(signal, 0, 255);
    if (quality > 170) return Color(76, 175, 80);
    if (quality > 85)  return Color(255, 152, 0);
    return Color(0xF4, 0x43, 0x36);
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
    // mesh.
    for (const auto& node : nodes) {
        if (node.IsRouter) continue;
        for (const auto& candidate : nodes) {
            if (!candidate.IsRouter || candidate.Protocol != node.Protocol) continue;
            NetworkLink link;
            link.SourceId = candidate.DeviceId;
            link.TargetId = node.DeviceId;
            link.LinkQuality = node.SignalStrength;
            link.IsActive = node.State == SmartHomeDeviceState::Online;
            links.push_back(link);
            break;
        }
    }
    lastRefresh = NowMs();
}

void SmartHomeNetworkTopology::ApplyRadialLayout() {
    const Rect2Df b = GetLocalBounds();
    const double cx = b.width / 2.0;
    const double cy = b.height / 2.0;
    const double radius = std::min(b.width, b.height) / 2.0 - 40.0;
    if (nodes.empty()) return;

    // Routers in the middle, everything else on the ring: the shape then says
    // something about the network rather than just spacing the dots out.
    std::vector<TopologyViewNode*> hubs, leaves;
    for (auto& n : nodes) (n.IsRouter ? hubs : leaves).push_back(&n);

    for (size_t i = 0; i < hubs.size(); ++i) {
        const double a = hubs.size() == 1 ? 0.0
                                          : 2.0 * M_PI * static_cast<double>(i) / hubs.size();
        hubs[i]->X = static_cast<float>(cx + (hubs.size() == 1 ? 0.0 : radius * 0.35 * std::cos(a)));
        hubs[i]->Y = static_cast<float>(cy + (hubs.size() == 1 ? 0.0 : radius * 0.35 * std::sin(a)));
    }
    for (size_t i = 0; i < leaves.size(); ++i) {
        const double a = 2.0 * M_PI * static_cast<double>(i) / std::max<size_t>(leaves.size(), 1);
        leaves[i]->X = static_cast<float>(cx + radius * std::cos(a));
        leaves[i]->Y = static_cast<float>(cy + radius * std::sin(a));
    }
}

void SmartHomeNetworkTopology::ApplyTreeLayout() {
    const Rect2Df b = GetLocalBounds();
    std::vector<TopologyViewNode*> hubs, leaves;
    for (auto& n : nodes) (n.IsRouter ? hubs : leaves).push_back(&n);

    const double hubY = kPad + 40.0;
    const double leafY = b.height - kPad - 50.0;
    for (size_t i = 0; i < hubs.size(); ++i) {
        hubs[i]->X = static_cast<float>(b.width * (i + 1.0) / (hubs.size() + 1.0));
        hubs[i]->Y = static_cast<float>(hubY);
    }
    for (size_t i = 0; i < leaves.size(); ++i) {
        leaves[i]->X = static_cast<float>(b.width * (i + 1.0) / (leaves.size() + 1.0));
        leaves[i]->Y = static_cast<float>(leafY);
    }
}

void SmartHomeNetworkTopology::ApplyForceLayout() {
    if (nodes.empty()) return;
    const Rect2Df b = GetLocalBounds();

    // Start from the radial arrangement so the relaxation has something sane to
    // improve on; from a random start it can settle into a tangle.
    ApplyRadialLayout();

    // A fixed, small number of passes: this runs on every refresh and the
    // arrangement only has to be readable, not optimal.
    for (int pass = 0; pass < 60; ++pass) {
        for (auto& a : nodes) {
            double fx = 0.0, fy = 0.0;
            for (const auto& c : nodes) {
                if (&a == &c) continue;
                double dx = a.X - c.X, dy = a.Y - c.Y;
                double d2 = dx * dx + dy * dy;
                if (d2 < 1.0) d2 = 1.0;
                const double repel = 4000.0 / d2;
                const double d = std::sqrt(d2);
                fx += dx / d * repel;
                fy += dy / d * repel;
            }
            for (const auto& l : links) {
                const bool isSource = l.SourceId == a.DeviceId;
                if (!isSource && l.TargetId != a.DeviceId) continue;
                const std::string& otherId = isSource ? l.TargetId : l.SourceId;
                auto it = std::find_if(nodes.begin(), nodes.end(),
                                       [&](const TopologyViewNode& n){ return n.DeviceId == otherId; });
                if (it == nodes.end()) continue;
                fx += (it->X - a.X) * 0.02;
                fy += (it->Y - a.Y) * 0.02;
            }
            a.X = static_cast<float>(std::clamp<double>(a.X + fx * 0.05, 30.0, b.width - 30.0));
            a.Y = static_cast<float>(std::clamp<double>(a.Y + fy * 0.05, 30.0, b.height - 40.0));
        }
    }
}

void SmartHomeNetworkTopology::CalculateLayout() {
    if (layoutAlgorithm == "radial")    ApplyRadialLayout();
    else if (layoutAlgorithm == "tree") ApplyTreeLayout();
    else                                ApplyForceLayout();
}

void SmartHomeNetworkTopology::RefreshTopology() {
    LoadNetworkData();
    CalculateLayout();
    RequestRedraw();
}

void SmartHomeNetworkTopology::SetProtocolFilter(SmartHomeProtocolType protocol) {
    protocolFilter = protocol;
    RefreshTopology();
}

void SmartHomeNetworkTopology::SetLayout(const std::string& algorithm) {
    layoutAlgorithm = algorithm;
    CalculateLayout();
    RequestRedraw();
}

void SmartHomeNetworkTopology::SetAutoRefresh(bool enable, int intervalMs) {
    autoRefresh = enable;
    // A zero or negative interval would refresh on every frame; keep a floor.
    refreshInterval = std::max(intervalMs, 250);
}

void SmartHomeNetworkTopology::RenderLinks(IRenderContext* ctx) {
    for (const auto& link : links) {
        auto s = std::find_if(nodes.begin(), nodes.end(),
                              [&](const TopologyViewNode& n){ return n.DeviceId == link.SourceId; });
        auto t = std::find_if(nodes.begin(), nodes.end(),
                              [&](const TopologyViewNode& n){ return n.DeviceId == link.TargetId; });
        if (s == nodes.end() || t == nodes.end()) continue;
        ctx->SetStrokePaint(link.IsActive ? GetSignalColor(link.LinkQuality) : kTrack);
        ctx->SetStrokeWidth(link.IsActive ? 1.5 : 1.0);
        ctx->DrawLine(Point2Dd(s->X, s->Y), Point2Dd(t->X, t->Y));
    }
}

void SmartHomeNetworkTopology::RenderNodes(IRenderContext* ctx) {
    ctx->SetFontSize(9.0);
    for (const auto& node : nodes) {
        const double r = node.IsRouter ? 12.0 : 8.0;
        ctx->SetFillPaint(node.State == SmartHomeDeviceState::Online
                              ? GetProtocolColor(node.Protocol)
                              : kTrack);
        ctx->FillEllipse(Rect2Dd(node.X - r, node.Y - r, r * 2, r * 2));

        if (node.DeviceId == selectedNodeId) {
            ctx->SetStrokePaint(kText);
            ctx->SetStrokeWidth(2.0);
            ctx->DrawEllipse(Rect2Dd(node.X - r - 3.0, node.Y - r - 3.0,
                                     (r + 3.0) * 2, (r + 3.0) * 2));
        }
        ctx->SetTextPaint(kTextDim);
        ctx->DrawTextInRect(node.Name, Rect2Dd(node.X - 40.0, node.Y + r + 2.0, 80.0, 12.0));
    }
}

void SmartHomeNetworkTopology::RenderLegend(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(9.0);
    ctx->DrawTextInRect(std::to_string(nodes.size()) + " devices, " +
                            std::to_string(links.size()) + " links · " + layoutAlgorithm,
                        Rect2Dd(kPad, b.height - kPad - 12.0, b.width - 2 * kPad, 12.0));
}

void SmartHomeNetworkTopology::RenderNodeInfo(IRenderContext* ctx) {
    if (selectedNodeId.empty()) return;
    auto it = std::find_if(nodes.begin(), nodes.end(),
                           [&](const TopologyViewNode& n){ return n.DeviceId == selectedNodeId; });
    if (it == nodes.end()) return;

    ctx->SetFillPaint(kSheet);
    ctx->FillRoundedRectangle(Rect2Dd(kPad, kPad, 180.0, 44.0), 6.0);
    ctx->SetTextPaint(kText);
    ctx->SetFontSize(11.0);
    ctx->DrawTextInRect(it->Name, Rect2Dd(kPad + 8.0, kPad + 6.0, 164.0, 14.0));
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(9.0);
    ctx->DrawTextInRect(ProtocolTypeToString(it->Protocol) +
                            (it->IsRouter ? " · router" : ""),
                        Rect2Dd(kPad + 8.0, kPad + 24.0, 164.0, 12.0));
}

void SmartHomeNetworkTopology::Render(IRenderContext* ctx, const Rect2Df&) {
    if (!ctx || !IsVisible()) return;
    if (autoRefresh && NowMs() - lastRefresh >= static_cast<uint64_t>(refreshInterval)) {
        RefreshTopology();
    }
    ctx->SetFillPaint(kBg);
    ctx->FillRectangle(Rect2Dd(GetLocalBounds()));

    if (nodes.empty()) {
        const Rect2Df b = GetLocalBounds();
        ctx->SetTextPaint(kTextDim);
        ctx->SetFontSize(12.0);
        ctx->DrawTextInRect("No devices to map",
                            Rect2Dd(0.0, b.height / 2.0 - 8.0, b.width, 18.0));
        return;
    }
    RenderLinks(ctx);
    RenderNodes(ctx);
    RenderNodeInfo(ctx);
    RenderLegend(ctx);
}

bool SmartHomeNetworkTopology::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    const int x = event.pointer.x;
    const int y = event.pointer.y;

    switch (event.type) {
        case UCEventType::MouseDown:
        case UCEventType::TouchStart: {
            if (!Contains(Point2Df(static_cast<float>(x), static_cast<float>(y)))) return false;
            for (const auto& node : nodes) {
                const double r = node.IsRouter ? 12.0 : 8.0;
                const double dx = x - node.X, dy = y - node.Y;
                if (std::sqrt(dx * dx + dy * dy) <= r + 4.0) {
                    selectedNodeId = node.DeviceId;
                    draggingNodeId = node.DeviceId;
                    if (onNodeSelected) onNodeSelected(node.DeviceId);
                    RequestRedraw();
                    return true;
                }
            }
            selectedNodeId.clear();
            RequestRedraw();
            return true;
        }

        case UCEventType::MouseMove:
        case UCEventType::TouchMove: {
            if (draggingNodeId.empty()) return false;
            auto it = std::find_if(nodes.begin(), nodes.end(),
                                   [&](const TopologyViewNode& n){ return n.DeviceId == draggingNodeId; });
            if (it != nodes.end()) {
                it->X = static_cast<float>(x);
                it->Y = static_cast<float>(y);
                RequestRedraw();
            }
            return true;
        }

        case UCEventType::MouseUp:
        case UCEventType::TouchEnd:
            if (draggingNodeId.empty()) return false;
            draggingNodeId.clear();
            return true;

        case UCEventType::MouseWheel:
            viewScale = std::clamp(viewScale + event.wheelDelta * 0.1f, 0.4f, 3.0f);
            RequestRedraw();
            return true;

        default:
            return false;
    }
}

// ============================================================================
// Energy monitor
// ============================================================================

SmartHomeEnergyMonitor::SmartHomeEnergyMonitor(const std::string& identifier,
                                               float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

SmartHomeEnergyMonitor::~SmartHomeEnergyMonitor() = default;

void SmartHomeEnergyMonitor::SetDevice(const std::string& id) {
    deviceId = id;
    readings.clear();
    stats = EnergyStats{};
    RequestRedraw();
}

void SmartHomeEnergyMonitor::SetTimeRange(int hours) {
    timeRangeHours = std::max(hours, 1);
    CalculateStats();
    RequestRedraw();
}

void SmartHomeEnergyMonitor::SetCostRate(float rate, const std::string& symbol) {
    costPerKwh = rate;
    currency = symbol;
    CalculateStats();
    RequestRedraw();
}

void SmartHomeEnergyMonitor::AddReading(const EnergyReading& reading) {
    readings.push_back(reading);
    constexpr size_t kMax = 4096;
    if (readings.size() > kMax) {
        readings.erase(readings.begin(), readings.begin() + (readings.size() - kMax));
    }
    CalculateStats();
    RequestRedraw();
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

void SmartHomeEnergyMonitor::RenderCurrentPower(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const float watts = readings.empty() ? 0.0f : readings.back().Power;
    ctx->SetTextPaint(kText);
    ctx->SetFontSize(26.0);
    ctx->DrawTextInRect(Fixed(watts, 0) + " W", Rect2Dd(kPad, kPad, b.width - 2 * kPad, 32.0));
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(10.0);
    ctx->DrawTextInRect(deviceId.empty() ? "whole home" : deviceId,
                        Rect2Dd(kPad, kPad + 32.0, b.width - 2 * kPad, 12.0));
}

void SmartHomeEnergyMonitor::RenderEnergyChart(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const Rect2Dd plot(kPad, kPad + 52.0, b.width - 2 * kPad, b.height - kPad - 132.0);
    if (plot.width <= 0.0 || plot.height <= 0.0 || readings.size() < 2) return;

    const uint64_t newest = readings.back().Timestamp;
    const uint64_t window = static_cast<uint64_t>(timeRangeHours) * 3600ull * 1000ull;

    size_t first = 0;
    for (size_t i = readings.size(); i-- > 0;) {
        if (newest > window && readings[i].Timestamp < newest - window) { first = i + 1; break; }
    }
    if (readings.size() - first < 2) return;

    float lo = 0.0f, hi = 0.0f;
    for (size_t i = first; i < readings.size(); ++i) {
        const float v = chartMode == 0 ? readings[i].Power : readings[i].Energy;
        hi = std::max(hi, v);
        lo = std::min(lo, v);
    }
    if (hi - lo < 0.001f) hi = lo + 1.0f;

    ctx->SetStrokePaint(kAccent);
    ctx->SetStrokeWidth(1.5);
    const size_t count = readings.size() - first;
    for (size_t i = first + 1; i < readings.size(); ++i) {
        const float v0 = chartMode == 0 ? readings[i - 1].Power : readings[i - 1].Energy;
        const float v1 = chartMode == 0 ? readings[i].Power     : readings[i].Energy;
        const double t0 = static_cast<double>(i - 1 - first) / (count - 1);
        const double t1 = static_cast<double>(i - first) / (count - 1);
        ctx->DrawLine(Point2Dd(plot.x + plot.width * t0,
                               plot.y + plot.height * (1.0 - (v0 - lo) / (hi - lo))),
                      Point2Dd(plot.x + plot.width * t1,
                               plot.y + plot.height * (1.0 - (v1 - lo) / (hi - lo))));
    }
}

void SmartHomeEnergyMonitor::RenderStatistics(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const double y = b.height - kPad - 62.0;
    ctx->SetFontSize(9.0);

    const std::pair<std::string, std::string> cells[] = {
        {"today",   Fixed(stats.TotalEnergyToday, 2) + " kWh"},
        {"average", Fixed(stats.AveragePower, 0) + " W"},
        {"peak",    Fixed(stats.PeakPower, 0) + " W"},
        {"cost",    currency + Fixed(stats.EstimatedCost, 2)},
    };
    const double w = (b.width - 2 * kPad) / 4.0;
    for (int i = 0; i < 4; ++i) {
        ctx->SetTextPaint(kTextDim);
        ctx->DrawTextInRect(cells[i].first, Rect2Dd(kPad + i * w, y, w, 12.0));
        ctx->SetTextPaint(kText);
        ctx->SetFontSize(11.0);
        ctx->DrawTextInRect(cells[i].second, Rect2Dd(kPad + i * w, y + 13.0, w, 14.0));
        ctx->SetFontSize(9.0);
    }
}

void SmartHomeEnergyMonitor::RenderDeviceBreakdown(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    static const char* kModes[] = {"Power", "Energy", "Cost"};
    const double w = 70.0;
    const double y = b.height - kPad - 26.0;
    for (int i = 0; i < 3; ++i) {
        const bool active = i == chartMode;
        Button(ctx, Rect2Dd(kPad + i * (w + 6.0), y, w, 24.0), kModes[i],
               active ? kAccent : kTrack, active ? Colors::White : kTextDim);
    }
}

void SmartHomeEnergyMonitor::Render(IRenderContext* ctx, const Rect2Df&) {
    if (!ctx || !IsVisible()) return;
    ctx->SetFillPaint(kBg);
    ctx->FillRectangle(Rect2Dd(GetLocalBounds()));
    RenderCurrentPower(ctx);
    RenderEnergyChart(ctx);
    RenderStatistics(ctx);
    RenderDeviceBreakdown(ctx);
}

bool SmartHomeEnergyMonitor::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;
    if (event.type != UCEventType::MouseDown && event.type != UCEventType::TouchStart) {
        return false;
    }
    const Rect2Df b = GetLocalBounds();
    const double y = b.height - kPad - 26.0;
    for (int i = 0; i < 3; ++i) {
        if (Hit(Rect2Dd(kPad + i * 76.0, y, 70.0, 24.0), event.pointer.x, event.pointer.y)) {
            chartMode = i;
            RequestRedraw();
            return true;
        }
    }
    return Contains(Point2Df(static_cast<float>(event.pointer.x),
                             static_cast<float>(event.pointer.y)));
}

// ============================================================================
// Scheduler
// ============================================================================

SmartHomeScheduler::SmartHomeScheduler(const std::string& identifier,
                                       float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

SmartHomeScheduler::~SmartHomeScheduler() = default;

void SmartHomeScheduler::RefreshSchedule() {
    // Scheduled events belong to automations with a time trigger; until the
    // manager exposes them directly, what the widget already holds is the
    // schedule, and hosts add to it through AddEvent.
    RequestRedraw();
}

void SmartHomeScheduler::AddEvent(const ScheduledEvent& event) {
    auto it = std::find_if(events.begin(), events.end(),
                           [&](const ScheduledEvent& e){ return e.EventId == event.EventId; });
    if (it != events.end()) *it = event;    // adding the same id twice is an edit
    else                    events.push_back(event);
    RequestRedraw();
}

void SmartHomeScheduler::RemoveEvent(const std::string& eventId) {
    events.erase(std::remove_if(events.begin(), events.end(),
                                [&](const ScheduledEvent& e){ return e.EventId == eventId; }),
                 events.end());
    if (selectedEventId == eventId) selectedEventId.clear();
    RequestRedraw();
}

void SmartHomeScheduler::ToggleEvent(const std::string& eventId) {
    for (auto& e : events) {
        if (e.EventId == eventId) { e.Enabled = !e.Enabled; break; }
    }
    RequestRedraw();
}

void SmartHomeScheduler::SetViewMode(const std::string& mode) {
    if (mode != "day" && mode != "week" && mode != "list") return;   // ignore nonsense
    viewMode = mode;
    scrollOffset = 0.0f;
    RequestRedraw();
}

void SmartHomeScheduler::RenderTimeGrid(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const double top = kPad + 26.0;
    const double height = b.height - top - kPad;
    ctx->SetStrokePaint(kTrack);
    ctx->SetStrokeWidth(1.0);
    ctx->SetFontSize(8.0);
    for (int hour = 0; hour <= 24; hour += 3) {
        const double y = top + height * (hour / 24.0) - scrollOffset;
        if (y < top || y > b.height) continue;
        ctx->DrawLine(Point2Dd(kPad + 26.0, y), Point2Dd(b.width - kPad, y));
        ctx->SetTextPaint(kTextDim);
        ctx->DrawTextInRect(std::to_string(hour) + ":00", Rect2Dd(kPad, y - 5.0, 24.0, 10.0));
    }
}

void SmartHomeScheduler::RenderEvents(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const double top = kPad + 26.0;
    const double height = b.height - top - kPad;
    const double left = kPad + 26.0;
    const double dayW = (b.width - left - kPad) / 7.0;

    ctx->SetFontSize(8.0);
    for (const auto& e : events) {
        // "HH:MM" — anything else has no place on a clock face.
        if (e.Time.size() < 4 || e.Time.find(':') == std::string::npos) continue;
        int hh = 0, mm = 0;
        if (std::sscanf(e.Time.c_str(), "%d:%d", &hh, &mm) != 2) continue;
        if (hh < 0 || hh > 23 || mm < 0 || mm > 59) continue;

        const double y = top + height * ((hh + mm / 60.0) / 24.0) - scrollOffset;
        for (int day : e.Days) {
            if (day < 0 || day > 6) continue;
            if (viewMode == "day" && day != selectedDay) continue;
            const double x = viewMode == "day" ? left : left + day * dayW;
            const double w = viewMode == "day" ? (b.width - left - kPad) : dayW - 2.0;
            const bool chosen = e.EventId == selectedEventId;
            ctx->SetFillPaint(!e.Enabled ? kTrack : (chosen ? kText : kAccent));
            ctx->FillRoundedRectangle(Rect2Dd(x, y, w, 14.0), 3.0);
            ctx->SetTextPaint(e.Enabled ? Colors::White : kTextDim);
            ctx->DrawTextInRect(e.Name, Rect2Dd(x + 3.0, y + 2.0, w - 6.0, 10.0));
        }
    }
}

void SmartHomeScheduler::RenderWeekView(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    static const char* kDays[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    const double left = kPad + 26.0;
    const double dayW = (b.width - left - kPad) / 7.0;

    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(9.0);
    for (int i = 0; i < 7; ++i) {
        ctx->DrawTextInRect(kDays[i], Rect2Dd(left + i * dayW, kPad + 8.0, dayW, 12.0));
    }
    RenderTimeGrid(ctx);
    RenderEvents(ctx);
}

void SmartHomeScheduler::RenderDayView(IRenderContext* ctx) {
    static const char* kDays[] = {"Sunday","Monday","Tuesday","Wednesday",
                                  "Thursday","Friday","Saturday"};
    ctx->SetTextPaint(kText);
    ctx->SetFontSize(11.0);
    ctx->DrawTextInRect(kDays[std::clamp(selectedDay, 0, 6)],
                        Rect2Dd(kPad + 26.0, kPad + 6.0, 120.0, 14.0));
    RenderTimeGrid(ctx);
    RenderEvents(ctx);
}

void SmartHomeScheduler::RenderListView(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    double y = kPad - scrollOffset;
    ctx->SetFontSize(11.0);
    for (const auto& e : events) {
        if (y > b.height - kPad) break;
        const bool chosen = e.EventId == selectedEventId;
        ctx->SetFillPaint(chosen ? kAccent : kSheet);
        ctx->FillRoundedRectangle(Rect2Dd(kPad, y, b.width - 2 * kPad, kRow), 5.0);
        ctx->SetTextPaint(chosen ? Colors::White : (e.Enabled ? kText : kTextDim));
        ctx->DrawTextInRect(e.Time + "  " + e.Name,
                            Rect2Dd(kPad + 8.0, y + 6.0, b.width - 2 * kPad - 16.0, 14.0));
        y += kRow + 4.0;
    }
    if (events.empty()) {
        ctx->SetTextPaint(kTextDim);
        ctx->DrawTextInRect("Nothing scheduled",
                            Rect2Dd(0.0, b.height / 2.0 - 8.0, b.width, 16.0));
    }
}

void SmartHomeScheduler::Render(IRenderContext* ctx, const Rect2Df&) {
    if (!ctx || !IsVisible()) return;
    ctx->SetFillPaint(kBg);
    ctx->FillRectangle(Rect2Dd(GetLocalBounds()));
    if (viewMode == "day")       RenderDayView(ctx);
    else if (viewMode == "list") RenderListView(ctx);
    else                         RenderWeekView(ctx);
}

bool SmartHomeScheduler::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    const Rect2Df b = GetLocalBounds();
    const int x = event.pointer.x;
    const int y = event.pointer.y;

    if (event.type == UCEventType::MouseWheel) {
        scrollOffset = std::max(0.0f, scrollOffset - event.wheelDelta * 16.0f);
        RequestRedraw();
        return true;
    }
    if (event.type != UCEventType::MouseDown && event.type != UCEventType::TouchStart) {
        return false;
    }
    if (!Contains(Point2Df(static_cast<float>(x), static_cast<float>(y)))) return false;

    if (viewMode == "list") {
        const int index = static_cast<int>((y - (kPad - scrollOffset)) / (kRow + 4.0));
        if (index >= 0 && index < static_cast<int>(events.size())) {
            selectedEventId = events[static_cast<size_t>(index)].EventId;
            if (onEventSelected) onEventSelected(selectedEventId);
            RequestRedraw();
        }
        return true;
    }

    // On a grid, a tap on empty space is a request to create something at that
    // day and hour, which is what onEventCreate reports.
    const double top = kPad + 26.0;
    const double height = b.height - top - kPad;
    const double left = kPad + 26.0;
    const double dayW = (b.width - left - kPad) / 7.0;
    if (y >= top && height > 0.0) {
        const int hour = static_cast<int>(((y + scrollOffset - top) / height) * 24.0);
        const int day = viewMode == "day" ? selectedDay
                                          : static_cast<int>((x - left) / dayW);
        if (day >= 0 && day <= 6 && hour >= 0 && hour <= 23 && onEventCreate) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "%02d:00", hour);
            onEventCreate(buf, day);
        }
    }
    return true;
}

// ============================================================================
// Group control
// ============================================================================

SmartHomeGroupControl::SmartHomeGroupControl(const std::string& identifier,
                                             float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

SmartHomeGroupControl::~SmartHomeGroupControl() = default;

void SmartHomeGroupControl::UpdateGroupState() {
    devices.clear();
    for (const auto& id : group.DeviceIds) {
        SmartHomeDeviceInfo info = SMARTHOME_API.GetDevice(id);
        if (!info.DeviceId.empty()) devices.push_back(info);
    }
    // The master switch reads on when anything in the group is on, so turning
    // it off is always the way to make the room dark.
    masterOn = false;
    for (const auto& d : devices) {
        if (SMARTHOME_API.GetSwitchState(d.DeviceId)) { masterOn = true; break; }
    }
    RequestRedraw();
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

void SmartHomeGroupControl::SetGroupBrightness(uint8_t brightness) {
    // This API is documented 0-100, while SmartHomeLightState::Brightness is
    // 0-255. Scale here so a group set to 50 matches a single light set to
    // half, rather than arriving as a fifth of full.
    masterBrightness = std::min<uint8_t>(brightness, 100);
    const int scaled = masterBrightness * 255 / 100;
    SendGroupCommand("setLevel", {{"brightness", std::to_string(scaled)}});
    RequestRedraw();
}

void SmartHomeGroupControl::SetGroupColorTemp(uint16_t colorTemp) {
    masterColorTemp = colorTemp;
    SendGroupCommand("setColorTemp", {{"colorTemp", std::to_string(colorTemp)}});
    RequestRedraw();
}

void SmartHomeGroupControl::RenderGroupHeader(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(kText);
    ctx->SetFontSize(14.0);
    ctx->DrawTextInRect(group.Name.empty() ? "Group" : group.Name,
                        Rect2Dd(kPad, kPad, b.width - 2 * kPad, 18.0));
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(10.0);
    ctx->DrawTextInRect(std::to_string(group.DeviceIds.size()) + " devices",
                        Rect2Dd(kPad, kPad + 20.0, b.width - 2 * kPad, 12.0));
}

void SmartHomeGroupControl::RenderMasterControls(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const double y = kPad + 40.0;
    Button(ctx, Rect2Dd(kPad, y, 70.0, 26.0), "All on",
           masterOn ? kAccent : kTrack, masterOn ? Colors::White : kText);
    Button(ctx, Rect2Dd(kPad + 78.0, y, 70.0, 26.0), "All off", kTrack, kText);

    const Rect2Dd track(kPad, y + 38.0, b.width - 2 * kPad, 6.0);
    ctx->SetFillPaint(kTrack);
    ctx->FillRoundedRectangle(track, 3.0);
    ctx->SetFillPaint(kAccent);
    ctx->FillRoundedRectangle(Rect2Dd(track.x, track.y, track.width * (masterBrightness / 100.0),
                                      6.0), 3.0);
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(9.0);
    ctx->DrawTextInRect(std::to_string(masterBrightness) + "%",
                        Rect2Dd(track.x, track.y + 10.0, 50.0, 12.0));
}

void SmartHomeGroupControl::RenderDeviceList(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    double y = kPad + 108.0 - scrollOffset;
    ctx->SetFontSize(11.0);
    for (const auto& d : devices) {
        if (y > b.height - 40.0) break;
        ctx->SetFillPaint(kSheet);
        ctx->FillRoundedRectangle(Rect2Dd(kPad, y, b.width - 2 * kPad, kRow), 5.0);
        ctx->SetTextPaint(d.State == SmartHomeDeviceState::Online ? kText : kTextDim);
        ctx->DrawTextInRect(d.Name.empty() ? d.DeviceId : d.Name,
                            Rect2Dd(kPad + 8.0, y + 6.0, b.width - 2 * kPad - 30.0, 14.0));
        ctx->SetTextPaint(kTextDim);
        ctx->DrawTextInRect("×", Rect2Dd(b.width - kPad - 18.0, y + 6.0, 12.0, 14.0));
        y += kRow + 4.0;
    }
    if (devices.empty()) {
        ctx->SetTextPaint(kTextDim);
        ctx->DrawTextInRect("This group has no devices yet.",
                            Rect2Dd(kPad, y + 4.0, b.width - 2 * kPad, 14.0));
    }
}

void SmartHomeGroupControl::RenderAddDeviceButton(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    Button(ctx, Rect2Dd(kPad, b.height - kPad - 26.0, 110.0, 26.0),
           "Add device", kTrack, kText);
}

void SmartHomeGroupControl::Render(IRenderContext* ctx, const Rect2Df&) {
    if (!ctx || !IsVisible()) return;
    ctx->SetFillPaint(kBg);
    ctx->FillRectangle(Rect2Dd(GetLocalBounds()));
    RenderGroupHeader(ctx);
    RenderMasterControls(ctx);
    RenderDeviceList(ctx);
    RenderAddDeviceButton(ctx);
}

bool SmartHomeGroupControl::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    const Rect2Df b = GetLocalBounds();
    const int x = event.pointer.x;
    const int y = event.pointer.y;
    const double my = kPad + 40.0;
    const Rect2Dd bright(kPad, my + 38.0, b.width - 2 * kPad, 6.0);

    switch (event.type) {
        case UCEventType::MouseDown:
        case UCEventType::TouchStart: {
            if (!Contains(Point2Df(static_cast<float>(x), static_cast<float>(y)))) return false;
            if (Hit(Rect2Dd(kPad, my, 70.0, 26.0), x, y))        { AllOn();  return true; }
            if (Hit(Rect2Dd(kPad + 78.0, my, 70.0, 26.0), x, y)) { AllOff(); return true; }

            if (y >= bright.y - 10.0 && y <= bright.y + 16.0) {
                dragging = 1;
                const double t = std::clamp((x - bright.x) / bright.width, 0.0, 1.0);
                SetGroupBrightness(static_cast<uint8_t>(t * 100.0));
                return true;
            }
            const int index = static_cast<int>((y - (kPad + 108.0 - scrollOffset)) / (kRow + 4.0));
            if (index >= 0 && index < static_cast<int>(devices.size()) &&
                x >= b.width - kPad - 20.0) {
                RemoveDevice(devices[static_cast<size_t>(index)].DeviceId);
                return true;
            }
            return true;
        }

        case UCEventType::MouseMove:
        case UCEventType::TouchMove:
            if (dragging != 1) return false;
            SetGroupBrightness(static_cast<uint8_t>(
                std::clamp((x - bright.x) / bright.width, 0.0, 1.0) * 100.0));
            return true;

        case UCEventType::MouseUp:
        case UCEventType::TouchEnd:
            if (dragging == 0) return false;
            dragging = 0;
            return true;

        default:
            return false;
    }
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
