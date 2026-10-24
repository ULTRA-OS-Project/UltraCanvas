// Apps/UltraDBAdmin/engine/DbAdminMonitor.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminMonitor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraDBAdmin {
namespace {

enum class Kind { Gauge, Rate, HitRatio };

struct Spec {
    const char* key;
    const char* title;
    const char* unit;
    Kind kind;
    const char* source;        // UltraDb_ServerStatus's name
    const char* second;        // the hit ratio's misses
    double scale;
};

// The charts, in the order the tab shows them.
const Spec kSpecs[] = {
    { "sessions", "Sessions", "", Kind::Gauge, "connections", nullptr, 1.0 },
    { "active", "Running now", "", Kind::Gauge, "active", nullptr, 1.0 },
    { "queries", "Statements", "/s", Kind::Rate, "queries", nullptr, 1.0 },
    { "transactions", "Transactions", "/s", Kind::Rate, "transactions", nullptr, 1.0 },
    { "rows_read", "Rows read", "/s", Kind::Rate, "rows_read", nullptr, 1.0 },
    { "rows_written", "Rows written", "/s", Kind::Rate, "rows_written", nullptr, 1.0 },
    { "cache_hits", "Cache hits", "%", Kind::HitRatio, "cache_reads", "disk_reads", 1.0 },
    { "received", "Received", "KB/s", Kind::Rate, "bytes_received", nullptr, 1.0 / 1024.0 },
    { "sent", "Sent", "KB/s", Kind::Rate, "bytes_sent", nullptr, 1.0 / 1024.0 },
};

const Spec* SpecOf(const std::string& key) {
    for (const Spec& spec : kSpecs)
        if (key == spec.key) return &spec;
    return nullptr;
}

bool Get(const std::map<std::string, int64_t>& values, const char* name, int64_t& out) {
    if (!name) return false;
    const auto it = values.find(name);
    if (it == values.end()) return false;
    out = it->second;
    return true;
}

} // namespace

void ServerMonitor::Clear() {
    charts_.clear();
    last_.clear();
    start_ = lastSeconds_ = 0;
    readings_ = 0;
}

void ServerMonitor::Add(double seconds, const UltraDbResultSet& status, std::time_t at) {
    if (readings_ > 0 && seconds <= lastSeconds_) return;
    std::map<std::string, int64_t> now;
    for (const auto& row : status)
        if (!row["name"].IsNull() && !row["value"].IsNull()) now[row["name"].AsString()] = row["value"].AsInt64();
    if (readings_ == 0) {
        start_ = seconds;
        // A chart for each measure the server keeps.
        for (const Spec& spec : kSpecs) {
            int64_t unused = 0;
            if (!Get(now, spec.source, unused)) continue;
            if (spec.kind == Kind::HitRatio && !Get(now, spec.second, unused)) continue;
            charts_.push_back(MonitorChart{ spec.key, spec.title, spec.unit, {} });
        }
    }
    const double elapsed = seconds - lastSeconds_;
    for (MonitorChart& chart : charts_) {
        const Spec* spec = SpecOf(chart.key);
        int64_t value = 0, before = 0;
        if (!spec || !Get(now, spec->source, value)) continue;
        double point = 0;
        if (spec->kind == Kind::Gauge) {
            point = static_cast<double>(value);
        } else {
            // A rate needs the reading before; a counter that went back
            // (the server restarted) waits for the next.
            if (readings_ == 0 || !Get(last_, spec->source, before) || value < before) continue;
            const double delta = static_cast<double>(value - before);
            if (spec->kind == Kind::Rate) {
                point = delta / elapsed * spec->scale;
            } else {
                int64_t misses = 0, missesBefore = 0;
                if (!Get(now, spec->second, misses) || !Get(last_, spec->second, missesBefore) ||
                    misses < missesBefore)
                    continue;
                // Nothing read: nothing missed.
                point = delta <= 0 ? 100.0
                                   : std::clamp(100.0 * (1.0 - static_cast<double>(misses - missesBefore) / delta),
                                                0.0, 100.0);
            }
        }
        chart.points.push_back(MonitorPoint{ seconds - start_, point, at });
        if (chart.points.size() > kMonitorPoints)
            chart.points.erase(chart.points.begin(),
                               chart.points.begin() + static_cast<std::ptrdiff_t>(chart.points.size() - kMonitorPoints));
    }
    last_ = std::move(now);
    lastSeconds_ = seconds;
    ++readings_;
}

std::string MonitorValueText(double value, const std::string& unit) {
    // Small values keep a decimal, so 0.5 statements a second is not 0.
    const bool decimal = std::fabs(value) < 10.0 && std::fabs(value - std::round(value)) >= 0.05;
    std::string text;
    if (decimal) {
        const long long tenths = std::llabs(std::llround(value * 10.0));
        text = (value < 0 ? "-" : "") + std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
    } else {
        std::string digits = std::to_string(std::llabs(std::llround(value)));
        for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) digits.insert(static_cast<size_t>(at), ",");
        text = (value < 0 && std::llround(value) != 0 ? "-" : "") + digits;
    }
    if (unit.empty()) return text;
    return text + (unit == "%" ? "" : " ") + unit;
}

std::string MonitorClockText(std::time_t at) {
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &at);
#else
    localtime_r(&at, &local);
#endif
    char text[16];
    std::snprintf(text, sizeof text, "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
    return text;
}

} // namespace UltraDBAdmin
